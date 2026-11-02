#ifndef OPENEDEN_OPENCS_TESTS_LOD_TILEFIXTURES_H
#define OPENEDEN_OPENCS_TESTS_LOD_TILEFIXTURES_H

// Small builders shared by the tile model tests (task 0055, stage S1e): generated terrain tiles, tiny packs built in
// memory and a temporary file that removes itself. No game data is used.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <components/lod/loderror.hpp>
#include <components/lod/terraintile.hpp>
#include <components/lod/tilebake.hpp>
#include <components/lod/tilekey.hpp>
#include <components/lod/tilepack.hpp>
#include <edenarchive/source.hpp>

#include "apps/opencs/model/lod/tilemodel.hpp"

namespace CSMLodTest
{
    /// Morrowind's cell size in world units.
    inline constexpr std::uint32_t cellSize = 8192;

    /// The tile at `level` that contains cell (cellX, cellY), worldspace 0.
    inline Lod::TileId tileAt(std::uint32_t level, std::int32_t cellX, std::int32_t cellY)
    {
        const std::optional<Lod::TileId> id = Lod::tileContainingCell(0, level, cellX, cellY);
        return id ? *id : Lod::TileId{};
    }

    /// A valid tile for `id` with N x N vertices, deterministic non-flat heights, optional normals (varied unit
    /// vectors, encoded) and optional albedo (2 x 2 Rgba8 with two mips). The origin and side follow the id.
    inline Lod::TerrainTile makeTile(const Lod::TileId& id, std::uint32_t n, float minHeight, float maxHeight,
        float error, bool normals = false, bool albedo = false)
    {
        Lod::TerrainTile tile;
        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
        Lod::tileOriginCell(id, cellX, cellY);
        tile.mGridSize = n;
        tile.mLevel = id.mLevel;
        tile.mSide = static_cast<float>(cellSize) * static_cast<float>(Lod::tileSideInCells(id));
        tile.mOriginX = static_cast<float>(cellX) * static_cast<float>(cellSize);
        tile.mOriginY = static_cast<float>(cellY) * static_cast<float>(cellSize);
        tile.mMinHeight = minHeight;
        tile.mMaxHeight = maxHeight;
        tile.mGeometricError = error;
        const std::size_t count = static_cast<std::size_t>(n) * n;
        tile.mHeights.resize(count);
        for (std::size_t k = 0; k < count; ++k)
            tile.mHeights[k] = static_cast<std::uint16_t>((k * 4099u + 17u) % 65536u);
        if (normals)
        {
            tile.mHasNormals = true;
            tile.mNormals.resize(count * 2);
            for (std::size_t k = 0; k < count; ++k)
            {
                const float a = 0.3f + 0.1f * static_cast<float>(k % 7);
                const float b = 0.7f * static_cast<float>(k);
                Lod::encodeNormal(std::sin(a) * std::cos(b), std::sin(a) * std::sin(b), std::cos(a),
                    tile.mNormals[2 * k], tile.mNormals[2 * k + 1]);
            }
        }
        if (albedo)
        {
            tile.mHasAlbedo = true;
            tile.mAlbedoWidth = 2;
            tile.mAlbedoHeight = 2;
            tile.mAlbedoFormat = Lod::AlbedoFormat::Rgba8;
            tile.mAlbedoMipCount = 2;
            tile.mAlbedo.resize(20);
            for (std::size_t k = 0; k < tile.mAlbedo.size(); ++k)
                tile.mAlbedo[k] = static_cast<std::uint8_t>((k * 7u + 3u) % 256u);
        }
        return tile;
    }

    /// A tile whose heights are all zero with minimum and maximum 0.
    inline Lod::TerrainTile makeFlatTile(const Lod::TileId& id, std::uint32_t n, float error)
    {
        Lod::TerrainTile tile = makeTile(id, n, 0.f, 0.f, error);
        std::fill(tile.mHeights.begin(), tile.mHeights.end(), std::uint16_t{ 0 });
        return tile;
    }

    inline Lod::BakedTile makeBaked(const Lod::TileId& id, Lod::TerrainTile tile)
    {
        Lod::BakedTile baked;
        baked.mId = id;
        baked.mData = std::move(tile);
        return baked;
    }

    /// A manifest for these tiles: levels in use, and (optionally) one worldspace with the given cell size.
    inline Lod::TileManifest makeManifest(const std::vector<Lod::BakedTile>& tiles, std::uint32_t gridSize,
        std::uint32_t cell = cellSize, bool withWorldspace = true)
    {
        Lod::TileManifest manifest;
        manifest.mBakerVersion = Lod::bakerVersion;
        if (withWorldspace)
            manifest.mWorldspaces.push_back(Lod::WorldspaceInfo{ 0, "Morrowind", cell });
        for (const Lod::BakedTile& tile : tiles)
            manifest.mLevels.push_back(tile.mId.mLevel);
        std::sort(manifest.mLevels.begin(), manifest.mLevels.end());
        manifest.mLevels.erase(std::unique(manifest.mLevels.begin(), manifest.mLevels.end()), manifest.mLevels.end());
        manifest.mGridSize = gridSize;
        manifest.mAlbedoSize = 0;
        for (const Lod::BakedTile& tile : tiles)
            if (tile.mData.mHasAlbedo)
                manifest.mAlbedoSize = 2;
        return manifest;
    }

    /// Builds a store-only pack in memory, opens it from a memory source and adopts it. Returns nullptr and sets
    /// `error` when any step fails.
    inline std::unique_ptr<CSMLod::TileModel> makeModel(
        const Lod::TileManifest& manifest, const std::vector<Lod::BakedTile>& tiles, Lod::LodError& error)
    {
        Lod::TilePackOptions options;
        options.mStoreOnly = true;
        std::vector<std::uint8_t> bytes;
        error = Lod::buildTilePack(manifest, tiles, options, bytes);
        if (!error.ok())
            return nullptr;
        std::shared_ptr<const EdenArchive::Source> source = EdenArchive::makeMemorySource(std::move(bytes));
        std::unique_ptr<Lod::TilePack> pack = Lod::TilePack::open(source, error);
        if (!pack)
            return nullptr;
        return CSMLod::TileModel::adopt(std::move(pack), error);
    }

    /// Level-1 flat tiles (N = 3) at the given south-west cells (each must be even), all with one geometric error.
    inline std::vector<Lod::BakedTile> makeFlatLevelOneTiles(
        const std::vector<std::pair<std::int32_t, std::int32_t>>& cells, float error)
    {
        std::vector<Lod::BakedTile> tiles;
        for (const auto& cell : cells)
        {
            const Lod::TileId id = tileAt(1, cell.first, cell.second);
            tiles.push_back(makeBaked(id, makeFlatTile(id, 3, error)));
        }
        return tiles;
    }

    /// A file name in the temporary folder that is removed again when the object goes away.
    class TempFile
    {
    public:
        TempFile()
        {
            static std::atomic<unsigned> counter{ 0 };
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path()
                / ("edenstudio-tilemodel-" + std::to_string(static_cast<long long>(ticks)) + "-"
                    + std::to_string(counter.fetch_add(1)) + ".edenpack");
        }
        ~TempFile()
        {
            std::error_code ignored;
            std::filesystem::remove(mPath, ignored);
        }
        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;

        const std::filesystem::path& path() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };
}

#endif
