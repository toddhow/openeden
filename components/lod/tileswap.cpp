#include "tileswap.hpp"

namespace Lod
{
    const char* tileSwapKindName(TileSwapKind kind) noexcept
    {
        switch (kind)
        {
            case TileSwapKind::New:
                return "new";
            case TileSwapKind::Refined:
                return "refined";
            case TileSwapKind::Coarsened:
                return "coarsened";
            case TileSwapKind::ToLive:
                return "to live";
            case TileSwapKind::Removed:
                return "removed";
        }
        return "unknown";
    }

    const char* tileSwapTraceName(TileSwapKind kind) noexcept
    {
        switch (kind)
        {
            case TileSwapKind::New:
                return "lod.tile.new";
            case TileSwapKind::Refined:
                return "lod.tile.refined";
            case TileSwapKind::Coarsened:
                return "lod.tile.coarsened";
            case TileSwapKind::ToLive:
                return "lod.tile.tolive";
            case TileSwapKind::Removed:
                return "lod.tile.removed";
        }
        return "lod.tile.unknown";
    }

    std::string formatTileSwap(std::uint64_t frame, const TileId& id, TileSwapKind kind)
    {
        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
        tileOriginCell(id, cellX, cellY);
        return "World LOD tile swap: frame " + std::to_string(frame) + ", level " + std::to_string(id.mLevel)
            + " tile (" + std::to_string(id.mX) + ", " + std::to_string(id.mY) + ") at cell (" + std::to_string(cellX)
            + ", " + std::to_string(cellY) + "): " + tileSwapKindName(kind);
    }
}
