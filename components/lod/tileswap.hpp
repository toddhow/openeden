#ifndef OPENMW_COMPONENTS_LOD_TILESWAP_H
#define OPENMW_COMPONENTS_LOD_TILESWAP_H

#include "tilekey.hpp"

#include <cstdint>
#include <string>

// OpenEden world LOD (task 0087, stage a). The words for one change of a terrain tile's drawn state, so that a pop seen
// in a video can be matched to a line in openmw.log (Verbose) or an event in load-trace.jsonl ([Cells] load trace).

namespace Lod
{
    enum class TileSwapKind
    {
        New, // the tile is drawn now, over the live terrain or a coarser tile
        Refined, // the tile is gone: its finer children are drawn instead
        Coarsened, // the tile is gone: a coarser tile covers its square
        ToLive, // the tile is gone: the live terrain draws its square
        Removed, // the tile is gone and nothing was drawn for it (freed by the residency planner)
    };

    /// The kind as it appears in the log: "new", "refined", "coarsened", "to live" or "removed".
    const char* tileSwapKindName(TileSwapKind kind) noexcept;

    /// The event name in the load trace: "lod.tile.new", "lod.tile.refined", "lod.tile.coarsened", "lod.tile.tolive"
    /// or "lod.tile.removed". The text has static storage (the trace keeps the pointer).
    const char* tileSwapTraceName(TileSwapKind kind) noexcept;

    /// One line, no trailing newline: "World LOD tile swap: frame 1234, level 3 tile (2097160, 2097147) at cell
    /// (-24, 40): refined". The numbers in the first brackets are the tile key's x and y as stored (biased, unsigned);
    /// the cell is the tile's south-west cell (tileOriginCell), signed.
    std::string formatTileSwap(std::uint64_t frame, const TileId& id, TileSwapKind kind);
}

#endif
