#include "tilekey.hpp"

namespace Lod
{
    namespace
    {
        constexpr std::uint64_t coordinateMask = (std::uint64_t{ 1 } << coordinateBits) - 1;

        // Spreads the low 22 bits of v so that bit k moves to bit 2k.
        std::uint64_t spreadBits(std::uint64_t v)
        {
            v &= coordinateMask;
            v = (v | (v << 16)) & 0x0000FFFF0000FFFFull;
            v = (v | (v << 8)) & 0x00FF00FF00FF00FFull;
            v = (v | (v << 4)) & 0x0F0F0F0F0F0F0F0Full;
            v = (v | (v << 2)) & 0x3333333333333333ull;
            v = (v | (v << 1)) & 0x5555555555555555ull;
            return v;
        }

        // The reverse of spreadBits: gathers every second bit.
        std::uint32_t gatherBits(std::uint64_t v)
        {
            v &= 0x5555555555555555ull;
            v = (v | (v >> 1)) & 0x3333333333333333ull;
            v = (v | (v >> 2)) & 0x0F0F0F0F0F0F0F0Full;
            v = (v | (v >> 4)) & 0x00FF00FF00FF00FFull;
            v = (v | (v >> 8)) & 0x0000FFFF0000FFFFull;
            v = (v | (v >> 16)) & 0x00000000FFFFFFFFull;
            return static_cast<std::uint32_t>(v & coordinateMask);
        }

        bool validFields(const TileId& id)
        {
            if (id.mLevel > maxLevel)
                return false;
            const std::uint64_t limit = std::uint64_t{ 1 } << (coordinateBits - id.mLevel);
            return id.mX < limit && id.mY < limit;
        }
    }

    std::optional<TileId> tileContainingCell(
        std::uint8_t worldspace, std::uint32_t level, std::int32_t cellX, std::int32_t cellY) noexcept
    {
        if (level > maxLevel)
            return std::nullopt;
        const std::int64_t x = static_cast<std::int64_t>(cellX) + cellBias;
        const std::int64_t y = static_cast<std::int64_t>(cellY) + cellBias;
        const std::int64_t limit = std::int64_t{ 1 } << coordinateBits;
        if (x < 0 || y < 0 || x >= limit || y >= limit)
            return std::nullopt;
        TileId id;
        id.mWorldspace = worldspace;
        id.mLevel = static_cast<std::uint8_t>(level);
        id.mX = static_cast<std::uint32_t>(x >> level);
        id.mY = static_cast<std::uint32_t>(y >> level);
        return id;
    }

    void tileOriginCell(const TileId& id, std::int32_t& cellX, std::int32_t& cellY) noexcept
    {
        cellX = static_cast<std::int32_t>((static_cast<std::int64_t>(id.mX) << id.mLevel) - cellBias);
        cellY = static_cast<std::int32_t>((static_cast<std::int64_t>(id.mY) << id.mLevel) - cellBias);
    }

    std::uint32_t tileSideInCells(const TileId& id) noexcept
    {
        return std::uint32_t{ 1 } << id.mLevel;
    }

    std::uint64_t mortonEncode(std::uint32_t x, std::uint32_t y) noexcept
    {
        return spreadBits(x) | (spreadBits(y) << 1);
    }

    void mortonDecode(std::uint64_t code, std::uint32_t& x, std::uint32_t& y) noexcept
    {
        x = gatherBits(code);
        y = gatherBits(code >> 1);
    }

    std::optional<std::uint64_t> packTileKey(const TileId& id) noexcept
    {
        if (!validFields(id))
            return std::nullopt;
        return (std::uint64_t{ tagTerrainTile } << 56) | (std::uint64_t{ id.mWorldspace } << 48)
            | (std::uint64_t{ id.mLevel } << 44) | mortonEncode(id.mX, id.mY);
    }

    std::optional<TileId> unpackTileKey(std::uint64_t key) noexcept
    {
        if ((key >> 56) != tagTerrainTile)
            return std::nullopt;
        TileId id;
        id.mWorldspace = static_cast<std::uint8_t>((key >> 48) & 0xFF);
        id.mLevel = static_cast<std::uint8_t>((key >> 44) & 0xF);
        mortonDecode(key & ((std::uint64_t{ 1 } << 44) - 1), id.mX, id.mY);
        if (!validFields(id))
            return std::nullopt;
        return id;
    }

    std::optional<TileId> parentTile(const TileId& id) noexcept
    {
        if (id.mLevel >= maxLevel)
            return std::nullopt;
        TileId parent = id;
        parent.mLevel = static_cast<std::uint8_t>(id.mLevel + 1);
        parent.mX = id.mX >> 1;
        parent.mY = id.mY >> 1;
        return parent;
    }

    bool childTiles(const TileId& id, TileId (&children)[4]) noexcept
    {
        if (id.mLevel == 0)
            return false;
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            children[i].mWorldspace = id.mWorldspace;
            children[i].mLevel = static_cast<std::uint8_t>(id.mLevel - 1);
            children[i].mX = (id.mX << 1) | (i & 1);
            children[i].mY = (id.mY << 1) | (i >> 1);
        }
        return true;
    }
}
