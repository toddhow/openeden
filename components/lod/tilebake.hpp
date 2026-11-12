#ifndef OPENMW_COMPONENTS_LOD_TILEBAKE_H
#define OPENMW_COMPONENTS_LOD_TILEBAKE_H

// OpenEden world LOD (task 0055, stage S1c). Baking one terrain tile from full-detail land data. Pure code: the land
// comes through the LandSource interface, which the game implements over the same terrain storage the live terrain
// uses (apps/openmw/lodterrainbaker.*) and the tests implement with generated data. Layout version 1.
//
// What a bake does for a tile of 2^level x 2^level cells with N x N vertices:
//  - heights: the full-detail grid sampled every 2^lod vertices, where 64 * cells / 2^lod + 1 == N (so 33 for a
//    2 x 2 cell tile samples every 4th of Morrowind's 65 vertices per cell), quantised to 16 bits between the tile's
//    minimum and maximum height;
//  - normals: taken from the full-detail data at the sampled vertices (not recomputed from the coarse grid), encoded
//    as two signed bytes (octahedral);
//  - geometric error: the largest difference, in world units, between the full-detail grid (every vertex) and the
//    coarse grid read back from its quantised values with bilinear interpolation. The bilinear surface is an
//    approximation of the triangles the renderer will draw (the draw code does not exist yet);
//  - no albedo in this stage (the spec's open question 2): the tile has no albedo section.
//
// Deterministic: the result depends only on the land data and the settings. Every container is walked in a fixed
// order and no clock or random number is used.

#include <cstdint>
#include <vector>

#include <edenarchive/hash.hpp>

#include "albedobake.hpp"
#include "loderror.hpp"
#include "tilepack.hpp"

namespace Lod
{
    /// Version of the baker's behaviour (goes into the manifest and the provenance parameters). Version 1 (the approved
    /// layout). Version 2: the source digest leaves out the normals at cell corners, so that a pack baked on one
    /// platform passes the staleness check on another (task 0060, review of the far eviction, finding 6); older packs
    /// are refused as stale.
    inline constexpr std::uint32_t bakerVersion = 2;
    /// Vertices per side of one cell of land in Morrowind (ESM::Land::LAND_SIZE); a cell has 64 intervals.
    inline constexpr std::uint32_t landVerticesPerCell = 65;

    /// Where the land comes from. Const member functions must be callable from one thread at a time; the baker
    /// calls them in a fixed order.
    class LandSource
    {
    public:
        virtual ~LandSource() = default;

        /// The square of `sizeInCells` x `sizeInCells` cells whose south-west cell is (cellX, cellY), sampled every
        /// 2^lod vertices of the 65-vertex-per-cell grid, with the shared edge vertices counted once: n = 64 *
        /// sizeInCells / 2^lod + 1 vertices per side. `heights` gets n * n values and `normals` 3 * n * n values (x,
        /// y, z of a unit vector), both with x fastest and the south row first. Cells without land give the engine's
        /// default height and an up-pointing normal.
        virtual void sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
            std::vector<float>& heights, std::vector<float>& normals) const = 0;
    };

    struct TileBakeSettings
    {
        std::uint32_t mLevel = 1; // 2^level cells a side
        std::uint32_t mGridSize = 33; // N; (64 << level) / (N - 1) must be a power of two
        std::uint32_t mCellSize = 8192; // world units, from the world source (never assumed)
        /// Vertices a side of one cell of land (task 0130): 65 for Morrowind, 33 for Skyrim SE. The cell's intervals
        /// (this minus 1) must be a power of two. The default is Morrowind's, and with the default nothing about a
        /// bake (digests, parameters, manifest) differs from before this field existed.
        std::uint32_t mVerticesPerCell = landVerticesPerCell;
        std::uint8_t mWorldspace = 0;
        /// Texels a side of the baked albedo picture (a power of two, 0 for none). Only used when an AlbedoSource is
        /// given to bakeTerrainTile; then the tile has an RGBA8 albedo of this size with one mip level.
        std::uint32_t mAlbedoSize = 0;
        /// With an albedo size, compute the tile's digest (which includes the albedo inputs) but do not blend: the
        /// tile has no albedo bytes. Used to check a pack against the game's data cheaply.
        bool mDigestOnly = false;
    };

    /// Octahedral encoding of a unit vector into two signed bytes, and back (the decoded vector is unit length).
    void encodeNormal(float x, float y, float z, std::int8_t& u, std::int8_t& v) noexcept;
    void decodeNormal(std::int8_t u, std::int8_t v, float& x, float& y, float& z) noexcept;

    /// The lod (log2 of the vertex step) a settings object implies, or false when it is not a power of two.
    bool vertexStepLod(const TileBakeSettings& settings, std::uint32_t& lod) noexcept;

    /// Bakes one tile. `id` must be valid and at settings.mLevel. Fails with a located error on bad settings. The
    /// tile's mSourceDigest is the BLAKE3 digest of the id, the settings and the full-detail samples it came from
    /// (so it changes exactly when its inputs change), and mParameters holds the settings as little-endian words.
    LodError bakeTerrainTile(
        const LandSource& source, const TileId& id, const TileBakeSettings& settings, BakedTile& tile);

    /// bakeTerrainTile with the ground colour: when `albedo` is not null and settings.mAlbedoSize is not 0 the tile
    /// also gets the baked albedo (RGBA8, one mip level, flagHasAlbedo) and its source digest includes the
    /// albedoInputsDigest of its square, so a changed texture, blend or vertex colour makes the digest differ. With a
    /// null `albedo` or a size of 0 this is exactly the four argument bakeTerrainTile (a size set with no source is not
    /// an error: no albedo is baked). A size that is not a power of two from 4 to 2048 is an error.
    LodError bakeTerrainTile(const LandSource& source, const AlbedoSource* albedo, const TileId& id,
        const TileBakeSettings& settings, BakedTile& tile);

    /// Which levels to bake and from which level up the tiles carry an albedo.
    struct TerrainBakePlan
    {
        std::vector<std::uint32_t> mLevels = { 1, 2, 3, 4, 5, 6 }; // 5 and 6 cover Morrowind in 2 x 2 tiles
        std::uint32_t mAlbedoFromLevel = 2; // levels below it use the live blend maps and have no albedo
        std::uint32_t mAlbedoSize = 256;
    };

    /// The manifest of a bake with this plan (what bakeTerrainLevels returns, and what a pack made from it holds).
    TileManifest makeTerrainManifest(
        const TileBakeSettings& base, const TerrainBakePlan& plan, bool withAlbedo, const std::string& worldspaceName);

    /// Bakes every level of the plan for the tiles covering `landCells` and builds the manifest (levels = the plan's,
    /// gridSize = base.mGridSize, albedoSize = plan.mAlbedoSize when `albedo` is not null, else 0). `tiles` comes
    /// back in key order. base.mLevel and base.mAlbedoSize are ignored (the plan's win); base.mDigestOnly is honoured.
    /// Fails if any tile fails or a level is not valid for base.mGridSize.
    LodError bakeTerrainLevels(const LandSource& source, const AlbedoSource* albedo,
        const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const TileBakeSettings& base,
        const TerrainBakePlan& plan, const std::string& worldspaceName, TileManifest& manifest,
        std::vector<BakedTile>& tiles);

    /// The tiles at settings.mLevel that contain at least one of the given land cells, in key order, each once.
    /// `landCells` may be in any order and contain duplicates.
    std::vector<TileId> tilesCoveringCells(
        const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const TileBakeSettings& settings);

    /// Bakes the tiles covering `landCells` (all of them, in key order) and builds the manifest. Fails if any tile
    /// fails.
    LodError bakeTerrain(const LandSource& source, const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells,
        const TileBakeSettings& settings, const std::string& worldspaceName, TileManifest& manifest,
        std::vector<BakedTile>& tiles);

    /// The digest of a whole bake: BLAKE3 over the manifest text and every tile's source digest in key order. A pack
    /// is up to date when its header's source digest equals this.
    EdenArchive::Hash contentDigest(const TileManifest& manifest, const std::vector<BakedTile>& tiles);
}

#endif
