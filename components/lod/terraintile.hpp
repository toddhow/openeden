#ifndef OPENMW_COMPONENTS_LOD_TERRAINTILE_H
#define OPENMW_COMPONENTS_LOD_TERRAINTILE_H

// OpenEden world LOD (task 0055, stage S1b). The payload of one baked terrain tile: a height grid, normals and an
// albedo picture. The bytes are one Eden Archive entry of type 0x0010.
//
// DRAFT: this is "layout version 0" (the spec's proposal, not approved by toddhow). The header's version field holds
// terrainTilePayloadVersion (0), and the reader refuses any other value, so a later final layout cannot be mistaken
// for this one. Nothing outside components/lod may depend on offsets; use these functions.
//
// Reading untrusted bytes: every size and offset is checked before it is used; a failure returns a LodError and
// never throws or reads outside the span.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "loderror.hpp"

namespace Lod
{
    inline constexpr std::uint16_t entryTypeTerrainTile = 0x0010;
    inline constexpr std::uint16_t terrainTilePayloadVersion = 0; // draft
    inline constexpr std::size_t terrainTileHeaderSize = 88;
    inline constexpr std::uint32_t minGridSize = 3;
    inline constexpr std::uint32_t maxGridSize = 129;
    inline constexpr std::uint32_t maxAlbedoSide = 2048;

    inline constexpr std::uint16_t flagHasNormals = 1u << 0;
    inline constexpr std::uint16_t flagHasAlbedo = 1u << 1;
    /// Reserved (task 0044's material channels). The reader refuses a payload that sets it; the writer never does.
    inline constexpr std::uint16_t flagHasMaterial = 1u << 2;

    enum class AlbedoFormat : std::uint32_t
    {
        Rgba8 = 0, // 4 bytes a texel
        Bc1 = 1, // 8 bytes per 4 x 4 block (the size is counted, S1 writes Rgba8)
    };

    /// The logical content of one tile. World units, Z up.
    struct TerrainTile
    {
        std::uint32_t mGridSize = 0; // vertices per side
        std::uint32_t mLevel = 0;
        float mSide = 0; // tile side in world units
        float mOriginX = 0, mOriginY = 0; // south-west corner
        float mMinHeight = 0, mMaxHeight = 0;
        float mGeometricError = 0;
        bool mHasNormals = false;
        bool mHasAlbedo = false;
        std::uint32_t mAlbedoWidth = 0, mAlbedoHeight = 0;
        AlbedoFormat mAlbedoFormat = AlbedoFormat::Rgba8;
        std::uint32_t mAlbedoMipCount = 0;
        /// N * N values, row after row from the south edge, west to east: height = min + v / 65535 * (max - min).
        std::vector<std::uint16_t> mHeights;
        /// N * N pairs of signed bytes (octahedral encoding), only when mHasNormals.
        std::vector<std::int8_t> mNormals;
        /// The mip chain, largest first, each level tightly packed, only when mHasAlbedo.
        std::vector<std::uint8_t> mAlbedo;
    };

    /// Where each section lies in the payload (all offsets 8-aligned; absent sections have offset 0 and size 0).
    struct TerrainTileLayout
    {
        std::uint64_t mHeightsOffset = 0, mHeightsSize = 0;
        std::uint64_t mNormalsOffset = 0, mNormalsSize = 0;
        std::uint64_t mAlbedoOffset = 0, mAlbedoSize = 0;
        std::uint64_t mTotalSize = 0; // the end of the last section rounded up to 8
    };

    /// The bytes of an albedo mip chain, or 0 when the width, height or mip count is not valid (power of two up to
    /// maxAlbedoSide, 1 to log2(max(width, height)) + 1 mips).
    std::uint64_t albedoByteSize(
        std::uint32_t width, std::uint32_t height, AlbedoFormat format, std::uint32_t mipCount) noexcept;

    /// The layout the writer uses for these fields. Fails when the fields are invalid (same checks as the reader).
    LodError computeTerrainTileLayout(const TerrainTile& tile, TerrainTileLayout& layout);

    /// Writes the payload. Fails (and leaves `out` empty) when the tile is invalid: grid size outside 3..129, min >
    /// max, a non-finite or negative error, array sizes that do not match, an invalid albedo.
    LodError writeTerrainTile(const TerrainTile& tile, std::vector<std::uint8_t>& out);

    /// Reads only the first terrainTileHeaderSize bytes plus the entry's total size, and checks them: magic, version
    /// (must be terrainTilePayloadVersion), flags (unknown bits and flagHasMaterial refused), grid size, reserved
    /// bytes zero, min <= max, finite non-negative error, albedo size, every offset 8-aligned and every section
    /// inside `entrySize` without overlap, sizes adding up to exactly `entrySize`. Fills the fields (not the arrays).
    LodError parseTerrainTileHeader(
        std::span<const std::uint8_t> header, std::uint64_t entrySize, TerrainTile& tile, TerrainTileLayout& layout);

    /// Parses a whole payload (parseTerrainTileHeader, then copies the three sections). Padding must be zero.
    LodError parseTerrainTile(std::span<const std::uint8_t> bytes, TerrainTile& tile);
}

#endif
