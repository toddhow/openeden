#include "clusterbuild.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include <meshoptimizer.h>

namespace Lod
{
    namespace
    {
        LodError fail(std::string field, std::string message)
        {
            LodError error;
            error.mField = std::move(field);
            error.mMessage = std::move(message);
            return error;
        }

        // One piece of an included object after the transform and the simplification.
        struct BuiltPiece
        {
            std::string mTexture;
            std::uint32_t mFlags = 0;
            float mAlphaCutoff = 0;
            std::vector<ClusterVertex> mVertices; // cell slot not yet set
            std::vector<std::uint32_t> mIndices; // into mVertices
        };

        struct BuiltObject
        {
            std::size_t mSource = 0; // index into the input
            std::vector<BuiltPiece> mPieces;
        };

        using GroupKey = std::tuple<std::string, std::uint32_t, float>;

        bool allFinite(const std::array<double, 12>& m)
        {
            return std::all_of(m.begin(), m.end(), [](double v) { return std::isfinite(v); });
        }

        // The matrix that carries a normal with the surface: the cofactor matrix, times the sign of the determinant
        // so that a mirrored transform keeps the normal pointing out. Row-major 3 x 3.
        struct NormalMatrix
        {
            double m[9];
            bool mMirrored;
        };

        NormalMatrix makeNormalMatrix(const std::array<double, 12>& t)
        {
            const double a = t[0], b = t[1], c = t[2];
            const double d = t[4], e = t[5], f = t[6];
            const double g = t[8], h = t[9], i = t[10];
            NormalMatrix result{};
            double cof[9] = { e * i - f * h, -(d * i - f * g), d * h - e * g, -(b * i - c * h), a * i - c * g,
                -(a * h - b * g), b * f - c * e, -(a * f - c * d), a * e - b * d };
            const double det = a * cof[0] + b * cof[1] + c * cof[2];
            result.mMirrored = det < 0;
            const double sign = result.mMirrored ? -1.0 : 1.0;
            for (int k = 0; k < 9; ++k)
                result.m[k] = cof[k] * sign;
            return result;
        }

        std::int8_t snorm8(double value)
        {
            const double scaled = std::clamp(value, -1.0, 1.0) * 127.0;
            return static_cast<std::int8_t>(std::lround(scaled));
        }

        // Transforms, simplifies and compacts one piece. Returns false when nothing is left. `error` is absolute, in
        // world units, and every vertex is already in tile space, so the simplifier's error is in world units too.
        bool buildPiece(const SourceObject& object, const SourcePiece& piece, const ClusterBuildInput& input,
            const NormalMatrix& normalMatrix, BuiltPiece& built, float& maxError)
        {
            if (piece.mIndices.empty() || piece.mVertices.empty())
                return false;
            const std::array<double, 12>& t = object.mTransform;
            // A path or state the payload cannot hold is made normal (case, slashes, cutoff) or the piece is left out.
            built.mTexture = piece.mTexture;
            std::replace(built.mTexture.begin(), built.mTexture.end(), '\\', '/');
            std::transform(built.mTexture.begin(), built.mTexture.end(), built.mTexture.begin(),
                [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
            while (!built.mTexture.empty() && built.mTexture.front() == '/')
                built.mTexture.erase(built.mTexture.begin());
            if (!isValidClusterTexturePath(built.mTexture)
                || (piece.mFlags & ~(clusterFlagAlphaTest | clusterFlagTwoSided)) != 0)
                return false;
            const std::size_t vertexCount = piece.mVertices.size();

            std::vector<ClusterVertex> vertices(vertexCount);
            std::vector<float> positions(vertexCount * 3);
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                const SourceVertex& s = piece.mVertices[i];
                const double x = s.mPosition[0], y = s.mPosition[1], z = s.mPosition[2];
                const double wx = t[0] * x + t[1] * y + t[2] * z + t[3] - input.mOriginX;
                const double wy = t[4] * x + t[5] * y + t[6] * z + t[7] - input.mOriginY;
                const double wz = t[8] * x + t[9] * y + t[10] * z + t[11];
                ClusterVertex& v = vertices[i];
                v.mPosition[0] = static_cast<float>(wx);
                v.mPosition[1] = static_cast<float>(wy);
                v.mPosition[2] = static_cast<float>(wz);
                positions[3 * i] = v.mPosition[0];
                positions[3 * i + 1] = v.mPosition[1];
                positions[3 * i + 2] = v.mPosition[2];
                if (!std::isfinite(v.mPosition[0]) || !std::isfinite(v.mPosition[1]) || !std::isfinite(v.mPosition[2]))
                    return false; // a transform so large that the tile-space position overflows a float

                const double nx = normalMatrix.m[0] * s.mNormal[0] + normalMatrix.m[1] * s.mNormal[1]
                    + normalMatrix.m[2] * s.mNormal[2];
                const double ny = normalMatrix.m[3] * s.mNormal[0] + normalMatrix.m[4] * s.mNormal[1]
                    + normalMatrix.m[5] * s.mNormal[2];
                const double nz = normalMatrix.m[6] * s.mNormal[0] + normalMatrix.m[7] * s.mNormal[1]
                    + normalMatrix.m[8] * s.mNormal[2];
                const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (std::isfinite(length) && length > 1e-12)
                {
                    v.mNormal[0] = snorm8(nx / length);
                    v.mNormal[1] = snorm8(ny / length);
                    v.mNormal[2] = snorm8(nz / length);
                }
                else
                {
                    v.mNormal[0] = 0;
                    v.mNormal[1] = 0;
                    v.mNormal[2] = 127;
                }
                for (std::size_t k = 0; k < 4; ++k)
                    v.mColor[k] = s.mColor[k];
                v.mUv[0] = s.mUv[0];
                v.mUv[1] = s.mUv[1];
            }

            std::vector<std::uint32_t> indices = piece.mIndices;
            if (normalMatrix.mMirrored)
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                    std::swap(indices[i + 1], indices[i + 2]);

            std::vector<std::uint32_t> simplified(indices.size());
            float achieved = 0;
            const std::size_t kept
                = meshopt_simplify(simplified.data(), indices.data(), indices.size(), positions.data(), vertexCount,
                    3 * sizeof(float), 0, input.mLevel.mError, meshopt_SimplifyErrorAbsolute, &achieved);
            simplified.resize(kept);
            if (simplified.empty())
            {
                // The whole piece collapsed: what is lost is about its own size, which the error must cover.
                float low[3] = { positions[0], positions[1], positions[2] };
                float high[3] = { positions[0], positions[1], positions[2] };
                for (std::size_t i = 1; i < vertexCount; ++i)
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        low[k] = std::min(low[k], positions[3 * i + k]);
                        high[k] = std::max(high[k], positions[3 * i + k]);
                    }
                const float dx = high[0] - low[0], dy = high[1] - low[1], dz = high[2] - low[2];
                maxError = std::max(maxError, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
                return false;
            }
            maxError = std::max(maxError, achieved);

            // Keep only the vertices the triangles use, in order of first use.
            std::vector<std::uint32_t> remap(vertexCount, 0xFFFFFFFFu);
            built.mVertices.clear();
            built.mIndices.clear();
            built.mIndices.reserve(simplified.size());
            for (const std::uint32_t index : simplified)
            {
                if (remap[index] == 0xFFFFFFFFu)
                {
                    remap[index] = static_cast<std::uint32_t>(built.mVertices.size());
                    built.mVertices.push_back(vertices[index]);
                }
                built.mIndices.push_back(remap[index]);
            }
            built.mFlags = piece.mFlags;
            built.mAlphaCutoff = (piece.mFlags & clusterFlagAlphaTest) != 0 ? std::max(0.f, piece.mAlphaCutoff) : 0.f;
            return true;
        }

        LodError validateInput(const ClusterBuildInput& input)
        {
            if (input.mLevel.mLevel < minClusterLevel || input.mLevel.mLevel > maxClusterLevel)
                return fail("level", "the level must be 1 to 3");
            if (!std::isfinite(input.mSide) || !(input.mSide > 0))
                return fail("side", "the side must be finite and above 0");
            if (!std::isfinite(input.mLevel.mError) || !(input.mLevel.mError > 0))
                return fail("error", "the error must be finite and above 0");
            if (!std::isfinite(input.mLevel.mDropFactor) || input.mLevel.mDropFactor < 0)
                return fail("dropFactor", "the drop factor must be finite and not negative");
            if (!std::isfinite(input.mCellSize) || !(input.mCellSize > 0))
                return fail("cellSize", "the cell size must be finite and above 0");
            if (!std::isfinite(input.mOriginX) || !std::isfinite(input.mOriginY))
                return fail("origin", "the origin must be finite");
            return {};
        }

        LodError validateObject(const SourceObject& object, std::size_t index, const ClusterBuildInput& input)
        {
            const std::string at = "objects[" + std::to_string(index) + "]";
            const double cells = std::clamp(std::round(input.mSide / input.mCellSize), 1.0, 1.0e9);
            const std::int64_t span = static_cast<std::int64_t>(cells);
            if (object.mCellX < input.mOriginCellX || object.mCellY < input.mOriginCellY
                || static_cast<std::int64_t>(object.mCellX) >= input.mOriginCellX + span
                || static_cast<std::int64_t>(object.mCellY) >= input.mOriginCellY + span)
                return fail(at + ".cell", "the object's cell is outside the tile");
            if (!allFinite(object.mTransform))
                return fail(at + ".transform", "a number is not finite");
            if (!std::isfinite(object.mTraits.mRadius))
                return fail(at + ".radius", "the radius is not finite");
            for (std::size_t p = 0; p < object.mPieces.size(); ++p)
            {
                const SourcePiece& piece = object.mPieces[p];
                const std::string pieceAt = at + ".pieces[" + std::to_string(p) + "]";
                if (piece.mIndices.size() % 3 != 0)
                    return fail(pieceAt + ".indices", "the index count is not a multiple of 3");
                for (const std::uint32_t i : piece.mIndices)
                    if (i >= piece.mVertices.size())
                        return fail(pieceAt + ".indices", "an index is beyond the vertices");
                if (!std::isfinite(piece.mAlphaCutoff))
                    return fail(pieceAt + ".alphaCutoff", "not finite");
                for (const SourceVertex& v : piece.mVertices)
                {
                    for (const float value : { v.mPosition[0], v.mPosition[1], v.mPosition[2], v.mNormal[0],
                             v.mNormal[1], v.mNormal[2], v.mUv[0], v.mUv[1] })
                        if (!std::isfinite(value))
                            return fail(pieceAt + ".vertices", "a number is not finite");
                }
            }
            return {};
        }
    }

    ExcludeReason classifyObject(const ObjectTraits& traits, const ClusterLevelParams& level) noexcept
    {
        if (traits.mKind != ObjectKind::Static)
            return ExcludeReason::NotStatic;
        if (traits.mTriangleCount == 0)
            return ExcludeReason::Empty;
        if (traits.mAnimated)
            return ExcludeReason::Animated;
        if (traits.mSkinned)
            return ExcludeReason::Skinned;
        if (traits.mHasParticles)
            return ExcludeReason::Particles;
        if (traits.mAlphaBlended)
            return ExcludeReason::AlphaBlended;
        if (traits.mRadius < level.mDropFactor * level.mError)
            return ExcludeReason::TooSmall;
        return ExcludeReason::None;
    }

    LodError buildObjectCluster(const ClusterBuildInput& input, ObjectCluster& cluster, ClusterBuildStats* stats)
    {
        cluster = ObjectCluster{};
        ClusterBuildStats local;
        ClusterBuildStats& s = stats != nullptr ? *stats : local;
        s = ClusterBuildStats{};

        if (LodError error = validateInput(input))
            return error;
        for (std::size_t i = 0; i < input.mObjects.size(); ++i)
            if (LodError error = validateObject(input.mObjects[i], i, input))
                return error;

        // The order that makes the output independent of the input order.
        std::vector<std::size_t> order(input.mObjects.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        const auto sortKey = [&input](std::size_t i) {
            const SourceObject& o = input.mObjects[i];
            return std::make_tuple(o.mCellY, o.mCellX, o.mRef.mIndex, o.mRef.mContentFile);
        };
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sortKey(a) < sortKey(b); });
        for (std::size_t i = 1; i < order.size(); ++i)
        {
            const ClusterRef& previous = input.mObjects[order[i - 1]].mRef;
            const ClusterRef& current = input.mObjects[order[i]].mRef;
            if (previous == current)
                return fail("objects[" + std::to_string(order[i]) + "].ref", "two objects have the same reference");
        }

        // Classify, transform, simplify.
        std::vector<BuiltObject> built;
        float maxSimplifyError = 0;
        float maxDropped = 0;
        for (const std::size_t index : order)
        {
            const SourceObject& object = input.mObjects[index];
            ++s.mObjectsSeen;
            const ExcludeReason reason = classifyObject(object.mTraits, input.mLevel);
            if (reason != ExcludeReason::None)
            {
                ++s.mExcluded[static_cast<std::size_t>(reason)];
                if (reason == ExcludeReason::TooSmall)
                    maxDropped = std::max(maxDropped, object.mTraits.mRadius);
                continue;
            }
            const NormalMatrix normalMatrix = makeNormalMatrix(object.mTransform);
            BuiltObject result;
            result.mSource = index;
            std::uint32_t sourceTriangles = 0;
            for (const SourcePiece& piece : object.mPieces)
            {
                BuiltPiece builtPiece;
                if (buildPiece(object, piece, input, normalMatrix, builtPiece, maxSimplifyError))
                {
                    sourceTriangles += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
                    result.mPieces.push_back(std::move(builtPiece));
                }
            }
            if (result.mPieces.empty())
            {
                // Everything collapsed or the model had no triangles: not included.
                ++s.mExcluded[static_cast<std::size_t>(ExcludeReason::Empty)];
                continue;
            }
            ++s.mObjectsIncluded;
            s.mTrianglesBefore += sourceTriangles;
            for (const BuiltPiece& piece : result.mPieces)
                s.mTrianglesAfter += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
            built.push_back(std::move(result));
        }
        s.mMaxSimplifyError = maxSimplifyError;
        s.mMaxDroppedRadius = maxDropped;
        if (built.empty())
            return {};

        // Cells in (y, x) order, which is also the order of `built`.
        ObjectCluster c;
        std::vector<std::size_t> slotOfBuilt(built.size());
        for (std::size_t i = 0; i < built.size(); ++i)
        {
            const SourceObject& object = input.mObjects[built[i].mSource];
            if (c.mCells.empty() || c.mCells.back().mCellX != object.mCellX || c.mCells.back().mCellY != object.mCellY)
            {
                if (c.mCells.size() >= maxClusterCells)
                    return fail("cells", "more than 64 cells hold objects");
                c.mCells.push_back(
                    ClusterCell{ object.mCellX, object.mCellY, static_cast<std::uint32_t>(c.mRefs.size()), 0 });
            }
            c.mCells.back().mRefCount++;
            c.mRefs.push_back(object.mRef);
            slotOfBuilt[i] = c.mCells.size() - 1;
        }

        // Groups, sorted by (texture, flags, cutoff).
        std::map<GroupKey, std::size_t> groupIndex;
        for (const BuiltObject& object : built)
            for (const BuiltPiece& piece : object.mPieces)
                groupIndex.emplace(GroupKey{ piece.mTexture, piece.mFlags, piece.mAlphaCutoff }, 0);
        std::size_t next = 0;
        for (auto& [key, index] : groupIndex)
        {
            index = next++;
            ClusterGroup group;
            group.mTexture = std::get<0>(key);
            group.mFlags = std::get<1>(key);
            group.mAlphaCutoff = std::get<2>(key);
            c.mGroups.push_back(std::move(group));
        }

        // Group by group, cell by cell, object by object.
        for (std::size_t g = 0; g < c.mGroups.size(); ++g)
        {
            const GroupKey key{ c.mGroups[g].mTexture, c.mGroups[g].mFlags, c.mGroups[g].mAlphaCutoff };
            c.mGroups[g].mFirstIndex = static_cast<std::uint32_t>(c.mIndices.size());
            std::size_t rangeFirst = c.mIndices.size();
            std::size_t rangeSlot = 0;
            bool inRange = false;
            const auto closeRange = [&]() {
                if (inRange && c.mIndices.size() > rangeFirst)
                    c.mRanges.push_back(ClusterRange{ static_cast<std::uint32_t>(g),
                        static_cast<std::uint32_t>(rangeSlot), static_cast<std::uint32_t>(rangeFirst),
                        static_cast<std::uint32_t>(c.mIndices.size() - rangeFirst) });
                inRange = false;
            };
            for (std::size_t o = 0; o < built.size(); ++o)
            {
                for (const BuiltPiece& piece : built[o].mPieces)
                {
                    if (GroupKey{ piece.mTexture, piece.mFlags, piece.mAlphaCutoff } != key)
                        continue;
                    if (!inRange || rangeSlot != slotOfBuilt[o])
                    {
                        closeRange();
                        rangeFirst = c.mIndices.size();
                        rangeSlot = slotOfBuilt[o];
                        inRange = true;
                    }
                    const auto base = static_cast<std::uint32_t>(c.mVertices.size());
                    for (ClusterVertex v : piece.mVertices)
                    {
                        v.mCellSlot = static_cast<std::uint8_t>(slotOfBuilt[o]);
                        c.mVertices.push_back(v);
                    }
                    for (const std::uint32_t i : piece.mIndices)
                        c.mIndices.push_back(base + i);
                }
            }
            closeRange();
            c.mGroups[g].mIndexCount = static_cast<std::uint32_t>(c.mIndices.size() - c.mGroups[g].mFirstIndex);
        }

        // Header values.
        c.mLevel = input.mLevel.mLevel;
        c.mSide = static_cast<float>(input.mSide);
        c.mOriginX = static_cast<float>(input.mOriginX);
        c.mOriginY = static_cast<float>(input.mOriginY);
        c.mSourceTriangles = s.mTrianglesBefore;
        c.mGeometricError = std::max(maxSimplifyError, maxDropped);
        c.mMinZ = c.mVertices.front().mPosition[2];
        c.mMaxZ = c.mMinZ;
        float overhang = 0;
        for (const ClusterVertex& v : c.mVertices)
        {
            c.mMinZ = std::min(c.mMinZ, v.mPosition[2]);
            c.mMaxZ = std::max(c.mMaxZ, v.mPosition[2]);
            for (int axis = 0; axis < 2; ++axis)
            {
                overhang = std::max(overhang, -v.mPosition[axis]);
                overhang = std::max(overhang, v.mPosition[axis] - c.mSide);
            }
        }
        // The validator tests side + overhang in float; make sure rounding cannot leave a vertex outside.
        for (const ClusterVertex& v : c.mVertices)
            for (int axis = 0; axis < 2; ++axis)
                while (c.mSide + overhang < v.mPosition[axis] || -overhang > v.mPosition[axis])
                    overhang = std::nextafter(overhang, std::numeric_limits<float>::infinity());
        c.mOverhang = overhang;

        if (LodError error = validateObjectCluster(c))
            return error;
        cluster = std::move(c);
        return {};
    }
}
