#include "lodneutralobjects.hpp"

#include <algorithm>
#include <cmath>

#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/vfs/pathutil.hpp>

namespace OMW::NeutralLod
{
    namespace
    {
        constexpr double maxCoordinate = 1.0e8; // as the Morrowind bake's 1e8 limit on a position
        constexpr double maxScale = 1000.0; // as the Morrowind bake's limit on a scale
        constexpr double quaternionTolerance = 1.0e-3;

        std::uint8_t toByte(float value)
        {
            if (!(value > 0.f))
                return 0;
            if (value >= 1.f)
                return 255;
            return static_cast<std::uint8_t>(std::lround(value * 255.f));
        }
    }

    bool isFarStaticKind(openeden::sdk::game::ObjectKind kind) noexcept
    {
        return kind == openeden::sdk::game::ObjectKind::Static || kind == openeden::sdk::game::ObjectKind::Tree;
    }

    bool isMarkerModel(std::string_view modelPath) noexcept
    {
        return Misc::StringUtils::ciStartsWith(modelPath, "marker");
    }

    bool placementMatrix(const openeden::sdk::game::WorldTransform& transform, std::array<double, 12>& out)
    {
        const double position[3] = { transform.position.x, transform.position.y, transform.position.z };
        for (const double c : position)
            if (!std::isfinite(c) || std::abs(c) >= maxCoordinate)
                return false;
        const double scale = transform.scale;
        if (!std::isfinite(scale) || !(scale > 0.0) || scale >= maxScale)
            return false;
        const double x = transform.rotation.x, y = transform.rotation.y, z = transform.rotation.z,
                     w = transform.rotation.w;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w))
            return false;
        const double length = std::sqrt(x * x + y * y + z * z + w * w);
        if (std::abs(length - 1.0) > quaternionTolerance)
            return false;
        const double qx = x / length, qy = y / length, qz = z / length, qw = w / length;
        out = { scale * (1 - 2 * (qy * qy + qz * qz)), scale * 2 * (qx * qy - qz * qw),
            scale * 2 * (qx * qz + qy * qw), position[0], scale * 2 * (qx * qy + qz * qw),
            scale * (1 - 2 * (qx * qx + qz * qz)), scale * 2 * (qy * qz - qx * qw), position[1],
            scale * 2 * (qx * qz - qy * qw), scale * 2 * (qy * qz + qx * qw),
            scale * (1 - 2 * (qx * qx + qy * qy)), position[2] };
        return true;
    }

    std::string meshVfsPath(std::string_view modelPath)
    {
        if (modelPath.empty())
            return {};
        return Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(modelPath)).value();
    }

    std::string textureVfsPath(std::string_view texturePath)
    {
        if (texturePath.empty())
            return {};
        return VFS::Path::Normalized(texturePath).value();
    }

    Lod::FlattenedModel toFlattenedModel(const NifScene::Flat::FlatModel& model, bool blendAsCutout)
    {
        Lod::FlattenedModel result;
        for (const NifScene::Flat::Shape& shape : model.mShapes)
        {
            const std::size_t vertexCount = shape.mPositions.size();
            if (shape.mIndices.empty() || shape.mIndices.size() % 3 != 0 || vertexCount == 0)
                continue;
            if (std::any_of(shape.mIndices.begin(), shape.mIndices.end(),
                    [vertexCount](std::uint32_t index) { return index >= vertexCount; }))
                continue;

            const NifScene::Flat::MaterialKey& key = shape.mKey;
            bool test = key.mTest;
            std::uint8_t threshold = key.mThreshold;
            if (key.mBlend && !test)
            {
                // Only the ordinary blend can be a cutout; an additive or other blend (glows, light shafts) would draw
                // as a solid sheet. The rule is Lod::flattenNifModel's.
                if (blendAsCutout && key.mSourceBlend == 6 && key.mDestinationBlend == 7 && !key.mDiffuse.empty()
                    && key.mAlpha >= 0.99f && !Lod::textureLooksTranslucent(key.mDiffuse))
                {
                    test = true;
                    threshold = Lod::blendCutoutThreshold;
                }
                else
                {
                    result.mTraits.mAlphaBlended = true;
                    continue;
                }
            }

            Lod::SourcePiece piece;
            piece.mTexture = key.mDiffuse;
            piece.mFlags = (test ? Lod::clusterFlagAlphaTest : 0u) | (key.mDoubleSided ? Lod::clusterFlagTwoSided : 0u);
            piece.mAlphaCutoff = test ? static_cast<float>(threshold) / 255.f : 0.f;
            piece.mVertices.resize(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                Lod::SourceVertex& out = piece.mVertices[i];
                for (int k = 0; k < 3; ++k)
                    out.mPosition[k] = shape.mPositions[i][k];
                if (i < shape.mNormals.size())
                    for (int k = 0; k < 3; ++k)
                        out.mNormal[k] = shape.mNormals[i][k];
                if (i < shape.mUV.size())
                {
                    out.mUv[0] = shape.mUV[i][0];
                    out.mUv[1] = shape.mUV[i][1];
                }
                if (i < shape.mColors.size())
                    for (int k = 0; k < 4; ++k)
                        out.mColor[k] = toByte(shape.mColors[i][k]);
            }
            piece.mIndices = shape.mIndices;
            result.mPieces.push_back(std::move(piece));
        }

        std::uint32_t triangles = 0;
        bool any = false;
        float low[3] = { 0, 0, 0 };
        float high[3] = { 0, 0, 0 };
        for (const Lod::SourcePiece& piece : result.mPieces)
        {
            triangles += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
            for (const Lod::SourceVertex& v : piece.mVertices)
            {
                for (int k = 0; k < 3; ++k)
                {
                    low[k] = any ? std::min(low[k], v.mPosition[k]) : v.mPosition[k];
                    high[k] = any ? std::max(high[k], v.mPosition[k]) : v.mPosition[k];
                }
                any = true;
            }
        }
        result.mTraits.mKind = Lod::ObjectKind::Static;
        result.mTraits.mTriangleCount = triangles;
        if (any)
        {
            const float centre[3]
                = { 0.5f * (low[0] + high[0]), 0.5f * (low[1] + high[1]), 0.5f * (low[2] + high[2]) };
            float radiusSquared = 0;
            for (const Lod::SourcePiece& piece : result.mPieces)
                for (const Lod::SourceVertex& v : piece.mVertices)
                {
                    float distanceSquared = 0;
                    for (int k = 0; k < 3; ++k)
                        distanceSquared += (v.mPosition[k] - centre[k]) * (v.mPosition[k] - centre[k]);
                    radiusSquared = std::max(radiusSquared, distanceSquared);
                }
            result.mTraits.mRadius = std::sqrt(radiusSquared);
        }
        return result;
    }
}
