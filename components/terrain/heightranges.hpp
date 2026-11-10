#ifndef OPENMW_COMPONENTS_TERRAIN_HEIGHTRANGES_H
#define OPENMW_COMPONENTS_TERRAIN_HEIGHTRANGES_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Terrain
{
    /// The lowest and highest height of each cell of a `gridSize` x `gridSize` grid laid over a square height array of
    /// `width` x `width` vertices (row-major: x + y * width, `width - 1` intervals), as `{min, max}` indexed
    /// `x + y * gridSize`. A grid cell spans the vertices from floor(g * (width - 1) / gridSize) to
    /// ceil((g + 1) * (width - 1) / gridSize) on each axis, so a grid finer than the vertices (a coarse tile: 32
    /// intervals over 64 or 512 grid cells) still gets the heights of the vertices around it. Before 2026-11-10 the
    /// builder derived the span from `numVerts(chunkSize, lod)`, which truncates to 1 vertex for a coarse tile, and
    /// every grid cell read only the vertex at the south-west corner of the tile: the cull box of a mountain tile of
    /// level 3 and up was a flat box at one corner height.
    inline std::vector<std::pair<float, float>> computeHeightRanges(
        const float* heights, std::uint32_t width, std::uint32_t gridSize)
    {
        std::vector<std::pair<float, float>> ranges;
        if (heights == nullptr || width < 2 || gridSize == 0)
            return ranges;
        ranges.resize(static_cast<std::size_t>(gridSize) * gridSize);
        const std::uint64_t intervals = width - 1;
        auto span = [&](std::uint32_t g, std::uint32_t& first, std::uint32_t& last) {
            first = static_cast<std::uint32_t>(std::uint64_t{ g } * intervals / gridSize);
            last = static_cast<std::uint32_t>((std::uint64_t{ g + 1 } * intervals + gridSize - 1) / gridSize);
            last = std::min<std::uint32_t>(last, width - 1);
            first = std::min(first, last);
        };
        for (std::uint32_t gy = 0; gy < gridSize; ++gy)
        {
            std::uint32_t y0 = 0, y1 = 0;
            span(gy, y0, y1);
            for (std::uint32_t gx = 0; gx < gridSize; ++gx)
            {
                std::uint32_t x0 = 0, x1 = 0;
                span(gx, x0, x1);
                float low = std::numeric_limits<float>::max();
                float high = -std::numeric_limits<float>::max();
                for (std::uint32_t y = y0; y <= y1; ++y)
                {
                    for (std::uint32_t x = x0; x <= x1; ++x)
                    {
                        const float h = heights[x + std::size_t{ y } * width];
                        low = std::min(low, h);
                        high = std::max(high, h);
                    }
                }
                ranges[gx + std::size_t{ gy } * gridSize] = { low, high };
            }
        }
        return ranges;
    }
}

#endif
