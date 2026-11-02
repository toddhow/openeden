#ifndef OPENMW_COMPONENTS_LOD_CLUSTERPLAN_H
#define OPENMW_COMPONENTS_LOD_CLUSTERPLAN_H

// OpenEden world LOD (task 0059, stage d). The decisions that tie the object clusters of a frame to each other and to
// the live objects, as pure functions (no GPU, no scene graph, no clock): MWRender::LodClusters and DistantObjects call
// them, and the tests call them on their own.
//
// A cell is "covered" when some cluster tile that is Solid (Lod::FadePhase::Solid) lists it as a free (not masked) cell;
// the live objects then leave out the statics the cluster draws for that cell.

#include <functional>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "clusterfade.hpp"
#include "objectcluster.hpp"

namespace Lod
{
    using Cell = std::pair<int, int>;

    /// One drawn (built) tile for the hand-over. A tile whose draws failed to compile has no cells: it covers nothing.
    struct CoverTile
    {
        TileFade mFade;
        bool mWanted = false; // the selection wants it, and it is built
        std::vector<Cell> mCells; // the cells it draws the statics of (not masked), empty when it has no draws
    };

    using LiveReadyFn = std::function<bool(const Cell&)>;

    /// Steps every tile one frame (Lod::stepTileFade). For a tile that is WaitingForLive, FadingOut or Reclaiming the
    /// live objects are "back" when every one of its cells either is covered by ANOTHER tile that is Solid (a finer or
    /// coarser cluster of the same ground takes over: the clusters hand over to each other without the live objects) or
    /// is ready according to `liveReady`. `liveReady` is called only for such tiles, only for cells no other Solid tile
    /// covers, and may be called more than once for a cell. Order of tiles does not matter: the covering is taken from
    /// the states before the step. Returns true when the set of Solid tiles changed.
    bool stepCoverage(
        std::span<CoverTile> tiles, float seconds, float fadeSeconds, const LiveReadyFn& liveReady);

    /// The cells covered by Solid tiles.
    std::set<Cell> solidCells(std::span<const CoverTile> tiles);

    /// Whether the live objects draw a cell's statics (or have nothing to draw there):
    ///  - false while a cluster covers it (`clustered`) or its chunk was dropped and is not built again (`rebuilding`);
    ///  - else, when a chunk is shown, true if it was built with every static (`shownSkipsStatics` false);
    ///  - else (no chunk) true only when none is expected for the cell (`chunkExpected` false: outside the view, or
    ///    inside the active grid, where the scene loads the objects).
    bool liveStaticsReady(bool clustered, bool rebuilding, std::optional<bool> shownSkipsStatics, bool chunkExpected);

    /// Whether the cluster draws this reference: `sortedRefs` is the sorted list of the references the cluster lists for
    /// the cell. The live objects skip exactly these statics, and keep every other (the cluster left them out: small,
    /// animated, particles, blended).
    bool clusterDrawsReference(std::span<const ClusterRef> sortedRefs, const ClusterRef& ref);
}

#endif
