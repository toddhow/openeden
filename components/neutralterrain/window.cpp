#include "window.hpp"

#include <algorithm>
#include <cmath>

namespace NeutralTerrain
{
    namespace
    {
        // Paging's largest batch (Terrain::Paging::mMaxBatchSize) and the margin of whole cells on every side: one
        // for the reuse distance, one for the rounding of a chunk's bounds.
        constexpr int kMaxBatch = 16;
        constexpr int kMargin = 2;
        constexpr double kMaxCoordinate = 1.0e9; // units; beyond it a cell index would not fit
    }

    CellWindow terrainEnsureWindow(double eyeX, double eyeY, float viewDistance, float cellSize)
    {
        if (!std::isfinite(eyeX) || !std::isfinite(eyeY) || !std::isfinite(viewDistance) || !std::isfinite(cellSize)
            || cellSize <= 0.f || viewDistance < 0.f || std::fabs(eyeX) > kMaxCoordinate
            || std::fabs(eyeY) > kMaxCoordinate)
            return {};
        const double viewCells = static_cast<double>(viewDistance) / cellSize;
        if (viewCells > 4096.0)
            return {};
        // Terrain::Paging::getBatchSize: nextPowerOfTwo(ceil(viewCells * 2 / 4)), at least 1, at most the largest.
        int batch = 1;
        const int wanted = static_cast<int>(std::ceil(viewCells * 2.0 / 4.0));
        while (batch < wanted && batch < kMaxBatch)
            batch *= 2;
        const std::int32_t radius = static_cast<std::int32_t>(std::ceil(viewCells)) + batch + kMargin;
        const std::int32_t cx = static_cast<std::int32_t>(std::floor(eyeX / cellSize));
        const std::int32_t cy = static_cast<std::int32_t>(std::floor(eyeY / cellSize));
        CellWindow window;
        window.mX0 = cx - radius;
        window.mY0 = cy - radius;
        window.mX1 = cx + radius + 1;
        window.mY1 = cy + radius + 1;
        return window;
    }

    std::pair<float, float> terrainHeightRange(const openeden::sdk::game::TerrainCell& cell, float defaultHeight)
    {
        const std::size_t expected = static_cast<std::size_t>(cell.vertsPerSide) * cell.vertsPerSide;
        if (!cell.hasHeights || expected == 0 || cell.heights.size() != expected)
            return { defaultHeight, defaultHeight };
        const auto [low, high] = std::minmax_element(cell.heights.begin(), cell.heights.end());
        return { *low, *high };
    }
}
