#include "skyrimmodel.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>

#include <components/nifscene/build.hpp>
#include <components/nifscene/header.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/sets.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/group.hpp>
#include <components/vsgutil/id.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/share.hpp>
#include <components/vsgutil/shareimage.hpp>

namespace MWRender::SkyrimModel
{
    namespace
    {
        // The NIF blend factor codes (NifScene::AlphaProperty) as Vulkan factors; the same table the Morrowind reader
        // uses (nif.cpp convertBlendFactor). An unknown code is the usual alpha blend factor for its side.
        VkBlendFactor blendFactor(std::uint8_t code, VkBlendFactor fallback)
        {
            switch (code)
            {
                case 0:
                    return VK_BLEND_FACTOR_ONE;
                case 1:
                    return VK_BLEND_FACTOR_ZERO;
                case 2:
                    return VK_BLEND_FACTOR_SRC_COLOR;
                case 3:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case 4:
                    return VK_BLEND_FACTOR_DST_COLOR;
                case 5:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case 6:
                    return VK_BLEND_FACTOR_SRC_ALPHA;
                case 7:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case 8:
                    return VK_BLEND_FACTOR_DST_ALPHA;
                case 9:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case 10:
                    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
                default:
                    return fallback;
            }
        }

        template <class Array, class Source>
        vsg::ref_ptr<Array> copyArray(const Source& vec)
        {
            vsg::ref_ptr<Array> array = Array::create(static_cast<std::uint32_t>(vec.size()));
            std::memcpy(
                static_cast<void*>(array->data()), vec.data(), vec.size() * sizeof(typename Source::value_type));
            return array;
        }

        // Skyrim texture paths use backslashes and may have upper case; the VFS wants its normalized form, which the
        // texture options' file callback builds from a slash separated path.
        std::string texturePath(const std::string& path)
        {
            std::string out = path;
            std::replace(out.begin(), out.end(), '\\', '/');
            return out;
        }

        using MaterialValue = vsg::Value<Pipeline::Data::Material>;

        // Resource::Fallback's image for a file that could not be read (resourcesystem.cpp createTextureOptions): one
        // magenta texel.
        bool isFallbackImage(const vsg::Data& data)
        {
            if (data.width() != 1 || data.height() != 1 || data.depth() != 1)
                return false;
            const auto* texel = dynamic_cast<const vsg::ubvec4Array2D*>(&data);
            return texel != nullptr && texel->at(0, 0) == vsg::ubvec4(0xff, 0x00, 0xff, 0xff);
        }

        // OPENEDEN_NIFSCENE_TRACE=1 writes one line per drawn shape to stderr (a developer aid, read once).
        bool traceEnabled()
        {
            static const bool enabled = std::getenv("OPENEDEN_NIFSCENE_TRACE") != nullptr;
            return enabled;
        }

        vsg::ref_ptr<vsg::Node> buildShape(
            const NifScene::Flat::Shape& shape, const BuildContext& context, BuildResult& result, Counters& totals)
        {
            const NifScene::Flat::MaterialKey& key = shape.mKey;
            Pipeline::Options options;
            auto& shader = options.shader;

            // Vertex streams, in the order the pipeline's attribute layout expects (VERTEX, NORMAL, COLOR, TEXCOORD).
            vsg::DataList dataList;
            shader.addMode(Pipeline::Mode::VERTEX);
            dataList.emplace_back(copyArray<vsg::vec3Array>(shape.mPositions));
            shader.addMode(Pipeline::Mode::NORMAL);
            dataList.emplace_back(copyArray<vsg::vec3Array>(shape.mNormals));
            vsgUtil::setID(*dataList.back(), static_cast<int>(Pipeline::Mode::NORMAL));
            if (!shape.mColors.empty())
            {
                shader.addMode(Pipeline::Mode::COLOR);
                dataList.emplace_back(copyArray<vsg::vec4Array>(shape.mColors));
                options.colorMode = 2; // the vertex colour is the ambient and diffuse colour, as NiVertexColorProperty
            }
            if (!shape.mUV.empty())
            {
                shader.addMode(Pipeline::Mode::TEXCOORD);
                dataList.emplace_back(copyArray<vsg::vec2Array>(shape.mUV));
                options.numUvSets = 1;
            }

            // The diffuse texture, read through the VFS like every other model texture.
            vsg::Descriptors descriptors;
            if (!key.mDiffuse.empty())
            {
                vsgUtil::ImageUse use;
                use.category = vsgUtil::TextureCap::Category::Object;
                use.kind = vsgUtil::TextureCap::Kind::Colour;
                if (key.mTest)
                    use.alphaCutoff = key.mThreshold / 255.f;
                auto data = vsgUtil::readOptionalImage(texturePath(key.mDiffuse), context.mImageOptions, use);
                if (data)
                {
                    auto sampler = context.mBuilder.createSampler();
                    sampler->addressModeU = (key.mClampMode >> 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                                  : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    sampler->addressModeV = (key.mClampMode & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                                 : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    descriptors.emplace_back(vsgUtil::sharedDescriptorImage(
                        context.mSharedObjects, sampler, data, Pipeline::Descriptors::DIFFUSE_UNIT));
                    shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
                    // The game's texture options end with a fallback reader that answers a missing file with a 1x1
                    // magenta image (as for Morrowind); it is drawn, so that a miss is visible, and counted as one.
                    if (isFallbackImage(*data))
                    {
                        ++result.mTexturesMissing;
                        ++totals.mTexturesMissing;
                    }
                    else
                    {
                        ++result.mTexturesLoaded;
                        ++totals.mTexturesLoaded;
                    }
                }
                else
                {
                    ++result.mTexturesMissing;
                    ++totals.mTexturesMissing;
                }
            }

            // Fixed state from the key.
            options.cullMode = key.mDoubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            if (key.mDoubleSided)
                ++totals.mDoubleSidedShapes;
            options.blend = key.mBlend;
            if (key.mBlend)
            {
                options.srcBlendFactor = options.srcAlphaBlendFactor
                    = blendFactor(key.mSourceBlend, VK_BLEND_FACTOR_SRC_ALPHA);
                options.dstBlendFactor = options.dstAlphaBlendFactor
                    = blendFactor(key.mDestinationBlend, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
                // As the Morrowind reader: a destination alpha factor is not meaningful on an opaque target.
                if (options.dstBlendFactor == VK_BLEND_FACTOR_DST_ALPHA)
                    options.dstBlendFactor = options.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                options.depthWrite = false; // a blended surface does not write depth
            }
            options.alphaTestMode = key.mTest ? key.mTestMode : 0;

            // The material: white, with the shader's alpha and the alpha test cutoff.
            auto material = MaterialValue::create(Pipeline::Material::createDefault());
            material->value().diffuse.a = key.mAlpha;
            material->value().alphaTestCutoff = key.mTest ? key.mThreshold / 255.f : 1.f;
            vsgUtil::share_if(context.mSharedObjects, material);
            auto materialDescriptor = vsg::DescriptorBuffer::create(
                material, Pipeline::Descriptors::MATERIAL_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            vsgUtil::share_if(context.mSharedObjects, materialDescriptor);
            descriptors.emplace_back(materialDescriptor);
            shader.addMode(Pipeline::Mode::MATERIAL);

            auto geometry = vsg::VertexIndexDraw::create();
            geometry->assignArrays(dataList);
            geometry->instanceCount = 1;
            geometry->indexCount = static_cast<std::uint32_t>(shape.mIndices.size());
            geometry->assignIndices(copyArray<vsg::uintArray>(shape.mIndices));

            auto bindPipeline = context.mBuilder.graphics->getOrCreate(options);
            vsg::ref_ptr<vsg::PipelineLayout> layout = bindPipeline->pipeline->layout;
            auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
            vsgUtil::share_if(context.mSharedObjects, descriptorSet);
            auto bindDescriptors = vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);
            vsgUtil::share_if(context.mSharedObjects, bindDescriptors);

            auto stateGroup = vsg::StateGroup::create();
            stateGroup->children = { geometry };
            stateGroup->stateCommands = { bindPipeline, bindDescriptors };
            ++result.mDraws;
            if (traceEnabled())
                std::cerr << "nifscene: " << context.mFilename << " shape '" << shape.mName << "' tex='" << key.mDiffuse
                          << "' loaded=" << (shader.hasMode(Pipeline::Mode::DIFFUSE_MAP) ? 1 : 0)
                          << " dbl=" << key.mDoubleSided << " blend=" << key.mBlend << " test=" << key.mTest
                          << " colors=" << key.mVertexColors << " verts=" << shape.mPositions.size()
                          << " f1=" << std::hex << shape.mFlags1 << " f2=" << shape.mFlags2 << std::dec
                          << " merged=" << shape.mMergedFrom << '\n';
            if (key.mBlend && !key.mNoSorter)
                return vsg::DepthSorted::create(context.mDepthSortedBin, vsgUtil::getBounds(stateGroup), stateGroup);
            return stateGroup;
        }
    }

    Counters& counters()
    {
        static Counters sCounters;
        return sCounters;
    }

    std::string describeCounters()
    {
        const Counters& c = counters();
        std::ostringstream out;
        out << "Skyrim models: " << c.mRequests.load() << " objects asked for a model, " << c.mModels.load()
            << " built (the rest came from the model cache) (" << c.mModelsEmpty.load() << " draw nothing, "
            << c.mFailed.load() << " failed), " << c.mDraws.load() << " draws, " << c.mTriangles.load()
            << " triangles, " << c.mVertices.load() << " vertices, textures " << c.mTexturesLoaded.load()
            << " loaded / " << c.mTexturesMissing.load() << " missing, double sided shapes "
            << c.mDoubleSidedShapes.load() << ", left out: " << c.mSkippedSkinned.load() << " skinned, "
            << c.mSkippedEffect.load() << " effect, " << c.mSkippedNoMaterial.load() << " no shader, "
            << c.mSkippedHidden.load() << " hidden, " << c.mDroppedTriangles.load() << " bad triangles";
        return out.str();
    }

    BuildResult buildNode(const NifScene::Flat::FlatModel& model, const BuildContext& context)
    {
        BuildResult result;
        Counters& totals = counters();
        totals.mModels.fetch_add(1, std::memory_order_relaxed);
        totals.mSkippedSkinned += model.mReport.mSkippedSkinned;
        totals.mSkippedEffect += model.mReport.mSkippedEffect;
        totals.mSkippedNoMaterial += model.mReport.mSkippedNoMaterial;
        totals.mSkippedHidden += model.mReport.mSkippedHidden;
        totals.mDroppedTriangles += model.mReport.mDroppedTriangles;
        totals.mTriangles += model.mReport.mTriangles;
        totals.mVertices += model.mReport.mVertices;

        vsg::Group::Children nodes;
        nodes.reserve(model.mShapes.size());
        for (const NifScene::Flat::Shape& shape : model.mShapes)
            nodes.emplace_back(buildShape(shape, context, result, totals));
        totals.mDraws += result.mDraws;
        if (nodes.empty())
        {
            totals.mModelsEmpty.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        vsg::ref_ptr<vsg::Node> root = vsgUtil::createCullNode(nodes);
        vsgUtil::addLeafCullNodes(root, 2);
        result.mNode = root;
        return result;
    }

    std::shared_ptr<const vsgAdapters::nif::SceneReader> makeSceneReader(
        const Pipeline::Builder& builder, vsg::ref_ptr<const vsg::Options> imageOptions, int depthSortedBin)
    {
        auto reader = std::make_shared<vsgAdapters::nif::SceneReader>();
        reader->accepts = [](std::string_view fileStart) { return NifScene::isSkyrimSeHeader(fileStart); };
        reader->read = [&builder, imageOptions, depthSortedBin](std::string data, const std::string& filename,
                           vsg::ref_ptr<const vsg::Options> options) -> vsg::ref_ptr<vsg::Object> {
            Counters& totals = counters();
            try
            {
                NifScene::SceneOptions sceneOptions;
                Files::IStreamPtr stream(new std::istringstream(std::move(data)));
                const NifScene::Scene scene
                    = NifScene::readScene(std::move(stream), VFS::Path::Normalized(filename), sceneOptions);
                const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene);
                const BuildContext context{ builder, imageOptions, options->sharedObjects, depthSortedBin, filename };
                BuildResult built = buildNode(flat, context);
                if (!built.mNode)
                    return vsg::Group::create(); // a model that draws nothing is still a model, not a missing one
                return built.mNode;
            }
            catch (std::exception& e)
            {
                totals.mFailed.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "!SkyrimModel::read(" << filename << "): " << e.what() << std::endl;
                return {};
            }
        };
        return reader;
    }
}
