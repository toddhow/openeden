#ifndef OPENEDEN_COMPONENTS_NEUTRALTERRAIN_WINDOW_H
#define OPENEDEN_COMPONENTS_NEUTRALTERRAIN_WINDOW_H

#include <cstdint>
#include <utility>

#include <openeden/game/world_terrain.hpp>

// Pure helpers of the neutral terrain in the engine (task 0125, stage E2 of docs/research/0053).

namespace NeutralTerrain
{
    /// An inclusive rectangle of cells.
    struct CellWindow
    {
        std::int32_t mX0 = 0;
        std::int32_t mY0 = 0;
        std::int32_t mX1 = -1;
        std::int32_t mY1 = -1;

        [[nodiscard]] bool empty() const { return mX1 < mX0 || mY1 < mY0; }
        [[nodiscard]] std::int64_t count() const
        {
            return empty() ? 0
                           : (static_cast<std::int64_t>(mX1) - mX0 + 1) * (static_cast<std::int64_t>(mY1) - mY0 + 1);
        }
        friend bool operator==(const CellWindow&, const CellWindow&) = default;
    };

    /// The cells the main thread must have loaded into the CellCache before Terrain::Paging builds chunks for a camera
    /// at (eyeX, eyeY) that sees `viewDistance` units. Paging works in whole cells, a chunk is a whole batch of up to
    /// 16 cells (Paging::mMaxBatchSize) and reads the cells to its east and north too. The window is the camera's cell
    /// +- (the view in cells, rounded up, + the batch size the view gives + 2), then one more cell east and north.
    /// Empty for a size or a position that is not usable.
    [[nodiscard]] CellWindow terrainEnsureWindow(double eyeX, double eyeY, float viewDistance, float cellSize);

    /// The lowest and highest height of a cell's land (the world's default height for a cell without usable heights).
    [[nodiscard]] std::pair<float, float> terrainHeightRange(
        const openeden::sdk::game::TerrainCell& cell, float defaultHeight);
}

#endif
