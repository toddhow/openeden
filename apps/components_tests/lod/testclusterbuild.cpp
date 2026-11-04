// Task 0059 (stage S4a): classification and building of an object cluster. Pure code on generated meshes; no GPU,
// window, clock or game data.
#include <gtest/gtest.h>

#include <components/lod/clusterbuild.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float nanValue = std::numeric_limits<float>::quiet_NaN();
    constexpr double cellSizeDefault = 8192.0;
    constexpr float tinyError = 0.001f; // small enough that nothing in these tests may be simplified

    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mState(seed)
        {
        }

        std::uint64_t next()
        {
            mState ^= mState << 13;
            mState ^= mState >> 7;
            mState ^= mState << 17;
            return mState;
        }

        std::uint64_t below(std::uint64_t bound) { return next() % bound; }

    private:
        std::uint64_t mState;
    };

    // ---- small vector maths, in doubles, independent of the code under test ----

    struct Vec3
    {
        double x = 0, y = 0, z = 0;
    };

    Vec3 operator+(const Vec3& a, const Vec3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 operator-(const Vec3& a, const Vec3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 operator*(const Vec3& a, double s)
    {
        return { a.x * s, a.y * s, a.z * s };
    }

    double dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 cross(const Vec3& a, const Vec3& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    double length(const Vec3& a)
    {
        return std::sqrt(dot(a, a));
    }

    // Distance from a point to a triangle (closest point after Ericson, Real-Time Collision Detection 5.1.5).
    double pointTriangleDistance(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
    {
        const Vec3 ab = b - a;
        const Vec3 ac = c - a;
        const Vec3 ap = p - a;
        const double d1 = dot(ab, ap);
        const double d2 = dot(ac, ap);
        if (d1 <= 0 && d2 <= 0)
            return length(p - a);
        const Vec3 bp = p - b;
        const double d3 = dot(ab, bp);
        const double d4 = dot(ac, bp);
        if (d3 >= 0 && d4 <= d3)
            return length(p - b);
        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0)
        {
            const double v = d1 / (d1 - d3);
            return length(p - (a + ab * v));
        }
        const Vec3 cp = p - c;
        const double d5 = dot(ab, cp);
        const double d6 = dot(ac, cp);
        if (d6 >= 0 && d5 <= d6)
            return length(p - c);
        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0)
        {
            const double w = d2 / (d2 - d6);
            return length(p - (a + ac * w));
        }
        const double va = d3 * d6 - d5 * d4;
        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        {
            const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return length(p - (b + (c - b) * w));
        }
        const double denom = 1.0 / (va + vb + vc);
        const double v = vb * denom;
        const double w = vc * denom;
        return length(p - (a + ab * v + ac * w));
    }

    // ---- generated meshes (model space) ----

    SourceVertex makeVertex(const Vec3& position, const Vec3& normal, float u, float v)
    {
        SourceVertex vertex;
        vertex.mPosition[0] = static_cast<float>(position.x);
        vertex.mPosition[1] = static_cast<float>(position.y);
        vertex.mPosition[2] = static_cast<float>(position.z);
        vertex.mNormal[0] = static_cast<float>(normal.x);
        vertex.mNormal[1] = static_cast<float>(normal.y);
        vertex.mNormal[2] = static_cast<float>(normal.z);
        vertex.mUv[0] = u;
        vertex.mUv[1] = v;
        return vertex;
    }

    // A cube of 24 vertices (flat face normals) and 12 triangles, counter-clockwise seen from outside.
    SourcePiece makeCubePiece(float half, const std::string& texture, std::uint32_t flags = 0, float cutoff = 0)
    {
        struct Face
        {
            Vec3 mNormal, mU, mV;
        };
        const Face faces[6] = {
            { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
            { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
            { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
            { { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 } },
        };
        const double corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        SourcePiece piece;
        piece.mTexture = texture;
        piece.mFlags = flags;
        piece.mAlphaCutoff = cutoff;
        for (const Face& face : faces)
        {
            const auto first = static_cast<std::uint32_t>(piece.mVertices.size());
            for (const auto& corner : corners)
            {
                const Vec3 position = (face.mNormal + face.mU * corner[0] + face.mV * corner[1]) * half;
                piece.mVertices.push_back(makeVertex(position, face.mNormal, static_cast<float>(corner[0] * 0.5 + 0.5),
                    static_cast<float>(corner[1] * 0.5 + 0.5)));
            }
            for (const std::uint32_t offset : { 0u, 1u, 2u, 0u, 2u, 3u })
            {
                piece.mIndices.push_back(first + offset);
            }
        }
        return piece;
    }

    // A closed, welded icosphere (no poles, no seams) with outward winding and radial unit normals.
    SourcePiece makeIcospherePiece(float radius, int subdivisions, const std::string& texture)
    {
        const double t = (1.0 + std::sqrt(5.0)) / 2.0;
        std::vector<Vec3> points = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
            { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
        for (Vec3& point : points)
        {
            point = point * (1.0 / length(point));
        }
        std::vector<std::array<std::uint32_t, 3>> faces
            = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
                  { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 },
                  { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        for (int step = 0; step < subdivisions; ++step)
        {
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
            const auto midpoint = [&points, &midpoints](std::uint32_t a, std::uint32_t b) {
                const auto key = std::make_pair(std::min(a, b), std::max(a, b));
                const auto found = midpoints.find(key);
                if (found != midpoints.end())
                    return found->second;
                const Vec3 sum = points[a] + points[b];
                points.push_back(sum * (1.0 / length(sum)));
                const auto index = static_cast<std::uint32_t>(points.size() - 1);
                midpoints[key] = index;
                return index;
            };
            std::vector<std::array<std::uint32_t, 3>> next;
            for (const auto& face : faces)
            {
                const std::uint32_t ab = midpoint(face[0], face[1]);
                const std::uint32_t bc = midpoint(face[1], face[2]);
                const std::uint32_t ca = midpoint(face[2], face[0]);
                next.push_back({ face[0], ab, ca });
                next.push_back({ face[1], bc, ab });
                next.push_back({ face[2], ca, bc });
                next.push_back({ ab, bc, ca });
            }
            faces = next;
        }
        SourcePiece piece;
        piece.mTexture = texture;
        for (const Vec3& point : points)
        {
            piece.mVertices.push_back(makeVertex(point * static_cast<double>(radius), point,
                static_cast<float>(0.5 + 0.5 * point.x), static_cast<float>(0.5 + 0.5 * point.y)));
        }
        for (const auto& face : faces)
        {
            const Vec3 centroid = points[face[0]] + points[face[1]] + points[face[2]];
            const Vec3 normal = cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]);
            const bool outward = dot(normal, centroid) > 0;
            piece.mIndices.push_back(face[0]);
            piece.mIndices.push_back(outward ? face[1] : face[2]);
            piece.mIndices.push_back(outward ? face[2] : face[1]);
        }
        return piece;
    }

    // A flat grid of cells x cells quads in the z = 0 plane, spanning [-size/2, size/2], normal +z.
    SourcePiece makePlanePiece(float size, int cells, const std::string& texture)
    {
        SourcePiece piece;
        piece.mTexture = texture;
        const int side = cells + 1;
        for (int j = 0; j < side; ++j)
        {
            for (int i = 0; i < side; ++i)
            {
                const double fx = static_cast<double>(i) / cells;
                const double fy = static_cast<double>(j) / cells;
                piece.mVertices.push_back(makeVertex({ (fx - 0.5) * size, (fy - 0.5) * size, 0 }, { 0, 0, 1 },
                    static_cast<float>(fx), static_cast<float>(fy)));
            }
        }
        for (int j = 0; j < cells; ++j)
        {
            for (int i = 0; i < cells; ++i)
            {
                const auto a = static_cast<std::uint32_t>(j * side + i);
                const auto b = a + 1;
                const auto c = a + static_cast<std::uint32_t>(side) + 1;
                const auto d = a + static_cast<std::uint32_t>(side);
                for (const std::uint32_t index : { a, b, c, a, c, d })
                {
                    piece.mIndices.push_back(index);
                }
            }
        }
        return piece;
    }

    // ---- objects and inputs ----

    SourceObject makeObject(std::uint32_t refIndex, std::int32_t cellX, std::int32_t cellY, SourcePiece piece,
        float radius, double cellSize = cellSizeDefault, double z = 1000.0)
    {
        SourceObject object;
        object.mRef = ClusterRef{ refIndex, 0 };
        object.mCellX = cellX;
        object.mCellY = cellY;
        object.mTraits.mKind = ObjectKind::Static;
        object.mTraits.mRadius = radius;
        object.mTraits.mTriangleCount = static_cast<std::uint32_t>(piece.mIndices.size() / 3);
        object.mTransform = { 1, 0, 0, (cellX + 0.5) * cellSize, 0, 1, 0, (cellY + 0.5) * cellSize, 0, 0, 1, z };
        object.mPieces.push_back(std::move(piece));
        return object;
    }

    SourceObject makeCubeObject(std::uint32_t refIndex, std::int32_t cellX, std::int32_t cellY,
        const std::string& texture, std::uint32_t flags = 0, float cutoff = 0, float half = 200)
    {
        return makeObject(refIndex, cellX, cellY, makeCubePiece(half, texture, flags, cutoff), half * 1.75f);
    }

    // A level 1 tile whose south-west cell is (-1, 0): the cells (-1,0), (0,0), (-1,1), (0,1).
    ClusterBuildInput makeInput(float error = 64)
    {
        ClusterBuildInput input;
        input.mLevel = ClusterLevelParams{ 1, error, 1.0f };
        input.mOriginX = -8192;
        input.mOriginY = 0;
        input.mSide = 16384;
        input.mCellSize = cellSizeDefault;
        input.mOriginCellX = -1;
        input.mOriginCellY = 0;
        return input;
    }

    struct Built
    {
        ObjectCluster mCluster;
        ClusterBuildStats mStats;
        LodError mError;
    };

    Built build(const ClusterBuildInput& input)
    {
        Built built;
        built.mError = buildObjectCluster(input, built.mCluster, &built.mStats);
        return built;
    }

    std::vector<std::uint8_t> bytesOf(const ObjectCluster& cluster)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = writeObjectCluster(cluster, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    bool sameStats(const ClusterBuildStats& a, const ClusterBuildStats& b)
    {
        return a.mObjectsSeen == b.mObjectsSeen && a.mObjectsIncluded == b.mObjectsIncluded
            && a.mExcluded == b.mExcluded && a.mTrianglesBefore == b.mTrianglesBefore
            && a.mTrianglesAfter == b.mTrianglesAfter && a.mMaxSimplifyError == b.mMaxSimplifyError
            && a.mMaxDroppedRadius == b.mMaxDroppedRadius;
    }

    std::uint32_t excludedCount(const ClusterBuildStats& stats, ExcludeReason reason)
    {
        return stats.mExcluded[static_cast<std::size_t>(reason)];
    }

    Vec3 applyTransform(const std::array<double, 12>& m, const float (&p)[3])
    {
        return { m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] };
    }

    Vec3 worldPosition(const ObjectCluster& cluster, std::size_t vertex)
    {
        const ClusterVertex& v = cluster.mVertices[vertex];
        return { cluster.mOriginX + static_cast<double>(v.mPosition[0]),
            cluster.mOriginY + static_cast<double>(v.mPosition[1]), static_cast<double>(v.mPosition[2]) };
    }

    Vec3 decodeNormal(const ClusterVertex& vertex)
    {
        return { vertex.mNormal[0] / 127.0, vertex.mNormal[1] / 127.0, vertex.mNormal[2] / 127.0 };
    }

    std::vector<std::array<Vec3, 3>> outputTriangles(const ObjectCluster& cluster)
    {
        std::vector<std::array<Vec3, 3>> triangles;
        for (std::size_t i = 0; i + 2 < cluster.mIndices.size(); i += 3)
        {
            triangles.push_back({ worldPosition(cluster, cluster.mIndices[i]),
                worldPosition(cluster, cluster.mIndices[i + 1]), worldPosition(cluster, cluster.mIndices[i + 2]) });
        }
        return triangles;
    }

    // Checks, without trusting validateObjectCluster, that the cluster's tables are well formed.
    void expectStructure(const ObjectCluster& cluster)
    {
        const LodError validated = validateObjectCluster(cluster);
        EXPECT_TRUE(validated.ok()) << validated.describe();

        std::uint32_t running = 0;
        for (std::size_t i = 0; i < cluster.mGroups.size(); ++i)
        {
            const ClusterGroup& group = cluster.mGroups[i];
            EXPECT_EQ(group.mFirstIndex, running) << "group " << i;
            running += group.mIndexCount;
            if (i > 0)
            {
                const ClusterGroup& before = cluster.mGroups[i - 1];
                EXPECT_TRUE(std::tie(before.mTexture, before.mFlags, before.mAlphaCutoff)
                    < std::tie(group.mTexture, group.mFlags, group.mAlphaCutoff))
                    << "groups " << i - 1 << " and " << i << " are not strictly sorted";
            }
        }
        EXPECT_EQ(running, cluster.mIndices.size());

        std::vector<std::uint32_t> cursor;
        for (const ClusterGroup& group : cluster.mGroups)
        {
            cursor.push_back(group.mFirstIndex);
        }
        for (std::size_t i = 0; i < cluster.mRanges.size(); ++i)
        {
            const ClusterRange& range = cluster.mRanges[i];
            ASSERT_LT(range.mGroup, cluster.mGroups.size());
            ASSERT_LT(range.mCellSlot, cluster.mCells.size());
            if (i > 0)
            {
                const ClusterRange& before = cluster.mRanges[i - 1];
                EXPECT_TRUE(std::tie(before.mGroup, before.mCellSlot) < std::tie(range.mGroup, range.mCellSlot));
            }
            EXPECT_GT(range.mIndexCount, 0u);
            EXPECT_EQ(range.mIndexCount % 3, 0u);
            EXPECT_EQ(range.mFirstIndex, cursor[range.mGroup]) << "range " << i;
            cursor[range.mGroup] += range.mIndexCount;
            for (std::uint32_t k = range.mFirstIndex; k < range.mFirstIndex + range.mIndexCount; ++k)
            {
                ASSERT_LT(cluster.mIndices[k], cluster.mVertices.size());
                EXPECT_EQ(cluster.mVertices[cluster.mIndices[k]].mCellSlot, range.mCellSlot) << "range " << i;
            }
        }
        for (std::size_t i = 0; i < cluster.mGroups.size(); ++i)
        {
            EXPECT_EQ(cursor[i], cluster.mGroups[i].mFirstIndex + cluster.mGroups[i].mIndexCount) << "group " << i;
        }

        std::uint32_t refCursor = 0;
        for (std::size_t i = 0; i < cluster.mCells.size(); ++i)
        {
            const ClusterCell& cell = cluster.mCells[i];
            if (i > 0)
            {
                const ClusterCell& before = cluster.mCells[i - 1];
                EXPECT_TRUE(std::tie(before.mCellY, before.mCellX) < std::tie(cell.mCellY, cell.mCellX));
            }
            EXPECT_EQ(cell.mFirstRef, refCursor);
            EXPECT_GT(cell.mRefCount, 0u);
            refCursor += cell.mRefCount;
            ASSERT_LE(refCursor, cluster.mRefs.size());
            for (std::uint32_t k = cell.mFirstRef + 1; k < cell.mFirstRef + cell.mRefCount; ++k)
            {
                EXPECT_TRUE(cluster.mRefs[k - 1] < cluster.mRefs[k]) << "cell " << i;
            }
        }
        EXPECT_EQ(refCursor, cluster.mRefs.size());

        for (const ClusterVertex& vertex : cluster.mVertices)
        {
            for (const float value : vertex.mPosition)
            {
                EXPECT_TRUE(std::isfinite(value));
            }
            EXPECT_GE(vertex.mPosition[0], -cluster.mOverhang - 1e-3f);
            EXPECT_LE(vertex.mPosition[0], cluster.mSide + cluster.mOverhang + 1e-3f);
            EXPECT_GE(vertex.mPosition[1], -cluster.mOverhang - 1e-3f);
            EXPECT_LE(vertex.mPosition[1], cluster.mSide + cluster.mOverhang + 1e-3f);
            EXPECT_GE(vertex.mPosition[2], cluster.mMinZ - 1e-3f);
            EXPECT_LE(vertex.mPosition[2], cluster.mMaxZ + 1e-3f);
        }
    }

    // Every triangle and every vertex normal of a cluster of one convex object faces away from its centre.
    void expectOutward(const ObjectCluster& cluster, const Vec3& centre)
    {
        ASSERT_FALSE(cluster.mIndices.empty());
        for (const auto& triangle : outputTriangles(cluster))
        {
            const Vec3 normal = cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
            if (length(normal) < 1e-6)
                continue;
            const Vec3 middle = (triangle[0] + triangle[1] + triangle[2]) * (1.0 / 3.0);
            EXPECT_GT(dot(normal, middle - centre), 0.0);
        }
        for (std::size_t i = 0; i < cluster.mVertices.size(); ++i)
        {
            const Vec3 outwards = worldPosition(cluster, i) - centre;
            EXPECT_GT(dot(decodeNormal(cluster.mVertices[i]), outwards), 0.0) << "vertex " << i;
        }
    }

    // ---- classification ----

    enum TraitFlags : unsigned
    {
        flagNone = 0,
        flagAnimated = 1,
        flagSkinned = 2,
        flagParticles = 4,
        flagAlpha = 8,
    };

    ObjectTraits makeTraits(ObjectKind kind, float radius, std::uint32_t triangles, unsigned flags)
    {
        ObjectTraits traits;
        traits.mKind = kind;
        traits.mRadius = radius;
        traits.mTriangleCount = triangles;
        traits.mAnimated = (flags & flagAnimated) != 0;
        traits.mSkinned = (flags & flagSkinned) != 0;
        traits.mHasParticles = (flags & flagParticles) != 0;
        traits.mAlphaBlended = (flags & flagAlpha) != 0;
        return traits;
    }

    TEST(LodClusterBuild, ClassifyTable)
    {
        struct Case
        {
            const char* mName;
            ObjectTraits mTraits;
            float mDropFactor;
            ExcludeReason mExpected;
        };
        const ObjectKind staticKind = ObjectKind::Static;
        const Case cases[] = {
            { "plain big static", makeTraits(staticKind, 100, 12, flagNone), 1, ExcludeReason::None },
            { "activator", makeTraits(ObjectKind::Activator, 100, 12, flagNone), 1, ExcludeReason::NotStatic },
            { "door", makeTraits(ObjectKind::Door, 100, 12, flagNone), 1, ExcludeReason::NotStatic },
            { "other", makeTraits(ObjectKind::Other, 100, 12, flagNone), 1, ExcludeReason::NotStatic },
            { "activator beats empty", makeTraits(ObjectKind::Activator, 100, 0, flagNone), 1,
                ExcludeReason::NotStatic },
            { "door beats everything",
                makeTraits(ObjectKind::Door, 1, 0, flagAnimated | flagSkinned | flagParticles | flagAlpha), 1,
                ExcludeReason::NotStatic },
            { "empty", makeTraits(staticKind, 100, 0, flagNone), 1, ExcludeReason::Empty },
            { "empty beats animated", makeTraits(staticKind, 100, 0, flagAnimated), 1, ExcludeReason::Empty },
            { "empty beats too small", makeTraits(staticKind, 1, 0, flagNone), 1, ExcludeReason::Empty },
            { "animated", makeTraits(staticKind, 100, 12, flagAnimated), 1, ExcludeReason::Animated },
            { "animated beats skinned", makeTraits(staticKind, 100, 12, flagAnimated | flagSkinned), 1,
                ExcludeReason::Animated },
            { "animated beats the rest",
                makeTraits(staticKind, 1, 12, flagAnimated | flagSkinned | flagParticles | flagAlpha), 1,
                ExcludeReason::Animated },
            { "skinned", makeTraits(staticKind, 100, 12, flagSkinned), 1, ExcludeReason::Skinned },
            { "skinned beats particles", makeTraits(staticKind, 100, 12, flagSkinned | flagParticles), 1,
                ExcludeReason::Skinned },
            { "skinned beats alpha and size", makeTraits(staticKind, 1, 12, flagSkinned | flagAlpha), 1,
                ExcludeReason::Skinned },
            { "particles", makeTraits(staticKind, 100, 12, flagParticles), 1, ExcludeReason::Particles },
            { "particles beats alpha", makeTraits(staticKind, 100, 12, flagParticles | flagAlpha), 1,
                ExcludeReason::Particles },
            { "particles beats too small", makeTraits(staticKind, 1, 12, flagParticles), 1, ExcludeReason::Particles },
            { "alpha blended", makeTraits(staticKind, 100, 12, flagAlpha), 1, ExcludeReason::AlphaBlended },
            { "alpha blended beats too small", makeTraits(staticKind, 1, 12, flagAlpha), 1,
                ExcludeReason::AlphaBlended },
            { "too small", makeTraits(staticKind, 63.99f, 12, flagNone), 1, ExcludeReason::TooSmall },
            { "radius zero", makeTraits(staticKind, 0, 12, flagNone), 1, ExcludeReason::TooSmall },
            { "radius equal to the drop size is included", makeTraits(staticKind, 64, 12, flagNone), 1,
                ExcludeReason::None },
            { "radius just above the drop size", makeTraits(staticKind, 64.01f, 12, flagNone), 1, ExcludeReason::None },
            { "drop factor 2: below", makeTraits(staticKind, 127.5f, 12, flagNone), 2, ExcludeReason::TooSmall },
            { "drop factor 2: equal", makeTraits(staticKind, 128, 12, flagNone), 2, ExcludeReason::None },
            { "drop factor 0 keeps tiny objects", makeTraits(staticKind, 0, 12, flagNone), 0, ExcludeReason::None },
            { "one triangle is enough", makeTraits(staticKind, 100, 1, flagNone), 1, ExcludeReason::None },
        };
        for (const Case& item : cases)
        {
            const ClusterLevelParams level{ 1, 64.0f, item.mDropFactor };
            EXPECT_EQ(classifyObject(item.mTraits, level), item.mExpected) << item.mName;
        }
    }

    TEST(LodClusterBuild, ClassifyUsesTheLevelsError)
    {
        const ObjectTraits traits = makeTraits(ObjectKind::Static, 100, 12, flagNone);
        EXPECT_EQ(classifyObject(traits, ClusterLevelParams{ 1, 64.0f, 1.0f }), ExcludeReason::None);
        EXPECT_EQ(classifyObject(traits, ClusterLevelParams{ 2, 192.0f, 1.0f }), ExcludeReason::TooSmall);
        EXPECT_EQ(classifyObject(traits, ClusterLevelParams{ 3, 576.0f, 1.0f }), ExcludeReason::TooSmall);
    }

    // ---- errors and empty input ----

    TEST(LodClusterBuild, EmptyInputGivesNoGroups)
    {
        const Built built = build(makeInput());
        EXPECT_TRUE(built.mError.ok()) << built.mError.describe();
        EXPECT_TRUE(built.mCluster.mGroups.empty());
        EXPECT_TRUE(built.mCluster.mRanges.empty());
        EXPECT_TRUE(built.mCluster.mIndices.empty());
        EXPECT_TRUE(built.mCluster.mVertices.empty());
        EXPECT_EQ(built.mStats.mObjectsSeen, 0u);
        EXPECT_EQ(built.mStats.mObjectsIncluded, 0u);
    }

    TEST(LodClusterBuild, OnlyExcludedObjectsGiveNoGroups)
    {
        ClusterBuildInput input = makeInput();
        SourceObject activator = makeCubeObject(1, 0, 0, "textures/a.dds");
        activator.mTraits.mKind = ObjectKind::Activator;
        SourceObject animated = makeCubeObject(2, 0, 0, "textures/a.dds");
        animated.mTraits.mAnimated = true;
        input.mObjects = { activator, animated };
        const Built built = build(input);
        EXPECT_TRUE(built.mError.ok()) << built.mError.describe();
        EXPECT_TRUE(built.mCluster.mGroups.empty());
        EXPECT_TRUE(built.mCluster.mRefs.empty());
        EXPECT_EQ(built.mStats.mObjectsSeen, 2u);
        EXPECT_EQ(built.mStats.mObjectsIncluded, 0u);
    }

    TEST(LodClusterBuild, InvalidInputsAreRefusedWithALocatedError)
    {
        struct Case
        {
            const char* mName;
            void (*mMutate)(ClusterBuildInput&);
        };
        const Case cases[] = {
            { "level 0", [](ClusterBuildInput& in) { in.mLevel.mLevel = 0; } },
            { "level 5", [](ClusterBuildInput& in) { in.mLevel.mLevel = 5; } },
            { "side zero", [](ClusterBuildInput& in) { in.mSide = 0; } },
            { "side negative", [](ClusterBuildInput& in) { in.mSide = -16384; } },
            { "side NaN", [](ClusterBuildInput& in) { in.mSide = std::numeric_limits<double>::quiet_NaN(); } },
            { "error zero", [](ClusterBuildInput& in) { in.mLevel.mError = 0; } },
            { "error negative", [](ClusterBuildInput& in) { in.mLevel.mError = -1; } },
            { "error NaN", [](ClusterBuildInput& in) { in.mLevel.mError = nanValue; } },
            { "cell size zero", [](ClusterBuildInput& in) { in.mCellSize = 0; } },
            { "cell size negative", [](ClusterBuildInput& in) { in.mCellSize = -8192; } },
            { "origin NaN", [](ClusterBuildInput& in) { in.mOriginX = std::numeric_limits<double>::quiet_NaN(); } },
            { "duplicate reference", [](ClusterBuildInput& in) { in.mObjects.push_back(in.mObjects[0]); } },
            { "cell east of the tile", [](ClusterBuildInput& in) { in.mObjects[0].mCellX = 1; } },
            { "cell west of the tile", [](ClusterBuildInput& in) { in.mObjects[0].mCellX = -2; } },
            { "cell south of the tile", [](ClusterBuildInput& in) { in.mObjects[0].mCellY = -1; } },
            { "cell north of the tile", [](ClusterBuildInput& in) { in.mObjects[0].mCellY = 2; } },
            { "index outside the vertices",
                [](ClusterBuildInput& in) {
                    SourcePiece& piece = in.mObjects[0].mPieces[0];
                    piece.mIndices[5] = static_cast<std::uint32_t>(piece.mVertices.size());
                } },
            { "index count not a multiple of 3",
                [](ClusterBuildInput& in) { in.mObjects[0].mPieces[0].mIndices.push_back(0); } },
            { "vertex NaN",
                [](ClusterBuildInput& in) { in.mObjects[0].mPieces[0].mVertices[3].mPosition[1] = nanValue; } },
            { "transform inf",
                [](ClusterBuildInput& in) { in.mObjects[0].mTransform[3] = std::numeric_limits<double>::infinity(); } },
            { "transform NaN",
                [](ClusterBuildInput& in) {
                    in.mObjects[0].mTransform[5] = std::numeric_limits<double>::quiet_NaN();
                } },
        };
        for (const Case& item : cases)
        {
            ClusterBuildInput input = makeInput();
            input.mObjects.push_back(makeCubeObject(1, 0, 0, "textures/a.dds"));
            const Built good = build(input);
            ASSERT_TRUE(good.mError.ok()) << good.mError.describe();

            item.mMutate(input);
            const Built bad = build(input);
            EXPECT_FALSE(bad.mError.mField.empty()) << item.mName;
        }
    }

    TEST(LodClusterBuild, TheLevelErrorsGrowAboutThreeTimesALevel)
    {
        EXPECT_EQ(clusterLevelError(0), 0.f);
        EXPECT_EQ(clusterLevelError(1), 128.f);
        EXPECT_EQ(clusterLevelError(2), 320.f);
        EXPECT_EQ(clusterLevelError(3), 960.f);
        EXPECT_EQ(clusterLevelError(4), 2900.f);
        EXPECT_EQ(clusterLevelError(5), 0.f); // above maxClusterLevel
        EXPECT_EQ(clusterLevelError(15), 0.f);
        for (std::uint32_t level = 2; level <= maxClusterLevel; ++level)
        {
            const float ratio = clusterLevelError(level) / clusterLevelError(level - 1);
            EXPECT_GT(ratio, 2.4f) << level;
            EXPECT_LT(ratio, 3.1f) << level;
        }
    }

    TEST(LodClusterBuild, MoreThanTwoHundredFiftySixDistinctCellsIsAnError)
    {
        // A tile of 32 x 32 cells of 4096 units: room for 1024 distinct cells, but a cluster takes 256 (a cell slot is
        // one byte).
        const auto makeCapInput = [](std::uint32_t objects) {
            ClusterBuildInput input;
            input.mLevel = ClusterLevelParams{ 4, 0.01f, 1.0f };
            input.mOriginX = 0;
            input.mOriginY = 0;
            input.mSide = 131072;
            input.mCellSize = 4096;
            input.mOriginCellX = 0;
            input.mOriginCellY = 0;
            for (std::uint32_t k = 0; k < objects; ++k)
            {
                input.mObjects.push_back(makeObject(k + 1, static_cast<std::int32_t>(k % 32),
                    static_cast<std::int32_t>(k / 32), makeCubePiece(50, "textures/a.dds"), 90, 4096.0));
            }
            return input;
        };

        const Built allowed = build(makeCapInput(256));
        ASSERT_TRUE(allowed.mError.ok()) << allowed.mError.describe();
        EXPECT_EQ(allowed.mCluster.mCells.size(), 256u);
        expectStructure(allowed.mCluster);

        const Built tooMany = build(makeCapInput(257));
        EXPECT_FALSE(tooMany.mError.mField.empty());
    }

    TEST(LodClusterBuild, SeveralObjectsInOneCellAreFine)
    {
        ClusterBuildInput input = makeInput(tinyError);
        for (std::uint32_t k = 1; k <= 40; ++k)
        {
            input.mObjects.push_back(makeCubeObject(k, 0, 0, "textures/a.dds", 0, 0, 20));
        }
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        EXPECT_EQ(built.mCluster.mCells.size(), 1u);
        EXPECT_EQ(built.mCluster.mRefs.size(), 40u);
        expectStructure(built.mCluster);
    }

    // ---- the result is a valid payload ----

    TEST(LodClusterBuild, ResultValidatesAndRoundTrips)
    {
        ClusterBuildInput input = makeInput();
        input.mObjects.push_back(makeCubeObject(1, -1, 0, "textures/a.dds"));
        input.mObjects.push_back(makeCubeObject(2, 0, 0, "textures/b.dds"));
        input.mObjects.push_back(makeObject(3, 0, 1, makeIcospherePiece(600, 3, "textures/rock.dds"), 600));
        input.mObjects.push_back(makeObject(4, -1, 1, makePlanePiece(1500, 6, "textures/floor.dds"), 1100));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        EXPECT_EQ(built.mCluster.mLevel, 1u);
        EXPECT_FLOAT_EQ(built.mCluster.mSide, 16384.0f);
        EXPECT_FLOAT_EQ(built.mCluster.mOriginX, -8192.0f);
        EXPECT_FLOAT_EQ(built.mCluster.mOriginY, 0.0f);

        const std::vector<std::uint8_t> bytes = bytesOf(built.mCluster);
        ASSERT_FALSE(bytes.empty());
        ObjectCluster back;
        const LodError error = readObjectCluster(bytes, back);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(back, built.mCluster);
    }

    // ---- transforms ----

    TEST(LodClusterBuild, VerticesAreInTileSpaceWithRotationScaleAndTranslation)
    {
        // Rotation by 90 degrees about z, uniform scale 2, translation: x' = -2y + tx, y' = 2x + ty, z' = 2z + tz.
        const double tx = -4000, ty = 5000, tz = 700;
        ClusterBuildInput input = makeInput(1.0f);
        SourceObject object = makeCubeObject(1, -1, 0, "textures/a.dds", 0, 0, 100);
        object.mTransform = { 0, -2, 0, tx, 2, 0, 0, ty, 0, 0, 2, tz };
        object.mTraits.mRadius = 346;
        input.mObjects.push_back(object);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);

        std::vector<Vec3> expected;
        for (const double sx : { -100.0, 100.0 })
        {
            for (const double sy : { -100.0, 100.0 })
            {
                for (const double sz : { -100.0, 100.0 })
                {
                    expected.push_back({ -2 * sy + tx, 2 * sx + ty, 2 * sz + tz });
                }
            }
        }
        const double tolerance = 0.05;
        for (std::size_t i = 0; i < built.mCluster.mVertices.size(); ++i)
        {
            const ClusterVertex& vertex = built.mCluster.mVertices[i];
            // x and y are relative to the tile origin (-8192, 0), z is absolute.
            const Vec3 world = { vertex.mPosition[0] - 8192.0, static_cast<double>(vertex.mPosition[1]),
                static_cast<double>(vertex.mPosition[2]) };
            bool matched = false;
            for (const Vec3& corner : expected)
            {
                matched = matched || length(world - corner) < tolerance;
            }
            EXPECT_TRUE(matched) << "vertex " << i << " at " << world.x << ", " << world.y << ", " << world.z;
        }
        for (const Vec3& corner : expected)
        {
            bool found = false;
            for (std::size_t i = 0; i < built.mCluster.mVertices.size(); ++i)
            {
                const ClusterVertex& vertex = built.mCluster.mVertices[i];
                const Vec3 world = { vertex.mPosition[0] - 8192.0, static_cast<double>(vertex.mPosition[1]),
                    static_cast<double>(vertex.mPosition[2]) };
                found = found || length(world - corner) < tolerance;
            }
            EXPECT_TRUE(found) << "missing corner " << corner.x << ", " << corner.y << ", " << corner.z;
        }
        expectOutward(built.mCluster, { tx, ty, tz });
    }

    TEST(LodClusterBuild, NormalsFollowTheCofactorMatrix)
    {
        // A quad on the plane z = x (normal (-1, 0, 1) / sqrt 2) scaled by (1, 1, 3) lies on z = 3x, whose normal is
        // (-3, 0, 1) / sqrt 10. Multiplying the normal by the matrix itself would give (-1, 0, 3) / sqrt 10.
        SourcePiece piece;
        piece.mTexture = "textures/a.dds";
        const Vec3 normal = { -1 / std::sqrt(2.0), 0, 1 / std::sqrt(2.0) };
        piece.mVertices.push_back(makeVertex({ -1000, -1000, -1000 }, normal, 0, 0));
        piece.mVertices.push_back(makeVertex({ 1000, -1000, 1000 }, normal, 1, 0));
        piece.mVertices.push_back(makeVertex({ 1000, 1000, 1000 }, normal, 1, 1));
        piece.mVertices.push_back(makeVertex({ -1000, 1000, -1000 }, normal, 0, 1));
        piece.mIndices = { 0, 1, 2, 0, 2, 3 };

        ClusterBuildInput input = makeInput(1.0f);
        SourceObject object = makeObject(1, 0, 0, piece, 3000);
        object.mTransform = { 1, 0, 0, 4096, 0, 1, 0, 4096, 0, 0, 3, 1000 };
        input.mObjects.push_back(object);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        ASSERT_FALSE(built.mCluster.mVertices.empty());
        const Vec3 expected = { -3 / std::sqrt(10.0), 0, 1 / std::sqrt(10.0) };
        for (const ClusterVertex& vertex : built.mCluster.mVertices)
        {
            const Vec3 decoded = decodeNormal(vertex);
            EXPECT_NEAR(decoded.x, expected.x, 0.03);
            EXPECT_NEAR(decoded.y, expected.y, 0.03);
            EXPECT_NEAR(decoded.z, expected.z, 0.03);
            EXPECT_NEAR(length(decoded), 1.0, 0.03);
        }
    }

    TEST(LodClusterBuild, NormalsAreUnitLengthAndRadialUnderRotationAndScale)
    {
        ClusterBuildInput input = makeInput(tinyError);
        SourceObject object = makeObject(1, 0, 0, makeIcospherePiece(600, 2, "textures/rock.dds"), 900);
        // Rotation about z by 30 degrees times a uniform scale of 1.5.
        const double c = 1.5 * std::cos(std::numbers::pi / 6);
        const double s = 1.5 * std::sin(std::numbers::pi / 6);
        object.mTransform = { c, -s, 0, 4096, s, c, 0, 4096, 0, 0, 1.5, 1500 };
        input.mObjects.push_back(object);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        ASSERT_FALSE(built.mCluster.mVertices.empty());
        const Vec3 centre = { 4096, 4096, 1500 };
        for (std::size_t i = 0; i < built.mCluster.mVertices.size(); ++i)
        {
            const Vec3 decoded = decodeNormal(built.mCluster.mVertices[i]);
            EXPECT_NEAR(length(decoded), 1.0, 0.03) << "vertex " << i;
            const Vec3 radial = worldPosition(built.mCluster, i) - centre;
            EXPECT_GT(dot(decoded, radial * (1.0 / length(radial))), 0.97) << "vertex " << i;
        }
        expectOutward(built.mCluster, centre);
    }

    TEST(LodClusterBuild, MirroredTransformKeepsOutwardFacingTriangles)
    {
        const double cx = 4096, cy = 4096, cz = 1000;
        const std::array<double, 12> mirrors[] = {
            { -1, 0, 0, cx, 0, 1, 0, cy, 0, 0, 1, cz }, // negative determinant
            { 1, 0, 0, cx, 0, -1, 0, cy, 0, 0, -1, cz }, // positive determinant (two mirrors)
            { 0, 1, 0, cx, 1, 0, 0, cy, 0, 0, 1, cz }, // swap of x and y: negative determinant
            { 2, 0, 0, cx, 0, 2, 0, cy, 0, 0, -2, cz }, // mirrored and scaled
        };
        for (const auto& transform : mirrors)
        {
            for (int shape = 0; shape < 2; ++shape)
            {
                ClusterBuildInput input = makeInput(tinyError);
                SourcePiece piece
                    = shape == 0 ? makeCubePiece(200, "textures/a.dds") : makeIcospherePiece(400, 2, "textures/a.dds");
                SourceObject object = makeObject(1, 0, 0, piece, 700);
                object.mTransform = transform;
                input.mObjects.push_back(object);
                const Built built = build(input);
                ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
                expectOutward(built.mCluster, { cx, cy, cz });
            }
        }
    }

    // ---- grouping and ranges ----

    TEST(LodClusterBuild, ObjectsAreGroupedByTextureFlagsAndCutoff)
    {
        ClusterBuildInput input = makeInput(tinyError);
        input.mObjects.push_back(makeCubeObject(10, 0, 0, "textures/b.dds"));
        input.mObjects.push_back(makeCubeObject(11, 0, 0, "textures/a.dds", clusterFlagAlphaTest, 0.5f));
        input.mObjects.push_back(makeCubeObject(12, -1, 0, "textures/a.dds")); // shares a group with 14
        input.mObjects.push_back(makeCubeObject(13, 0, 0, "textures/a.dds", clusterFlagTwoSided));
        input.mObjects.push_back(makeCubeObject(14, 0, 1, "textures/a.dds"));
        input.mObjects.push_back(makeCubeObject(15, 0, 0, "textures/a.dds", clusterFlagAlphaTest, 0.25f));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);

        const std::vector<ClusterGroup>& groups = built.mCluster.mGroups;
        ASSERT_EQ(groups.size(), 5u);
        EXPECT_EQ(groups[0].mTexture, "textures/a.dds");
        EXPECT_EQ(groups[0].mFlags, 0u);
        EXPECT_EQ(groups[0].mIndexCount, 2u * 36u); // two cubes of 12 triangles in ONE group
        EXPECT_EQ(groups[1].mTexture, "textures/a.dds");
        EXPECT_EQ(groups[1].mFlags, clusterFlagAlphaTest);
        EXPECT_FLOAT_EQ(groups[1].mAlphaCutoff, 0.25f);
        EXPECT_EQ(groups[1].mIndexCount, 36u);
        EXPECT_EQ(groups[2].mTexture, "textures/a.dds");
        EXPECT_EQ(groups[2].mFlags, clusterFlagAlphaTest);
        EXPECT_FLOAT_EQ(groups[2].mAlphaCutoff, 0.5f);
        EXPECT_EQ(groups[3].mTexture, "textures/a.dds");
        EXPECT_EQ(groups[3].mFlags, clusterFlagTwoSided);
        EXPECT_FLOAT_EQ(groups[3].mAlphaCutoff, 0.0f);
        EXPECT_EQ(groups[4].mTexture, "textures/b.dds");
        EXPECT_EQ(groups[4].mFlags, 0u);
        EXPECT_EQ(built.mCluster.mIndices.size(), 6u * 36u);
    }

    TEST(LodClusterBuild, RangesAndCellsFollowTheObjects)
    {
        ClusterBuildInput input = makeInput(tinyError);
        // Inserted in an order that is neither cell order nor reference order.
        input.mObjects.push_back(makeCubeObject(9, 0, 1, "textures/b.dds"));
        input.mObjects.push_back(makeCubeObject(3, -1, 0, "textures/a.dds"));
        input.mObjects.push_back(makeCubeObject(7, -1, 1, "textures/a.dds"));
        input.mObjects.push_back(makeCubeObject(2, 0, 0, "textures/b.dds"));
        input.mObjects.push_back(makeCubeObject(5, -1, 0, "textures/b.dds"));
        input.mObjects.push_back(makeCubeObject(1, -1, 0, "textures/a.dds"));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);

        const std::vector<ClusterCell>& cells = built.mCluster.mCells;
        ASSERT_EQ(cells.size(), 4u);
        const std::int32_t expectedCells[4][2] = { { -1, 0 }, { 0, 0 }, { -1, 1 }, { 0, 1 } }; // sorted by (y, x)
        const std::vector<std::vector<std::uint32_t>> expectedRefs = { { 1, 3, 5 }, { 2 }, { 7 }, { 9 } };
        for (std::size_t i = 0; i < 4; ++i)
        {
            EXPECT_EQ(cells[i].mCellX, expectedCells[i][0]) << i;
            EXPECT_EQ(cells[i].mCellY, expectedCells[i][1]) << i;
            ASSERT_EQ(cells[i].mRefCount, expectedRefs[i].size()) << i;
            for (std::size_t k = 0; k < expectedRefs[i].size(); ++k)
            {
                EXPECT_EQ(built.mCluster.mRefs[cells[i].mFirstRef + k].mIndex, expectedRefs[i][k]) << i;
                EXPECT_EQ(built.mCluster.mRefs[cells[i].mFirstRef + k].mContentFile, 0u) << i;
            }
        }

        // Group "a" has cubes in slots 0 (two) and 2; group "b" has slots 0, 1 and 3.
        ASSERT_EQ(built.mCluster.mGroups.size(), 2u);
        std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;
        for (const ClusterRange& range : built.mCluster.mRanges)
        {
            seen.emplace_back(range.mGroup, range.mCellSlot, range.mIndexCount);
        }
        const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> expected
            = { { 0, 0, 72 }, { 0, 2, 36 }, { 1, 0, 36 }, { 1, 1, 36 }, { 1, 3, 36 } };
        EXPECT_EQ(seen, expected);
    }

    TEST(LodClusterBuild, ReferencesCarryTheContentFileNumber)
    {
        ClusterBuildInput input = makeInput(tinyError);
        SourceObject first = makeCubeObject(4, 0, 0, "textures/a.dds");
        first.mRef.mContentFile = 2;
        SourceObject second = makeCubeObject(4, 0, 0, "textures/a.dds"); // same index, another content file
        second.mRef.mContentFile = 1;
        input.mObjects = { first, second };
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        ASSERT_EQ(built.mCluster.mRefs.size(), 2u);
        EXPECT_EQ(built.mCluster.mRefs[0], (ClusterRef{ 4, 1 }));
        EXPECT_EQ(built.mCluster.mRefs[1], (ClusterRef{ 4, 2 }));
    }

    // ---- exclusion and statistics ----

    TEST(LodClusterBuild, ExcludedObjectsAreAbsentAndCountedByReason)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects.push_back(makeCubeObject(1, 0, 0, "textures/a.dds"));
        SourceObject activator = makeCubeObject(2, 0, 0, "textures/a.dds");
        activator.mTraits.mKind = ObjectKind::Activator;
        SourceObject door = makeCubeObject(3, 0, 0, "textures/a.dds");
        door.mTraits.mKind = ObjectKind::Door;
        SourceObject other = makeCubeObject(4, 0, 0, "textures/a.dds");
        other.mTraits.mKind = ObjectKind::Other;
        SourceObject animated = makeCubeObject(5, 0, 0, "textures/a.dds");
        animated.mTraits.mAnimated = true;
        SourceObject skinned = makeCubeObject(6, 0, 0, "textures/a.dds");
        skinned.mTraits.mSkinned = true;
        SourceObject particles = makeCubeObject(7, 0, 0, "textures/a.dds");
        particles.mTraits.mHasParticles = true;
        SourceObject blended = makeCubeObject(8, 0, 0, "textures/a.dds");
        blended.mTraits.mAlphaBlended = true;
        SourceObject empty = makeCubeObject(9, 0, 0, "textures/a.dds");
        empty.mTraits.mTriangleCount = 0;
        empty.mPieces.clear();
        SourceObject smallOne = makeCubeObject(10, 0, 0, "textures/a.dds");
        smallOne.mTraits.mRadius = 30;
        SourceObject smallTwo = makeCubeObject(11, 0, 0, "textures/a.dds");
        smallTwo.mTraits.mRadius = 10;
        input.mObjects.insert(input.mObjects.end(),
            { activator, door, other, animated, skinned, particles, blended, empty, smallOne, smallTwo });

        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        ASSERT_EQ(built.mCluster.mRefs.size(), 1u);
        EXPECT_EQ(built.mCluster.mRefs[0], (ClusterRef{ 1, 0 }));
        EXPECT_EQ(built.mCluster.mIndices.size(), 36u);

        EXPECT_EQ(built.mStats.mObjectsSeen, 11u);
        EXPECT_EQ(built.mStats.mObjectsIncluded, 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::NotStatic), 3u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::Empty), 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::Animated), 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::Skinned), 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::Particles), 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::AlphaBlended), 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::TooSmall), 2u);
        EXPECT_FLOAT_EQ(built.mStats.mMaxDroppedRadius, 30.0f);
        EXPECT_GE(built.mCluster.mGeometricError, 30.0f);
    }

    TEST(LodClusterBuild, TooSmallIsStrictAtTheDropSize)
    {
        ClusterBuildInput input = makeInput(64);
        SourceObject exact = makeCubeObject(1, 0, 0, "textures/a.dds");
        exact.mTraits.mRadius = 64; // equal to dropFactor * error: included
        SourceObject below = makeCubeObject(2, -1, 0, "textures/a.dds");
        below.mTraits.mRadius = 63.9f;
        input.mObjects = { exact, below };
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        ASSERT_EQ(built.mCluster.mRefs.size(), 1u);
        EXPECT_EQ(built.mCluster.mRefs[0].mIndex, 1u);
        EXPECT_EQ(excludedCount(built.mStats, ExcludeReason::TooSmall), 1u);
        EXPECT_FLOAT_EQ(built.mStats.mMaxDroppedRadius, 63.9f);
    }

    TEST(LodClusterBuild, ExcludedObjectsDoNotCountAsPresentInTheirCell)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects.push_back(makeCubeObject(1, 0, 0, "textures/a.dds"));
        SourceObject activator = makeCubeObject(2, -1, 1, "textures/a.dds");
        activator.mTraits.mKind = ObjectKind::Activator;
        input.mObjects.push_back(activator);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        ASSERT_EQ(built.mCluster.mCells.size(), 1u);
        EXPECT_EQ(built.mCluster.mCells[0].mCellX, 0);
        EXPECT_EQ(built.mCluster.mCells[0].mCellY, 0);
    }

    TEST(LodClusterBuild, StatsAddUp)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects.push_back(makeObject(1, 0, 0, makeIcospherePiece(600, 3, "textures/rock.dds"), 600));
        input.mObjects.push_back(makeCubeObject(2, -1, 0, "textures/a.dds"));
        SourceObject animated = makeCubeObject(3, 0, 1, "textures/a.dds");
        animated.mTraits.mAnimated = true;
        input.mObjects.push_back(animated);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        const ClusterBuildStats& stats = built.mStats;
        std::uint32_t excluded = 0;
        for (std::size_t i = 1; i < stats.mExcluded.size(); ++i)
        {
            excluded += stats.mExcluded[i];
        }
        EXPECT_EQ(stats.mObjectsSeen, 3u);
        EXPECT_EQ(stats.mObjectsIncluded + excluded, stats.mObjectsSeen);
        EXPECT_EQ(stats.mObjectsIncluded, 2u);
        EXPECT_EQ(stats.mTrianglesBefore, 1280u + 12u); // the included objects only
        EXPECT_EQ(built.mCluster.mSourceTriangles, stats.mTrianglesBefore);
        EXPECT_EQ(stats.mTrianglesAfter * 3, built.mCluster.mIndices.size());
        EXPECT_LE(stats.mTrianglesAfter, stats.mTrianglesBefore);
    }

    // ---- simplification ----

    TEST(LodClusterBuild, SphereIsSimplifiedWithinTheLevelError)
    {
        for (const float error : { 16.0f, 64.0f })
        {
            ClusterBuildInput input = makeInput(error);
            const SourceObject object = makeObject(1, 0, 0, makeIcospherePiece(600, 4, "textures/rock.dds"), 600);
            input.mObjects.push_back(object);
            const Built built = build(input);
            ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
            expectStructure(built.mCluster);
            const ClusterBuildStats& stats = built.mStats;
            EXPECT_EQ(stats.mTrianglesBefore, 5120u);
            EXPECT_EQ(built.mCluster.mSourceTriangles, 5120u);
            EXPECT_LT(stats.mTrianglesAfter, stats.mTrianglesBefore) << "error " << error;
            EXPECT_GT(stats.mTrianglesAfter, 0u);
            EXPECT_EQ(stats.mTrianglesAfter * 3, built.mCluster.mIndices.size());
            EXPECT_LE(stats.mMaxSimplifyError, error + 0.01f);
            EXPECT_GE(built.mCluster.mGeometricError, stats.mMaxSimplifyError);
            EXPECT_FLOAT_EQ(built.mCluster.mGeometricError, std::max(stats.mMaxSimplifyError, stats.mMaxDroppedRadius));

            std::vector<std::array<Vec3, 3>> original;
            const SourcePiece& piece = object.mPieces[0];
            for (std::size_t i = 0; i + 2 < piece.mIndices.size(); i += 3)
            {
                original.push_back({ applyTransform(object.mTransform, piece.mVertices[piece.mIndices[i]].mPosition),
                    applyTransform(object.mTransform, piece.mVertices[piece.mIndices[i + 1]].mPosition),
                    applyTransform(object.mTransform, piece.mVertices[piece.mIndices[i + 2]].mPosition) });
            }

            // Every vertex of the simplified mesh lies on (or within the error of) the original surface.
            for (std::size_t v = 0; v < built.mCluster.mVertices.size(); ++v)
            {
                const Vec3 point = worldPosition(built.mCluster, v);
                double best = std::numeric_limits<double>::max();
                for (const auto& triangle : original)
                {
                    best = std::min(best, pointTriangleDistance(point, triangle[0], triangle[1], triangle[2]));
                }
                EXPECT_LE(best, error + 0.05) << "output vertex " << v << ", error " << error;
            }

            // And the original surface stays near the simplified one (loose bound: 2 x the error).
            const std::vector<std::array<Vec3, 3>> simplified = outputTriangles(built.mCluster);
            for (std::size_t v = 0; v < piece.mVertices.size(); ++v)
            {
                const Vec3 point = applyTransform(object.mTransform, piece.mVertices[v].mPosition);
                double best = std::numeric_limits<double>::max();
                for (const auto& triangle : simplified)
                {
                    best = std::min(best, pointTriangleDistance(point, triangle[0], triangle[1], triangle[2]));
                }
                EXPECT_LE(best, 2.0 * error + 0.05) << "original vertex " << v << ", error " << error;
            }
        }
    }

    TEST(LodClusterBuild, TinyErrorKeepsEveryTriangle)
    {
        ClusterBuildInput input = makeInput(tinyError);
        input.mObjects.push_back(makeObject(1, 0, 0, makeIcospherePiece(600, 3, "textures/rock.dds"), 600));
        input.mObjects.push_back(makeCubeObject(2, -1, 0, "textures/a.dds"));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        EXPECT_EQ(built.mStats.mTrianglesBefore, 1280u + 12u);
        EXPECT_EQ(built.mStats.mTrianglesAfter, built.mStats.mTrianglesBefore);
        EXPECT_EQ(built.mCluster.mIndices.size(), 3u * (1280u + 12u));
        EXPECT_LE(built.mStats.mMaxSimplifyError, tinyError + 1e-4f);
    }

    TEST(LodClusterBuild, FlatPlaneCollapsesToFewTriangles)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects.push_back(makeObject(1, 0, 0, makePlanePiece(2000, 20, "textures/floor.dds"), 1500));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        EXPECT_EQ(built.mStats.mTrianglesBefore, 800u);
        EXPECT_LT(built.mStats.mTrianglesAfter, 400u);
        EXPECT_GT(built.mStats.mTrianglesAfter, 0u);
        // The plane is flat, so the simplification costs nothing.
        EXPECT_LE(built.mStats.mMaxSimplifyError, 1.0f);
    }

    TEST(LodClusterBuild, MeshesSmallerThanTheErrorNeverLeaveDegenerateOutput)
    {
        for (const float half : { 1.0f, 5.0f, 20.0f, 60.0f })
        {
            for (int shape = 0; shape < 3; ++shape)
            {
                ClusterBuildInput input = makeInput(64);
                input.mLevel.mDropFactor = 0; // nothing is dropped by size, so the simplifier decides alone
                SourcePiece piece = shape == 0 ? makeCubePiece(half, "textures/a.dds")
                    : shape == 1               ? makeIcospherePiece(half, 2, "textures/a.dds")
                                               : makePlanePiece(half * 2, 4, "textures/a.dds");
                input.mObjects.push_back(makeObject(1, 0, 0, piece, half * 1.75f));
                const Built built = build(input);
                ASSERT_TRUE(built.mError.ok()) << built.mError.describe() << " half " << half << " shape " << shape;
                if (built.mCluster.mGroups.empty())
                {
                    EXPECT_TRUE(built.mCluster.mIndices.empty());
                    EXPECT_TRUE(built.mCluster.mRanges.empty());
                    EXPECT_TRUE(built.mCluster.mRefs.empty());
                    EXPECT_EQ(built.mStats.mObjectsIncluded, 0u);
                }
                else
                {
                    expectStructure(built.mCluster);
                    ASSERT_FALSE(built.mCluster.mIndices.empty());
                    std::vector<std::uint8_t> bytes;
                    const LodError written = writeObjectCluster(built.mCluster, bytes);
                    EXPECT_TRUE(written.ok()) << "half " << half << " shape " << shape;
                }
            }
        }
    }

    // ---- bounds ----

    TEST(LodClusterBuild, ZRangeAndSourceTrianglesAreReported)
    {
        ClusterBuildInput input = makeInput(tinyError);
        input.mObjects.push_back(makeObject(1, 0, 0, makeCubePiece(200, "textures/a.dds"), 350, cellSizeDefault, 1000));
        input.mObjects.push_back(
            makeObject(2, -1, 0, makeCubePiece(100, "textures/b.dds"), 175, cellSizeDefault, 3000));
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        EXPECT_NEAR(built.mCluster.mMinZ, 800.0f, 0.01f);
        EXPECT_NEAR(built.mCluster.mMaxZ, 3100.0f, 0.01f);
        EXPECT_EQ(built.mCluster.mSourceTriangles, 24u);
        EXPECT_FLOAT_EQ(built.mCluster.mOverhang, 0.0f);
    }

    TEST(LodClusterBuild, ObjectsOutsideTheTileSquareGiveAnOverhang)
    {
        ClusterBuildInput input = makeInput(tinyError);
        // A cube of half size 200 centred on the tile's western edge x = -8192 sticks out by 200.
        SourceObject object = makeObject(1, -1, 0, makeCubePiece(200, "textures/a.dds"), 350);
        object.mTransform = { 1, 0, 0, -8192, 0, 1, 0, 4096, 0, 0, 1, 1000 };
        input.mObjects.push_back(object);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        expectStructure(built.mCluster);
        EXPECT_NEAR(built.mCluster.mOverhang, 200.0f, 0.05f);

        // The overhang is the largest over x and y: a second cube hanging 500 over the northern edge y = 16384.
        SourceObject north = makeObject(2, 0, 1, makeCubePiece(500, "textures/a.dds"), 875);
        north.mTransform = { 1, 0, 0, 4096, 0, 1, 0, 16384, 0, 0, 1, 1000 };
        input.mObjects.push_back(north);
        const Built both = build(input);
        ASSERT_TRUE(both.mError.ok()) << both.mError.describe();
        expectStructure(both.mCluster);
        EXPECT_NEAR(both.mCluster.mOverhang, 500.0f, 0.05f);
    }

    TEST(LodClusterBuild, GeometricErrorCoversSimplificationAndDroppedObjects)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects.push_back(makeObject(1, 0, 0, makeIcospherePiece(600, 4, "textures/rock.dds"), 600));
        SourceObject dropped = makeCubeObject(2, -1, 0, "textures/a.dds");
        dropped.mTraits.mRadius = 40;
        input.mObjects.push_back(dropped);
        const Built built = build(input);
        ASSERT_TRUE(built.mError.ok()) << built.mError.describe();
        EXPECT_FLOAT_EQ(built.mStats.mMaxDroppedRadius, 40.0f);
        EXPECT_GE(built.mCluster.mGeometricError, built.mStats.mMaxSimplifyError);
        EXPECT_GE(built.mCluster.mGeometricError, 40.0f);
        EXPECT_FLOAT_EQ(built.mCluster.mGeometricError, std::max(built.mStats.mMaxSimplifyError, 40.0f));
    }

    // ---- determinism ----

    std::vector<SourceObject> makeMixedObjects()
    {
        std::vector<SourceObject> objects;
        objects.push_back(makeCubeObject(1, -1, 0, "textures/a.dds"));
        objects.push_back(makeCubeObject(2, 0, 0, "textures/a.dds"));
        objects.push_back(makeCubeObject(3, 0, 0, "textures/b.dds", clusterFlagTwoSided));
        objects.push_back(makeCubeObject(4, -1, 1, "textures/tree.dds", clusterFlagAlphaTest, 0.5f));
        objects.push_back(makeCubeObject(5, 0, 1, "textures/tree.dds", clusterFlagAlphaTest, 0.25f));
        objects.push_back(makeObject(6, -1, 0, makeIcospherePiece(700, 3, "textures/rock.dds"), 700));
        objects.push_back(makeObject(7, 0, 1, makeIcospherePiece(500, 2, "textures/rock.dds"), 500));
        objects.push_back(makeObject(8, -1, 1, makePlanePiece(1800, 8, "textures/floor.dds"), 1300));
        SourceObject mirrored = makeCubeObject(9, 0, 0, "textures/a.dds", 0, 0, 150);
        mirrored.mTransform = { -1, 0, 0, 4096, 0, 1, 0, 4096, 0, 0, 1, 900 };
        objects.push_back(mirrored);
        SourceObject activator = makeCubeObject(10, 0, 0, "textures/a.dds");
        activator.mTraits.mKind = ObjectKind::Activator;
        objects.push_back(activator);
        SourceObject tiny = makeCubeObject(11, 0, 1, "textures/a.dds");
        tiny.mTraits.mRadius = 20;
        objects.push_back(tiny);
        SourceObject second = makeCubeObject(12, -1, 0, "textures/a.dds", 0, 0, 120);
        second.mRef.mContentFile = 3;
        objects.push_back(second);
        return objects;
    }

    std::vector<SourceObject> shuffled(std::vector<SourceObject> objects, std::uint64_t seed)
    {
        Rng rng(seed);
        for (std::size_t i = objects.size(); i > 1; --i)
        {
            std::swap(objects[i - 1], objects[static_cast<std::size_t>(rng.below(i))]);
        }
        return objects;
    }

    TEST(LodClusterBuild, InputOrderDoesNotChangeTheResult)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects = makeMixedObjects();
        const Built reference = build(input);
        ASSERT_TRUE(reference.mError.ok()) << reference.mError.describe();
        expectStructure(reference.mCluster);
        const std::vector<std::uint8_t> referenceBytes = bytesOf(reference.mCluster);
        ASSERT_FALSE(referenceBytes.empty());

        std::vector<std::vector<SourceObject>> orders;
        orders.push_back(std::vector<SourceObject>(input.mObjects.rbegin(), input.mObjects.rend()));
        for (const std::uint64_t seed : { 11u, 22u, 33u, 44u })
        {
            orders.push_back(shuffled(input.mObjects, seed));
        }
        ASSERT_EQ(orders.size(), 5u);
        for (std::size_t i = 0; i < orders.size(); ++i)
        {
            ClusterBuildInput permuted = input;
            permuted.mObjects = orders[i];
            const Built built = build(permuted);
            ASSERT_TRUE(built.mError.ok()) << built.mError.describe() << " order " << i;
            EXPECT_EQ(built.mCluster, reference.mCluster) << "order " << i;
            EXPECT_EQ(bytesOf(built.mCluster), referenceBytes) << "order " << i;
            EXPECT_TRUE(sameStats(built.mStats, reference.mStats)) << "order " << i;
        }
    }

    TEST(LodClusterBuild, BuildingTwiceGivesTheSameBytes)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects = makeMixedObjects();
        const Built first = build(input);
        const Built second = build(input);
        ASSERT_TRUE(first.mError.ok()) << first.mError.describe();
        ASSERT_TRUE(second.mError.ok()) << second.mError.describe();
        const std::vector<std::uint8_t> bytes = bytesOf(first.mCluster);
        ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(bytesOf(second.mCluster), bytes);
    }

    TEST(LodClusterBuild, ThreadsDoNotChangeTheBytes)
    {
        ClusterBuildInput input = makeInput(64);
        input.mObjects = makeMixedObjects();
        input.mObjects.push_back(makeObject(20, 0, 0, makeIcospherePiece(800, 4, "textures/hill.dds"), 800));
        const Built single = build(input);
        ASSERT_TRUE(single.mError.ok()) << single.mError.describe();
        const std::vector<std::uint8_t> reference = bytesOf(single.mCluster);
        ASSERT_FALSE(reference.empty());

        for (int round = 0; round < 2; ++round)
        {
            std::vector<std::vector<std::uint8_t>> results(4);
            std::vector<LodError> errors(4);
            std::vector<std::thread> threads;
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                threads.emplace_back([&input, &results, &errors, i]() {
                    ObjectCluster local;
                    errors[i] = buildObjectCluster(input, local);
                    if (errors[i].ok())
                    {
                        const LodError written = writeObjectCluster(local, results[i]);
                        if (!written.ok())
                            errors[i] = written;
                    }
                });
            }
            for (std::thread& thread : threads)
            {
                thread.join();
            }
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                EXPECT_TRUE(errors[i].ok()) << errors[i].describe();
                EXPECT_EQ(results[i], reference) << "thread " << i << " round " << round;
            }
        }
    }
}
