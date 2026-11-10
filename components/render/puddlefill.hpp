#ifndef OPENEDEN_RENDER_PUDDLEFILL_H
#define OPENEDEN_RENDER_PUDDLEFILL_H

// OpenEden (task 0095, stage W-a): where water collects on a height grid. Pure maths, no engine, VSG or Jolt types, no
// file or game data; the baker (stage W-b) feeds it and the shader twin (stage W-d) reads what it makes. Design:
// docs/research/0049-puddles-and-wetness.md section 4.
//
// Pour water on a height grid and ask where it stays. `fillDepressions` raises every pit to its spill height (the
// lowest point on its rim) with a Priority-Flood: start from the places where water leaves the model (the grid's edge
// and the sink texels), flood inward lowest first, and give each new texel the larger of its own height and the height
// of the texel it was reached from (Barnes, Lehman and Mulla 2014, "Priority-Flood", Computers & Geosciences; the
// algorithm is recalled, the proof of the minimax property in the comments below is ours). The capacity of a texel is
// the filled height minus its own height, clamped to a cap. The pits are the 8-connected components of capacity above
// zero (`labelBasins`); water drains along the flood tree (`accumulateFlow`), which gives each pit its catchment
// (`catchmentRatio`).
//
// Facts the rest of the code relies on (each has a test):
//  - The filled height F of a texel is the smallest, over all 8-connected paths to a sink, of the highest ground on the
//    path. It does not depend on the order equal texels are visited, so rotating or mirroring the grid rotates or
//    mirrors the result.
//  - F is the same on every texel of one pit (two neighbouring texels that both hold water have the same F: otherwise
//    the lower one could drain into the higher), so a pit has one spill height.
//  - Following `parent` from any texel reaches a sink with F never rising, so the filled surface has no pit left.
//  - Taking a smaller piece of the grid only adds sinks, so F can only fall; it does not change where the piece's own
//    water cannot reach the new edge (the region independence test).
//
// Limits and simplifications, on purpose:
//  - Nested and joined pits that fill to one level are one pit (the component of capacity above zero). The exact answer
//    is a depression hierarchy (Barnes, Callaghan and Wickert 2020; later).
//  - The catchment of a pit counts every texel that drains into it, also the water a pit upstream would first have to
//    fill; and where two passes of exactly equal height lead into one pit the inflow can be counted twice (the ratio is
//    clamped when it is stored).
//  - The grid's border is always a sink.
//
// Hostile input: sizes and heights are checked and a located error is returned (never thrown); nothing here reads
// outside its arrays. The caps below bound memory (about 30 bytes a texel while filling).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Render::Puddle
{
    // The biggest grid the maths accepts: a side of 4096 texels and 4 Mi texels in all. The baker works on a cell and
    // a halo (3 x 3 cells of 128 x 128 texels at 64 units); this leaves room for 7 x 7 cells at the same spacing.
    constexpr int maxGridSide = 4096;
    constexpr std::size_t maxGridTexels = std::size_t{ 4 } << 20;

    // The largest height the maths accepts, in either direction (Morrowind's land spans about -2,200 to 19,000 units).
    // It keeps every filled height minus a height finite, so no capacity can overflow to infinity (review F6).
    constexpr float maxAbsHeight = 1.0e7f;

    // The default cap on a capacity, in game units (about 17 cm): a deeper hollow is a pond, which the water shader's
    // water level covers or which stays partly filled (research 0049 section 4.2, risk R3).
    constexpr float defaultCapacityCap = 12.f;

    // The value `flowDirection` gives to a texel that drains out of the model (a sink or the grid's edge).
    constexpr std::uint8_t flowNone = 8;

    // The 8 neighbours, in the fixed order every loop here uses (x then y offset). A direction code is an index into
    // this table: 0 is west, 1 north-west, ..., in the order below. The order is part of the output (`flowDirection`),
    // so it is pinned by a test.
    constexpr std::array<std::array<int, 2>, 8> neighbours = { {
        { -1, 0 },
        { -1, -1 },
        { 0, -1 },
        { 1, -1 },
        { 1, 0 },
        { 1, 1 },
        { 0, 1 },
        { -1, 1 },
    } };

    // The result of filling a grid. `error` is empty on success; on failure every other member is empty.
    struct FillResult
    {
        std::string error;
        int width = 0;
        int height = 0;
        // The cap `fillDepressions` was called with (infinity for none). `capacityFromFloor` needs an infinite one.
        float capacityCap = 0.f;
        // The filled height of each texel (row major, index = y * width + x).
        std::vector<float> filled;
        // The filled height minus the ground height, clamped to the cap; never negative; zero on sinks.
        std::vector<float> capacity;
        // The texel each texel's water drains to (the texel it was reached from), or -1 for a sink or an edge texel.
        std::vector<std::int32_t> parent;
        // Every texel once, in the order the flood closed them; a texel always comes after its parent.
        std::vector<std::uint32_t> order;

        bool ok() const { return error.empty(); }
    };

    namespace Detail
    {
        inline FillResult failure(std::string message)
        {
            FillResult result;
            result.error = std::move(message);
            return result;
        }

        // One flood entry. Equal heights come out in the order they were pushed (`sequence`), which is fixed by the
        // scan order of the sinks and the neighbour table, so the flood is deterministic and a flat area drains by the
        // shortest way out (a first-in first-out queue among equals is the "epsilon" of the epsilon variant).
        struct FloodEntry
        {
            float key;
            std::uint32_t sequence;
            std::uint32_t index;
        };

        struct FloodLater
        {
            bool operator()(const FloodEntry& a, const FloodEntry& b) const
            {
                if (a.key != b.key)
                    return a.key > b.key;
                return a.sequence > b.sequence;
            }
        };
    }

    // Fills the depressions of `heights` (row major, `width` x `height` texels, world Z). `sinks` is empty or has one
    // byte per texel, non-zero where water leaves the model (the sea, a water body, land that is not loaded); the
    // grid's border is a sink as well. `capacityCap` must be above zero (infinity means no cap, which the tests use).
    inline FillResult fillDepressions(std::span<const float> heights, std::span<const std::uint8_t> sinks, int width,
        int height, float capacityCap = defaultCapacityCap)
    {
        if (width < 1 || height < 1)
            return Detail::failure(
                "fillDepressions: the grid is empty (" + std::to_string(width) + " x " + std::to_string(height) + ")");
        if (width > maxGridSide || height > maxGridSide)
            return Detail::failure("fillDepressions: the grid is " + std::to_string(width) + " x "
                + std::to_string(height) + ", more than " + std::to_string(maxGridSide) + " on a side");
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (count > maxGridTexels)
            return Detail::failure("fillDepressions: the grid has " + std::to_string(count) + " texels, more than "
                + std::to_string(maxGridTexels));
        if (heights.size() != count)
            return Detail::failure("fillDepressions: " + std::to_string(heights.size()) + " heights for a grid of "
                + std::to_string(count) + " texels");
        if (!sinks.empty() && sinks.size() != count)
            return Detail::failure("fillDepressions: " + std::to_string(sinks.size()) + " sink flags for a grid of "
                + std::to_string(count) + " texels");
        if (!(capacityCap > 0.f))
            return Detail::failure("fillDepressions: the capacity cap must be above zero");
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!std::isfinite(heights[i]) || std::abs(heights[i]) > maxAbsHeight)
                return Detail::failure("fillDepressions: height at texel (" + std::to_string(i % width) + ", "
                    + std::to_string(i / width) + ") is not finite or beyond +-" + std::to_string(maxAbsHeight));
        }

        FillResult result;
        result.width = width;
        result.height = height;
        result.capacityCap = capacityCap;
        result.filled.assign(count, 0.f);
        result.capacity.assign(count, 0.f);
        result.parent.assign(count, -1);
        result.order.reserve(count);

        std::vector<std::uint8_t> closed(count, 0);
        std::vector<Detail::FloodEntry> storage;
        storage.reserve(std::min<std::size_t>(count, std::size_t{ 1 } << 16));
        std::priority_queue<Detail::FloodEntry, std::vector<Detail::FloodEntry>, Detail::FloodLater> queue(
            Detail::FloodLater{}, std::move(storage));
        std::uint32_t sequence = 0;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                const bool border = x == 0 || y == 0 || x == width - 1 || y == height - 1;
                if (!border && (sinks.empty() || sinks[i] == 0))
                    continue;
                closed[i] = 1;
                result.filled[i] = heights[i];
                queue.push({ heights[i], sequence++, static_cast<std::uint32_t>(i) });
            }
        }

        while (!queue.empty())
        {
            const Detail::FloodEntry current = queue.top();
            queue.pop();
            result.order.push_back(current.index);
            const int cx = static_cast<int>(current.index % static_cast<std::uint32_t>(width));
            const int cy = static_cast<int>(current.index / static_cast<std::uint32_t>(width));
            for (const auto& offset : neighbours)
            {
                const int nx = cx + offset[0];
                const int ny = cy + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                const std::size_t n = static_cast<std::size_t>(ny) * width + nx;
                if (closed[n] != 0)
                    continue;
                closed[n] = 1;
                const float level = std::max(heights[n], result.filled[current.index]);
                result.filled[n] = level;
                result.parent[n] = static_cast<std::int32_t>(current.index);
                queue.push({ level, sequence++, static_cast<std::uint32_t>(n) });
            }
        }

        for (std::size_t i = 0; i < count; ++i)
            result.capacity[i] = std::min(result.filled[i] - heights[i], capacityCap);
        return result;
    }

    // The direction a texel's water leaves in: an index into `neighbours`, or `flowNone` for a sink or edge texel.
    inline std::uint8_t flowDirection(const FillResult& fill, std::size_t index)
    {
        if (index >= fill.parent.size() || fill.width < 1)
            return flowNone;
        const std::int32_t parent = fill.parent[index];
        if (parent < 0)
            return flowNone;
        const int dx = static_cast<int>(parent % fill.width) - static_cast<int>(index % fill.width);
        const int dy = static_cast<int>(parent / fill.width) - static_cast<int>(index / fill.width);
        for (std::size_t d = 0; d < neighbours.size(); ++d)
        {
            if (neighbours[d][0] == dx && neighbours[d][1] == dy)
                return static_cast<std::uint8_t>(d);
        }
        return flowNone;
    }

    // For each texel, the number of texels (itself included) whose water passes through it on the way out: the area
    // draining into it. D8 on the flood tree: every texel sends all it has to the neighbour it was reached from, which
    // is its lowest-filled neighbour.
    inline std::vector<std::uint32_t> accumulateFlow(const FillResult& fill)
    {
        std::vector<std::uint32_t> area(fill.filled.size(), 1);
        for (std::size_t i = fill.order.size(); i-- > 0;)
        {
            const std::uint32_t texel = fill.order[i];
            const std::int32_t parent = fill.parent[texel];
            if (parent >= 0)
                area[static_cast<std::size_t>(parent)] += area[texel];
        }
        return area;
    }

    // One pit: a connected component (8 neighbours, like the flood) of texels with capacity above zero.
    struct Basin
    {
        // The number of texels.
        std::uint32_t area = 0;
        // The highest capacity of any texel of the pit (after the cap).
        float maxCapacity = 0.f;
        // The index of the first texel (lowest index) that has that capacity.
        std::uint32_t deepest = 0;
        // The height the pit fills to before it spills (the filled height of its texels).
        float spillHeight = 0.f;
        // The lowest-index texel outside the pit, next to it, whose ground is at the spill height and whose water
        // drains away from the pit (its flood parent is not in the pit): where the water leaves. -1 when there is none.
        std::int32_t outlet = -1;
    };

    struct Basins
    {
        // The pit of each texel, or -1. Pits are numbered in the order of their lowest-index texel.
        std::vector<std::int32_t> id;
        std::vector<Basin> basins;
        // A digest of the fill these basins were made from (size, cap and capacities); `capacityFromFloor` and
        // `catchmentRatio` refuse basins that do not match the fill they are given.
        std::uint64_t source = 0;
    };

    namespace Detail
    {
        inline std::uint64_t sourceDigest(const FillResult& fill)
        {
            std::uint64_t hash = 1469598103934665603ull;
            auto mix = [&hash](const void* data, std::size_t bytes) {
                const auto* p = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i < bytes; ++i)
                {
                    hash ^= p[i];
                    hash *= 1099511628211ull;
                }
            };
            mix(&fill.width, sizeof(fill.width));
            mix(&fill.height, sizeof(fill.height));
            mix(&fill.capacityCap, sizeof(fill.capacityCap));
            if (!fill.capacity.empty())
                mix(fill.capacity.data(), fill.capacity.size() * sizeof(float));
            return hash;
        }
    }

    // Labels the pits. `heights` is the grid `fill` was made from. Two wet texels that touch, also at a corner, are in
    // the same pit: the flood is 8-connected and neighbouring wet texels have the same filled height, so they are one
    // body of water (a diagonal ditch is one pit, not one per texel).
    inline Basins labelBasins(const FillResult& fill, std::span<const float> heights)
    {
        Basins out;
        if (!fill.ok() || heights.size() != fill.filled.size())
            return out;
        out.source = Detail::sourceDigest(fill);
        const int width = fill.width;
        const int height = fill.height;
        const std::size_t count = fill.filled.size();
        out.id.assign(count, -1);
        std::vector<std::uint32_t> stack;
        for (std::size_t start = 0; start < count; ++start)
        {
            if (!(fill.capacity[start] > 0.f) || out.id[start] >= 0)
                continue;
            const std::int32_t label = static_cast<std::int32_t>(out.basins.size());
            Basin basin;
            basin.spillHeight = fill.filled[start];
            basin.deepest = static_cast<std::uint32_t>(start);
            basin.maxCapacity = fill.capacity[start];
            out.id[start] = label;
            stack.push_back(static_cast<std::uint32_t>(start));
            while (!stack.empty())
            {
                const std::uint32_t current = stack.back();
                stack.pop_back();
                ++basin.area;
                const float capacity = fill.capacity[current];
                if (capacity > basin.maxCapacity || (capacity == basin.maxCapacity && current < basin.deepest))
                {
                    basin.maxCapacity = capacity;
                    basin.deepest = current;
                }
                const int cx = static_cast<int>(current % static_cast<std::uint32_t>(width));
                const int cy = static_cast<int>(current / static_cast<std::uint32_t>(width));
                for (const auto& offset : neighbours)
                {
                    const int nx = cx + offset[0];
                    const int ny = cy + offset[1];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const std::size_t n = static_cast<std::size_t>(ny) * width + nx;
                    if (fill.capacity[n] > 0.f && out.id[n] < 0)
                    {
                        out.id[n] = label;
                        stack.push_back(static_cast<std::uint32_t>(n));
                    }
                }
            }
            out.basins.push_back(basin);
        }

        // The outlet of each pit: scan the dry texels in index order, so the first one found is the lowest index.
        // It must drain away: a bump at the spill height that the pit's water reaches (its flood parent is in the
        // pit) is a dead end, not a pass.
        for (std::size_t t = 0; t < count; ++t)
        {
            if (out.id[t] >= 0)
                continue;
            const int tx = static_cast<int>(t % static_cast<std::size_t>(width));
            const int ty = static_cast<int>(t / static_cast<std::size_t>(width));
            const std::int32_t parent = fill.parent[t];
            const std::int32_t parentBasin = parent < 0 ? -1 : out.id[static_cast<std::size_t>(parent)];
            for (const auto& offset : neighbours)
            {
                const int nx = tx + offset[0];
                const int ny = ty + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                const std::int32_t label = out.id[static_cast<std::size_t>(ny) * width + nx];
                if (label < 0)
                    continue;
                Basin& basin = out.basins[static_cast<std::size_t>(label)];
                if (basin.outlet < 0 && heights[t] == basin.spillHeight && parentBasin != label)
                    basin.outlet = static_cast<std::int32_t>(t);
            }
        }
        return out;
    }

    // The capacity measured up from the pit's floor, for pits deeper than `cap`. `fillDepressions` clamps each texel's
    // capacity on its own, so a pit 40 units deep becomes a flat plate with 12 units everywhere (real hollows are like
    // that: see the stage log). A deep pit is a pond, and a pond only fills from the bottom: this keeps the lowest
    // `cap` units of it. Each texel's capacity becomes max(0, C - (Cmax - cap)), at most `cap`, and the pit's maximum
    // becomes min(Cmax, cap); in the run-time formula that gives exactly the depth the uncapped pit has while its level
    // is at most `cap`. Pits that are not deeper than `cap` are unchanged.
    //
    // `uncapped` must come from `fillDepressions` with an infinite cap (the capped one has lost the depth) and `basins`
    // from `labelBasins` of that same fill; anything else is refused with an error, not guessed at. `cap` must be a
    // finite number above zero.
    struct FloorCapacity
    {
        std::string error;
        std::vector<float> capacity;

        bool ok() const { return error.empty(); }
    };

    inline FloorCapacity capacityFromFloor(const FillResult& uncapped, const Basins& basins, float cap)
    {
        FloorCapacity out;
        if (!uncapped.ok())
        {
            out.error = "capacityFromFloor: the fill failed: " + uncapped.error;
            return out;
        }
        if (!(cap > 0.f) || !std::isfinite(cap))
        {
            out.error = "capacityFromFloor: the cap must be a finite number above zero";
            return out;
        }
        if (uncapped.capacityCap != std::numeric_limits<float>::infinity())
        {
            out.error = "capacityFromFloor: the fill was made with a finite cap ("
                + std::to_string(uncapped.capacityCap) + "), which has lost the depth; fill with an infinite cap";
            return out;
        }
        if (basins.id.size() != uncapped.capacity.size() || basins.source != Detail::sourceDigest(uncapped))
        {
            out.error = "capacityFromFloor: the basins were not made from this fill";
            return out;
        }
        out.capacity.assign(uncapped.capacity.size(), 0.f);
        for (std::size_t i = 0; i < out.capacity.size(); ++i)
        {
            const std::int32_t label = basins.id[i];
            if (label < 0)
                continue;
            const float depth = basins.basins[static_cast<std::size_t>(label)].maxCapacity;
            out.capacity[i] = std::min(std::max(uncapped.capacity[i] - std::max(depth - cap, 0.f), 0.f), cap);
        }
        return out;
    }

    // The catchment ratio kappa of each pit: (its own area plus the area that drains into it from outside it) over its
    // own area. 1 means only the rain that falls on the pit; 4 means three more pit-areas of ground drain into it.
    // `area` is `accumulateFlow(fill)`. Refused (with an error) when the basins or the area do not match the fill.
    struct Catchment
    {
        std::string error;
        std::vector<float> ratio;

        bool ok() const { return error.empty(); }
    };

    namespace Detail
    {
        inline Catchment catchmentOf(const Basins& basins, const FillResult& fill, std::span<const std::uint32_t> area,
            std::span<const float> pond)
        {
            Catchment out;
            if (!fill.ok())
            {
                out.error = "catchmentRatio: the fill failed: " + fill.error;
                return out;
            }
            if (area.size() != fill.filled.size() || basins.id.size() != fill.filled.size()
                || basins.source != sourceDigest(fill))
            {
                out.error = "catchmentRatio: the basins or the flow areas were not made from this fill";
                return out;
            }
            if (!pond.empty() && pond.size() != fill.filled.size())
            {
                out.error = "catchmentRatio: the pond capacities do not match the fill";
                return out;
            }
            std::vector<double> inflow(basins.basins.size(), 0.0);
            std::vector<double> footprint(basins.basins.size(), 0.0);
            for (std::size_t t = 0; t < fill.parent.size(); ++t)
            {
                if (!pond.empty() && basins.id[t] >= 0 && pond[t] > 0.f)
                    footprint[static_cast<std::size_t>(basins.id[t])] += 1.0;
                const std::int32_t parent = fill.parent[t];
                if (parent < 0)
                    continue;
                const std::int32_t into = basins.id[static_cast<std::size_t>(parent)];
                if (into >= 0 && basins.id[t] != into)
                    inflow[static_cast<std::size_t>(into)] += area[t];
            }
            const double total = static_cast<double>(fill.filled.size());
            out.ratio.assign(basins.basins.size(), 1.f);
            for (std::size_t b = 0; b < out.ratio.size(); ++b)
            {
                const double own = basins.basins[b].area;
                const double water = pond.empty() ? own : footprint[b];
                if (!(water > 0.0))
                    continue;
                out.ratio[b] = static_cast<float>(std::min(own + inflow[b], total) / water);
            }
            return out;
        }
    }

    inline Catchment catchmentRatio(const Basins& basins, const FillResult& fill, std::span<const std::uint32_t> area)
    {
        return Detail::catchmentOf(basins, fill, area, {});
    }

    // The same for a pond measured from the floor (`capacityFromFloor`): all the area that drains into the pit
    // (its own texels included, the slopes above the pond too) still brings water, but it collects in the pond, so the
    // divisor is the pond's footprint (the texels of the pit with a pond capacity above zero), not the whole pit's area
    // (review F4). For a pit not deeper than the cap the two are the same.
    inline Catchment catchmentRatio(const Basins& basins, const FillResult& fill, std::span<const std::uint32_t> area,
        std::span<const float> pondCapacity)
    {
        if (pondCapacity.empty())
        {
            Catchment out;
            out.error = "catchmentRatio: no pond capacities given";
            return out;
        }
        return Detail::catchmentOf(basins, fill, area, pondCapacity);
    }
}

#endif
