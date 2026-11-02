#ifndef OPENMW_COMPONENTS_LOD_TILEKEY_H
#define OPENMW_COMPONENTS_LOD_TILEKEY_H

// OpenEden world LOD (task 0055, stage S1a). The tile key of a baked terrain tile.
//
// DRAFT: this layout is "layout version 0". toddhow has not approved it and it is NOT stable. Nothing outside
// components/lod may depend on the bit positions: use the functions below.
//
// A key is 64 bits: bits 63..56 the tag (0x10 terrain tile), bits 55..48 the worldspace index, bits 47..44 the level
// (0 to 15), bits 43..0 the Morton code of the tile's (x, y), 22 bits each: bit 2k of the code is bit k of x and bit
// 2k+1 is bit k of y. A tile coordinate at level L is (cellCoordinate + 2^21) >> L, so negative cells are not negative.

#include <cstdint>
#include <optional>

namespace Lod
{
    /// Version of the key layout and the payload layout together. 0 means "draft, may change without notice".
    inline constexpr std::uint32_t tileKeyLayoutVersion = 0;
    /// The container's key tag of a terrain tile (spec section 10.3, 11.3).
    inline constexpr std::uint8_t tagTerrainTile = 0x10;
    inline constexpr std::uint32_t maxLevel = 15;
    inline constexpr std::uint32_t coordinateBits = 22;
    /// Added to a cell coordinate before the shift, so that Morrowind's negative cells become non-negative.
    inline constexpr std::int64_t cellBias = std::int64_t{ 1 } << 21;

    /// One tile. `x` and `y` are the biased tile coordinates at `level` (always below 2^(22 - level)).
    struct TileId
    {
        std::uint8_t mWorldspace = 0;
        std::uint8_t mLevel = 0;
        std::uint32_t mX = 0;
        std::uint32_t mY = 0;
        bool operator==(const TileId&) const = default;
    };

    /// The tile at `level` that contains cell (cellX, cellY). Empty when the level is above maxLevel or the cell is
    /// outside [-2^21, 2^21 - 1].
    std::optional<TileId> tileContainingCell(
        std::uint8_t worldspace, std::uint32_t level, std::int32_t cellX, std::int32_t cellY) noexcept;

    /// The functions below that take a TileId expect a valid one (what tileContainingCell or unpackTileKey returns).
    /// The signed cell coordinates of the tile's south-west cell.
    void tileOriginCell(const TileId& id, std::int32_t& cellX, std::int32_t& cellY) noexcept;
    /// The number of cells along one side of the tile: 2^level.
    std::uint32_t tileSideInCells(const TileId& id) noexcept;

    /// Interleaves two 22-bit numbers (bit 2k from x, bit 2k+1 from y). Bits above 22 are ignored.
    std::uint64_t mortonEncode(std::uint32_t x, std::uint32_t y) noexcept;
    void mortonDecode(std::uint64_t code, std::uint32_t& x, std::uint32_t& y) noexcept;

    /// The 64-bit key. Never 0. Empty when the id is invalid: level above maxLevel, or a coordinate of 2^(22 - level)
    /// or more.
    std::optional<std::uint64_t> packTileKey(const TileId& id) noexcept;
    /// Empty when the tag is not tagTerrainTile or the fields are invalid in the same way.
    std::optional<TileId> unpackTileKey(std::uint64_t key) noexcept;

    /// The tile one level up that contains this one. Empty at maxLevel.
    std::optional<TileId> parentTile(const TileId& id) noexcept;
    /// The four tiles one level down, in Morton order (x low bit 0/1 fastest, then y). Returns false at level 0.
    bool childTiles(const TileId& id, TileId (&children)[4]) noexcept;
}

#endif
