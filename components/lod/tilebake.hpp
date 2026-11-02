#ifndef OPENMW_COMPONENTS_LOD_TILEBAKE_H
#define OPENMW_COMPONENTS_LOD_TILEBAKE_H

// OpenEden world LOD (task 0055, stage S1c). Baking one terrain tile from full-detail land data. Pure code: the land
// comes through the LandSource interface, which the game implements over the same terrain storage the live terrain
// uses (apps/openmw/lodterrainbaker.*) and the tests implement with generated data. DRAFT, layout version 0.
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

#include "loderror.hpp"
#include "tilepack.hpp"

namespace Lod
{
    /// Version of the baker's behaviour (goes into the manifest and the provenance parameters). Draft: 0.
    inline constexpr std::uint32_t bakerVersion = 0;
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
        std::uint8_t mWorldspace = 0;
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
