#include "skinned.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace NifScene::Skinned
{
    namespace
    {
        constexpr std::uint32_t sHiddenNodeFlag = 1u; // NiAVObject flags, bit 0
        constexpr std::size_t kInfluencesPerVertex = 4;

        bool usable(float v, double limit)
        {
            return std::isfinite(v) && std::fabs(static_cast<double>(v)) <= limit;
        }

        // Hidden when the node, or any node above it, has the hidden flag or is an editor marker. The walk is bounded
        // by the node count, so a parent loop ends.
        bool isHidden(const Scene& scene, std::size_t node, std::size_t walked)
        {
            std::size_t at = node;
            for (std::size_t steps = 0; steps <= walked; ++steps)
            {
                const Node& n = scene.mNodes[at];
                if ((n.mFlags & sHiddenNodeFlag) != 0 || Flat::isEditorMarkerName(n.mName))
                    return true;
                if (n.mParent >= walked || n.mParent == at)
                    return false;
                at = n.mParent;
            }
            return false;
        }

        bool isIdentity(const Transform& t)
        {
            constexpr float eps = 1e-6f;
            const Transform identity;
            if (std::fabs(t.mScale - identity.mScale) > eps)
                return false;
            for (int i = 0; i < 3; ++i)
                if (std::fabs(t.mTranslation[i] - identity.mTranslation[i]) > eps)
                    return false;
            for (int i = 0; i < 9; ++i)
                if (std::fabs(t.mRotation[i] - identity.mRotation[i]) > eps)
                    return false;
            return true;
        }

        std::array<float, 3> unit(double x, double y, double z)
        {
            const double length = std::sqrt(x * x + y * y + z * z);
            if (!(length > 1e-12) || !std::isfinite(length))
                return { 0.f, 0.f, 1.f };
            return { static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length) };
        }

        float clamp01(float v)
        {
            if (!std::isfinite(v))
                return 1.f;
            return std::clamp(v, 0.f, 1.f);
        }

        // BSLightingShaderType 4 (face tint) keeps the actor's baked facegen tint in texture slot 6 (verified on a
        // vanilla facegen file: slot 6 is data\\Textures\\Actors\\Character\\FaceGenData\\FaceTint\\<plugin>\\<id>.dds).
        constexpr std::uint32_t kFaceTintShaderType = 4;
        constexpr std::size_t kFaceTintSlot = 6;
        constexpr std::uint32_t kHairTintShaderType = 6;

        Flat::MaterialKey makeKey(
            const Scene& scene, const Geometry& geometry, const ShaderMaterial& material, bool hasUV, const Limits& limits)
        {
            Flat::MaterialKey key;
            key.mDoubleSided = (material.mFlags2 & Flat::ShaderFlags::sDoubleSided) != 0;
            key.mVertexColors = false;
            key.mClampMode = static_cast<std::uint8_t>(material.mClampMode & 3u);
            key.mAlpha = std::isfinite(material.mAlpha) ? clamp01(material.mAlpha) : 1.f;
            if (hasUV && material.mTextureSet < scene.mTextureSets.size())
            {
                const auto& set = scene.mTextureSets[material.mTextureSet].mTextures;
                if (!set.empty() && !set[0].empty() && set[0].size() <= limits.mMaxPathLength)
                    key.mDiffuse = set[0];
            }
            if (geometry.mAlpha < scene.mAlphaProperties.size())
            {
                const AlphaProperty& alpha = scene.mAlphaProperties[geometry.mAlpha];
                key.mBlend = alpha.mBlend;
                key.mTest = alpha.mTest;
                if (alpha.mBlend)
                {
                    key.mSourceBlend = alpha.mSourceBlend;
                    key.mDestinationBlend = alpha.mDestinationBlend;
                    key.mNoSorter = alpha.mNoSorter;
                }
                if (alpha.mTest)
                {
                    key.mTestMode = alpha.mTestMode;
                    key.mThreshold = alpha.mThreshold;
                }
            }
            return key;
        }
    }

    SkinnedModel flattenSkinned(const Scene& scene, const Limits& limits)
    {
        SkinnedModel model;
        Report& report = model.mReport;
        report.mGeometries = static_cast<std::uint32_t>(std::min<std::size_t>(scene.mGeometries.size(), 0xFFFFFFFFu));
        const std::size_t walked = std::min(scene.mNodes.size(), limits.mMaxNodes);
        std::size_t acceptedVertices = 0;
        std::size_t acceptedTriangles = 0;

        for (std::size_t n = 0; n < walked; ++n)
        {
            const Node& node = scene.mNodes[n];
            if (node.mGeometry >= scene.mGeometries.size())
                continue;
            const Geometry& geometry = scene.mGeometries[node.mGeometry];
            if (isHidden(scene, n, walked))
            {
                ++report.mSkippedHidden;
                continue;
            }
            if (geometry.mSkin >= scene.mSkins.size())
            {
                ++report.mSkippedUnskinned;
                continue;
            }
            if (geometry.mMaterial >= scene.mMaterials.size())
            {
                ++report.mSkippedNoMaterial;
                continue;
            }
            const ShaderMaterial& material = scene.mMaterials[geometry.mMaterial];
            if (material.mKind == ShaderKind::Effect
                || (material.mFlags1 & (Flat::ShaderFlags::sRefraction | Flat::ShaderFlags::sFireRefraction)) != 0)
            {
                ++report.mSkippedEffect;
                continue;
            }
            const Skin& skin = scene.mSkins[geometry.mSkin];
            const std::size_t vertexCount = geometry.mPositions.size();
            const std::size_t fullTriangles = geometry.mTriangles.size() / 3;
            const std::size_t boneCount = skin.mBoneNames.size();
            if (geometry.mNoVertexData || vertexCount == 0 || fullTriangles == 0 || boneCount == 0)
            {
                ++report.mSkippedNoGeometry;
                continue;
            }
            if (model.mShapes.size() >= limits.mMaxShapes || boneCount > limits.mMaxBonesPerShape
                || acceptedVertices + vertexCount > limits.mMaxVertices
                || acceptedTriangles + fullTriangles > limits.mMaxTriangles)
            {
                ++report.mSkippedOverCap;
                continue;
            }

            Shape shape;
            shape.mName = geometry.mName;

            // Positions stay in the shape's own space.
            std::vector<char> bad(vertexCount, 0);
            shape.mPositions.resize(vertexCount);
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                const auto& p = geometry.mPositions[v];
                if (usable(p[0], limits.mMaxCoordinate) && usable(p[1], limits.mMaxCoordinate)
                    && usable(p[2], limits.mMaxCoordinate))
                    shape.mPositions[v] = p;
                else
                {
                    shape.mPositions[v] = { 0.f, 0.f, 0.f };
                    bad[v] = 1;
                    ++report.mBadVertices;
                }
            }

            // Triangles that can be drawn.
            std::uint32_t dropped = geometry.mTriangles.size() % 3 != 0 ? 1u : 0u;
            for (std::size_t t = 0; t < fullTriangles; ++t)
            {
                const std::uint32_t a = geometry.mTriangles[t * 3];
                const std::uint32_t b = geometry.mTriangles[t * 3 + 1];
                const std::uint32_t c = geometry.mTriangles[t * 3 + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount || bad[a] || bad[b] || bad[c])
                {
                    ++dropped;
                    continue;
                }
                shape.mIndices.insert(shape.mIndices.end(), { a, b, c });
            }
            report.mDroppedTriangles += dropped;
            if (shape.mIndices.empty())
            {
                ++report.mSkippedNoGeometry;
                continue;
            }

            // Normals: the file's if all are numbers, else made from the triangles.
            bool normalsGood = geometry.mNormals.size() == vertexCount;
            shape.mNormals.resize(vertexCount, { 0.f, 0.f, 1.f });
            if (normalsGood)
            {
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    const auto& nv = geometry.mNormals[v];
                    if (!std::isfinite(nv[0]) || !std::isfinite(nv[1]) || !std::isfinite(nv[2]))
                    {
                        normalsGood = false;
                        break;
                    }
                    shape.mNormals[v] = unit(nv[0], nv[1], nv[2]);
                }
            }
            if (!normalsGood)
            {
                std::vector<std::array<double, 3>> sums(vertexCount, { 0.0, 0.0, 0.0 });
                for (std::size_t i = 0; i + 2 < shape.mIndices.size(); i += 3)
                {
                    const auto& p0 = shape.mPositions[shape.mIndices[i]];
                    const auto& p1 = shape.mPositions[shape.mIndices[i + 1]];
                    const auto& p2 = shape.mPositions[shape.mIndices[i + 2]];
                    const double ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
                    const double vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
                    const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                    for (int k = 0; k < 3; ++k)
                    {
                        auto& s = sums[shape.mIndices[i + k]];
                        s[0] += nx;
                        s[1] += ny;
                        s[2] += nz;
                    }
                }
                for (std::size_t v = 0; v < vertexCount; ++v)
                    shape.mNormals[v] = unit(sums[v][0], sums[v][1], sums[v][2]);
            }

            // Texture coordinates with the shader's offset and scale baked in.
            if (geometry.mUV0.size() == vertexCount)
            {
                shape.mUV.resize(vertexCount);
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    const float u = geometry.mUV0[v][0] * material.mUVScale[0] + material.mUVOffset[0];
                    const float w = geometry.mUV0[v][1] * material.mUVScale[1] + material.mUVOffset[1];
                    shape.mUV[v] = { std::isfinite(u) ? u : 0.f, std::isfinite(w) ? w : 0.f };
                }
            }
            shape.mKey = makeKey(scene, geometry, material, !shape.mUV.empty(), limits);
            shape.mShaderType = material.mShaderType;
            if (material.mShaderType == kHairTintShaderType && std::isfinite(material.mHairTintColor[0])
                && std::isfinite(material.mHairTintColor[1]) && std::isfinite(material.mHairTintColor[2]))
            {
                shape.mHasTintColor = true;
                for (std::size_t c = 0; c < 3; ++c)
                    shape.mTintColor[c] = std::clamp(material.mHairTintColor[c], 0.f, 4.f);
            }
            if (material.mShaderType == kFaceTintShaderType && material.mTextureSet < scene.mTextureSets.size())
            {
                const auto& set = scene.mTextureSets[material.mTextureSet].mTextures;
                if (set.size() > kFaceTintSlot && !set[kFaceTintSlot].empty()
                    && set[kFaceTintSlot].size() <= limits.mMaxPathLength)
                    shape.mTintTexture = set[kFaceTintSlot];
            }

            // The skin's bones.
            shape.mBones.resize(boneCount);
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                shape.mBones[b].mName = skin.mBoneNames[b];
                if (b < skin.mBindTransforms.size())
                    shape.mBones[b].mInverseBind = skin.mBindTransforms[b];
            }
            shape.mHasSkinTransform = !isIdentity(skin.mSkinTransform);
            shape.mSkinTransform = skin.mSkinTransform;

            // Influences: at most four a vertex, largest first, renormalised.
            const bool startsOk = geometry.mInfluenceStart.size() == vertexCount + 1;
            shape.mBoneIndices.assign(vertexCount, { 0, 0, 0, 0 });
            shape.mBoneWeights.assign(vertexCount, { 0.f, 0.f, 0.f, 0.f });
            std::vector<Influence> mine;
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                mine.clear();
                if (startsOk)
                {
                    const std::size_t first = geometry.mInfluenceStart[v];
                    const std::size_t last = std::min<std::size_t>(geometry.mInfluenceStart[v + 1], geometry.mInfluences.size());
                    for (std::size_t i = first; i < last; ++i)
                    {
                        const Influence& influence = geometry.mInfluences[i];
                        if (influence.mBone >= boneCount || !std::isfinite(influence.mWeight))
                        {
                            ++report.mBadInfluences;
                            continue;
                        }
                        if (influence.mWeight > 0.f)
                            mine.push_back(influence);
                    }
                }
                std::stable_sort(mine.begin(), mine.end(), [](const Influence& a, const Influence& b) {
                    return a.mWeight != b.mWeight ? a.mWeight > b.mWeight : a.mBone < b.mBone;
                });
                if (mine.size() > kInfluencesPerVertex)
                    mine.resize(kInfluencesPerVertex);
                double sum = 0.0;
                for (const Influence& influence : mine)
                    sum += influence.mWeight;
                if (mine.empty() || !(sum > 0.0))
                {
                    shape.mBoneIndices[v][0] = 0;
                    shape.mBoneWeights[v][0] = 1.f;
                    ++report.mVerticesWithoutInfluence;
                    continue;
                }
                for (std::size_t k = 0; k < mine.size(); ++k)
                {
                    shape.mBoneIndices[v][k] = mine[k].mBone;
                    shape.mBoneWeights[v][k] = static_cast<float>(mine[k].mWeight / sum);
                }
            }

            acceptedVertices += vertexCount;
            acceptedTriangles += fullTriangles;
            report.mVertices += static_cast<std::uint32_t>(vertexCount);
            report.mTriangles += static_cast<std::uint32_t>(shape.mIndices.size() / 3);
            ++report.mDrawn;
            model.mShapes.push_back(std::move(shape));
        }
        return model;
    }
}
