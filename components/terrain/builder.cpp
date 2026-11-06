#include "builder.hpp"

#include <algorithm>
#include <cmath>

#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <tuple>

#include <vsg/commands/Commands.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/utils/SharedObjects.h>

#include <components/misc/loadtrace.hpp>
#include <components/pipeline/sets.hpp>
#include <components/pipeline/terrain.hpp>
#include <components/render/parallaxmath.hpp>
#include <components/vsgutil/arraystate.hpp>
#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/bounds.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/readimage.hpp>

#include "batchdata.hpp"
#include "heightarraystate.hpp"
#include "indexbuffer.hpp"

namespace Pipeline::Descriptors
{
#include <files/shaders/terrain/bindings.glsl>
}
namespace Terrain
{
    namespace
    {
        // Task 0049: the terrain's textures opt in to the texture downscaler under the Terrain category, so every
        // layer of an array is brought to the same cap before the "most common size" rule runs.
        vsgUtil::ImageUse terrainImageUse(vsgUtil::TextureCap::Kind kind, int floor = 0)
        {
            vsgUtil::ImageUse use;
            use.category = vsgUtil::TextureCap::Category::Terrain;
            use.kind = kind;
            use.floor = floor;
            return use;
        }

        // Task 0044: reads the files and keeps those of the most common size and format, as the normal map array does
        // (convertToArrayTexture fills any other with grey bytes). Returns, for each path, its place in `kept` or -1.
        std::vector<int> readUniformImages(const std::vector<std::string>& paths,
            vsg::ref_ptr<const vsg::Options> options, vsg::DataList& kept, const char* what,
            bool (*accept)(VkFormat) = nullptr, const vsgUtil::ImageUse& use = {})
        {
            using ImageKey = std::tuple<uint32_t, uint32_t, VkFormat>;
            std::vector<vsg::ref_ptr<vsg::Data>> images;
            std::map<ImageKey, size_t> counts;
            const auto readImages = vsgUtil::readArrayImages(paths, options, use, false);
            for (size_t n = 0; n < paths.size(); ++n)
            {
                const auto& path = paths[n];
                auto data = readImages[n];
                // Task 0045: the blur and the mean height of a height file read coarser mip levels.
                if (data && accept == Render::Parallax::usableHeightFormat
                    && !Render::Parallax::heightMipsUsable(data->properties.format,
                        data->width() * data->properties.blockWidth, data->height() * data->properties.blockHeight,
                        vsgUtil::detail::mipLevels(*data).size()))
                {
                    std::cerr << "Terrain " << what << " " << path << " has no mip levels and is not used" << std::endl;
                    data = nullptr;
                }
                else if (data && accept && !accept(data->properties.format))
                {
                    std::cerr << "Terrain " << what << " " << path << " is not a usable image and is not used"
                              << std::endl;
                    data = nullptr;
                }
                else if (data)
                {
                    vsgUtil::setName(*data, path);
                    ++counts[{ data->width(), data->height(), data->properties.format }];
                }
                else if (!data)
                    std::cerr << "Terrain " << what << " " << path << " could not be read and is not used" << std::endl;
                images.push_back(data);
            }
            ImageKey reference{};
            size_t referenceCount = 0;
            for (const auto& [key, count] : counts)
                if (count > referenceCount)
                {
                    reference = key;
                    referenceCount = count;
                }
            // Task 0089: a layer convertToArrayTexture converts exactly (another size of the array's uncompressed
            // format, BC1 to BC3 in an RGBA8 array) is kept; the rest is left out. Only a key rarer than the
            // reference is kept, so convertToArrayTexture picks the same reference (it takes the first most common).
            vsgUtil::LayerShape referenceShape;
            for (const auto& image : images)
                if (image && ImageKey{ image->width(), image->height(), image->properties.format } == reference)
                {
                    referenceShape = vsgUtil::layerShapeOf(*image);
                    break;
                }
            std::vector<int> indices(paths.size(), -1);
            for (size_t i = 0; i < images.size(); ++i)
            {
                if (!images[i])
                    continue;
                if (ImageKey{ images[i]->width(), images[i]->height(), images[i]->properties.format } != reference
                    && !(counts[{ images[i]->width(), images[i]->height(), images[i]->properties.format }]
                            < referenceCount
                        && vsgUtil::arrayLayerConvertible(vsgUtil::layerShapeOf(*images[i]), referenceShape)))
                {
                    std::cerr << "Terrain " << what << " " << paths[i]
                              << " differs in size or format from the others and is not used" << std::endl;
                    continue;
                }
                indices[i] = static_cast<int>(kept.size());
                kept.push_back(images[i]);
            }
            return indices;
        }
    }

    Builder::Builder(vsg::ref_ptr<const vsg::Options> in_imageOptions,
        vsg::ref_ptr<const vsg::Options> in_shaderOptions, vsg::ref_ptr<vsg::Sampler> in_samplerOptions,
        TerrainMapSettings in_mapSettings)
        : mImageOptions(in_imageOptions)
        , mSamplerOptions(in_samplerOptions)
        , mMapSettings(std::move(in_mapSettings))
    {
        mPipeline = Pipeline::terrain(in_shaderOptions, mMapSettings.enabled(), mMapSettings.gbufferOutputs,
            mMapSettings.giSky, mMapSettings.wetness, mMapSettings.pbr, mMapSettings.cover,
            mMapSettings.parallaxQuality, mMapSettings.aoTerrain, mMapSettings.pointShadow);
    }

    void Builder::pruneCache() const
    {
        dataCache.removeObjects([](const Builder::BatchData& d) -> bool {
            for (auto& sc : d.stateCommands)
            {
                if (sc->referenceCount() <= 1)
                    return true;
            }
            return false;
        });
    }

    void Builder::clearCache() const
    {
        dataCache.removeObjects([](const Builder::BatchData&) -> bool { return true; });
    }

    Builder::~Builder()
    {
        // The build uses this object's members.
        try
        {
            waitForLayers();
        }
        catch (...)
        {
        }
    }

    bool Builder::layersReady() const
    {
        return !mLayersReady.valid() || mLayersReady.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void Builder::waitForLayers() const
    {
        if (!mLayersReady.valid())
            return;
        if (mLayersReady.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            Misc::LoadTrace::Scope traceWait("terrain.layers.wait");
            mLayersReady.wait();
        }
        mLayersReady.get(); // rethrows what the build threw
    }

    void Builder::setStorage(Storage* in_storage)
    {
        // An earlier build still writes the descriptors.
        try
        {
            waitForLayers();
        }
        catch (...)
        {
        }
        mLayersReady = {};
        mStorage = in_storage;

        auto layers = mStorage->getLayers();
        if (layers.size() > 256)
        {
            // Vulkan guarantees a minimum of 256 maxImageArrayLayers.
            // uint8_t type is used for blendMap channels.
            std::cerr << "layerInfos.size() > 256" << std::endl;
            layers.resize(256);
        }
        mLayerList = layers;

        if (mMapSettings.enabled())
        {
            setLayersWithMaps(layers);
            return;
        }

        // Task 0089: the reads and the array are built on a thread of their own (see waitForLayers).
        const std::vector<std::string> layerPaths(layers.begin(), layers.end());
        mLayersReady = std::async(std::launch::async, [this, layerPaths] {
            Misc::LoadTrace::Scope traceTotal("terrain.layers.total");
            vsg::DataList imageData;
            const auto layerImages = vsgUtil::readArrayImages(
                layerPaths, mImageOptions, terrainImageUse(vsgUtil::TextureCap::Kind::Colour), true);
            for (size_t n = 0; n < layerPaths.size(); ++n)
            {
                vsgUtil::setName(*layerImages[n], layerPaths[n]);
                imageData.push_back(layerImages[n]);
            }
            auto arrayData = vsgUtil::convertToArrayTexture(imageData);
            mLayerDescriptor
                = vsg::DescriptorImage::create(mSamplerOptions, arrayData, Pipeline::Descriptors::LAYER_BINDING);
        }).share();
    }

    bool Builder::setStorageSharingLayers(Storage* in_storage, const Builder& shareFrom)
    {
        auto layers = in_storage->getLayers();
        if (layers.size() > 256)
            layers.resize(256);
        if (&shareFrom != this && shareFrom.mStorage != nullptr
            && layerArraysShareable(layers, mMapSettings, shareFrom.mLayerList, shareFrom.mMapSettings))
        {
            shareFrom.waitForLayers();
            // The descriptors are read under the source's lock: a texture mip swap may be replacing them.
            std::scoped_lock lock(shareFrom.mDescriptorMutex);
            if (shareFrom.mLayerDescriptor)
            {
                // An earlier build of this builder's own would still write the members below.
                try
                {
                    waitForLayers();
                }
                catch (...)
                {
                }
                mLayersReady = {};
                mStorage = in_storage;
                mLayerList = std::move(layers);
                std::scoped_lock own(mDescriptorMutex);
                mLayerDescriptor = shareFrom.mLayerDescriptor;
                mNormalLayerDescriptor = shareFrom.mNormalLayerDescriptor;
                mLayerFlagsDescriptor = shareFrom.mLayerFlagsDescriptor;
                mMaterialLayerDescriptor = shareFrom.mMaterialLayerDescriptor;
                mLayerScalesDescriptor = shareFrom.mLayerScalesDescriptor;
                mHeightLayerDescriptor = shareFrom.mHeightLayerDescriptor;
                mLayerHeightsDescriptor = shareFrom.mLayerHeightsDescriptor;
                return true;
            }
        }
        setStorage(in_storage);
        return false;
    }

    void Builder::setLayersWithMaps(const std::vector<LayerInfo>& layers)
    {
        // Layers are named as the land textures name them; the maps are found next to the resolved path, as OpenMW's
        // getLayerInfo did with corrected texture paths.
        std::vector<std::string> resolved;
        resolved.reserve(layers.size());
        for (const auto& layer : layers)
        {
            if (mImageOptions && mImageOptions->findFileCallback)
                resolved.push_back(mImageOptions->findFileCallback(layer, mImageOptions.get()).string());
            else
                resolved.push_back(layer);
        }
        const TerrainLayerMaps maps = findTerrainLayerMaps(resolved, mMapSettings.autoMaps, mMapSettings.exists);
        // The material table is read here, on the calling thread, for every layer: the build runs on another one.
        std::vector<Render::Pbr::OverrideValues> values;
        if (mMapSettings.layerValues)
        {
            values.reserve(resolved.size());
            for (const std::string& path : resolved)
                values.push_back(mMapSettings.layerValues(path));
        }

        // Task 0089: the reads and the arrays (hundreds of megabytes with a texture mod) are built on a thread of
        // their own; whoever first builds a batch waits for it (waitForLayers), which is in the loading screen's time.
        mLayersReady = std::async(std::launch::async, [this, maps, values] {
            Misc::LoadTrace::Scope traceTotal("terrain.layers.total");
            buildLayersWithMaps(maps, values);
        }).share();
    }

    void Builder::buildLayersWithMaps(
        const TerrainLayerMaps& maps, const std::vector<Render::Pbr::OverrideValues>& layerValues)
    {
        // The normal map array keeps only maps of its most common size and format: convertToArrayTexture would fill
        // any other with grey bytes, which decode to a badly tilted normal rather than a flat one. Layers whose map is
        // left out are drawn without one.
        std::vector<vsg::ref_ptr<vsg::Data>> normalImages;
        using ImageKey = std::tuple<uint32_t, uint32_t, VkFormat>;
        std::map<ImageKey, size_t> keyCounts;
        Misc::LoadTrace::Scope traceNormalReads(
            "terrain.layers.read.normal", static_cast<std::int64_t>(maps.normalMaps.size()));
        const auto normalReads = vsgUtil::readArrayImages(
            maps.normalMaps, mImageOptions, terrainImageUse(vsgUtil::TextureCap::Kind::Normal), true);
        traceNormalReads.end();
        for (size_t n = 0; n < maps.normalMaps.size(); ++n)
        {
            const auto& data = normalReads[n];
            vsgUtil::setName(*data, maps.normalMaps[n]);
            normalImages.push_back(data);
            ++keyCounts[{ data->width(), data->height(), data->properties.format }];
        }
        ImageKey reference{};
        size_t referenceCount = 0;
        vsgUtil::LayerShape referenceShape;
        for (const auto& image : normalImages)
        {
            const ImageKey key{ image->width(), image->height(), image->properties.format };
            if (keyCounts[key] > referenceCount)
            {
                reference = key;
                referenceCount = keyCounts[key];
                referenceShape = vsgUtil::layerShapeOf(*image);
            }
        }
        vsg::DataList normalData;
        std::vector<int> arrayIndex(normalImages.size(), -1);
        for (size_t i = 0; i < normalImages.size(); ++i)
        {
            const auto& image = normalImages[i];
            // Task 0089: a map the array converts exactly (another size of its uncompressed format) is kept.
            if (ImageKey{ image->width(), image->height(), image->properties.format } != reference
                && !(keyCounts[{ image->width(), image->height(), image->properties.format }] < referenceCount
                    && vsgUtil::arrayLayerConvertible(vsgUtil::layerShapeOf(*image), referenceShape)))
            {
                std::cerr << "Terrain normal map " << maps.normalMaps[i]
                          << " differs in size or format from the others and is not used" << std::endl;
                continue;
            }
            arrayIndex[i] = static_cast<int>(normalData.size());
            normalData.push_back(image);
        }

        // Task 0044: the same for the packed material maps.
        vsg::DataList materialData;
        std::vector<int> materialArrayIndex;
        if (mMapSettings.pbr)
            materialArrayIndex = readUniformImages(maps.materialMaps, mImageOptions, materialData, "material map",
                nullptr, terrainImageUse(vsgUtil::TextureCap::Kind::Data));

        // Task 0045: the same for the height files.
        vsg::DataList heightData;
        std::vector<int> heightArrayIndex;
        vsg::ref_ptr<vsg::vec4Array> heights;
        if (mMapSettings.parallaxQuality > 0)
        {
            heightArrayIndex = readUniformImages(maps.heightMaps, mImageOptions, heightData, "height map",
                Render::Parallax::usableHeightFormat,
                terrainImageUse(vsgUtil::TextureCap::Kind::Data, vsgUtil::TextureCap::heightMapFloor));
            heights = vsg::vec4Array::create(static_cast<uint32_t>(std::max<size_t>(maps.layers.size(), 1)));
            heights->set(0, vsg::vec4(-1.f, 0.f, 0.f, 0.f));
        }

        vsg::DataList imageData;
        auto flags = vsg::uintArray::create(static_cast<uint32_t>(std::max<size_t>(maps.layers.size(), 1)));
        // Task 0044: the scales exist only with pbr; without it the flags keep the bits they had (the low 20).
        vsg::ref_ptr<vsg::vec4Array> scales;
        if (mMapSettings.pbr)
        {
            scales = vsg::vec4Array::create(static_cast<uint32_t>(std::max<size_t>(maps.layers.size(), 1)));
            scales->set(0, vsg::vec4(0.f, -1.f, 0.f, 0.f));
        }
        const uint32_t flagMask = mMapSettings.pbr ? 0xffffffffu : (1u << layerMaterialShift) - 1u;
        flags->set(0, packLayerFlags(-1, false, false, -1, Render::Pbr::MapLayout::Orm) & flagMask);
        std::vector<std::string> diffusePaths;
        diffusePaths.reserve(maps.layers.size());
        for (const TerrainLayer& layer : maps.layers)
            diffusePaths.push_back(layer.diffuseMap);
        Misc::LoadTrace::Scope traceDiffuseReads(
            "terrain.layers.read.diffuse", static_cast<std::int64_t>(diffusePaths.size()));
        const auto diffuseImages = vsgUtil::readArrayImages(
            diffusePaths, mImageOptions, terrainImageUse(vsgUtil::TextureCap::Kind::Colour), true);
        traceDiffuseReads.end();
        for (size_t i = 0; i < maps.layers.size(); ++i)
        {
            const TerrainLayer& layer = maps.layers[i];
            vsgUtil::setName(*diffuseImages[i], layer.diffuseMap);
            imageData.push_back(diffuseImages[i]);
            const int normalIndex = layer.normalMapIndex >= 0 ? arrayIndex[layer.normalMapIndex] : -1;
            const int materialIndex = (layer.materialMapIndex >= 0 && !materialArrayIndex.empty())
                ? materialArrayIndex[layer.materialMapIndex]
                : -1;
            flags->set(i,
                packLayerFlags(normalIndex, layer.parallax, layer.specular, materialIndex, layer.materialLayout)
                    & flagMask);
            // The tier 2 scales of the layer's texture: a roughness scale (0: none) and a reflectance (-1: none).
            vsg::vec4 scale(0.f, -1.f, 0.f, 0.f);
            if (mMapSettings.pbr && i < layerValues.size())
            {
                const Render::Pbr::OverrideValues& values = layerValues[i];
                if (values.roughnessScale)
                    scale.x = static_cast<float>(*values.roughnessScale);
                if (values.reflectance)
                    scale.y = static_cast<float>(*values.reflectance);
                else if (values.specularLevel)
                    scale.y = Render::Pbr::reflectanceFromF0(static_cast<float>(*values.specularLevel));
            }
            if (scales)
                scales->set(i, scale);
            if (heights)
            {
                // The layer's array index (-1: none) and its depth in texture coordinates, `parallax scale` included.
                const int heightIndex = layer.heightMapIndex >= 0 ? heightArrayIndex[layer.heightMapIndex] : -1;
                float displacement = std::numeric_limits<float>::quiet_NaN(); // not set: the default
                if (i < layerValues.size())
                {
                    const Render::Pbr::OverrideValues& values = layerValues[i];
                    if (values.displacementScale)
                        displacement = static_cast<float>(*values.displacementScale);
                }
                heights->set(i,
                    vsg::vec4(static_cast<float>(heightIndex),
                        Render::Parallax::depthScale(displacement, mMapSettings.parallaxScale), 0.f, 0.f));
            }
        }
        Misc::LoadTrace::Scope traceConvert("terrain.layers.arrays");
        auto arrayData = vsgUtil::convertToArrayTexture(imageData);
        mLayerDescriptor
            = vsg::DescriptorImage::create(mSamplerOptions, arrayData, Pipeline::Descriptors::LAYER_BINDING);

        if (normalData.empty())
        {
            // A flat normal, so the binding is valid; no layer refers to it.
            normalData.push_back(vsg::ubvec4Array2D::create(
                1, 1, vsg::ubvec4(128, 128, 255, 255), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM }));
        }
        mNormalLayerDescriptor = vsg::DescriptorImage::create(
            mSamplerOptions, vsgUtil::convertToArrayTexture(normalData), Pipeline::Descriptors::NORMAL_LAYER_BINDING);
        mLayerFlagsDescriptor = vsg::DescriptorBuffer::create(
            flags, Pipeline::Descriptors::LAYER_FLAGS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        if (heights)
        {
            if (heightData.empty())
            {
                // A single RGBA texel (the array conversion takes only RGBA8 or block formats) so the binding is valid;
                // no layer refers to it.
                heightData.push_back(vsg::ubvec4Array2D::create(
                    1, 1, vsg::ubvec4(255, 255, 255, 255), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM }));
            }
            vsg::ref_ptr<vsg::Data> heightArray;
            try
            {
                heightArray = vsgUtil::convertToArrayTexture(heightData);
            }
            catch (const std::exception& e)
            {
                // A height file of a format the array cannot hold (one byte a texel, say): no layer uses its file.
                std::cerr << "Terrain height maps are not used: " << e.what() << std::endl;
                heightArray = vsgUtil::convertToArrayTexture(vsg::DataList{ vsg::ubvec4Array2D::create(
                    1, 1, vsg::ubvec4(255, 255, 255, 255), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM }) });
                for (size_t i = 0; i < heights->size(); ++i)
                {
                    vsg::vec4 info = heights->at(i);
                    info.x = -1.f;
                    heights->set(i, info);
                }
            }
            mHeightLayerDescriptor = vsg::DescriptorImage::create(
                mSamplerOptions, heightArray, Pipeline::Descriptors::HEIGHT_LAYER_BINDING);
            mLayerHeightsDescriptor = vsg::DescriptorBuffer::create(
                heights, Pipeline::Descriptors::LAYER_HEIGHTS_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
        if (mMapSettings.pbr)
        {
            if (materialData.empty())
            {
                // A neutral texel so the binding is valid; no layer refers to it.
                materialData.push_back(vsg::ubvec4Array2D::create(
                    1, 1, vsg::ubvec4(255, 0, 255, 255), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM }));
            }
            mMaterialLayerDescriptor = vsg::DescriptorImage::create(mSamplerOptions,
                vsgUtil::convertToArrayTexture(materialData), Pipeline::Descriptors::MATERIAL_LAYER_BINDING);
            mLayerScalesDescriptor = vsg::DescriptorBuffer::create(
                scales, Pipeline::Descriptors::LAYER_SCALES_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
    }

    bool Builder::replaceArrayDescriptor(const vsg::Descriptor* old, const vsg::ref_ptr<vsg::Descriptor>& replacement)
    {
        waitForLayers();
        std::scoped_lock lock(mDescriptorMutex);
        for (auto* member :
            { &mLayerDescriptor, &mNormalLayerDescriptor, &mMaterialLayerDescriptor, &mHeightLayerDescriptor })
        {
            if (member->get() == old)
            {
                *member = replacement;
                return true;
            }
        }
        return false;
    }

    void Builder::replaceBinds(
        const std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>& replaced)
    {
        dataCache.forEach([&](const Key&, BatchData& data) {
            for (auto& command : data.stateCommands)
            {
                if (const auto* bind = command->cast<vsg::BindDescriptorSet>())
                {
                    const auto found = replaced.find(bind);
                    if (found != replaced.end())
                        command = found->second;
                }
            }
        });
    }

    Builder::BatchData Builder::create(const Key& key) const
    {
        waitForLayers();
        const auto& bounds = key.first;
        auto lod = key.second;

        auto data = mStorage->getVertexData(lod, bounds);

        // Task 0055: a baked picture replaces the layers and the blend map (not with the terrain maps on, whose
        // per-layer data it cannot give).
        vsg::ref_ptr<vsg::Data> diffuseOverride;
        if (!mMapSettings.enabled())
            diffuseOverride = mStorage->getDiffuseOverride(bounds);
        vsg::ref_ptr<vsg::Data> blendmap;
        vsg::ref_ptr<vsg::Descriptor> layerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> normalLayerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> materialLayerDescriptor;
        vsg::ref_ptr<vsg::Descriptor> heightLayerDescriptor;
        {
            // A texture mip swap may replace these while a paging thread builds a batch.
            std::scoped_lock lock(mDescriptorMutex);
            layerDescriptor = mLayerDescriptor;
            normalLayerDescriptor = mNormalLayerDescriptor;
            materialLayerDescriptor = mMaterialLayerDescriptor;
            heightLayerDescriptor = mHeightLayerDescriptor;
        }
        if (diffuseOverride)
        {
            // One blend texel whose four layer indices are 0: the shader then reads layer 0 at the chunk's own texture
            // coordinates, which run once over the chunk.
            blendmap
                = vsg::ubvec4Array2D::create(1, 1, vsg::ubvec4(), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
            auto bakedSampler = vsg::Sampler::create();
            bakedSampler->minFilter = VK_FILTER_LINEAR;
            bakedSampler->magFilter = VK_FILTER_LINEAR;
            bakedSampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            bakedSampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            bakedSampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if (mSamplerOptions)
            {
                bakedSampler->anisotropyEnable = mSamplerOptions->anisotropyEnable;
                bakedSampler->maxAnisotropy = mSamplerOptions->maxAnisotropy;
            }
            if (auto& sharedObjects = mImageOptions->sharedObjects)
                sharedObjects->share(bakedSampler);
            layerDescriptor = vsg::DescriptorImage::create(bakedSampler,
                vsgUtil::convertToArrayTexture({ diffuseOverride }), Pipeline::Descriptors::LAYER_BINDING);
        }
        else
            blendmap = mStorage->getBlendmap(bounds);

        auto layout = mPipeline->pipeline->layout;

        auto blendSampler = vsg::Sampler::create();
        if (auto& sharedObjects = mImageOptions->sharedObjects)
            sharedObjects->share(blendSampler);
        auto blendDescriptor
            = vsg::DescriptorImage::create(blendSampler, blendmap, Pipeline::Descriptors::BLENDMAP_BINDING);

        auto heightsDescriptor
            = vsg::DescriptorImage::create(mSamplerOptions, data.heights, Pipeline::Descriptors::HEIGHTMAP_BINDING);
        auto normalsDescriptor
            = vsg::DescriptorImage::create(mSamplerOptions, data.normals, Pipeline::Descriptors::NORMALMAP_BINDING);
        auto colorDescriptor
            = vsg::DescriptorImage::create(mSamplerOptions, data.colors, Pipeline::Descriptors::COLORMAP_BINDING);

        auto batchData = vsg::Value<Pipeline::Data::Batch>::create(makeBatchUniform(bounds.min,
            static_cast<float>(mStorage->numVerts(bounds.size, lod)), mStorage->cellWorldSize * bounds.size));
        // Task 0087: a world LOD tile changes its fade every frame it dithers, so its uniform is uploaded again when
        // marked (the live terrain keeps the static copy).
        if (mDynamicBatchData)
            batchData->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        auto batchDescriptor = vsg::DescriptorBuffer::create(batchData, Pipeline::Descriptors::BATCH_DATA_BINDING);

        vsg::Descriptors descriptors = { blendDescriptor, batchDescriptor, layerDescriptor, heightsDescriptor,
            normalsDescriptor, colorDescriptor };
        if (normalLayerDescriptor)
        {
            descriptors.push_back(normalLayerDescriptor);
            descriptors.push_back(mLayerFlagsDescriptor);
        }
        if (materialLayerDescriptor)
        {
            descriptors.push_back(materialLayerDescriptor);
            descriptors.push_back(mLayerScalesDescriptor);
        }

        if (heightLayerDescriptor)
        {
            descriptors.push_back(heightLayerDescriptor);
            descriptors.push_back(mLayerHeightsDescriptor);
        }

        auto bds = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptors);

        /*
         * Assigns grid of height ranges so we can quickly look up the bounds for a given chunk.
         */
        uint32_t gridSize = bounds.size / Bounds::minSize;
        auto ranges = vsg::vec2Array2D::create(gridSize, gridSize);
        {
            for (uint32_t gridX = 0; gridX < gridSize; ++gridX)
            {
                for (uint32_t gridY = 0; gridY < gridSize; ++gridY)
                {
                    float minH = std::numeric_limits<float>::max();
                    float maxH = -std::numeric_limits<float>::max();
                    uint32_t verts0 = mStorage->numVerts(bounds.size / gridSize, lod);
                    uint32_t startX = gridX * (verts0 - 1);
                    uint32_t startY = gridY * (verts0 - 1);
                    for (uint32_t x = startX; x < startX + verts0; ++x)
                    {
                        for (uint32_t y = startY; y < startY + verts0; ++y)
                        {
                            float h = data.heights->at(x, y);
                            maxH = std::max(maxH, h);
                            minH = std::min(minH, h);
                        }
                    }
                    ranges->at(gridX, gridY) = { minH, maxH };
                }
            }
        }

        return { vsg::StateCommands{ mPipeline, bds }, ranges, batchData };
    }

    bool Builder::setBatchFade(const BatchData& data, float fade)
    {
        auto* uniform = data.batchUniform ? data.batchUniform->cast<vsg::Value<Pipeline::Data::Batch>>() : nullptr;
        if (!uniform)
            return false;
        const float clamped = std::clamp(fade, 0.f, 1.f);
        if (uniform->value().fade != clamped)
        {
            uniform->value().fade = clamped;
            uniform->dirty();
        }
        return true;
    }

    bool Builder::setBatchShare(const BatchData& data, float fade, float complementOf)
    {
        auto* uniform = data.batchUniform ? data.batchUniform->cast<vsg::Value<Pipeline::Data::Batch>>() : nullptr;
        if (!uniform)
            return false;
        constexpr float steps = 1024.f;
        auto rounded = [](float value) { return std::round(std::clamp(value, 0.f, 1.f) * steps) / steps; };
        const float code = complementOf >= 0.f ? -1.f - rounded(complementOf) : rounded(fade);
        if (uniform->value().fade != code)
        {
            uniform->value().fade = code;
            uniform->dirty();
        }
        return true;
    }

    Builder::Batch Builder::createBatch(const Bounds& bounds, uint8_t lod) const
    {
        auto batchData = getBatchData(bounds, lod);
        auto sg = vsg::StateGroup::create();
        sg->stateCommands = batchData.stateCommands;

        vsg::vec2 origin = bounds.min * mStorage->cellWorldSize;

        auto arrayState = HeightArrayState::create();
        arrayState->vertexScale = bounds.size * mStorage->cellWorldSize;
        arrayState->origin = origin;
        arrayState->heightBinding = Pipeline::Descriptors::HEIGHTMAP_BINDING;
        sg->prototypeArrayState = arrayState;
        arrayState->parent = sg.get();

        auto cullNode = createCullNode(sg, bounds, batchData, bounds);
        return { bounds, lod, batchData, cullNode, sg };
    }

    Builder::BatchData Builder::getBatchData(const Bounds& bounds, uint8_t lod) const
    {
        return dataCache.getOrCreate(std::make_pair(bounds, lod), *this);
    }

    vsg::ref_ptr<vsg::Node> Builder::createDraw(const Builder::Batch& batch, const Bounds& bounds, uint8_t lod,
        uint32_t lodFlags, ChunkShare share, DrawHandle* handle) const
    {
        uint32_t numVerts = mStorage->numVerts(bounds.size, lod);
        auto bindIndexBuffer = indexCache.getOrCreate(std::make_pair(numVerts, lodFlags), CreateIndexBuffer());
        uint32_t instance = static_cast<uint32_t>(lod - batch.lod);
        uint32_t shiftSize = ilog2(batch.bounds.size / bounds.size);
        instance |= shiftSize << 8;
        vsg::vec2 gridPos = (bounds.min - batch.bounds.min) / bounds.size;
        instance |= static_cast<uint32_t>(gridPos.x) << 16;
        instance |= static_cast<uint32_t>(gridPos.y) << 24;
        auto draw = vsg::DrawIndexed::create(
            bindIndexBuffer->indices->data->valueCount(), 1, 0, 0, encodeChunkShare(instance, share));
        if (handle != nullptr)
        {
            handle->draw = draw;
            handle->baseInstance = instance;
        }
        auto commands = vsg::Commands::create();
        commands->children = { bindIndexBuffer, draw };
        if (bounds.size < batch.bounds.size)
            return createCullNode(commands, batch.bounds, batch.data, bounds);
        else
            return commands;
    }

    vsg::ref_ptr<vsg::Node> Builder::createCullNode(vsg::ref_ptr<vsg::Node> child, const Bounds& batchBounds,
        const Builder::BatchData& data, const Bounds& bounds) const
    {
        const vsg::vec2 range = heightRange(data, batchBounds, bounds);
        const float minH = range.x;
        const float maxH = range.y;
        float worldSize = bounds.size * mStorage->cellWorldSize;
        vsg::vec2 offset = bounds.min * mStorage->cellWorldSize;
        vsg::dbox bb
            = { vsg::dvec3(offset.x, offset.y, minH), vsg::dvec3(offset.x + worldSize, offset.y + worldSize, maxH) };
        return vsg::CullNode::create(vsgUtil::toSphere(bb), child);
    }

    vsg::vec2 Builder::heightRange(const BatchData& data, const Bounds& batchBounds, const Bounds& bounds) const
    {
        vsg::vec2 heightRangeGridPos = (bounds.min - batchBounds.min) / Bounds::minSize;
        uint32_t heightRangeWidth = static_cast<uint32_t>(bounds.size / Bounds::minSize);
        float minH = std::numeric_limits<float>::max();
        float maxH = -std::numeric_limits<float>::max();
        for (uint32_t x = 0; x < heightRangeWidth; ++x)
        {
            for (uint32_t y = 0; y < heightRangeWidth; ++y)
            {
                uint32_t hx = x + heightRangeGridPos.x;
                uint32_t hy = y + heightRangeGridPos.y;
                hx = std::max(0u, std::min(hx, data.heightRanges->width() - 1));
                hy = std::max(0u, std::min(hy, data.heightRanges->height() - 1));
                const auto& range = data.heightRanges->at(hx, hy);
                minH = std::min(range.x, minH);
                maxH = std::max(range.y, maxH);
            }
        }
        return { minH, maxH };
    }
}
