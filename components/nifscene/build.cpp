#include "build.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include <components/nif/data.hpp>
#include <components/nif/exception.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>

#include "vertexdecode.hpp"

namespace NifScene
{
    namespace
    {
        [[noreturn]] void fail(const Nif::NIFFile& file, const std::string& message)
        {
            throw Nif::Exception(message, file.mPath);
        }

        Transform convert(const Nif::NiTransform& source)
        {
            Transform result;
            result.mTranslation = { source.mTranslation.x, source.mTranslation.y, source.mTranslation.z };
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                    result.mRotation[row * 3 + column] = source.mRotation.mValues[row][column];
            }
            result.mScale = source.mScale;
            return result;
        }

        struct Builder
        {
            const Nif::NIFFile& mFile;
            const SceneOptions& mOptions;
            Scene mScene;
            std::map<std::uint32_t, std::uint32_t> mNodeOfRecord; // record index to node index
            std::map<std::uint32_t, std::uint32_t> mSkinOfRecord; // skin instance record index to skin index
            std::map<std::uint32_t, std::uint32_t> mMaterialOfRecord; // shader property record to material index
            std::map<std::uint32_t, std::uint32_t> mTextureSetOfRecord;
            std::map<std::uint32_t, std::uint32_t> mAlphaOfRecord;

            Builder(const Nif::NIFFile& file, const SceneOptions& options)
                : mFile(file)
                , mOptions(options)
            {
            }

            std::uint32_t nodeIndex(const Nif::Record* record) const
            {
                if (record == nullptr)
                    return sNone;
                const auto found = mNodeOfRecord.find(record->mRecordIndex);
                return found == mNodeOfRecord.end() ? sNone : found->second;
            }

            void buildNodes()
            {
                for (const auto& record : mFile.mRecords)
                {
                    const auto* object = dynamic_cast<const Nif::NiAVObject*>(record.get());
                    if (object == nullptr)
                        continue;
                    Node node;
                    node.mName = object->mName;
                    node.mRecordType = record->mRecordName;
                    node.mRecordIndex = record->mRecordIndex;
                    node.mFlags = object->mFlags;
                    node.mTransform = convert(object->mTransform);
                    mNodeOfRecord[record->mRecordIndex] = static_cast<std::uint32_t>(mScene.mNodes.size());
                    mScene.mNodes.push_back(std::move(node));
                }
                // The first parent in record order wins; a node listed by two parents keeps the earlier one.
                for (const auto& record : mFile.mRecords)
                {
                    const auto* group = dynamic_cast<const Nif::NiNode*>(record.get());
                    if (group == nullptr)
                        continue;
                    const std::uint32_t parent = nodeIndex(group);
                    for (const auto& child : group->mChildren)
                    {
                        if (child.empty())
                            continue;
                        const std::uint32_t index = nodeIndex(child.getPtr());
                        if (index != sNone && mScene.mNodes[index].mParent == sNone && index != parent)
                        {
                            // The tree stays a forest: the new parent must not already hang below the child.
                            // Every earlier assignment passed this test, so the walk up ends at a root.
                            for (std::uint32_t up = parent; up != sNone; up = mScene.mNodes[up].mParent)
                            {
                                if (up == index)
                                {
                                    fail(mFile,
                                        "node '" + mScene.mNodes[index].mName + "' is its own ancestor: '"
                                            + mScene.mNodes[parent].mName + "' lists it as a child (parent cycle)");
                                }
                            }
                            mScene.mNodes[index].mParent = parent;
                        }
                    }
                }
                for (const Nif::Record* root : mFile.mRoots)
                {
                    const std::uint32_t index = nodeIndex(root);
                    if (index != sNone)
                        mScene.mRoots.push_back(index);
                }
            }

            void warn(const std::string& message) { mScene.mWarnings.push_back(message); }

            std::uint32_t buildSkin(const Nif::Record* skinRecord, const std::string& shapeName)
            {
                if (skinRecord == nullptr)
                    return sNone;
                const auto known = mSkinOfRecord.find(skinRecord->mRecordIndex);
                if (known != mSkinOfRecord.end())
                    return known->second;

                Skin skin;
                if (const auto* instance = dynamic_cast<const Nif::NiSkinInstance*>(skinRecord))
                {
                    for (const auto& bone : instance->mBones)
                    {
                        skin.mBoneNames.push_back(bone.empty() ? std::string() : bone->mName);
                        skin.mBoneNodes.push_back(bone.empty() ? sNone : nodeIndex(bone.getPtr()));
                    }
                    if (!instance->mData.empty())
                    {
                        skin.mSkinTransform = convert(instance->mData->mTransform);
                        for (const auto& bone : instance->mData->mBones)
                            skin.mBindTransforms.push_back(convert(bone.mTransform));
                    }
                }
                else if (const auto* compact = dynamic_cast<const Nif::BSSkinInstance*>(skinRecord))
                {
                    for (const auto& bone : compact->mBones)
                    {
                        skin.mBoneNames.push_back(bone.empty() ? std::string() : bone->mName);
                        skin.mBoneNodes.push_back(bone.empty() ? sNone : nodeIndex(bone.getPtr()));
                    }
                    if (!compact->mData.empty())
                    {
                        for (const auto& bone : compact->mData->mBones)
                            skin.mBindTransforms.push_back(convert(bone.mTransform));
                    }
                }
                else
                {
                    warn("shape '" + shapeName + "': skin record " + std::to_string(skinRecord->mRecordIndex) + " is a "
                        + skinRecord->mRecordName + ", which the scene does not read");
                    return sNone;
                }
                const auto index = static_cast<std::uint32_t>(mScene.mSkins.size());
                mScene.mSkins.push_back(std::move(skin));
                mSkinOfRecord[skinRecord->mRecordIndex] = index;
                return index;
            }

            static const Nif::NiSkinPartition* partitionsOf(const Nif::Record* skinRecord)
            {
                if (const auto* instance = dynamic_cast<const Nif::NiSkinInstance*>(skinRecord))
                    return instance->getPartitions();
                return nullptr;
            }

            void checkStride(const Nif::BSVertexDesc& desc, const std::string& name)
            {
                const std::uint32_t expected = packedVertexStride(desc.mFlags);
                const std::uint32_t declared = static_cast<std::uint32_t>(desc.mVertexDataSize) * 4;
                if (expected == 0)
                {
                    warn("shape '" + name + "': vertex attributes " + std::to_string(desc.mFlags)
                        + " are not supported");
                }
                else if (expected != declared)
                {
                    warn("shape '" + name + "': the descriptor declares a vertex size of " + std::to_string(declared)
                        + " bytes, the attribute flags make " + std::to_string(expected));
                }
            }

            // The reader reads the Fallout 4 era rim light fields up to this stream version
            // (BSLightingShaderProperty::read).
            static constexpr std::uint32_t sLastRimLightStream = 139;

            std::uint32_t buildTextureSet(const Nif::BSShaderTextureSet& set)
            {
                const auto known = mTextureSetOfRecord.find(set.mRecordIndex);
                if (known != mTextureSetOfRecord.end())
                    return known->second;
                TextureSet result;
                result.mTextures = set.mTextures;
                const auto index = static_cast<std::uint32_t>(mScene.mTextureSets.size());
                mScene.mTextureSets.push_back(std::move(result));
                mTextureSetOfRecord[set.mRecordIndex] = index;
                return index;
            }

            static Vec2 toVec2(const vsg::vec2& v) { return { v.x, v.y }; }
            static Vec3 toVec3(const vsg::vec3& v) { return { v.x, v.y, v.z }; }
            static Vec4 toVec4(const vsg::vec4& v) { return { v.x, v.y, v.z, v.w }; }

            // Only the fields the file stores for its stream version (and shader type) are copied: the record keeps
            // the others uninitialised, and copying them would make the scene differ from run to run.
            void fillLighting(ShaderMaterial& m, const Nif::BSLightingShaderProperty& l)
            {
                const std::uint32_t stream = mFile.mBethVersion;
                m.mKind = ShaderKind::Lighting;
                m.mShaderType = l.mType;
                if (!l.mTextureSet.empty())
                    m.mTextureSet = buildTextureSet(*l.mTextureSet.getPtr());
                m.mEmissiveColor = toVec3(l.mEmissive);
                m.mEmissiveMultiple = l.mEmissiveMult;
                m.mClampMode = l.mClamp;
                m.mAlpha = l.mAlpha;
                m.mRefractionStrength = l.mRefractionStrength;
                m.mSpecularColor = toVec3(l.mSpecular);
                m.mSpecularStrength = l.mSpecStrength;
                const bool fo4 = stream >= Nif::NIFFile::BethVersion::BETHVER_FO4;
                if (!fo4)
                {
                    m.mGlossiness = l.mGlossiness;
                    m.mLightingEffects = { l.mLightingEffects[0], l.mLightingEffects[1] };
                }
                else
                {
                    m.mRootMaterial = l.mRootMaterial;
                    m.mSmoothness = l.mSmoothness;
                    if (stream <= sLastRimLightStream)
                    {
                        m.mSubsurfaceRolloff = l.mSubsurfaceRolloff;
                        m.mRimlightPower = l.mRimlightPower;
                        if (l.mRimlightPower == std::numeric_limits<float>::max())
                            m.mBacklightPower = l.mBacklightPower;
                    }
                    m.mGrayscaleToPaletteScale = l.mGrayscaleToPaletteScale;
                    m.mFresnelPower = l.mFresnelPower;
                    m.mWetnessSpecScale = l.mWetness.mSpecScale;
                    m.mWetnessSpecPower = l.mWetness.mSpecPower;
                    m.mWetnessMinVar = l.mWetness.mMinVar;
                    if (stream == Nif::NIFFile::BethVersion::BETHVER_FO4)
                        m.mWetnessEnvMapScale = l.mWetness.mEnvMapScale;
                    m.mWetnessFresnelPower = l.mWetness.mFresnelPower;
                    m.mWetnessMetalness = l.mWetness.mMetalness;
                }
                switch (static_cast<Nif::BSLightingShaderType>(l.mType))
                {
                    case Nif::BSLightingShaderType::ShaderType_EnvMap:
                        if (stream <= sLastRimLightStream)
                            m.mEnvMapScale = l.mEnvMapScale;
                        if (fo4)
                        {
                            m.mUseSSR = l.mUseSSR;
                            m.mWetnessUseSSR = l.mWetnessUseSSR;
                        }
                        break;
                    case Nif::BSLightingShaderType::ShaderType_SkinTint:
                        m.mSkinTintColor = toVec3(l.mSkinTintColor);
                        if (fo4)
                            m.mSkinTintAlpha = l.mSkinTintAlpha;
                        break;
                    case Nif::BSLightingShaderType::ShaderType_HairTint:
                        m.mHairTintColor = toVec3(l.mHairTintColor);
                        break;
                    case Nif::BSLightingShaderType::ShaderType_ParallaxOcc:
                        m.mParallaxMaxPasses = l.mParallax.mMaxPasses;
                        m.mParallaxScale = l.mParallax.mScale;
                        break;
                    case Nif::BSLightingShaderType::ShaderType_MultiLayerParallax:
                        m.mInnerLayerThickness = l.mMultiLayerParallax.mInnerLayerThickness;
                        m.mInnerRefractionScale = l.mMultiLayerParallax.mRefractionScale;
                        m.mInnerLayerTextureScale = toVec2(l.mMultiLayerParallax.mInnerLayerTextureScale);
                        m.mInnerEnvMapScale = l.mMultiLayerParallax.mEnvMapScale;
                        break;
                    case Nif::BSLightingShaderType::ShaderType_SparkleSnow:
                        m.mSparkle = toVec4(l.mSparkle);
                        break;
                    case Nif::BSLightingShaderType::ShaderType_EyeEnvmap:
                        m.mEyeCubeMapScale = l.mCubeMapScale;
                        m.mLeftEyeReflectionCenter = toVec3(l.mLeftEyeReflectionCenter);
                        m.mRightEyeReflectionCenter = toVec3(l.mRightEyeReflectionCenter);
                        break;
                    default:
                        break;
                }
            }

            void fillEffect(ShaderMaterial& m, const Nif::BSEffectShaderProperty& e)
            {
                m.mKind = ShaderKind::Effect;
                m.mSourceTexture = e.mSourceTexture;
                m.mClampMode = e.mClamp;
                m.mLightingInfluence = e.mLightingInfluence;
                m.mEnvMapMinLod = e.mEnvMapMinLOD;
                m.mFalloff = toVec4(e.mFalloffParams);
                m.mBaseColor = toVec4(e.mBaseColor);
                m.mBaseColorScale = e.mBaseColorScale;
                m.mSoftFalloffDepth = e.mFalloffDepth;
                m.mGreyscaleTexture = e.mGreyscaleTexture;
                if (mFile.mBethVersion >= Nif::NIFFile::BethVersion::BETHVER_FO4)
                {
                    m.mEnvMapTexture = e.mEnvMapTexture;
                    m.mNormalTexture = e.mNormalTexture;
                    m.mEnvMaskTexture = e.mEnvMaskTexture;
                    m.mEffectEnvMapScale = e.mEnvMapScale;
                }
            }

            std::uint32_t buildMaterial(const Nif::BSShaderProperty& property, const std::string& shapeName)
            {
                const auto known = mMaterialOfRecord.find(property.mRecordIndex);
                if (known != mMaterialOfRecord.end())
                    return known->second;
                const auto* lighting = dynamic_cast<const Nif::BSLightingShaderProperty*>(&property);
                const auto* effect = dynamic_cast<const Nif::BSEffectShaderProperty*>(&property);
                if (lighting == nullptr && effect == nullptr)
                {
                    warn("shape '" + shapeName + "': shader property record " + std::to_string(property.mRecordIndex)
                        + " is a " + property.mRecordName + ", which the scene does not read");
                    return sNone;
                }
                if (mFile.mBethVersion >= Nif::NIFFile::BethVersion::BETHVER_F76 || mFile.mUserVersion <= 11)
                {
                    warn("shape '" + shapeName + "': a " + property.mRecordName + " of stream version "
                        + std::to_string(mFile.mBethVersion) + " is not read");
                    return sNone;
                }
                ShaderMaterial m;
                m.mRecordType = property.mRecordName;
                m.mName = property.mName;
                m.mFlags1 = property.mShaderFlags1;
                m.mFlags2 = property.mShaderFlags2;
                m.mUVOffset = toVec2(property.mUVOffset);
                m.mUVScale = toVec2(property.mUVScale);
                if (lighting != nullptr)
                    fillLighting(m, *lighting);
                else
                    fillEffect(m, *effect);
                const auto index = static_cast<std::uint32_t>(mScene.mMaterials.size());
                mScene.mMaterials.push_back(std::move(m));
                mMaterialOfRecord[property.mRecordIndex] = index;
                return index;
            }

            std::uint32_t buildAlpha(const Nif::NiAlphaProperty& property)
            {
                const auto known = mAlphaOfRecord.find(property.mRecordIndex);
                if (known != mAlphaOfRecord.end())
                    return known->second;
                AlphaProperty a;
                a.mFlags = property.mFlags;
                a.mThreshold = property.mThreshold;
                a.mBlend = property.useAlphaBlending();
                a.mTest = property.useAlphaTesting();
                a.mNoSorter = property.noSorter();
                a.mSourceBlend = static_cast<std::uint8_t>(property.sourceBlendMode());
                a.mDestinationBlend = static_cast<std::uint8_t>(property.destinationBlendMode());
                a.mTestMode = static_cast<std::uint8_t>(property.alphaTestMode());
                const auto index = static_cast<std::uint32_t>(mScene.mAlphaProperties.size());
                mScene.mAlphaProperties.push_back(a);
                mAlphaOfRecord[property.mRecordIndex] = index;
                return index;
            }

            void linkMaterial(Geometry& g, const Nif::BSShaderPropertyPtr& shader, const Nif::NiAlphaPropertyPtr& alpha)
            {
                if (!shader.empty())
                    g.mMaterial = buildMaterial(*shader.getPtr(), g.mName);
                if (!alpha.empty())
                    g.mAlpha = buildAlpha(*alpha.getPtr());
            }

            void buildBSTriShape(const Nif::BSTriShape& shape, std::uint32_t nodeIdx)
            {
                Geometry g;
                g.mName = shape.mName;
                g.mNode = nodeIdx;
                if (shape.mRecordType == Nif::RC_BSDynamicTriShape)
                    g.mKind = GeometryKind::DynamicTriShape;
                else if (shape.mRecordType == Nif::RC_BSSubIndexTriShape)
                    g.mKind = GeometryKind::SubIndexTriShape;

                const Nif::Record* skinRecord = shape.mSkin.empty() ? nullptr : shape.mSkin.getPtr();
                g.mSkin = buildSkin(skinRecord, shape.mName);
                const Nif::NiSkinPartition* partitions = partitionsOf(skinRecord);

                std::uint16_t flags = shape.mVertDesc.mFlags;
                DecodedVertices decoded;
                std::vector<std::uint32_t> triangles;
                bool fromPartitions = false;
                if (shape.mDataSize > 0)
                {
                    checkStride(shape.mVertDesc, shape.mName);
                    decoded = decodeVertices(flags, shape.mVertData);
                    triangles.assign(shape.mTriangles.begin(), shape.mTriangles.end());
                }
                else if (partitions != nullptr && !partitions->mVertexData.empty())
                {
                    fromPartitions = true;
                    g.mVerticesFromSkinPartition = true;
                    flags = partitions->mVertexDesc.mFlags;
                    checkStride(partitions->mVertexDesc, shape.mName);
                    decoded = decodeVertices(flags, partitions->mVertexData);
                    for (const auto& partition : partitions->mPartitions)
                    {
                        if (!partition.mTrueStrips.empty())
                            warn("shape '" + shape.mName + "': a partition with triangle strips is not read");
                        for (unsigned short index : partition.mTrueTriangles)
                            triangles.push_back(index);
                    }
                }
                else
                {
                    g.mNoVertexData = true;
                }

                g.mPositions = std::move(decoded.mPositions);
                g.mNormals = std::move(decoded.mNormals);
                g.mTangents = std::move(decoded.mTangents);
                g.mUV0 = std::move(decoded.mUV0);
                g.mColors = std::move(decoded.mColors);
                g.mTriangles = std::move(triangles);

                if (g.mKind == GeometryKind::DynamicTriShape)
                {
                    const auto& dynamic = static_cast<const Nif::BSDynamicTriShape&>(shape);
                    // The packed vertices of a dynamic shape may carry no position attribute at all (the heads of
                    // Skyrim SE do not); the positions are in the dynamic array either way.
                    if (dynamic.mDynamicData.size() == shape.mNumVertices && shape.mNumVertices != 0)
                    {
                        g.mPositions.clear();
                        for (const auto& position : dynamic.mDynamicData)
                            g.mPositions.push_back({ position.x, position.y, position.z });
                    }
                    else if (!dynamic.mDynamicData.empty())
                    {
                        warn("shape '" + shape.mName + "': the dynamic position array has "
                            + std::to_string(dynamic.mDynamicData.size()) + " entries for "
                            + std::to_string(shape.mNumVertices) + " vertices and is not used");
                    }
                }

                const std::size_t vertexCount = g.mPositions.size();
                if (g.mKind == GeometryKind::DynamicTriShape)
                {
                    // The dynamic array replaced the positions; the other attributes come from the packed vertices
                    // or the skin partition, whose count may not be the shape's.
                    const auto checkLength = [&](const char* what, std::size_t count) {
                        if (count != 0 && count != vertexCount)
                        {
                            fail(mFile,
                                "dynamic shape '" + shape.mName + "' has " + std::to_string(vertexCount)
                                    + " vertices but " + std::to_string(count) + " " + what);
                        }
                    };
                    checkLength("normals", g.mNormals.size());
                    checkLength("tangents", g.mTangents.size());
                    checkLength("texture coordinates", g.mUV0.size());
                    checkLength("colours", g.mColors.size());
                }
                for (std::size_t i = 0; i < g.mTriangles.size(); ++i)
                {
                    if (g.mTriangles[i] >= vertexCount)
                    {
                        fail(mFile,
                            "triangle index " + std::to_string(g.mTriangles[i]) + " is outside the "
                                + std::to_string(vertexCount) + " vertices of shape '" + shape.mName + "'");
                    }
                }

                linkMaterial(g, shape.mShaderProperty, shape.mAlphaProperty);

                if (!decoded.mWeights.empty())
                    buildInfluences(g, decoded, shape.mName, partitions, fromPartitions);
                warnNonFiniteWeights(g, shape.mName);
                if (fromPartitions)
                    describePartitions(g, partitions);

                const auto index = static_cast<std::uint32_t>(mScene.mGeometries.size());
                if (nodeIdx != sNone)
                    mScene.mNodes[nodeIdx].mGeometry = index;
                mScene.mGeometries.push_back(std::move(g));
            }

            // A packed half with exponent 31 is Inf or NaN, and NiSkinData stores plain floats: the scene keeps the
            // value (it is the file's), so a consumer must filter. This only makes the warning visible.
            void warnNonFiniteWeights(const Geometry& g, const std::string& name)
            {
                std::size_t count = 0;
                for (const Influence& influence : g.mInfluences)
                {
                    if (!std::isfinite(influence.mWeight))
                        ++count;
                }
                if (count != 0)
                    warn("shape '" + name + "': " + std::to_string(count)
                        + " bone influences have a non-finite weight (kept as read; weights are not validated)");
            }

            void buildInfluences(Geometry& g, const DecodedVertices& decoded, const std::string& name,
                const Nif::NiSkinPartition* partitions, bool fromPartitions)
            {
                if (g.mSkin == sNone)
                {
                    warn("shape '" + name + "': the vertices carry bone weights but the shape has no skin");
                    return;
                }
                Skin& skin = mScene.mSkins[g.mSkin];
                const std::size_t boneCount = skin.mBoneNames.size();
                g.mInfluenceStart.reserve(decoded.mWeights.size() + 1);
                g.mInfluenceStart.push_back(0);
                for (std::size_t v = 0; v < decoded.mWeights.size(); ++v)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        const float weight = decoded.mWeights[v][k];
                        const std::uint32_t bone = decoded.mBoneIndices[v][k];
                        if (weight == 0.f && !mOptions.mKeepZeroWeights)
                            continue;
                        // A kept zero-weight entry is stored, so its bone must exist too.
                        if (bone >= boneCount)
                        {
                            fail(mFile,
                                "bone index " + std::to_string(bone) + " of vertex " + std::to_string(v)
                                    + " is outside the " + std::to_string(boneCount) + " bones of the skin of shape '"
                                    + name + "'");
                        }
                        g.mInfluences.push_back({ bone, weight });
                    }
                    g.mInfluenceStart.push_back(static_cast<std::uint32_t>(g.mInfluences.size()));
                }

                if (!fromPartitions || partitions == nullptr)
                    return;
                // Research note 0036: the packed indices are skin bone indices. Count where the partitions' own lists
                // disagree, so that a consumer can see it.
                std::uint32_t nonIdentity = 0;
                std::uint32_t outside = 0;
                for (const auto& partition : partitions->mPartitions)
                {
                    bool identity = true;
                    for (std::size_t i = 0; i < partition.mBones.size(); ++i)
                    {
                        if (partition.mBones[i] != i)
                            identity = false;
                    }
                    if (!identity)
                        ++nonIdentity;
                    for (unsigned short vertex : partition.mVertexMap)
                    {
                        if (vertex >= decoded.mWeights.size())
                            fail(mFile,
                                "vertex map entry " + std::to_string(vertex) + " of a skin partition of shape '" + name
                                    + "' is outside its " + std::to_string(decoded.mWeights.size()) + " vertices");
                        for (int k = 0; k < 4; ++k)
                        {
                            if (decoded.mWeights[vertex][k] == 0.f)
                                continue;
                            const auto bone = static_cast<unsigned short>(decoded.mBoneIndices[vertex][k]);
                            if (std::find(partition.mBones.begin(), partition.mBones.end(), bone)
                                == partition.mBones.end())
                                ++outside;
                        }
                    }
                }
                skin.mNonIdentityPartitionBoneMaps = nonIdentity;
                skin.mInfluencesOutsidePartitionBones = outside;
                if (nonIdentity != 0)
                {
                    warn("shape '" + name + "': " + std::to_string(nonIdentity)
                        + " skin partition bone lists are not 0, 1, 2, ...; the vertex bone indices are read as skin bone indices");
                }
            }

            void describePartitions(Geometry& g, const Nif::NiSkinPartition* partitions)
            {
                if (g.mSkin == sNone || partitions == nullptr)
                    return;
                Skin& skin = mScene.mSkins[g.mSkin];
                if (!skin.mPartitions.empty())
                    return;
                std::uint32_t first = 0;
                for (const auto& partition : partitions->mPartitions)
                {
                    Partition described;
                    described.mBones = partition.mBones;
                    described.mFirstTriangle = first;
                    described.mTriangleCount = static_cast<std::uint32_t>(partition.mTrueTriangles.size() / 3);
                    described.mLodLevel = partition.mLODLevel;
                    described.mGlobalVB = partition.mGlobalVB;
                    first += described.mTriangleCount;
                    skin.mPartitions.push_back(std::move(described));
                }
            }

            // The per-bone (vertex, weight) lists of a NiSkinData as per-vertex influences, in bone order. Nothing is
            // stored when no bone lists a weight (the weights of later games live in the skin partition).
            void buildBoneWeights(Geometry& g, const Nif::NiSkinData& data, const std::string& name)
            {
                const std::size_t vertexCount = g.mPositions.size();
                std::vector<std::uint32_t> starts(vertexCount + 1, 0);
                std::size_t total = 0;
                for (std::size_t bone = 0; bone < data.mBones.size(); ++bone)
                {
                    for (const auto& weight : data.mBones[bone].mWeights)
                    {
                        if (weight.first >= vertexCount)
                        {
                            fail(mFile,
                                "bone " + std::to_string(bone) + " weights vertex " + std::to_string(weight.first)
                                    + ", outside the " + std::to_string(vertexCount) + " vertices of shape '" + name
                                    + "'");
                        }
                        ++starts[weight.first + 1];
                        ++total;
                    }
                }
                if (total == 0)
                    return;
                for (std::size_t v = 0; v < vertexCount; ++v)
                    starts[v + 1] += starts[v];
                g.mInfluenceStart = starts;
                g.mInfluences.resize(total);
                std::vector<std::uint32_t> next(starts.begin(), starts.end() - 1);
                for (std::size_t bone = 0; bone < data.mBones.size(); ++bone)
                {
                    for (const auto& weight : data.mBones[bone].mWeights)
                        g.mInfluences[next[weight.first]++] = { static_cast<std::uint32_t>(bone), weight.second };
                }
            }

            // Morrowind and Oblivion shapes: positions, normals, the first UV set, colours and triangles.
            void buildTriShape(const Nif::NiTriShape& shape, std::uint32_t nodeIdx)
            {
                if (shape.mData.empty())
                    return;
                const auto* data = dynamic_cast<const Nif::NiTriShapeData*>(shape.mData.getPtr());
                if (data == nullptr)
                    return;
                Geometry g;
                g.mName = shape.mName;
                g.mNode = nodeIdx;
                for (const auto& v : data->mVertices)
                    g.mPositions.push_back({ v.x, v.y, v.z });
                for (const auto& n : data->mNormals)
                    g.mNormals.push_back({ n.x, n.y, n.z });
                if (!data->mUVList.empty())
                {
                    for (const auto& uv : data->mUVList[0])
                        g.mUV0.push_back({ uv.x, uv.y });
                }
                for (const auto& c : data->mColors)
                    g.mColors.push_back({ c.x, c.y, c.z, c.w });
                g.mTriangles.assign(data->mTriangles.begin(), data->mTriangles.end());
                for (std::uint32_t index : g.mTriangles)
                {
                    if (index >= g.mPositions.size())
                    {
                        fail(mFile,
                            "triangle index " + std::to_string(index) + " is outside the "
                                + std::to_string(g.mPositions.size()) + " vertices of shape '" + shape.mName + "'");
                    }
                }
                const Nif::NiSkinInstance* skinInstance = shape.mSkin.empty() ? nullptr : shape.mSkin.getPtr();
                g.mSkin = buildSkin(skinInstance, shape.mName);
                if (g.mSkin != sNone && skinInstance != nullptr && !skinInstance->mData.empty())
                    buildBoneWeights(g, *skinInstance->mData.getPtr(), shape.mName);
                warnNonFiniteWeights(g, shape.mName);
                linkMaterial(g, shape.mShaderProperty, shape.mAlphaProperty);
                const auto index = static_cast<std::uint32_t>(mScene.mGeometries.size());
                if (nodeIdx != sNone)
                    mScene.mNodes[nodeIdx].mGeometry = index;
                mScene.mGeometries.push_back(std::move(g));
            }

            void buildGeometries()
            {
                for (const auto& record : mFile.mRecords)
                {
                    if (const auto* shape = dynamic_cast<const Nif::BSTriShape*>(record.get()))
                        buildBSTriShape(*shape, nodeIndex(record.get()));
                    else if (record->mRecordType == Nif::RC_NiTriShape)
                        buildTriShape(static_cast<const Nif::NiTriShape&>(*record), nodeIndex(record.get()));
                }
            }

            Scene run()
            {
                mScene.mVersion = mFile.mVersion;
                mScene.mUserVersion = mFile.mUserVersion;
                mScene.mBethVersion = mFile.mBethVersion;
                buildNodes();
                buildGeometries();
                if (mOptions.mReadHavok)
                    mScene.mHavok = Havok::readCollision(mFile);
                return std::move(mScene);
            }
        };

        void dumpTransform(std::ostream& out, const Transform& t)
        {
            out << " t=" << t.mTranslation[0] << "," << t.mTranslation[1] << "," << t.mTranslation[2]
                << " s=" << t.mScale << " r=";
            for (float value : t.mRotation)
                out << value << ",";
        }
    }

    Scene buildScene(const Nif::NIFFile& file, const SceneOptions& options)
    {
        return Builder(file, options).run();
    }

    Scene readScene(Files::IStreamPtr&& stream, VFS::Path::NormalizedView path, const SceneOptions& options,
        const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        Nif::NIFFile file(path);
        Nif::Reader reader(file, encoder);
        reader.parse(std::move(stream), options.mRead);
        return buildScene(file, options);
    }

    namespace
    {
        void dumpMaterials(std::ostream& out, const Scene& scene)
        {
            for (std::size_t i = 0; i < scene.mGeometries.size(); ++i)
            {
                const Geometry& g = scene.mGeometries[i];
                if (g.mMaterial == sNone && g.mAlpha == sNone)
                    continue;
                out << "geometry-material " << i
                    << " material=" << static_cast<std::int64_t>(g.mMaterial == sNone ? -1 : g.mMaterial)
                    << " alpha=" << static_cast<std::int64_t>(g.mAlpha == sNone ? -1 : g.mAlpha) << "\n";
            }
            for (std::size_t i = 0; i < scene.mTextureSets.size(); ++i)
            {
                out << "textureset " << i << " paths=";
                for (const std::string& texture : scene.mTextureSets[i].mTextures)
                    out << texture << ";";
                out << "\n";
            }
            auto vec = [&out](const char* label, const auto& values) {
                out << " " << label << "=";
                for (float value : values)
                    out << value << ",";
            };
            for (std::size_t i = 0; i < scene.mMaterials.size(); ++i)
            {
                const ShaderMaterial& m = scene.mMaterials[i];
                out << "material " << i << " " << m.mRecordType << " name=" << m.mName << " root=" << m.mRootMaterial
                    << " type=" << m.mShaderType << " flags1=" << m.mFlags1 << " flags2=" << m.mFlags2
                    << " textureset=" << static_cast<std::int64_t>(m.mTextureSet == sNone ? -1 : m.mTextureSet)
                    << " clamp=" << m.mClampMode << " alpha=" << m.mAlpha;
                vec("uvoffset", m.mUVOffset);
                vec("uvscale", m.mUVScale);
                vec("emissive", m.mEmissiveColor);
                out << " emissivemult=" << m.mEmissiveMultiple << " refraction=" << m.mRefractionStrength;
                vec("specular", m.mSpecularColor);
                out << " specstrength=" << m.mSpecularStrength << " glossiness=" << m.mGlossiness
                    << " smoothness=" << m.mSmoothness << " rolloff=" << m.mSubsurfaceRolloff
                    << " rim=" << m.mRimlightPower << " backlight=" << m.mBacklightPower
                    << " palette=" << m.mGrayscaleToPaletteScale << " fresnel=" << m.mFresnelPower
                    << " wet=" << m.mWetnessSpecScale << "," << m.mWetnessSpecPower << "," << m.mWetnessMinVar << ","
                    << m.mWetnessEnvMapScale << "," << m.mWetnessFresnelPower << "," << m.mWetnessMetalness;
                vec("lightingeffects", m.mLightingEffects);
                out << " envscale=" << m.mEnvMapScale << " ssr=" << m.mUseSSR << "," << m.mWetnessUseSSR;
                vec("skintint", m.mSkinTintColor);
                out << " skintintalpha=" << m.mSkinTintAlpha;
                vec("hairtint", m.mHairTintColor);
                out << " parallax=" << m.mParallaxMaxPasses << "," << m.mParallaxScale
                    << " inner=" << m.mInnerLayerThickness << "," << m.mInnerRefractionScale << ","
                    << m.mInnerEnvMapScale;
                vec("innerscale", m.mInnerLayerTextureScale);
                vec("sparkle", m.mSparkle);
                out << " eye=" << m.mEyeCubeMapScale;
                vec("lefteye", m.mLeftEyeReflectionCenter);
                vec("righteye", m.mRightEyeReflectionCenter);
                out << " source=" << m.mSourceTexture << " greyscale=" << m.mGreyscaleTexture
                    << " envmap=" << m.mEnvMapTexture << " normal=" << m.mNormalTexture
                    << " envmask=" << m.mEnvMaskTexture << " influence=" << static_cast<int>(m.mLightingInfluence)
                    << " minlod=" << static_cast<int>(m.mEnvMapMinLod);
                vec("falloff", m.mFalloff);
                vec("basecolor", m.mBaseColor);
                out << " basescale=" << m.mBaseColorScale << " softdepth=" << m.mSoftFalloffDepth
                    << " effectenvscale=" << m.mEffectEnvMapScale << "\n";
            }
            for (std::size_t i = 0; i < scene.mAlphaProperties.size(); ++i)
            {
                const AlphaProperty& a = scene.mAlphaProperties[i];
                out << "alpha " << i << " flags=" << a.mFlags << " threshold=" << static_cast<int>(a.mThreshold)
                    << " blend=" << a.mBlend << " test=" << a.mTest << " nosorter=" << a.mNoSorter
                    << " src=" << static_cast<int>(a.mSourceBlend) << " dst=" << static_cast<int>(a.mDestinationBlend)
                    << " testmode=" << static_cast<int>(a.mTestMode) << "\n";
            }
        }
    }

    std::string dump(const Scene& scene)
    {
        std::ostringstream out;
        out << std::setprecision(9);
        out << "version " << scene.mVersion << " user " << scene.mUserVersion << " stream " << scene.mBethVersion
            << "\n";
        for (std::size_t i = 0; i < scene.mNodes.size(); ++i)
        {
            const Node& n = scene.mNodes[i];
            out << "node " << i << " " << n.mRecordType << " record=" << n.mRecordIndex << " name=" << n.mName
                << " parent=" << static_cast<std::int64_t>(n.mParent == sNone ? -1 : n.mParent) << " flags=" << n.mFlags
                << " geometry=" << static_cast<std::int64_t>(n.mGeometry == sNone ? -1 : n.mGeometry);
            dumpTransform(out, n.mTransform);
            out << "\n";
        }
        out << "roots=";
        for (std::uint32_t root : scene.mRoots)
            out << root << ",";
        out << "\n";
        for (std::size_t i = 0; i < scene.mGeometries.size(); ++i)
        {
            const Geometry& g = scene.mGeometries[i];
            out << "geometry " << i << " name=" << g.mName << " kind=" << static_cast<int>(g.mKind)
                << " node=" << static_cast<std::int64_t>(g.mNode == sNone ? -1 : g.mNode)
                << " skin=" << static_cast<std::int64_t>(g.mSkin == sNone ? -1 : g.mSkin)
                << " fromPartition=" << g.mVerticesFromSkinPartition << " noData=" << g.mNoVertexData << "\n";
            out << " positions=";
            for (const Vec3& p : g.mPositions)
                out << p[0] << "," << p[1] << "," << p[2] << ";";
            out << "\n normals=";
            for (const Vec3& p : g.mNormals)
                out << p[0] << "," << p[1] << "," << p[2] << ";";
            out << "\n tangents=";
            for (const Vec4& p : g.mTangents)
                out << p[0] << "," << p[1] << "," << p[2] << "," << p[3] << ";";
            out << "\n uv0=";
            for (const Vec2& p : g.mUV0)
                out << p[0] << "," << p[1] << ";";
            out << "\n colors=";
            for (const Vec4& p : g.mColors)
                out << p[0] << "," << p[1] << "," << p[2] << "," << p[3] << ";";
            out << "\n triangles=";
            for (std::uint32_t index : g.mTriangles)
                out << index << ",";
            out << "\n influenceStart=";
            for (std::uint32_t index : g.mInfluenceStart)
                out << index << ",";
            out << "\n influences=";
            for (const Influence& influence : g.mInfluences)
                out << influence.mBone << ":" << influence.mWeight << ",";
            out << "\n";
        }
        for (std::size_t i = 0; i < scene.mSkins.size(); ++i)
        {
            const Skin& s = scene.mSkins[i];
            out << "skin " << i << " bones=";
            for (std::size_t b = 0; b < s.mBoneNames.size(); ++b)
                out << s.mBoneNames[b] << "@"
                    << static_cast<std::int64_t>(s.mBoneNodes[b] == sNone ? -1 : s.mBoneNodes[b]) << ",";
            out << " skinTransform";
            dumpTransform(out, s.mSkinTransform);
            out << "\n";
            for (const Transform& t : s.mBindTransforms)
            {
                out << " bind";
                dumpTransform(out, t);
                out << "\n";
            }
            for (const Partition& p : s.mPartitions)
            {
                out << " partition first=" << p.mFirstTriangle << " count=" << p.mTriangleCount
                    << " lod=" << static_cast<int>(p.mLodLevel) << " global=" << p.mGlobalVB << " bones=";
                for (std::uint16_t b : p.mBones)
                    out << b << ",";
                out << "\n";
            }
            out << " nonIdentity=" << s.mNonIdentityPartitionBoneMaps
                << " outside=" << s.mInfluencesOutsidePartitionBones << "\n";
        }
        dumpMaterials(out, scene);
        for (const std::string& warning : scene.mWarnings)
            out << "warning " << warning << "\n";
        return out.str();
    }
}
