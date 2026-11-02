#include "clusterplan.hpp"

#include <algorithm>
#include <vector>

namespace Lod
{
    namespace
    {
        bool needsLiveObjects(FadePhase phase)
        {
            return phase == FadePhase::WaitingForLive || phase == FadePhase::FadingOut
                || phase == FadePhase::Reclaiming;
        }
    }

    bool stepCoverage(std::span<CoverTile> tiles, float seconds, float fadeSeconds, const LiveReadyFn& liveReady)
    {
        // The cells that a Solid tile covers, taken before anything moves, and only when some tile needs them.
        std::vector<Cell> solid; // sorted: the cells a Solid tile covers
        const bool needed = std::any_of(
            tiles.begin(), tiles.end(), [](const CoverTile& tile) { return needsLiveObjects(tile.mFade.mPhase); });
        if (needed)
            for (const CoverTile& tile : tiles)
                if (tile.mFade.mPhase == FadePhase::Solid)
                    solid.insert(solid.end(), tile.mCells.begin(), tile.mCells.end());
        std::sort(solid.begin(), solid.end());

        bool changed = false;
        for (CoverTile& tile : tiles)
        {
            const bool wasSolid = tile.mFade.mPhase == FadePhase::Solid;
            bool liveBack = true;
            if (needsLiveObjects(tile.mFade.mPhase))
            {
                for (const Cell& cell : tile.mCells)
                {
                    // Another Solid tile covers the cell: the clusters hand over to each other (this tile is not Solid
                    // in these phases, so any count is another tile's).
                    if (std::binary_search(solid.begin(), solid.end(), cell))
                        continue;
                    if (!liveReady(cell))
                    {
                        liveBack = false;
                        break;
                    }
                }
            }
            stepTileFade(tile.mFade, tile.mWanted, liveBack, seconds, fadeSeconds);
            changed = changed || (tile.mFade.mPhase == FadePhase::Solid) != wasSolid;
        }
        return changed;
    }

    std::set<Cell> solidCells(std::span<const CoverTile> tiles)
    {
        std::set<Cell> cells;
        for (const CoverTile& tile : tiles)
            if (tile.mFade.mPhase == FadePhase::Solid)
                cells.insert(tile.mCells.begin(), tile.mCells.end());
        return cells;
    }

    bool liveStaticsReady(bool clustered, bool rebuilding, std::optional<bool> shownSkipsStatics, bool chunkExpected)
    {
        if (clustered || rebuilding)
            return false;
        if (shownSkipsStatics.has_value())
            return !*shownSkipsStatics;
        return !chunkExpected;
    }

    bool clusterDrawsReference(std::span<const ClusterRef> sortedRefs, const ClusterRef& ref)
    {
        return std::binary_search(sortedRefs.begin(), sortedRefs.end(), ref);
    }
}
