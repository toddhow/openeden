#ifndef OPENMW_COMPONENTS_LOD_TERRAINFADE_H
#define OPENMW_COMPONENTS_LOD_TERRAINFADE_H

#include "clusterfade.hpp"
#include "tileselection.hpp"

#include <cstdint>
#include <span>

// OpenEden world LOD (task 0087, stage c). How the baked terrain tiles hand over to each other and to the live terrain
// with a dithered fade, as a pure state machine (no GPU, no clock): MWRender::LodTerrain runs it every frame and the
// tests run it on its own. It reuses Lod::stepTileFade (the object clusters' machine) for one tile.
//
// The rule that keeps the ground whole (no hole, no doubled coverage), the same as the clusters':
//  - A tile that appears fades IN over whatever drew its square (the live terrain, a coarser tile, or its finer
//    tiles); that stays drawn, and counts as covering, until the tile is solid.
//  - A tile that is replaced by finer tiles or by a coarser one stays SOLID until the replacement is solid, and is then
//    dropped in one frame (the replacement hides it completely: nothing changes on screen).
//  - A tile that is given to the live terrain fades OUT over it (the live terrain draws its square from the first
//    frame of the fade).
//  - A fade can turn around in the middle (the tile is wanted again): it goes on from the value it has.
// "Covers" is what the live terrain needs to know: a tile covers its square (the live chunks under it are left out)
// while it is Solid or waiting to go. A tile that is fading in or out does not: the live terrain draws beneath it.

namespace Lod
{
    /// What the caller worked out about a tile whose selection is Live (it is refined to finer tiles, or the live
    /// terrain draws its square), from the drawn state of the tiles around it.
    enum class RefinedGate : std::uint8_t
    {
        Stay, // something still depends on it (a finer tile is not solid yet, or failed): keep it solid
        ToLive, // the live terrain draws its square: fade out over it
        Covered, // solid tiles cover its square: drop it at once
    };

    struct TerrainFadeInput
    {
        bool mBuilt = false; // the tile has a draw
        TileChoice mChoice = TileChoice::Unset;
        std::int32_t mParent = -1; // nearest ancestor in the pack (as nearestAncestors), -1 for none
        RefinedGate mGate = RefinedGate::Stay; // used when mChoice is Live
    };

    struct TerrainFadeState
    {
        TileFade mFade; // phase and the dither share (0 to 1)
        bool mCovers = false; // the live terrain leaves out this tile's square
    };

    enum class TerrainFadeEvent : std::uint8_t
    {
        None,
        StartedLeaving, // the tile began to fade out (it stays drawn until Finished)
        Remove, // dropped at once: what replaces it is solid. The draw can go now.
        Finished, // the fade out is complete. The draw can go now.
    };

    /// True when the tile is drawn fully (Solid, or only waiting to go): what a replacement waits for.
    bool terrainTileIsSolid(const TerrainFadeState& state);

    /// Moves every tile one frame. `states`, `inputs` and `events` are parallel and the same length; the tiles are
    /// in the order of nearestAncestors (a parent before its children). A tile that is not built is reset. After a
    /// Remove or Finished event the state is reset (Hidden) and the caller must drop the draw. `fadeSeconds` must be
    /// above 0 (the caller does not use the machine without a fade). Rules, per built tile:
    ///  - Tile choice: wanted (fades in from Hidden, rises to Solid, a fade out turns around).
    ///  - Live choice: mGate Stay keeps it wanted; ToLive lets it fade out (Solid goes straight to FadingOut in the
    ///  same
    ///    frame); Covered drops it at once (Remove).
    ///  - Covered choice: when the nearest ancestor that is a Tile choice is solid, Remove; until then it is wanted
    ///    (it stays, or goes on fading in).
    ///  - Unset: treated as Live with the ToLive gate (it was chosen once and is not now).
    void stepTerrainFades(std::span<TerrainFadeState> states, std::span<const TerrainFadeInput> inputs,
        std::span<TerrainFadeEvent> events, float seconds, float fadeSeconds);
}

#endif
