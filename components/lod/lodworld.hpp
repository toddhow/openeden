#ifndef OPENEDEN_COMPONENTS_LOD_LODWORLD_H
#define OPENEDEN_COMPONENTS_LOD_LODWORLD_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "objectcluster.hpp"
#include "tilepack.hpp"
#include "viewlimits.hpp"

// Which world a LOD pack belongs to (task 0133, milestone M-S2 of docs/design/skyrim-se-implementation-plan.md).
//
// Morrowind's packs live where they always did: <user data>/openeden/lod/terrain.edenpak and objects.edenpak (an empty
// game id selects them, and nothing about them changes). A game pack's neutral world gets a folder of its own, keyed by
// game and world: <user data>/openeden/lod/<game>/<world>/terrain.edenpak. So a Skyrim bake can never overwrite, or be
// mistaken for, a Morrowind pack, and two worlds of one game do not share a file.
//
// Pure: no file is touched, no engine type is used.

namespace Lod
{
    /// A name safe as one path component: ASCII letters and digits, '-' and '_' kept, anything else (separators, dots,
    /// spaces, non-ASCII) becomes '_'; lower case; at most 64 characters; never empty ("_").
    [[nodiscard]] std::string safePathComponent(std::string_view text);

    /// `lodRoot` is <user data>/openeden/lod. An empty `gameId` is Morrowind's legacy place.
    [[nodiscard]] std::filesystem::path terrainPackPath(
        const std::filesystem::path& lodRoot, std::string_view gameId, std::string_view worldName);
    [[nodiscard]] std::filesystem::path objectPackPath(
        const std::filesystem::path& lodRoot, std::string_view gameId, std::string_view worldName);

    /// What a neutral world's pack is checked against at run time instead of re-reading all the land (which a worker
    /// thread cannot do: the neutral cells are loaded by the main thread, a window at a time).
    struct NeutralWorldFacts
    {
        std::string mGameId;
        std::string mWorldEditorId;
        float mCellSizeUnits = 0;
        std::uint32_t mVertsPerSide = 0;
        float mDefaultLandHeight = 0;
        /// WorldCellSource::cellCount(): changes when a plugin adds or removes cells.
        std::uint64_t mCellCount = 0;
    };

    /// 64-bit FNV-1a over the facts, as 16 lower case hex digits. Equal facts give equal text on any platform.
    [[nodiscard]] std::string neutralFingerprint(const NeutralWorldFacts& facts);

    /// Is a neutral pack's manifest the one a bake of this world would write now? Empty when it is; otherwise the first
    /// difference, in words, for the log line that says the pack is not used. Compared: layout and baker version, the
    /// grid size, the albedo size, and the single worldspace (its name, which carries the fingerprint, and its cell
    /// size). The pack's list of levels is not compared (a pack may hold fewer than the plan).
    [[nodiscard]] std::string describeNeutralPackMismatch(const TileManifest& pack, const std::string& worldspaceName,
        std::uint32_t cellSize, std::uint32_t gridSize, std::uint32_t albedoSize);

    /// Texels a side of the baked ground picture of a tile that has one, for a world whose cell is `cellSizeUnits`
    /// wide: Morrowind's 256 at its 8192 units a cell, scaled so that a texel covers the same ground (128 units),
    /// rounded down to a power of two and kept between 32 and 256. 8192 gives 256 (so Morrowind's plan is unchanged),
    /// Skyrim SE's 4096 gives 128. Not a valid size (0 or above 100,000,000 units) gives 256.
    [[nodiscard]] std::uint32_t albedoSizeForCell(float cellSizeUnits);

    /// The box of land cells the tiles of a pack cover (every tile's square of 2^level cells; the pack's tiles reach
    /// out past the land by up to a tile, so the box is generous). Empty when no tile is valid. Used for the view
    /// limits of a neutral world (`view distance = auto` with `map view`), which has no ESM land to measure.
    [[nodiscard]] LandExtent extentOfTiles(const std::vector<TileId>& tiles);

    /// Task 0133 stage 2: a neutral placement has a 64-bit id; a cluster lists the references it stands in for in two
    /// 32-bit words (the layout is frozen at version 1), so the id travels in them as its low word (mIndex) and its
    /// high word (mContentFile). The two functions are inverses for every id.
    [[nodiscard]] ClusterRef clusterRefFromPlacement(std::uint64_t placementId) noexcept;
    [[nodiscard]] std::uint64_t placementFromClusterRef(const ClusterRef& ref) noexcept;

    /// Task 0133 stage 3: the levels an object cluster bake writes: 1 to 4 for every world. (The stage 3 draft dropped
    /// level 1 for neutral worlds; it left the 10,000 to 25,000 unit band bare and saved no held memory, so toddhow
    /// chose a larger first error instead: objectClusterLevelError.)
    [[nodiscard]] std::vector<std::uint32_t> objectClusterLevels(bool neutralWorld);

    /// The simplification error (world units) of a cluster level in a bake. Morrowind: Lod::clusterLevelError (128,
    /// 320, 960, 2900). A neutral (Skyrim) world's level 1 is 192 (toddhow, 2026-11-12: level 1 at 128 kept 38M
    /// triangles; the objects between 192 and 320 stay in the band the live grid does not cover). A manifest value.
    [[nodiscard]] float objectClusterLevelError(std::uint32_t level, bool neutralWorld) noexcept;

    /// Is the terrain tile pack of this kind of world written stored (not compressed)? Morrowind's is, so that its
    /// bytes depend only on the land (byte-stable digests); a neutral world's is compressed with zstd (task 0133 stage
    /// 3).
    [[nodiscard]] bool terrainPackStored(bool neutralWorld);

    /// The worldspace name written into a neutral pack's manifest: "<game>:<world editor id>:<fingerprint>".
    [[nodiscard]] std::string neutralWorldspaceName(const NeutralWorldFacts& facts);
}

#endif
