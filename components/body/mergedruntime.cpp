#include "mergedruntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Body
{
    MergedBodyReason decideMergedBody(const MergedBodyFacts& facts)
    {
        if (!facts.mSettingOn)
            return MergedBodyReason::SettingOff;
        if (!facts.mThirdPerson)
            return MergedBodyReason::NotThirdPerson;
        if (facts.mWerewolf)
            return MergedBodyReason::Werewolf;
        if (!facts.mCacheLoaded)
            return MergedBodyReason::NoCache;
        if (!facts.mEntryPresent)
            return MergedBodyReason::NoEntry;
        if (!facts.mKeyComputed)
            return MergedBodyReason::KeyNotComputed;
        if (!facts.mKeyCurrent)
            return MergedBodyReason::StaleKey;
        if (!facts.mPayloadRead)
            return MergedBodyReason::PayloadRefused;
        if (!facts.mBuilt)
            return MergedBodyReason::BuildFailed;
        if (facts.mSlotsCovered)
            return MergedBodyReason::EquipmentCovers;
        return MergedBodyReason::Merged;
    }

    std::string_view describe(MergedBodyReason reason)
    {
        switch (reason)
        {
            case MergedBodyReason::Merged:
                return "the merged body is drawn";
            case MergedBodyReason::SettingOff:
                return "[Bodies] merged body is off";
            case MergedBodyReason::NotThirdPerson:
                return "first person and head-only views keep their parts";
            case MergedBodyReason::Werewolf:
                return "werewolves keep their parts";
            case MergedBodyReason::NoCache:
                return "there is no merged body cache: run openmw --bake-bodies";
            case MergedBodyReason::NoEntry:
                return "the cache has no body for this race and sex: run openmw --bake-bodies";
            case MergedBodyReason::KeyNotComputed:
                return "the parts of this race and sex cannot be read now";
            case MergedBodyReason::StaleKey:
                return "the cache is stale (the data changed since the bake): re-run openmw --bake-bodies";
            case MergedBodyReason::PayloadRefused:
                return "the cached body does not read: re-run openmw --bake-bodies";
            case MergedBodyReason::BuildFailed:
                return "the merged body could not be built";
            case MergedBodyReason::EquipmentCovers:
                return "equipment covers a merged slot";
        }
        return "unknown reason";
    }

    bool mergedSlotsCovered(std::span<const int> mergedSlots, std::span<const int> priorities)
    {
        for (const int slot : mergedSlots)
        {
            if (slot < 0 || static_cast<std::size_t>(slot) >= priorities.size())
                return true;
            if (priorities[static_cast<std::size_t>(slot)] >= 1)
                return true;
        }
        return false;
    }

    std::vector<int> mergedSlotsOf(const MergedBody& body)
    {
        std::vector<int> slots;
        slots.reserve(body.mRegions.size());
        for (const RegionRange& region : body.mRegions)
            slots.push_back(region.mSlot);
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
        return slots;
    }

    std::vector<MergedDraw> planMergedDraws(const MergedBody& body)
    {
        const std::size_t triangleCount = body.mTriangles.size() / 3;
        const std::size_t vertexCount = body.mPositions.size();
        constexpr std::uint32_t unused = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> local(vertexCount, unused);
        std::vector<MergedDraw> draws;
        draws.reserve(body.mSubmeshes.size());
        for (const Submesh& submesh : body.mSubmeshes)
        {
            const std::uint64_t end
                = static_cast<std::uint64_t>(submesh.mFirstTriangle) + static_cast<std::uint64_t>(submesh.mTriangleCount);
            if (end > triangleCount || submesh.mMaterial >= body.mMaterials.size())
                return {};
            MergedDraw draw;
            draw.mMaterial = submesh.mMaterial;
            draw.mSlot = submesh.mSlot;
            draw.mIndices.reserve(static_cast<std::size_t>(submesh.mTriangleCount) * 3);
            for (std::uint64_t t = submesh.mFirstTriangle; t < end; ++t)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const std::uint32_t vertex = body.mTriangles[static_cast<std::size_t>(t) * 3 + k];
                    if (vertex >= vertexCount)
                        return {};
                    if (local[vertex] == unused)
                    {
                        local[vertex] = static_cast<std::uint32_t>(draw.mVertices.size());
                        draw.mVertices.push_back(vertex);
                    }
                    draw.mIndices.push_back(local[vertex]);
                }
            }
            for (const std::uint32_t vertex : draw.mVertices)
                local[vertex] = unused;
            draws.push_back(std::move(draw));
        }
        return draws;
    }

    bool invertAffine(const Affine34& in, Affine34& out)
    {
        for (const double value : in)
        {
            if (!std::isfinite(value))
                return false;
        }
        const double a = in[0], b = in[1], c = in[2];
        const double d = in[4], e = in[5], f = in[6];
        const double g = in[8], h = in[9], i = in[10];
        const double c00 = e * i - f * h;
        const double c01 = c * h - b * i;
        const double c02 = b * f - c * e;
        const double c10 = f * g - d * i;
        const double c11 = a * i - c * g;
        const double c12 = c * d - a * f;
        const double c20 = d * h - e * g;
        const double c21 = b * g - a * h;
        const double c22 = a * e - b * d;
        const double determinant = a * c00 + b * c10 + c * c20;
        // A rest matrix is a rotation times a positive scale; a determinant this small means a degenerate bone.
        constexpr double minDeterminant = 1e-12;
        if (!std::isfinite(determinant) || std::abs(determinant) < minDeterminant)
            return false;
        const double s = 1.0 / determinant;
        Affine34 result{ c00 * s, c01 * s, c02 * s, 0, c10 * s, c11 * s, c12 * s, 0, c20 * s, c21 * s, c22 * s, 0 };
        const double tx = in[3], ty = in[7], tz = in[11];
        result[3] = -(result[0] * tx + result[1] * ty + result[2] * tz);
        result[7] = -(result[4] * tx + result[5] * ty + result[6] * tz);
        result[11] = -(result[8] * tx + result[9] * ty + result[10] * tz);
        for (const double value : result)
        {
            if (!std::isfinite(value))
                return false;
        }
        out = result;
        return true;
    }
}
