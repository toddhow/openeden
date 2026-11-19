#include "skyrimmodel.hpp"

#include "skyrimeffectscroll.hpp"

#include "skyrimmaterial.hpp"
#include "skyrimtreesway.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
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
#include <components/debug/debuglog.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/facetint.hpp>
#include <components/nifscene/header.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/sets.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/nif/automaps.hpp>
#include <components/vsgutil/arraytexture.hpp>
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

        // Task 0191: the spell models live under meshes/magic and draw their effect shader shapes behind their own
        // setting; task 0359: every other model draws them behind [Game] neutral fire effects (effectShapesWanted).
        bool isMagicModelPath(const std::string& filename)
        {
            std::string path;
            path.reserve(filename.size());
            for (char c : filename)
                path.push_back(c == '\\' ? '/' : static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
            return path.find("meshes/magic/") != std::string::npos;
        }

        // Task 0359: see the palette effect branch of buildShape.
        constexpr float kPaletteAlphaGain = 2.f;

        // OPENEDEN_NIFSCENE_TRACE=1 writes one line per drawn shape to stderr (a developer aid, read once).
        bool traceEnabled()
        {
            static const bool enabled = std::getenv("OPENEDEN_NIFSCENE_TRACE") != nullptr;
            return enabled;
        }

        // Task 0153 follow-up: a model read with a Pipeline::Override that has a materialData hook (the neutral grass's
        // groundcover context) gets that hook applied to the shape's material, and the hook's diffuse colour also
        // multiplies the shape's vertex colours, because the lit shader takes the vertex colour in place of the
        // material's colour when a shape has them (colour mode 2). Without an override, or a hook, this is white and
        // nothing changes. The hook's alpha test cutoff is NOT taken (the file's own threshold stays).
        std::array<float, 3> overrideTint(const BuildContext& context)
        {
            std::array<float, 3> tint{ 1.f, 1.f, 1.f };
            if (context.mOverride && context.mOverride->materialData)
            {
                Pipeline::Data::Material probe = Pipeline::Material::createDefault();
                context.mOverride->materialData(probe);
                tint = { probe.diffuse.r, probe.diffuse.g, probe.diffuse.b };
            }
            return tint;
        }

        // The material side of a draw, shared by static and skinned shapes: the diffuse texture, the fixed state from
        // the key (culling, blending, alpha test) and the material descriptor, added to `options` and `descriptors`.
        void addSurface(const NifScene::Flat::MaterialKey& key, const BuildContext& context, BuildResult& result,
            Counters& totals, Pipeline::Options& options, vsg::Descriptors& descriptors,
            vsg::ref_ptr<vsg::Data> diffuseOverride = {}, const std::array<float, 3>* tintColour = nullptr)
        {
            auto& shader = options.shader;
            // The diffuse texture, read through the VFS like every other model texture (or the face's tinted copy).
            // OPENEDEN_NIFSCENE_TINT=1 (developer aid, task 0152): no textures, every model in a flat colour of its own
            static const bool tintModels = std::getenv("OPENEDEN_NIFSCENE_TINT") != nullptr;
            if (!key.mDiffuse.empty() && !tintModels)
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
                    sampler->addressModeV
                        = (key.mClampMode & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    descriptors.emplace_back(vsgUtil::sharedDescriptorImage(
                        context.mSharedObjects, sampler, data, Pipeline::Descriptors::DIFFUSE_UNIT));
                    shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
                    // The game's texture options end with a fallback reader that answers a missing file with a 1x1
                    // magenta image (as for Morrowind); it is drawn, so that a miss is visible, and counted as one.
                    if (isFallbackImage(*data))
                    {
                        ++result.mTexturesMissing;
                        ++totals.mTexturesMissing;
                        if (traceEnabled())
                            Log(Debug::Info)
                                << "NIFSCENE missing texture '" << key.mDiffuse << "' in " << context.mFilename;
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
                    if (traceEnabled())
                        Log(Debug::Info) << "NIFSCENE missing texture (no image) '" << key.mDiffuse << "' in "
                                         << context.mFilename;
                }
            }

            // Task 0130: the normal map (tangent space; its tangents ride in the draw, see buildShape) and the glow
            // map, each with the diffuse texture's clamp mode and UV set, as the Morrowind reader binds its maps.
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
                sampler->addressModeU
                    = (key.mClampMode >> 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                sampler->addressModeV
                    = (key.mClampMode & 1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
            // OPENEDEN_NIFSCENE_NOCULL=1 (a developer aid, task 0152) draws every shape double sided: a hole that fills
            // up is a face culled from behind.
            static const bool noCull = std::getenv("OPENEDEN_NIFSCENE_NOCULL") != nullptr;
            options.cullMode = (key.mDoubleSided || noCull) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
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
                // A hair tint shader's colour (task 0125): the grey hair map is multiplied by it, in the ambient and
                // the diffuse term alike (the alpha of the ambient colour is the material's flag word and stays).
                for (std::size_t c = 0; c < 3; ++c)
                {
                    material->value().diffuse[static_cast<int>(c)] = (*tintColour)[c];
                    material->value().ambient[static_cast<int>(c)] = (*tintColour)[c];
                }
            }
            if (tintModels)
            {
                const std::size_t h = std::hash<std::string>{}(context.mFilename);
                const int hue = int((h >> 5) % 6);
                const float r = (hue == 0 || hue == 2 || hue == 5) ? 1.f : 0.1f;
                const float g = (hue == 1 || hue == 2 || hue == 3) ? 1.f : 0.1f;
                const float b = (hue == 3 || hue == 4 || hue == 5) ? 1.f : 0.1f;
                for (int c = 0; c < 3; ++c)
                {
                    const float v = c == 0 ? r : c == 1 ? g : b;
                    material->value().diffuse[c] = v;
                    material->value().ambient[c] = v;
                }
            }
            if (context.mOverride && context.mOverride->materialData)
                context.mOverride->materialData(material->value());
            material->value().alphaTestCutoff = key.mTest ? key.mThreshold / 255.f : 1.f;
            vsgUtil::share_if(context.mSharedObjects, material);
            auto materialDescriptor = vsg::DescriptorBuffer::create(
                material, Pipeline::Descriptors::MATERIAL_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            vsgUtil::share_if(context.mSharedObjects, materialDescriptor);
            descriptors.emplace_back(materialDescriptor);
            shader.addMode(Pipeline::Mode::MATERIAL);
        }

        // An RGBA8 image with a full mip chain (the chain is made by the engine's own integer resize, so it is the same
        // on every platform). The layout is what vsgXchange gives a DDS with mips.
        vsg::ref_ptr<vsg::Data> makeMippedRgba8(
            std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> level)
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

        // BSLightingShaderType of the face tint shader (the same number as in components/nifscene/skinned.cpp).
        constexpr std::uint32_t kFaceTintShaderType = 4;

        // The face's diffuse map multiplied by the actor's baked tint (NifScene::FaceTint), or null when either map
        // cannot be read and decoded (the shape then draws its plain diffuse map, as before).
        vsg::ref_ptr<vsg::Data> tintedDiffuse(const NifScene::Skinned::Shape& shape, const BuildContext& context)
        {
            if (shape.mTintTexture.empty() || shape.mKey.mDiffuse.empty())
                return {};
            NifScene::FaceTint::Image base, tint, product;
            if (!readRgba(shape.mKey.mDiffuse, context, base) || !readRgba(shape.mTintTexture, context, tint))
            {
                std::cerr << "SkyrimModel: face tint of '" << context.mFilename
                          << "' not applied (a map is missing or not decodable)\n";
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

        vsg::ref_ptr<vsg::Node> buildSkinnedShape(
            const NifScene::Skinned::Shape& shape, const BuildContext& context, BuildResult& result, Counters& totals)
        {
            const NifScene::Flat::MaterialKey& key = shape.mKey;
            Pipeline::Options options;
            auto& shader = options.shader;
            const auto count = static_cast<std::uint32_t>(shape.mPositions.size());

            // Vertex streams, in the order of the default shader (nif.cpp handleGeometry): vertex, normal, bone
            // indices and weights, texture coordinates.
            shader.addMode(Pipeline::Mode::VERTEX);
            shader.addMode(Pipeline::Mode::NORMAL);
            // Task 0355 stage e: lit by the cell's ambient cube where one is loaded (else the flat ambient).
            if (Settings::game().mNeutralAmbientCube.get())
                shader.addMode(Pipeline::Mode::AMBIENT_CUBE);
            shader.addMode(Pipeline::Mode::SKIN);
            // Task 0355 follow-up: Skyrim surfaces take their point lights per pixel (round pools, not vertex smears).
            shader.addMode(Pipeline::Mode::FORCE_PER_PIXEL_LIGHTING);
            vsg::DataList dataList;
            auto positions = copyArray<vsg::vec3Array>(shape.mPositions);
            if (context.mMorphableFace && shape.mShaderType == kFaceTintShaderType)
            {
                positions->properties.dataVariance = vsg::DYNAMIC_DATA;
                result.mFaceShapes.push_back(positions);
            }
            dataList.emplace_back(positions);
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
            // Task 0355 follow-up: Skyrim surfaces take their point lights per pixel (round pools, not vertex smears).
            shader.addMode(Pipeline::Mode::FORCE_PER_PIXEL_LIGHTING);
            // Task 0355 stage e: as above.
            if (Settings::game().mNeutralAmbientCube.get())
                shader.addMode(Pipeline::Mode::AMBIENT_CUBE);
            dataList.emplace_back(copyArray<vsg::vec3Array>(shape.mNormals));
            vsgUtil::setID(*dataList.back(), static_cast<int>(Pipeline::Mode::NORMAL));
            const bool swaying = key.mSway && context.mSwayUniform && !shape.mColors.empty();
            if (key.mEffect)
            {
                // Task 0191: an effect shape is drawn unlit (colour mode 3: the vertex colour times the texture, alpha
                // included): the shader's base colour times the file's vertex colour, and its alpha times the vertex
                // alpha when the shader uses it. Blending comes from the key.
                std::array<float, 4> base = key.mEffectColor;
                bool paletteRead = false; // set when the palette texel below replaced the colour
                if (!key.mPalette.empty())
                {
                    // The palette lookup is not played: one texel of the greyscale texture (below) replaces the base
                    // colour. A palette that cannot be read leaves the base colour.
                    NifScene::FaceTint::Image palette;
                    if (readRgba(key.mPalette, context, palette) && palette.mWidth > 0 && palette.mHeight > 0)
                    {
                        // The palette is a gradient (greyscale value along it, the effect's alpha down it): the lookup
                        // is not played, the texel of the column five eighths along that is the most colourful and
                        // opaque stands for it, looking at the upper three quarters of the rows (orange for the fire
                        // gradients, blue for the ward's; the top rows are transparent, the bottom ones white or blue).
                        const std::size_t x = std::min<std::size_t>(palette.mWidth - 1, palette.mWidth * 5 / 8);
                        std::size_t best = 0;
                        int bestScore = -1;
                        for (std::size_t row = 0; row < palette.mHeight * 3 / 4; ++row)
                        {
                            const std::uint8_t* t = &palette.mRgba[(row * palette.mWidth + x) * 4];
                            const int chroma = std::max({ t[0], t[1], t[2] }) - std::min({ t[0], t[1], t[2] });
                            const int score = chroma * (static_cast<int>(t[3]) + 32);
                            if (score > bestScore)
                            {
                                bestScore = score;
                                best = row;
                            }
                        }
                        const std::size_t i = (best * palette.mWidth + x) * 4;
                        // The hue of that texel at full brightness: the real lookup runs along the whole gradient and
                        // its alpha controller fades the effect in, which a still draw does not do, so the mid
                        // gradient's dim colour would draw the ward nearly black.
                        const float peak = static_cast<float>(
                            std::max({ palette.mRgba[i], palette.mRgba[i + 1], palette.mRgba[i + 2] }));
                        for (std::size_t c = 0; c < 3; ++c)
                            base[c] = peak > 0.f ? static_cast<float>(palette.mRgba[i + c]) / peak : base[c];
                        paletteRead = true;
                    }
                }
                if (traceEnabled())
                {
                    float alphaMin = 1.f, alphaMax = 0.f, alphaSum = 0.f;
                    for (const auto& colour : shape.mColors)
                    {
                        alphaMin = std::min(alphaMin, colour[3]);
                        alphaMax = std::max(alphaMax, colour[3]);
                        alphaSum += colour[3];
                    }
                    Log(Debug::Info) << "NIFSCENE effect base " << base[0] << "," << base[1] << "," << base[2]
                                     << " palette '" << key.mPalette << "' in " << context.mFilename << " vertex alpha "
                                     << alphaMin << ".." << alphaMax << " mean "
                                     << (shape.mColors.empty() ? 0.f : alphaSum / static_cast<float>(shape.mColors.size()))
                                     << " blend " << int(key.mSourceBlend) << "," << int(key.mDestinationBlend)
                                     << " scroll " << key.mScroll;
                }
                // (the spell models of task 0191 keep their look: only world models take this)
                const bool ignoreVertexRgb = paletteRead && !isMagicModelPath(context.mFilename);
                std::vector<std::array<float, 4>> tinted(shape.mPositions.size(), base);
                if (shape.mColors.size() == tinted.size())
                    for (std::size_t v = 0; v < tinted.size(); ++v)
                        // Task 0359: a palette effect (the flame cards) gets its colour from the palette, so the
                        // vertex colour only carries its alpha here: its RGB is a dim authoring value (0.1 to 0.3 on
                        // the hearth's flames) that would leave a still, un-pulsed card nearly black.
                    {
                        for (std::size_t c = ignoreVertexRgb ? 3 : 0; c < 4; ++c)
                            tinted[v][c] *= shape.mColors[v][c];
                        // The real shader's alpha comes from the palette (near 1 where the flame is bright); the
                        // vertex alpha (0.4 on average on the hearth's cards) only softens the edges, so a still draw
                        // doubles it (ours, set by eye).
                        if (ignoreVertexRgb)
                            tinted[v][3] = std::min(1.f, tinted[v][3] * kPaletteAlphaGain);
                    }
                // OPENEDEN_NIFSCENE_EFFECT_DEBUG=1 (developer aid, task 0359): every effect shape of a world model is
                // drawn in opaque-ish magenta, to see where the cards are.
                static const bool effectDebug = std::getenv("OPENEDEN_NIFSCENE_EFFECT_DEBUG") != nullptr;
                static const char* debugMode = std::getenv("OPENEDEN_NIFSCENE_EFFECT_DEBUG");
                if (effectDebug && !isMagicModelPath(context.mFilename))
                    for (auto& colour : tinted)
                    {
                        if (debugMode[0] == '2') // the palette colour at full alpha
                            colour = { base[0], base[1], base[2], 1.f };
                        else
                            colour = { 1.f, 0.f, 1.f, 1.f };
                    }
                shader.addMode(Pipeline::Mode::COLOR);
                dataList.emplace_back(copyArray<vsg::vec4Array>(tinted));
                options.colorMode = 3;
            }
            else if (!shape.mColors.empty())
            {
                shader.addMode(Pipeline::Mode::COLOR);
                // Task 0136: a swaying shape's alpha is the sway weight the vertex shader squares (the file's own
                // alpha, its flexibility, scaled by the model's height), and the shader draws it with alpha 1.
                if (swaying)
                {
                    std::vector<std::array<float, 4>> weighted = shape.mColors;
                    for (auto& colour : weighted)
                        colour[3] = static_cast<float>(SkyrimTreeSway::weightByte(colour[3], modelHeight)) / 255.f;
                    dataList.emplace_back(copyArray<vsg::vec4Array>(weighted));
                    shader.addMode(Pipeline::Mode::LOD_SWAY);
                }
                else
                {
                    const std::array<float, 3> tint = overrideTint(context);
                    if (tint[0] != 1.f || tint[1] != 1.f || tint[2] != 1.f)
                    {
                        std::vector<std::array<float, 4>> tinted = shape.mColors;
                        for (auto& colour : tinted)
                            for (std::size_t c = 0; c < 3; ++c)
                                colour[c] *= tint[c];
                        dataList.emplace_back(copyArray<vsg::vec4Array>(tinted));
                    }
                    else
                        dataList.emplace_back(copyArray<vsg::vec4Array>(shape.mColors));
                }
                options.colorMode = 2; // the vertex colour is the ambient and diffuse colour, as NiVertexColorProperty
            }
            // The tangent array goes before the texture coordinates (Pipeline::Graphics, lib/attributes.glsl); it is
            // inserted below only if the normal map was read.
            const std::size_t tangentPosition = dataList.size();
            // Task 0359: an effect shape with UV controllers has the file's raw coordinates; the driver's texture matrix
            // applies them (and moves them), or, with no driver, the matrix at time 0 is applied here once.
            const bool scrolls = shape.mScroll != nullptr && !shape.mUV.empty();
            const bool animatedScroll = scrolls && context.mEffectScroll != nullptr;
            if (!shape.mUV.empty())
            {
                shader.addMode(Pipeline::Mode::TEXCOORD);
                if (scrolls && !animatedScroll)
                {
                    const vsg::mat4 m = SkyrimEffectScroll::matrixAt(*shape.mScroll, 0.0);
                    std::vector<std::array<float, 2>> placed = shape.mUV;
                    for (auto& uv : placed)
                        uv = { uv[0] * m(0, 0) + m(3, 0), uv[1] * m(1, 1) + m(3, 1) };
                    dataList.emplace_back(copyArray<vsg::vec2Array>(placed));
                }
                else
                    dataList.emplace_back(copyArray<vsg::vec2Array>(shape.mUV));
                options.numUvSets = 1;
            }
            vsg::ref_ptr<vsg::mat4Value> scrollUniform;
            if (animatedScroll)
            {
                scrollUniform = vsg::mat4Value::create();
                shader.addMode(Pipeline::Mode::TEXMAT);
            }

            vsg::Descriptors descriptors;
            addSurface(key, context, result, totals, options, descriptors);
            if (swaying)
                totals.mSwayShapes.fetch_add(1, std::memory_order_relaxed);
            if (scrollUniform)
            {
                descriptors.push_back(vsg::DescriptorBuffer::create(
                    scrollUniform, Pipeline::Descriptors::TEXMAT_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
                context.mEffectScroll->add(scrollUniform, shape.mScroll);
                totals.mScrollShapes.fetch_add(1, std::memory_order_relaxed);
            }
            if (swaying)
                descriptors.push_back(vsg::DescriptorBuffer::create(
                    context.mSwayUniform, Pipeline::Descriptors::TEXMAT_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
            if (shader.hasMode(Pipeline::Mode::NORMAL_MAP))
            {
                if (shape.mTangents.size() == shape.mPositions.size())
                    dataList.insert(dataList.begin() + static_cast<std::ptrdiff_t>(tangentPosition),
                        copyArray<vsg::vec4Array>(shape.mTangents));
                else
                    shader.removeMode(
                        Pipeline::Mode::NORMAL_MAP); // cannot happen: a key with a normal map has tangents
            }

            auto geometry = vsg::VertexIndexDraw::create();
            geometry->assignArrays(dataList);
            geometry->instanceCount = 1;
            geometry->indexCount = static_cast<std::uint32_t>(shape.mIndices.size());
            geometry->assignIndices(copyArray<vsg::uintArray>(shape.mIndices));

            if (context.mOverride && context.mOverride->pipelineOptions)
                context.mOverride->pipelineOptions(options);
            auto bindPipeline = context.mBuilder.graphics->getOrCreate(options);
            vsg::ref_ptr<vsg::PipelineLayout> layout = bindPipeline->pipeline->layout;
            auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
            // a scrolling shape's set holds its own uniform, so it is never merged with another shape's equal-looking one
            if (!scrollUniform)
                vsgUtil::share_if(context.mSharedObjects, descriptorSet);
            auto bindDescriptors = vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);
            if (!scrollUniform)
                vsgUtil::share_if(context.mSharedObjects, bindDescriptors);

            auto stateGroup = vsg::StateGroup::create();
            stateGroup->children = { geometry };
            stateGroup->stateCommands = { bindPipeline, bindDescriptors };
            ++result.mDraws;
            if (traceEnabled())
            {
                std::ostringstream line;
                line << "NIFSCENE " << context.mFilename << " shape '" << shape.mName << "' tex='" << key.mDiffuse
                     << "' loaded=" << (shader.hasMode(Pipeline::Mode::DIFFUSE_MAP) ? 1 : 0)
                     << " dbl=" << key.mDoubleSided << " blend=" << key.mBlend << " test=" << key.mTest
                     << " colors=" << key.mVertexColors << " verts=" << shape.mPositions.size()
                     << " tris=" << shape.mIndices.size() / 3 << " thr=" << key.mThreshold
                     << " emis=" << key.mEmissive[0] << "," << key.mEmissive[1] << "," << key.mEmissive[2]
                     << " fx=" << key.mEffect << ":" << key.mEffectColor[0] << "," << key.mEffectColor[1] << ","
                     << key.mEffectColor[2] << ","
                     << key.mEffectColor[3]
                     // Task 0153 follow-up: what the file's vertex colours and normals hold (mean rgb, mean normal z).
                     << " vcol=" <<
                    [&shape] {
                        std::ostringstream text;
                        std::array<double, 3> sum{};
                        for (const auto& c : shape.mColors)
                            for (int i = 0; i < 3; ++i)
                                sum[i] += c[i];
                        const double n = std::max<double>(1.0, static_cast<double>(shape.mColors.size()));
                        text << sum[0] / n << "," << sum[1] / n << "," << sum[2] / n;
                        return text.str();
                    }()
                     << " nz=" <<
                    [&shape] {
                        double sum = 0.0;
                        for (const auto& v : shape.mNormals)
                            sum += v[2];
                        return sum / std::max<double>(1.0, static_cast<double>(shape.mNormals.size()));
                    }()
                     << " box=" <<
                    [&shape] {
                        std::ostringstream box;
                        std::array<float, 3> lo{ 1e30f, 1e30f, 1e30f }, hi{ -1e30f, -1e30f, -1e30f };
                        for (const auto& v : shape.mPositions)
                            for (int c = 0; c < 3; ++c)
                            {
                                lo[c] = std::min(lo[c], v[c]);
                                hi[c] = std::max(hi[c], v[c]);
                            }
                        box << lo[0] << "," << lo[1] << "," << lo[2] << ".." << hi[0] << "," << hi[1] << "," << hi[2];
                        return box.str();
                    }()
                     << " f1=" << std::hex << shape.mFlags1 << " f2=" << shape.mFlags2 << std::dec
                     << " merged=" << shape.mMergedFrom;
                Log(Debug::Info) << line.str();
            }
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
            << c.mDoubleSidedShapes.load() << ", left out: " << c.mSkippedSkinned.load() << " skinned (and "
            << c.mSkinnedAtRest.load() << " skinned shapes drawn in their rest pose), " << c.mSkippedEffect.load()
            << " effect, " << c.mSkippedNoMaterial.load() << " no shader, " << c.mSkippedHidden.load() << " hidden, "
            << c.mDroppedTriangles.load() << " bad triangles; materials: " << c.mNormalMaps.load() << " normal maps, "
            << c.mGlowMaps.load() << " glow maps, " << c.mNormalMapsNoTangents.load()
            << " normal maps without tangents, " << c.mNormalMapsModelSpace.load()
            << " model-space normal maps left out, " << c.mSwayShapes.load() << " swaying tree shapes, " << c.mScrollShapes.load()
            << " scrolling effect shapes";
        return out.str();
    }

    BuildResult buildNode(const NifScene::Flat::FlatModel& model, const BuildContext& context)
    {
        BuildResult result;
        Counters& totals = counters();
        totals.mModels.fetch_add(1, std::memory_order_relaxed);
        totals.mSkippedSkinned += model.mReport.mSkippedSkinned;
        totals.mSkinnedAtRest += model.mReport.mSkinnedAtRest;
        if (model.mReport.mSkinnedAtRest != 0 && traceEnabled())
            std::cerr << "nifscene: " << context.mFilename << " has " << model.mReport.mSkinnedAtRest
                      << " skinned shapes drawn in their rest pose\n";
        if (traceEnabled()
            && (model.mReport.mSkippedHidden | model.mReport.mSkippedSkinned | model.mReport.mSkippedNoMaterial
                   | model.mReport.mSkippedEffect | model.mReport.mSkippedNoGeometry | model.mReport.mSkippedOverCap
                   | model.mReport.mDroppedTriangles | model.mReport.mBadVertices)
                != 0)
            Log(Debug::Info) << "NIFSCENE-SKIPS " << context.mFilename << " geometries " << model.mReport.mGeometries
                             << " drawn " << model.mReport.mDrawn << " hidden " << model.mReport.mSkippedHidden
                             << " skinned " << model.mReport.mSkippedSkinned << " nomaterial "
                             << model.mReport.mSkippedNoMaterial << " effect " << model.mReport.mSkippedEffect
                             << " nogeometry " << model.mReport.mSkippedNoGeometry << " overcap "
                             << model.mReport.mSkippedOverCap << " droppedtris " << model.mReport.mDroppedTriangles
                             << " badverts " << model.mReport.mBadVertices;
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
        // OPENEDEN_NIFSCENE_NOCULLNODES=1 (developer aid, task 0152): a plain group, no culling nodes of our own.
        static const bool noCullNodes = std::getenv("OPENEDEN_NIFSCENE_NOCULLNODES") != nullptr;
        if (noCullNodes)
        {
            auto group = vsg::Group::create();
            group->children = nodes;
            result.mNode = group;
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

    bool effectShapesWanted(const std::string& filename, bool spellEffects, bool worldEffects)
    {
        return isMagicModelPath(filename) ? spellEffects : worldEffects;
    }

    std::shared_ptr<const vsgAdapters::nif::SceneReader> makeSceneReader(const Pipeline::Builder& builder,
        vsg::ref_ptr<const vsg::Options> imageOptions, int depthSortedBin, vsg::ref_ptr<vsg::mat4Value> swayUniform,
        std::shared_ptr<SkyrimEffectScroll::Driver> effectScroll)
    {
        auto reader = std::make_shared<vsgAdapters::nif::SceneReader>();
        const bool surfaceMaps = Settings::game().mNeutralWorldMaterials; // read once, as the neutral world switch is
        const bool spellEffects = Settings::game().mNeutralSpellEffects; // task 0191, read once as well
        const bool worldEffects = Settings::game().mNeutralFireEffects; // task 0359
        reader->accepts = [](std::string_view fileStart) { return NifScene::isSkyrimSeHeader(fileStart); };
        reader->read
            = [&builder, imageOptions, depthSortedBin, surfaceMaps, spellEffects, worldEffects, swayUniform, effectScroll](std::string data,
                  const std::string& filename, vsg::ref_ptr<const vsg::Options> options) -> vsg::ref_ptr<vsg::Object> {
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
                limits.mEffects = effectShapesWanted(filename, spellEffects, worldEffects);
                // Task 0136: the trunks and branches of trees are skinned shapes; as static models they are drawn in
                // the rest pose (this reader is for statics only, actors have their own skinned path).
                limits.mSkinnedAtRest = true;
                const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene, limits);
                totals.mNormalMapsNoTangents += flat.mReport.mNormalMapsNoTangents;
                totals.mNormalMapsModelSpace += flat.mReport.mNormalMapsModelSpace;
                BuildContext context{ builder, imageOptions, options->sharedObjects, depthSortedBin, filename,
                    swayUniform };
                context.mOverride = options ? Pipeline::Override::get(*options) : nullptr;
                context.mEffectScroll = effectScroll.get(); // the reader holds the driver, so it outlives the build
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
