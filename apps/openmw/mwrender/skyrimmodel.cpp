#include "skyrimmodel.hpp"

#include "skyrimmaterial.hpp"
#include "skyrimtreesway.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#include <vsg/core/MipmapLayout.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>

#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/facetint.hpp>
#include <components/nifscene/header.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/sets.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/group.hpp>
#include <components/settings/values.hpp>
#include <components/vsgadapters/nif/automaps.hpp>
#include <components/vsgutil/arraytexture.hpp>
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
        // texture options' file callback builds from a slash separated path. A face's own texture slots name their
        // files without the "textures" root ("Actors\\Character\\Male\\MaleHead.dds", task 0125); the game adds it
        // itself, and so does the normaliser.
        std::string texturePath(const std::string& path)
        {
            return NifScene::FaceTint::normaliseTexturePath(path);
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

        // The material side of a draw, shared by static and skinned shapes: the diffuse texture, the fixed state from
        // the key (culling, blending, alpha test) and the material descriptor, added to `options` and `descriptors`.
        void addSurface(const NifScene::Flat::MaterialKey& key, const BuildContext& context, BuildResult& result,
            Counters& totals, Pipeline::Options& options, vsg::Descriptors& descriptors,
            vsg::ref_ptr<vsg::Data> diffuseOverride = {}, const std::array<float, 3>* tintColour = nullptr)
        {
            auto& shader = options.shader;
            // The diffuse texture, read through the VFS like every other model texture (or the face's tinted copy).
            if (!key.mDiffuse.empty())
            {
                vsgUtil::ImageUse use;
                use.category = vsgUtil::TextureCap::Category::Object;
                use.kind = vsgUtil::TextureCap::Kind::Colour;
                if (key.mTest)
                    use.alphaCutoff = key.mThreshold / 255.f;
                auto data = diffuseOverride
                    ? diffuseOverride
                    : vsgUtil::readOptionalImage(texturePath(key.mDiffuse), context.mImageOptions, use);
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

            // Task 0130: the normal map (tangent space; its tangents ride in the draw, see buildShape) and the glow map,
            // each with the diffuse texture's clamp mode and UV set, as the Morrowind reader binds its maps.
            auto addMap = [&](const std::string& path, std::uint32_t binding, vsgUtil::TextureCap::Kind kind,
                              Pipeline::Mode mode) -> vsg::ref_ptr<vsg::Data> {
                vsgUtil::ImageUse mapUse;
                mapUse.category = vsgUtil::TextureCap::Category::Object;
                mapUse.kind = kind;
                auto map = vsgUtil::readOptionalImage(texturePath(path), context.mImageOptions, mapUse);
                if (!map || isFallbackImage(*map))
                {
                    ++result.mTexturesMissing;
                    ++totals.mTexturesMissing;
                    return {};
                }
                auto sampler = context.mBuilder.createSampler();
                sampler->addressModeU = (key.mClampMode >> 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                              : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                sampler->addressModeV = (key.mClampMode & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                             : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                descriptors.emplace_back(vsgUtil::sharedDescriptorImage(context.mSharedObjects, sampler, map, binding));
                shader.addMode(mode);
                ++result.mTexturesLoaded;
                ++totals.mTexturesLoaded;
                return map;
            };
            if (!key.mNormal.empty() && shader.hasMode(Pipeline::Mode::TEXCOORD))
            {
                if (auto map = addMap(key.mNormal, Pipeline::Descriptors::NORMAL_UNIT,
                        vsgUtil::TextureCap::Kind::Normal, Pipeline::Mode::NORMAL_MAP))
                {
                    if (vsgAdapters::isTwoChannelFormat(map->properties.format))
                        shader.addMode(Pipeline::Mode::RECONSTRUCT_NORMAL_Z);
                    ++totals.mNormalMaps;
                }
            }
            if (!key.mGlow.empty() && shader.hasMode(Pipeline::Mode::TEXCOORD))
            {
                if (addMap(key.mGlow, Pipeline::Descriptors::GLOW_UNIT, vsgUtil::TextureCap::Kind::Colour,
                        Pipeline::Mode::GLOW_MAP))
                    ++totals.mGlowMaps;
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
            // (task 0130: SkyrimMaterial::makeMaterial is the default material with the shader's alpha for a key
            // made without [Game] neutral world materials)
            auto material = MaterialValue::create(SkyrimMaterial::makeMaterial(key));
            if (tintColour != nullptr)
            {
                // A hair tint shader's colour (task 0125): the grey hair map is multiplied by it, in the ambient and the
                // diffuse term alike (the alpha of the ambient colour is the material's flag word and stays).
                for (std::size_t c = 0; c < 3; ++c)
                {
                    material->value().diffuse[static_cast<int>(c)] = (*tintColour)[c];
                    material->value().ambient[static_cast<int>(c)] = (*tintColour)[c];
                }
            }
            material->value().alphaTestCutoff = key.mTest ? key.mThreshold / 255.f : 1.f;
            vsgUtil::share_if(context.mSharedObjects, material);
            auto materialDescriptor = vsg::DescriptorBuffer::create(
                material, Pipeline::Descriptors::MATERIAL_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            vsgUtil::share_if(context.mSharedObjects, materialDescriptor);
            descriptors.emplace_back(materialDescriptor);
            shader.addMode(Pipeline::Mode::MATERIAL);
        }

        // An RGBA8 image with a full mip chain (the chain is made by the engine's own integer resize, so it is the same on
        // every platform). The layout is what vsgXchange gives a DDS with mips.
        vsg::ref_ptr<vsg::Data> makeMippedRgba8(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> level)
        {
            std::vector<std::vector<std::uint8_t>> chain;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
            chain.push_back(std::move(level));
            sizes.emplace_back(width, height);
            while (sizes.back().first > 1 || sizes.back().second > 1)
            {
                const std::uint32_t w = std::max(1u, sizes.back().first / 2);
                const std::uint32_t h = std::max(1u, sizes.back().second / 2);
                std::vector<std::uint8_t> next(static_cast<std::size_t>(w) * h * 4);
                vsgUtil::RgbaResize::resize(
                    chain.back().data(), sizes.back().first, sizes.back().second, next.data(), w, h);
                chain.push_back(std::move(next));
                sizes.emplace_back(w, h);
            }
            std::size_t total = 0;
            for (const auto& l : chain)
                total += l.size();
            auto* memory = static_cast<vsg::ubvec4*>(vsg::allocate(total, vsg::ALLOCATOR_AFFINITY_DATA));
            auto layout = vsg::MipmapLayout::create(static_cast<std::uint32_t>(chain.size()));
            std::size_t offset = 0;
            for (std::size_t i = 0; i < chain.size(); ++i)
            {
                std::memcpy(reinterpret_cast<std::uint8_t*>(memory) + offset, chain[i].data(), chain[i].size());
                layout->set(i, vsg::uivec4(sizes[i].first, sizes[i].second, 1u, static_cast<std::uint32_t>(offset)));
                offset += chain[i].size();
            }
            vsg::Data::Properties properties;
            properties.format = VK_FORMAT_R8G8B8A8_UNORM;
            properties.mipLevels = static_cast<std::uint8_t>(chain.size());
            properties.allocatorType = vsg::ALLOCATOR_TYPE_VSG_ALLOCATOR;
            return vsg::ubvec4Array2D::create(width, height, memory, properties, layout);
        }

        bool readRgba(const std::string& path, const BuildContext& context, NifScene::FaceTint::Image& out)
        {
            auto data = vsgUtil::readOptionalImage(texturePath(path), context.mImageOptions, vsgUtil::ImageUse{});
            if (!data || isFallbackImage(*data))
                return false;
            return vsgUtil::detail::decodeToRgba8(*data, out.mRgba, out.mWidth, out.mHeight);
        }

        // The face's diffuse map multiplied by the actor's baked tint (NifScene::FaceTint), or null when either map
        // cannot be read and decoded (the shape then draws its plain diffuse map, as before).
        vsg::ref_ptr<vsg::Data> tintedDiffuse(const NifScene::Skinned::Shape& shape, const BuildContext& context)
        {
            if (shape.mTintTexture.empty() || shape.mKey.mDiffuse.empty())
                return {};
            NifScene::FaceTint::Image base, tint, product;
            if (!readRgba(shape.mKey.mDiffuse, context, base) || !readRgba(shape.mTintTexture, context, tint))
            {
                std::cerr << "SkyrimModel: face tint of '" << context.mFilename << "' not applied (a map is missing or not decodable)\n";
                return {};
            }
            std::string error;
            if (!NifScene::FaceTint::composite(base, tint, product, error))
                return {};
            return makeMippedRgba8(product.mWidth, product.mHeight, std::move(product.mRgba));
        }

        // A NIF transform (skin space to bone space) as the matrix Anim::Skin calls invBindMatrix, converted the way
        // vsgAdapters::nif::convertTrafo does it (the file's rotation is row-major, Anim::Transform's is transposed).
        vsg::mat4 toSkinMatrix(const NifScene::Transform& source)
        {
            Anim::Transform t;
            t.translation = vsg::vec3(source.mTranslation[0], source.mTranslation[1], source.mTranslation[2]);
            t.setScale(source.mScale);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    t.rotation(j, i) = source.mRotation[static_cast<std::size_t>(i * 3 + j)];
            return t.t_transform(vsg::mat4());
        }

        vsg::ref_ptr<vsg::Node> buildSkinnedShape(const NifScene::Skinned::Shape& shape, const BuildContext& context,
            BuildResult& result, Counters& totals)
        {
            const NifScene::Flat::MaterialKey& key = shape.mKey;
            Pipeline::Options options;
            auto& shader = options.shader;
            const auto count = static_cast<std::uint32_t>(shape.mPositions.size());

            // Vertex streams, in the order of the default shader (nif.cpp handleGeometry): vertex, normal, bone
            // indices and weights, texture coordinates.
            shader.addMode(Pipeline::Mode::VERTEX);
            shader.addMode(Pipeline::Mode::NORMAL);
            shader.addMode(Pipeline::Mode::SKIN);
            vsg::DataList dataList;
            dataList.emplace_back(copyArray<vsg::vec3Array>(shape.mPositions));
            dataList.emplace_back(copyArray<vsg::vec3Array>(shape.mNormals));
            auto boneIndices = vsg::vec4Array::create(count);
            auto boneWeights = vsg::vec4Array::create(count);
            for (std::uint32_t v = 0; v < count; ++v)
                for (int k = 0; k < 4; ++k)
                {
                    (*boneIndices)[v][k] = static_cast<float>(shape.mBoneIndices[v][static_cast<std::size_t>(k)]);
                    (*boneWeights)[v][k] = shape.mBoneWeights[v][static_cast<std::size_t>(k)];
                }
            dataList.emplace_back(boneIndices);
            dataList.emplace_back(boneWeights);
            if (!shape.mUV.empty())
            {
                shader.addMode(Pipeline::Mode::TEXCOORD);
                dataList.emplace_back(copyArray<vsg::vec2Array>(shape.mUV));
                options.numUvSets = 1;
            }

            vsg::Descriptors descriptors;
            addSurface(key, context, result, totals, options, descriptors, tintedDiffuse(shape, context),
                shape.mHasTintColor ? &shape.mTintColor : nullptr);

            // Bounds of the shape in its own space, and per bone the same sphere in that bone's space.
            vsg::dvec3 lower(1e300, 1e300, 1e300);
            vsg::dvec3 upper(-1e300, -1e300, -1e300);
            for (const auto& p : shape.mPositions)
                for (int k = 0; k < 3; ++k)
                {
                    lower[k] = std::min(lower[k], static_cast<double>(p[static_cast<std::size_t>(k)]));
                    upper[k] = std::max(upper[k], static_cast<double>(p[static_cast<std::size_t>(k)]));
                }
            const vsg::dvec3 centre = (lower + upper) * 0.5;
            const double radius = vsg::length(upper - lower) * 0.5;

            auto skin = Anim::Skin::create();
            skin->bones.reserve(shape.mBones.size());
            for (const NifScene::Skinned::Bone& bone : shape.mBones)
            {
                const vsg::mat4 inverseBind = toSkinMatrix(bone.mInverseBind);
                const vsg::dvec4 boneCentre = vsg::dmat4(inverseBind) * vsg::dvec4(centre.x, centre.y, centre.z, 1.0);
                skin->bones.push_back({ inverseBind,
                    vsg::dsphere(vsg::dvec3(boneCentre.x, boneCentre.y, boneCentre.z), radius), bone.mName });
            }
            // The skin data's own transform (shape.mSkinTransform) is deliberately NOT applied, unlike Morrowind's
            // (Anim::Skin::transform, nif.cpp): in Skyrim SE files the per-bone binds already map shape-local vertices
            // to bone space, and the skin transform is the shape's node transform (a lift of about 120 units for the
            // body, hands, feet and head-space clothing in the vanilla files, 302 of 605 sampled shapes). Applying it
            // on top dropped every such part 120 units below the actor and left only hair, eyes and brows (no skin
            // transform) standing as a floating head. Measured on the five masters: without it the skinned bodies span
            // z 8 to 115, hands 59 to 79, boots 0 to 33, hair and eyes 105 to 135; with it they sit at -120 to -5.
            skin->boneIndices = boneIndices;
            skin->boneWeights = boneWeights;
            auto boneMatrices = vsg::mat4Array::create(
                static_cast<std::uint32_t>(shape.mBones.size() * (Anim::Skin::keepPreviousMatrices ? 2 : 1)));
            skin->attachTo(*boneMatrices);
            descriptors.push_back(vsg::DescriptorBuffer::create(
                boneMatrices, Pipeline::Descriptors::BONE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

            auto geometry = vsg::VertexIndexDraw::create();
            geometry->assignArrays(dataList);
            geometry->instanceCount = 1;
            geometry->indexCount = static_cast<std::uint32_t>(shape.mIndices.size());
            geometry->assignIndices(copyArray<vsg::uintArray>(shape.mIndices));

            auto bindPipeline = context.mBuilder.graphics->getOrCreate(options);
            vsg::ref_ptr<vsg::PipelineLayout> layout = bindPipeline->pipeline->layout;
            // Not shared through SharedObjects: the bone matrices are this actor's own.
            auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
            auto bindDescriptors = vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);

            auto stateGroup = vsg::StateGroup::create();
            stateGroup->children = { geometry };
            stateGroup->stateCommands = { bindPipeline, bindDescriptors };
            ++result.mDraws;
            if (key.mBlend && !key.mNoSorter)
                return vsg::DepthSorted::create(context.mDepthSortedBin, vsg::dsphere(centre, radius), stateGroup);
            return vsg::CullNode::create(vsg::dsphere(centre, radius), stateGroup);
        }

        vsg::ref_ptr<vsg::Node> buildShape(const NifScene::Flat::Shape& shape, const BuildContext& context,
            BuildResult& result, Counters& totals, float modelHeight)
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
            const bool swaying = key.mSway && context.mSwayUniform && !shape.mColors.empty();
            if (!shape.mColors.empty())
            {
                shader.addMode(Pipeline::Mode::COLOR);
                // Task 0136: a swaying shape's alpha is the sway weight the vertex shader squares (the file's own alpha,
                // its flexibility, scaled by the model's height), and the shader draws it with alpha 1.
                if (swaying)
                {
                    std::vector<std::array<float, 4>> weighted = shape.mColors;
                    for (auto& colour : weighted)
                        colour[3] = static_cast<float>(SkyrimTreeSway::weightByte(colour[3], modelHeight)) / 255.f;
                    dataList.emplace_back(copyArray<vsg::vec4Array>(weighted));
                    shader.addMode(Pipeline::Mode::LOD_SWAY);
                }
                else
                    dataList.emplace_back(copyArray<vsg::vec4Array>(shape.mColors));
                options.colorMode = 2; // the vertex colour is the ambient and diffuse colour, as NiVertexColorProperty
            }
            // The tangent array goes before the texture coordinates (Pipeline::Graphics, lib/attributes.glsl); it is
            // inserted below only if the normal map was read.
            const std::size_t tangentPosition = dataList.size();
            if (!shape.mUV.empty())
            {
                shader.addMode(Pipeline::Mode::TEXCOORD);
                dataList.emplace_back(copyArray<vsg::vec2Array>(shape.mUV));
                options.numUvSets = 1;
            }

            vsg::Descriptors descriptors;
            addSurface(key, context, result, totals, options, descriptors);
            if (swaying)
                totals.mSwayShapes.fetch_add(1, std::memory_order_relaxed);
            if (swaying)
                descriptors.push_back(vsg::DescriptorBuffer::create(context.mSwayUniform,
                    Pipeline::Descriptors::TEXMAT_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
            if (shader.hasMode(Pipeline::Mode::NORMAL_MAP))
            {
                if (shape.mTangents.size() == shape.mPositions.size())
                    dataList.insert(dataList.begin() + static_cast<std::ptrdiff_t>(tangentPosition),
                        copyArray<vsg::vec4Array>(shape.mTangents));
                else
                    shader.removeMode(Pipeline::Mode::NORMAL_MAP); // cannot happen: a key with a normal map has tangents
            }

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
            << c.mSkippedHidden.load() << " hidden, " << c.mDroppedTriangles.load() << " bad triangles; materials: "
            << c.mNormalMaps.load() << " normal maps, " << c.mGlowMaps.load() << " glow maps, "
            << c.mNormalMapsNoTangents.load() << " normal maps without tangents, " << c.mNormalMapsModelSpace.load()
            << " model-space normal maps left out, " << c.mSwayShapes.load() << " swaying tree shapes";
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
        const float modelHeight = model.mBoundsMax[2] - model.mBoundsMin[2];
        for (const NifScene::Flat::Shape& shape : model.mShapes)
            nodes.emplace_back(buildShape(shape, context, result, totals, modelHeight));
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

    BuildResult buildSkinnedNode(const NifScene::Skinned::SkinnedModel& model, const BuildContext& context)
    {
        BuildResult result;
        Counters& totals = counters();
        totals.mModels.fetch_add(1, std::memory_order_relaxed);
        totals.mTriangles += model.mReport.mTriangles;
        totals.mVertices += model.mReport.mVertices;
        totals.mDroppedTriangles += model.mReport.mDroppedTriangles;
        vsg::Group::Children nodes;
        nodes.reserve(model.mShapes.size());
        for (const NifScene::Skinned::Shape& shape : model.mShapes)
            nodes.emplace_back(buildSkinnedShape(shape, context, result, totals));
        totals.mDraws += result.mDraws;
        if (nodes.empty())
        {
            totals.mModelsEmpty.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        auto group = vsg::Group::create();
        group->children = std::move(nodes);
        result.mNode = group;
        return result;
    }

    std::shared_ptr<const vsgAdapters::nif::SceneReader> makeSceneReader(const Pipeline::Builder& builder,
        vsg::ref_ptr<const vsg::Options> imageOptions, int depthSortedBin, vsg::ref_ptr<vsg::mat4Value> swayUniform)
    {
        auto reader = std::make_shared<vsgAdapters::nif::SceneReader>();
        const bool surfaceMaps = Settings::game().mNeutralWorldMaterials; // read once, as the neutral world switch is
        reader->accepts = [](std::string_view fileStart) { return NifScene::isSkyrimSeHeader(fileStart); };
        reader->read = [&builder, imageOptions, depthSortedBin, surfaceMaps, swayUniform](std::string data, const std::string& filename,
                           vsg::ref_ptr<const vsg::Options> options) -> vsg::ref_ptr<vsg::Object> {
            Counters& totals = counters();
            try
            {
                NifScene::SceneOptions sceneOptions;
                Files::IStreamPtr stream(new std::istringstream(std::move(data)));
                const NifScene::Scene scene
                    = NifScene::readScene(std::move(stream), VFS::Path::Normalized(filename), sceneOptions);
                NifScene::Flat::Limits limits;
                limits.mSurfaceMaps = surfaceMaps;
                limits.mTreeSway = swayUniform != nullptr;
                const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene, limits);
                totals.mNormalMapsNoTangents += flat.mReport.mNormalMapsNoTangents;
                totals.mNormalMapsModelSpace += flat.mReport.mNormalMapsModelSpace;
                const BuildContext context{ builder, imageOptions, options->sharedObjects, depthSortedBin, filename,
                    swayUniform };
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
