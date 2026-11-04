#include "seamanalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace BodyNif
{
    namespace
    {
        using PositionKey = std::array<std::uint32_t, 3>;

        // A key that is equal exactly when the coordinates are equal as numbers: -0 is +0, every NaN is one NaN, so
        // that the keys have a total order whatever a hostile file holds.
        PositionKey positionKey(const Vec3& position)
        {
            PositionKey key{};
            for (std::size_t i = 0; i < 3; ++i)
            {
                const float value = position[i];
                if (value == 0.f)
                {
                    key[i] = 0;
                }
                else if (std::isnan(value))
                {
                    key[i] = 0x7fc00000u;
                }
                else
                {
                    std::memcpy(&key[i], &value, sizeof(float));
                }
            }
            return key;
        }

        // For each vertex the lowest index of a vertex at the same position.
        std::vector<std::size_t> weldMap(const PartMesh& part)
        {
            const std::size_t count = part.mPositions.size();
            std::vector<std::size_t> order(count);
            for (std::size_t i = 0; i < count; ++i)
                order[i] = i;
            std::vector<PositionKey> keys(count);
            for (std::size_t i = 0; i < count; ++i)
                keys[i] = positionKey(part.mPositions[i]);
            std::stable_sort(order.begin(), order.end(),
                [&keys](std::size_t left, std::size_t right) { return keys[left] < keys[right]; });
            std::vector<std::size_t> representative(count);
            for (std::size_t i = 0; i < count;)
            {
                std::size_t end = i + 1;
                while (end < count && keys[order[end]] == keys[order[i]])
                    ++end;
                const std::size_t lowest = order[i]; // stable: the lowest index of the run
                for (std::size_t k = i; k < end; ++k)
                    representative[order[k]] = lowest;
                i = end;
            }
            return representative;
        }

        // How many triangles use each edge of the welded mesh; an edge is a pair of weld representatives (low, high).
        std::map<std::pair<std::size_t, std::size_t>, int> countEdgeUses(
            const PartMesh& part, const std::vector<std::size_t>& weld)
        {
            std::map<std::pair<std::size_t, std::size_t>, int> edgeUses;
            const std::size_t count = part.mPositions.size();
            for (std::size_t t = 0; t + 2 < part.mTriangles.size(); t += 3)
            {
                const std::uint32_t a = part.mTriangles[t], b = part.mTriangles[t + 1], c = part.mTriangles[t + 2];
                if (a >= count || b >= count || c >= count)
                    continue;
                const std::size_t ra = weld[a], rb = weld[b], rc = weld[c];
                if (ra == rb || rb == rc || ra == rc)
                    continue; // degenerate once welded
                for (const auto& [from, to] : { std::pair{ ra, rb }, std::pair{ rb, rc }, std::pair{ rc, ra } })
                    ++edgeUses[{ std::min(from, to), std::max(from, to) }];
            }
            return edgeUses;
        }

        double length(const std::array<double, 3>& v)
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        std::array<double, 3> difference(const Vec3& to, const Vec3& from)
        {
            return { static_cast<double>(to[0]) - from[0], static_cast<double>(to[1]) - from[1],
                static_cast<double>(to[2]) - from[2] };
        }

        std::array<double, 3> centre(const PartMesh& part)
        {
            std::array<double, 3> sum{ 0, 0, 0 };
            if (part.mPositions.empty())
                return sum;
            for (const Vec3& p : part.mPositions)
            {
                for (std::size_t i = 0; i < 3; ++i)
                    sum[i] += p[i];
            }
            for (double& value : sum)
                value /= static_cast<double>(part.mPositions.size());
            return sum;
        }

        bool hasSkin(const PartMesh& part)
        {
            return part.mSkinned && part.mSkin.size() == part.mPositions.size();
        }

        // The vertex's kept influences as (bone name, weight renormalised to a sum of 1), sorted by name.
        std::vector<std::pair<std::string, double>> namedWeights(const PartMesh& part, std::size_t vertex)
        {
            std::vector<std::pair<std::string, double>> result;
            const VertexSkin& skin = part.mSkin[vertex];
            double sum = 0;
            for (std::size_t i = 0; i < skin.mCount && i < maxInfluences; ++i)
            {
                if (skin.mInfluences[i].mBone < part.mBones.size() && skin.mInfluences[i].mWeight > 0.f)
                    sum += skin.mInfluences[i].mWeight;
            }
            if (sum <= 0)
                return result;
            for (std::size_t i = 0; i < skin.mCount && i < maxInfluences; ++i)
            {
                const Influence& influence = skin.mInfluences[i];
                if (influence.mBone < part.mBones.size() && influence.mWeight > 0.f)
                    result.emplace_back(part.mBones[influence.mBone], influence.mWeight / sum);
            }
            std::sort(result.begin(), result.end());
            return result;
        }

        struct Pair
        {
            std::size_t mA = 0;
            std::size_t mB = 0;
            double mDistance = 0;
        };

        double squaredDistance(const Vec3& left, const Vec3& right)
        {
            double sum = 0;
            for (std::size_t i = 0; i < 3; ++i)
            {
                const double d = static_cast<double>(left[i]) - right[i];
                sum += d * d;
            }
            return sum;
        }

        // For each vertex of `from` (indices into `fromPart`) its nearest vertex of `to` (the lowest index on a tie)
        // when it is within the window.
        void nearestPairs(const PartMesh& fromPart, const std::vector<std::size_t>& from, const PartMesh& toPart,
            const std::vector<std::size_t>& to, double window, bool fromIsA, std::vector<Pair>& out)
        {
            const double windowSquared = window * window;
            for (const std::size_t f : from)
            {
                bool found = false;
                std::size_t best = 0;
                double bestSquared = 0;
                for (const std::size_t t : to)
                {
                    const double d = squaredDistance(fromPart.mPositions[f], toPart.mPositions[t]);
                    if (!found || d < bestSquared)
                    {
                        found = true;
                        best = t;
                        bestSquared = d;
                    }
                }
                if (found && bestSquared <= windowSquared)
                {
                    Pair pair;
                    pair.mA = fromIsA ? f : best;
                    pair.mB = fromIsA ? best : f;
                    pair.mDistance = std::sqrt(bestSquared);
                    out.push_back(pair);
                }
            }
        }
    }

    PartMesh makePartMesh(const std::string& name, const std::vector<Shape>& shapes)
    {
        PartMesh part;
        part.mName = name;
        part.mShapeCount = shapes.size();
        bool anyNormals = false;
        bool anyUvs = false;
        for (const Shape& shape : shapes)
        {
            part.mSkinned = part.mSkinned || shape.mSkinned;
            anyNormals = anyNormals || !shape.mNormals.empty();
            anyUvs = anyUvs || !shape.mUvs.empty();
        }
        for (const Shape& shape : shapes)
        {
            const auto offset = static_cast<std::uint32_t>(part.mPositions.size());
            const std::size_t count = shape.mPositions.size();
            part.mPositions.insert(part.mPositions.end(), shape.mPositions.begin(), shape.mPositions.end());
            if (anyNormals)
            {
                if (shape.mNormals.size() == count)
                    part.mNormals.insert(part.mNormals.end(), shape.mNormals.begin(), shape.mNormals.end());
                else
                    part.mNormals.insert(part.mNormals.end(), count, Vec3{ 0.f, 0.f, 0.f });
            }
            if (anyUvs)
            {
                if (shape.mUvs.size() == count)
                    part.mUvs.insert(part.mUvs.end(), shape.mUvs.begin(), shape.mUvs.end());
                else
                    part.mUvs.insert(part.mUvs.end(), count, Vec2{ 0.f, 0.f });
            }
            for (const std::uint32_t index : shape.mTriangles)
                part.mTriangles.push_back(index + offset);
            if (!part.mSkinned)
                continue;
            // Merge this shape's bone table into the part's, in order of first use.
            std::vector<std::uint16_t> remap;
            for (const std::string& bone : shape.mBones)
            {
                const auto found = std::find(part.mBones.begin(), part.mBones.end(), bone);
                if (found == part.mBones.end())
                {
                    remap.push_back(static_cast<std::uint16_t>(part.mBones.size()));
                    part.mBones.push_back(bone);
                }
                else
                {
                    remap.push_back(static_cast<std::uint16_t>(found - part.mBones.begin()));
                }
            }
            for (std::size_t v = 0; v < count; ++v)
            {
                VertexSkin skin;
                if (shape.mSkinned && v < shape.mSkin.size())
                {
                    skin = shape.mSkin[v];
                    for (std::size_t i = 0; i < skin.mCount && i < maxInfluences; ++i)
                    {
                        if (skin.mInfluences[i].mBone < remap.size())
                            skin.mInfluences[i].mBone = remap[skin.mInfluences[i].mBone];
                    }
                }
                part.mSkin.push_back(skin);
            }
        }
        return part;
    }

    Distribution distribution(std::vector<double> values)
    {
        values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }),
            values.end());
        Distribution result;
        result.mCount = values.size();
        if (values.empty())
            return result;
        std::sort(values.begin(), values.end());
        const std::size_t n = values.size();
        result.mMin = values.front();
        result.mMax = values.back();
        result.mMedian = values[(n + 1) / 2 - 1]; // rank ceil(n / 2)
        result.mP95 = values[(95 * n + 99) / 100 - 1]; // rank ceil(0.95 n)
        return result;
    }

    std::vector<std::size_t> boundaryVertices(const PartMesh& part)
    {
        const std::vector<std::size_t> weld = weldMap(part);
        const std::map<std::pair<std::size_t, std::size_t>, int> edgeUses = countEdgeUses(part, weld);
        const std::size_t count = part.mPositions.size();
        std::vector<bool> onBoundary(count, false);
        for (const auto& [edge, uses] : edgeUses)
        {
            if (uses == 1)
            {
                onBoundary[edge.first] = true;
                onBoundary[edge.second] = true;
            }
        }
        std::vector<std::size_t> result;
        for (std::size_t v = 0; v < count; ++v)
        {
            if (onBoundary[weld[v]])
                result.push_back(v);
        }
        return result;
    }

    PartStats analysePart(const PartMesh& part)
    {
        PartStats stats;
        stats.mVertices = part.mPositions.size();
        stats.mTriangles = part.mTriangles.size() / 3;
        stats.mBones = part.mBones.size();
        stats.mShapes = part.mShapeCount;
        stats.mSkinned = hasSkin(part);
        stats.mBoundaryVertices = boundaryVertices(part).size();
        for (const auto& [edge, uses] : countEdgeUses(part, weldMap(part)))
        {
            if (uses == 1)
                ++stats.mOpenEdges;
            else if (uses == 2)
                ++stats.mSharedEdges;
            else
                ++stats.mOverUsedEdges;
        }
        for (std::size_t i = 0; i < part.mPositions.size(); ++i)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                const double value = part.mPositions[i][k];
                stats.mBoundsMin[k] = i == 0 ? value : std::min(stats.mBoundsMin[k], value);
                stats.mBoundsMax[k] = i == 0 ? value : std::max(stats.mBoundsMax[k], value);
            }
        }

        if (stats.mSkinned)
        {
            std::vector<double> sums;
            sums.reserve(part.mSkin.size());
            for (const VertexSkin& skin : part.mSkin)
            {
                ++stats.mInfluenceHistogram[std::min<std::size_t>(skin.mCount, maxInfluences)];
                if (skin.mBeforeCut > maxInfluences)
                    ++stats.mOverCut;
                double sum = 0;
                for (std::size_t i = 0; i < skin.mCount && i < maxInfluences; ++i)
                    sum += skin.mInfluences[i].mWeight;
                sums.push_back(sum);
                if (std::abs(sum - 1.0) > 1e-3)
                    ++stats.mWeightSumOff;
            }
            stats.mWeightSum = distribution(std::move(sums));
        }

        std::map<PositionKey, std::size_t> first;
        for (std::size_t v = 0; v < part.mPositions.size(); ++v)
        {
            const auto [it, inserted] = first.emplace(positionKey(part.mPositions[v]), v);
            if (inserted)
                continue;
            ++stats.mDuplicatePositions;
            const std::size_t earlier = it->second;
            bool differs = false;
            if (part.mUvs.size() == part.mPositions.size())
            {
                for (std::size_t k = 0; k < 2; ++k)
                    differs = differs || std::abs(part.mUvs[v][k] - part.mUvs[earlier][k]) > 1e-6f;
            }
            if (part.mNormals.size() == part.mPositions.size())
            {
                for (std::size_t k = 0; k < 3; ++k)
                    differs = differs || std::abs(part.mNormals[v][k] - part.mNormals[earlier][k]) > 1e-6f;
            }
            if (differs)
                ++stats.mDuplicateSplits;
        }
        return stats;
    }

    SeamReport analyseSeam(const PartMesh& a, const PartMesh& b, const SeamOptions& options)
    {
        SeamReport report;
        const std::vector<std::size_t> boundaryA = boundaryVertices(a);
        const std::vector<std::size_t> boundaryB = boundaryVertices(b);
        report.mBoundaryA = boundaryA.size();
        report.mBoundaryB = boundaryB.size();

        std::vector<Pair> pairs;
        nearestPairs(a, boundaryA, b, boundaryB, options.mWindow, true, pairs);
        report.mCandidatesA = pairs.size();
        nearestPairs(b, boundaryB, a, boundaryA, options.mWindow, false, pairs);
        report.mCandidatesB = pairs.size() - report.mCandidatesA;
        // Over all vertices (a closed part has no boundary vertices).
        {
            std::vector<std::size_t> allA(a.mPositions.size()), allB(b.mPositions.size());
            for (std::size_t i = 0; i < allA.size(); ++i)
                allA[i] = i;
            for (std::size_t i = 0; i < allB.size(); ++i)
                allB[i] = i;
            std::vector<Pair> anyPairs;
            nearestPairs(a, allA, b, allB, options.mWindow, true, anyPairs);
            report.mAnyCandidatesA = anyPairs.size();
            nearestPairs(b, allB, a, allA, options.mWindow, false, anyPairs);
            report.mAnyCandidatesB = anyPairs.size() - report.mAnyCandidatesA;
            std::vector<double> anyDistances;
            for (const Pair& pair : anyPairs)
            {
                anyDistances.push_back(pair.mDistance);
                for (std::size_t i = 0; i < report.mAnyWithin.size(); ++i)
                {
                    if (pair.mDistance <= options.mLadder[i])
                        ++report.mAnyWithin[i];
                }
            }
            report.mAnyDistance = distribution(std::move(anyDistances));
        }
        report.mTouches = !pairs.empty();
        if (pairs.empty())
            return report;

        // The seam normal: from the centre of A to the centre of B.
        const std::array<double, 3> centreA = centre(a), centreB = centre(b);
        std::array<double, 3> normal = { centreB[0] - centreA[0], centreB[1] - centreA[1], centreB[2] - centreA[2] };
        const double normalLength = length(normal);
        if (normalLength > 0)
        {
            for (double& value : normal)
                value /= normalLength;
        }

        const bool skinned = hasSkin(a) && hasSkin(b);
        const bool uvs = a.mUvs.size() == a.mPositions.size() && b.mUvs.size() == b.mPositions.size();
        const bool normals = a.mNormals.size() == a.mPositions.size() && b.mNormals.size() == b.mPositions.size();

        std::vector<double> distances, gaps, disagreements, angles;
        for (const Pair& pair : pairs)
        {
            distances.push_back(pair.mDistance);
            for (std::size_t i = 0; i < report.mWithin.size(); ++i)
            {
                if (pair.mDistance <= options.mLadder[i])
                    ++report.mWithin[i];
            }
            const std::array<double, 3> delta = difference(b.mPositions[pair.mB], a.mPositions[pair.mA]);
            const double gap = delta[0] * normal[0] + delta[1] * normal[1] + delta[2] * normal[2];
            gaps.push_back(gap);
            if (gap < -options.mCoincident)
                ++report.mOverlapPairs;
            else if (gap > options.mCoincident)
                ++report.mGapPairs;
            else
                ++report.mMeetingPairs;

            if (pair.mDistance > options.mPairDistance)
                continue;
            if (skinned)
            {
                const auto weightsA = namedWeights(a, pair.mA);
                const auto weightsB = namedWeights(b, pair.mB);
                double largest = 0;
                bool sameBones = weightsA.size() == weightsB.size();
                std::size_t i = 0, j = 0;
                while (i < weightsA.size() || j < weightsB.size())
                {
                    double wa = 0, wb = 0;
                    if (j >= weightsB.size() || (i < weightsA.size() && weightsA[i].first < weightsB[j].first))
                    {
                        wa = weightsA[i].second;
                        sameBones = false;
                        ++i;
                    }
                    else if (i >= weightsA.size() || weightsB[j].first < weightsA[i].first)
                    {
                        wb = weightsB[j].second;
                        sameBones = false;
                        ++j;
                    }
                    else
                    {
                        wa = weightsA[i].second;
                        wb = weightsB[j].second;
                        ++i;
                        ++j;
                    }
                    largest = std::max(largest, std::abs(wa - wb));
                }
                disagreements.push_back(largest);
                if (!sameBones)
                    ++report.mBoneSetDiffers;
            }
            if (uvs)
            {
                const Vec2& ua = a.mUvs[pair.mA];
                const Vec2& ub = b.mUvs[pair.mB];
                if (std::abs(ua[0] - ub[0]) > options.mUvEqual || std::abs(ua[1] - ub[1]) > options.mUvEqual)
                    ++report.mUvDiffers;
            }
            if (normals)
            {
                const Vec3& na = a.mNormals[pair.mA];
                const Vec3& nb = b.mNormals[pair.mB];
                const double la = std::sqrt(static_cast<double>(na[0]) * na[0] + static_cast<double>(na[1]) * na[1]
                    + static_cast<double>(na[2]) * na[2]);
                const double lb = std::sqrt(static_cast<double>(nb[0]) * nb[0] + static_cast<double>(nb[1]) * nb[1]
                    + static_cast<double>(nb[2]) * nb[2]);
                if (la > 1e-12 && lb > 1e-12)
                {
                    const double dot = (static_cast<double>(na[0]) * nb[0] + static_cast<double>(na[1]) * nb[1]
                                           + static_cast<double>(na[2]) * nb[2])
                        / (la * lb);
                    constexpr double radiansToDegrees = 57.29577951308232;
                    angles.push_back(std::acos(std::clamp(dot, -1.0, 1.0)) * radiansToDegrees);
                }
            }
        }
        report.mDistance = distribution(std::move(distances));
        report.mSignedGap = distribution(std::move(gaps));
        report.mWeightDisagreement = distribution(std::move(disagreements));
        report.mNormalAngle = distribution(std::move(angles));
        return report;
    }
}
