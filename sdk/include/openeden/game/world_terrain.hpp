#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// Exterior terrain as plain values (task 0120, stage E1 of docs/research/0053-skyrim-exteriors-neutral-model.md).
// A game pack describes a worldspace (WorldSpaceInfo), hands over one cell's land (TerrainCell) and lists the ground
// textures the cells name (GroundTexture) through the additive terrain functions at the end of WorldCellSource
// (world_source.hpp 1.1). The core builds its terrain from these; it never sees a game record. No third-party, engine,
// Jolt or Qt type.
//
// E1 carries only what drawing the terrain with each quadrant's BASE ground texture needs: heights, normals, colours
// and four base texture ids. The alpha layers (weights) are a later stage and arrive as additive members.
//
// Why a new header and not world_model.hpp: world_model.hpp is FROZEN (task 0102) and its pinned tests stay untouched.
// Why WorldSpaceInfo and not WorldInfo (the name the note uses): openeden::sdk::game::WorldInfo already exists in
// world_view.hpp (id, editor id, name).
//
// Units: the game's own length unit (Skyrim SE: 4096 per cell, 128 per vertex interval), Z up. Orientation (settled
// for Skyrim SE by task 0117 on the installed data): index = row * vertsPerSide + column, column = +X (east), row = +Y
// (north); quadrant 0 south-west, 1 south-east, 2 north-west, 3 north-east.
//
// Threading: a source builds on the main thread (WorldCellSource is main thread only). The values are plain data;
// the core copies them into an immutable snapshot before any worker thread reads them.

namespace openeden::sdk::game
{
    /// Same version policy as world_model.hpp: a new type or a new member at the END of a struct raises the minor
    /// number; anything else is a major change. CI (CI/check_extension_sdk.sh) keeps this line and the number in place.
    inline constexpr Version kWorldTerrainVersion{ 1, 1 }; // 1.1 (task 0149): TerrainLayer and TerrainCell::layers, appended

    /// The bit of WorldSpaceInfo::parentUsage that says the parent lends its land (Skyrim SE's PNAM bit 0x01 as UESP
    /// orders it; 0x01 and 0x02 are never apart in the installed data, so which of land and LOD it is stays unproven).
    namespace TerrainParentUsage
    {
        inline constexpr std::uint32_t Land = 0x01;
    }

    /// A named exterior space and what the terrain needs to know about it.
    struct WorldSpaceInfo
    {
        WorldId id = kNoWorldId;
        std::string editorId;
        /// The parent worldspace (none for a root) and the TerrainParentUsage bits that say what it lends.
        WorldId parent = kNoWorldId;
        std::uint32_t parentUsage = 0;
        /// One exterior cell's side in units, and its land's vertices per side (33 for Skyrim SE: 32 intervals).
        float cellSizeUnits = 4096.0f;
        std::uint32_t vertsPerSide = 33;
        /// The height a cell with no land has.
        float defaultLandHeight = 0.0f;
    };

    /// Vertices per side of one quadrant's layer opacity grid (a Skyrim SE quadrant is 16 intervals: 17 x 17 vertices).
    inline constexpr std::uint32_t kTerrainLayerSide = 17;

    /// One alpha layer of a quadrant (task 0149): a ground texture drawn over what lies under it with a per-vertex
    /// opacity. `opacity` has kTerrainLayerSide^2 values, index = row * 17 + column, column = +X (east), row = +Y
    /// (north) inside the quadrant, 0 = not drawn, 255 = fully drawn.
    struct TerrainLayer
    {
        WorldId texture = 0; ///< a GroundTexture id (0: the default; the source maps a null or dangling id to 0)
        std::vector<std::uint8_t> opacity;
    };

    /// One cell's land. With hasHeights false the world's default land height applies; likewise the normals (straight
    /// up) and the colours (white).
    struct TerrainCell
    {
        WorldId world = kNoWorldId;
        std::int32_t gridX = 0;
        std::int32_t gridY = 0;
        std::uint32_t vertsPerSide = 0;
        bool hasHeights = false;
        bool hasNormals = false;
        bool hasColours = false;
        std::vector<float> heights; ///< vertsPerSide^2, in units
        std::vector<std::int8_t> normals; ///< vertsPerSide^2 x (x, y, z), signed bytes as stored
        std::vector<std::uint8_t> colours; ///< vertsPerSide^2 x (r, g, b)
        /// A GroundTexture id per quadrant; 0 is the game's default ground texture (the source maps a stored null id
        /// and a dangling id to 0).
        std::array<WorldId, 4> baseTextures{};
        /// Task 0149 (version 1.1): per quadrant, the alpha layers in draw order (the first is drawn over the base
        /// texture, each next one over the result). Empty when the cell has none.
        std::array<std::vector<TerrainLayer>, 4> layers;
    };

    /// A ground texture a cell names. Paths are VFS paths (lower case, backslashes, with the leading `textures\`).
    struct GroundTexture
    {
        WorldId id = kNoWorldId; ///< 0 is the default ground texture
        std::string diffusePath;
    };

    /// What buildTerrain found.
    enum class TerrainResult : std::uint8_t
    {
        Built, ///< `out` holds the cell's land
        NoLand, ///< the world has no land at this cell (not an error: the default height applies)
        Failed, ///< a located message is in `error`; `out` is left empty
        NotSupported, ///< the source has no terrain at all (the default of a source that does not implement it)
    };
}
