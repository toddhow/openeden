#include "builder.hpp"

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

#include <components/pipeline/sets.hpp>
#include <components/pipeline/terrain.hpp>
#include <components/render/parallaxmath.hpp>
#include <components/vsgutil/arraystate.hpp>
#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/bounds.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/readimage.hpp>

#include "heightarraystate.hpp"
#include "indexbuffer.hpp"

namespace Pipeline::Descriptors
{
#include <files/shaders/terrain/bindings.glsl>
}
namespace Pipeline::Data
{
    using vsg::vec2;
#include <files/shaders/terrain/data.glsl>
}
namespace Terrain
{
    namespace
    {
        // Task 0044: reads the files and keeps those of the most common size and format, as the normal map array does
        // (convertToArrayTexture fills any other with grey bytes). Returns, for each path, its place in `kept` or -1.
        std::vector<int> readUniformImages(const std::vector<std::string>& paths,
            vsg::ref_ptr<const vsg::Options> options, vsg::DataList& kept, const char* what,
            bool (*accept)(VkFormat) = nullptr)
        {
            using ImageKey = std::tuple<uint32_t, uint32_t, VkFormat>;
            std::vector<vsg::ref_ptr<vsg::Data>> images;
            std::map<ImageKey, size_t> counts;
            for (const auto& path : paths)
            {
                auto data = vsgUtil::readOptionalImage(path, options);
                if (data && accept && !accept(data->properties.format))
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
            std::vector<int> indices(paths.size(), -1);
            for (size_t i = 0; i < images.size(); ++i)
            {
                if (!images[i])
                    continue;
                if (ImageKey{ images[i]->width(), images[i]->height(), images[i]->properties.format } != reference)
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
            mMapSettings.parallaxQuality);
    }

    Builder::~Builder() {}

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

    void Builder::setStorage(Storage* in_storage)
    {
        mStorage = in_storage;

        auto layers = mStorage->getLayers();
        if (layers.size() > 256)
        {
            // Vulkan guarantees a minimum of 256 maxImageArrayLayers.
            // uint8_t type is used for blendMap channels.
            std::cerr << "layerInfos.size() > 256" << std::endl;
            layers.resize(256);
        }

        if (mMapSettings.enabled())
        {
            setLayersWithMaps(layers);
            return;
        }

        vsg::DataList imageData;
        for (const auto& layer : layers)
        {
            auto data = vsgUtil::readImage(layer, mImageOptions);
            vsgUtil::setName(*data, layer);
            imageData.push_back(data);
        }
        auto arrayData = vsgUtil::convertToArrayTexture(imageData);
        mLayerDescriptor
            = vsg::DescriptorImage::create(mSamplerOptions, arrayData, Pipeline::Descriptors::LAYER_BINDING);
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

        // The normal map array keeps only maps of its most common size and format: convertToArrayTexture would fill
        // any other with grey bytes, which decode to a badly tilted normal rather than a flat one. Layers whose map is
        // left out are drawn without one.
        std::vector<vsg::ref_ptr<vsg::Data>> normalImages;
        using ImageKey = std::tuple<uint32_t, uint32_t, VkFormat>;
        std::map<ImageKey, size_t> keyCounts;
        for (const auto& path : maps.normalMaps)
        {
            auto data = vsgUtil::readImage(path, mImageOptions);
            vsgUtil::setName(*data, path);
            normalImages.push_back(data);
            ++keyCounts[{ data->width(), data->height(), data->properties.format }];
        }
        ImageKey reference{};
        size_t referenceCount = 0;
        for (const auto& image : normalImages)
        {
            const ImageKey key{ image->width(), image->height(), image->properties.format };
            if (keyCounts[key] > referenceCount)
            {
                reference = key;
                referenceCount = keyCounts[key];
            }
        }
        vsg::DataList normalData;
        std::vector<int> arrayIndex(normalImages.size(), -1);
        for (size_t i = 0; i < normalImages.size(); ++i)
        {
            const auto& image = normalImages[i];
            if (ImageKey{ image->width(), image->height(), image->properties.format } != reference)
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
            materialArrayIndex = readUniformImages(maps.materialMaps, mImageOptions, materialData, "material map");

        // Task 0045: the same for the height files.
        vsg::DataList heightData;
        std::vector<int> heightArrayIndex;
        vsg::ref_ptr<vsg::vec4Array> heights;
        if (mMapSettings.parallaxQuality > 0)
        {
            heightArrayIndex = readUniformImages(
                maps.heightMaps, mImageOptions, heightData, "height map", Render::Parallax::usableHeightFormat);
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
        for (size_t i = 0; i < maps.layers.size(); ++i)
        {
            const TerrainLayer& layer = maps.layers[i];
            auto data = vsgUtil::readImage(layer.diffuseMap, mImageOptions);
            vsgUtil::setName(*data, layer.diffuseMap);
            imageData.push_back(data);
            const int normalIndex = layer.normalMapIndex >= 0 ? arrayIndex[layer.normalMapIndex] : -1;
            const int materialIndex = (layer.materialMapIndex >= 0 && !materialArrayIndex.empty())
                ? materialArrayIndex[layer.materialMapIndex]
                : -1;
            flags->set(i,
                packLayerFlags(normalIndex, layer.parallax, layer.specular, materialIndex, layer.materialLayout)
                    & flagMask);
            // The tier 2 scales of the layer's texture: a roughness scale (0: none) and a reflectance (-1: none).
            vsg::vec4 scale(0.f, -1.f, 0.f, 0.f);
            if (mMapSettings.pbr && mMapSettings.layerValues)
            {
                const Render::Pbr::OverrideValues values = mMapSettings.layerValues(resolved[i]);
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
                if (mMapSettings.layerValues)
                {
                    const Render::Pbr::OverrideValues values = mMapSettings.layerValues(resolved[i]);
                    if (values.displacementScale)
                        displacement = static_cast<float>(*values.displacementScale);
                }
                heights->set(i,
                    vsg::vec4(static_cast<float>(heightIndex),
                        Render::Parallax::depthScale(displacement, mMapSettings.parallaxScale),
                        0.f, 0.f));
            }
        }
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

    Builder::BatchData Builder::create(const Key& key) const
    {
        const auto& bounds = key.first;
        auto lod = key.second;

        auto data = mStorage->getVertexData(lod, bounds);

        vsg::ref_ptr<vsg::Data> blendmap = mStorage->getBlendmap(bounds);

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

        auto batchData = vsg::Value<Pipeline::Data::Batch>::create(Pipeline::Data::Batch{ .origin = bounds.min,
            .numVerts = static_cast<float>(mStorage->numVerts(bounds.size, lod)),
            .vertexScale = mStorage->cellWorldSize * bounds.size });
        auto batchDescriptor = vsg::DescriptorBuffer::create(batchData, Pipeline::Descriptors::BATCH_DATA_BINDING);

        vsg::Descriptors descriptors = { blendDescriptor, batchDescriptor, mLayerDescriptor, heightsDescriptor,
            normalsDescriptor, colorDescriptor };
        if (mNormalLayerDescriptor)
        {
            descriptors.push_back(mNormalLayerDescriptor);
            descriptors.push_back(mLayerFlagsDescriptor);
        }
        if (mMaterialLayerDescriptor)
        {
            descriptors.push_back(mMaterialLayerDescriptor);
            descriptors.push_back(mLayerScalesDescriptor);
        }

        if (mHeightLayerDescriptor)
        {
            descriptors.push_back(mHeightLayerDescriptor);
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

        return { vsg::StateCommands{ mPipeline, bds }, ranges };
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

    vsg::ref_ptr<vsg::Node> Builder::createDraw(
        const Builder::Batch& batch, const Bounds& bounds, uint8_t lod, uint32_t lodFlags) const
    {
        uint32_t numVerts = mStorage->numVerts(bounds.size, lod);
        auto bindIndexBuffer = indexCache.getOrCreate(std::make_pair(numVerts, lodFlags), CreateIndexBuffer());
        uint32_t instance = static_cast<uint32_t>(lod - batch.lod);
        uint32_t shiftSize = ilog2(batch.bounds.size / bounds.size);
        instance |= shiftSize << 8;
        vsg::vec2 gridPos = (bounds.min - batch.bounds.min) / bounds.size;
        instance |= static_cast<uint32_t>(gridPos.x) << 16;
        instance |= static_cast<uint32_t>(gridPos.y) << 24;
        auto draw = vsg::DrawIndexed::create(bindIndexBuffer->indices->data->valueCount(), 1, 0, 0, instance);
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
        float worldSize = bounds.size * mStorage->cellWorldSize;
        vsg::vec2 offset = bounds.min * mStorage->cellWorldSize;
        vsg::dbox bb
            = { vsg::dvec3(offset.x, offset.y, minH), vsg::dvec3(offset.x + worldSize, offset.y + worldSize, maxH) };
        return vsg::CullNode::create(vsgUtil::toSphere(bb), child);
    }
}
