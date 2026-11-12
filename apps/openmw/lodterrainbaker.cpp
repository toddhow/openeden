#include "lodterrainbaker.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadland.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/lodworld.hpp>
#include <components/lod/tilebake.hpp>
#include <components/lod/tilepack.hpp>
#include <components/lod/tileselection.hpp>
#include <components/neutralterrain/cellcache.hpp>
#include <components/neutralterrain/storage.hpp>
#include <components/render/processmemory.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/terrain/bounds.hpp>

#include <components/settings/values.hpp>

#include <cstdlib>
#include <cmath>

#include "mwbase/environment.hpp"
#include "mwbase/extensionmanager.hpp"
#include "mwrender/lodterrain.hpp"
#include "mwrender/terrainstorage.hpp"
#include "mwworld/esmstore.hpp"

#include <components/worldmodel/store.hpp>

namespace OMW
{
    namespace
    {
        // Time spent baking between two frames; the window keeps drawing in between.
        constexpr std::chrono::milliseconds sliceTime{ 10 };
        // The name Morrowind's one exterior world gets in the manifest (worldspace index 0).
        constexpr const char* worldspaceName = "Morrowind";
        // Task 0130: the world a bake of a game pack's neutral world takes when OPENEDEN_LOD_BAKE_WORLD is not set.
        constexpr const char* defaultNeutralWorld = "Tamriel";
        // How far from the origin (in cells) the scan for land looks. Skyrim's Tamriel spans about -60..60 by -45..50;
        // a larger world raises this (and the cache cap) on purpose.
        constexpr std::int32_t neutralScanRadius = 100;
        constexpr std::size_t neutralCacheCells = (2 * neutralScanRadius + 1) * (2 * neutralScanRadius + 1) + 1024;
    }

    struct LodTerrainBaker::State
    {
        State(std::filesystem::path out, Resource::ResourceSystem& resources)
            : mOut(std::move(out))
            , mResources(resources)
        {
        }

        std::filesystem::path mOut;
        Resource::ResourceSystem& mResources;
        // Morrowind: the ESM store's terrain storage. A game pack's neutral world: the neutral storage over a cache of
        // its own that holds the whole land (task 0130). mStorage points at whichever is in use.
        std::unique_ptr<MWRender::TerrainStorage> mEsmStorage;
        std::shared_ptr<NeutralTerrain::CellCache> mNeutralCache;
        std::unique_ptr<NeutralTerrain::Storage> mNeutralStorage;
        Terrain::Storage* mStorage = nullptr;
        std::unique_ptr<MWRender::StorageLandSource> mSource;
        bool mNeutral = false;
        std::string mGameId;
        std::string mWorldName; // the world's editor id (the pack's folder)
        std::string mManifestName;
        std::int32_t mScanRow = -neutralScanRadius; // the next row of cells the neutral scan reads
        bool mScanning = false;
        std::vector<std::pair<std::int32_t, std::int32_t>> mLandCells;
        std::unique_ptr<MWRender::StorageAlbedoSource> mAlbedo;
        Lod::TileBakeSettings mSettings;
        Lod::TerrainBakePlan mPlan;
        // What is left to bake: the tile and the settings of its level.
        std::vector<std::pair<Lod::TileId, Lod::TileBakeSettings>> mWork;
        std::vector<Lod::BakedTile> mTiles;
        Lod::TileManifest mManifest;
        bool mStarted = false;
        bool mFinished = false;
        std::chrono::steady_clock::time_point mStart;
    };


    namespace
    {
        // Plans the tiles of every level for the land cells found and the albedo source (both kinds of world).
        void planWork(LodTerrainBaker::State& state)
        {
            for (const std::uint32_t level : state.mPlan.mLevels)
            {
                Lod::TileBakeSettings settings = state.mSettings;
                settings.mLevel = level;
                settings.mAlbedoSize = level >= state.mPlan.mAlbedoFromLevel ? state.mPlan.mAlbedoSize : 0;
                const std::vector<Lod::TileId> ids = Lod::tilesCoveringCells(state.mLandCells, settings);
                for (const Lod::TileId& id : ids)
                    state.mWork.emplace_back(id, settings);
                Log(Debug::Info) << "LOD terrain bake: level " << level << ": " << ids.size() << " tiles"
                                 << (settings.mAlbedoSize != 0 ? " with a ground picture" : "");
            }
            state.mAlbedo = std::make_unique<MWRender::StorageAlbedoSource>(*state.mStorage,
                state.mStorage->getLayers(), state.mResources.textureOptions, state.mResources.getVFS());
            state.mManifest = Lod::makeTerrainManifest(state.mSettings, state.mPlan, true, state.mManifestName);
            state.mTiles.reserve(state.mWork.size());
            Log(Debug::Info) << "LOD terrain bake: " << state.mLandCells.size() << " land cells, "
                             << state.mWork.size() << " tiles in all (layout version " << Lod::tileKeyLayoutVersion
                             << ").";
        }

        // Task 0130: a session of a game pack with a neutral world source bakes that world's land. True when this is
        // such a session and the bake is set up (the scan of the land follows, slice by slice); false for Morrowind's;
        // with state.mFinished set when it IS such a session and cannot go on (the reason is logged).
        bool beginNeutral(LodTerrainBaker::State& state)
        {
            if (!Settings::game().mNeutralWorld)
                return false;
            NeutralWorld::NeutralWorldStore* store
                = MWBase::Environment::get().getExtensionManager()->neutralWorldStore();
            if (store == nullptr || store->sourceCount() == 0)
                return false;
            const std::vector<std::string> games = store->gameIds();
            state.mGameId = games.front();
            openeden::sdk::game::WorldCellSource* source = store->source(state.mGameId);
            const char* requested = std::getenv("OPENEDEN_LOD_BAKE_WORLD");
            const std::string worldName
                = requested != nullptr && *requested != 0 ? std::string(requested) : std::string(defaultNeutralWorld);
            const openeden::sdk::game::WorldId world = store->findWorldByEditorId(state.mGameId, worldName);
            if (source == nullptr || world == openeden::sdk::game::kNoWorldId)
            {
                Log(Debug::Error) << "LOD terrain bake failed, no pack written: " << state.mGameId << " has no world '"
                                  << worldName << "' (set OPENEDEN_LOD_BAKE_WORLD to an editor id).";
                state.mFinished = true;
                return false;
            }
            NeutralTerrain::CacheLimits limits;
            limits.mMaxCells = neutralCacheCells;
            limits.mMaxEnsureCells = 2 * neutralScanRadius + 1;
            state.mNeutralCache = std::make_shared<NeutralTerrain::CellCache>(*source, limits);
            std::string error;
            if (!state.mNeutralCache->setWorld(world, error))
            {
                Log(Debug::Error) << "LOD terrain bake failed, no pack written: " << error;
                state.mFinished = true;
                return false;
            }
            state.mNeutralStorage = std::make_unique<NeutralTerrain::Storage>(state.mNeutralCache);
            state.mStorage = state.mNeutralStorage.get();
            state.mSource = std::make_unique<MWRender::StorageLandSource>(*state.mStorage);
            const openeden::sdk::game::WorldSpaceInfo& info = state.mNeutralCache->world();
            state.mWorldName = info.editorId;
            state.mSettings = MWRender::terrainBakeSettings(
                static_cast<std::uint32_t>(info.cellSizeUnits), static_cast<std::uint32_t>(info.vertsPerSide));
            state.mPlan = MWRender::terrainBakePlan(info.cellSizeUnits, true);
            state.mManifestName = Lod::neutralWorldspaceName(MWRender::neutralWorldFacts(*source, info));
            state.mNeutral = true;
            state.mScanning = true;
            Log(Debug::Info) << "LOD terrain bake: neutral world " << state.mGameId << " / " << state.mWorldName
                             << ", cells of " << info.cellSizeUnits << " units with " << info.vertsPerSide
                             << " vertices; scanning cells " << -neutralScanRadius << ".." << neutralScanRadius << ".";
            return true;
        }
    }

    LodTerrainBaker::LodTerrainBaker(std::filesystem::path outFolder, Resource::ResourceSystem& resources)
        : mState(std::make_unique<State>(std::move(outFolder), resources))
    {
    }

    LodTerrainBaker::~LodTerrainBaker() = default;

    bool LodTerrainBaker::update()
    {
        State& state = *mState;
        if (state.mFinished)
            return true;

        // Only called in the running state, where the store exists (the accessor asserts it).
        const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();

        if (!state.mStarted)
        {
            state.mStarted = true;
            state.mStart = std::chrono::steady_clock::now();
            if (!beginNeutral(state))
            {
                if (state.mFinished)
                    return true;
                state.mEsmStorage = std::make_unique<MWRender::TerrainStorage>(&state.mResources);
                state.mEsmStorage->setStore(store);
                state.mStorage = state.mEsmStorage.get();
                state.mSource = std::make_unique<MWRender::StorageLandSource>(*state.mStorage);
                // The cell size comes from the storage (8192 for Morrowind), never from a literal here.
                state.mSettings
                    = MWRender::terrainBakeSettings(static_cast<std::uint32_t>(state.mStorage->cellWorldSize));
                state.mPlan = MWRender::terrainBakePlan();
                state.mManifestName = worldspaceName;
                for (const ESM::Land& land : store->get<ESM::Land>())
                    state.mLandCells.emplace_back(land.mX, land.mY);
                planWork(state);
            }
        }

        if (state.mScanning)
        {
            // Task 0130: read the land of a neutral world a row of cells at a time (the source is main thread only).
            const auto scanEnd = std::chrono::steady_clock::now() + sliceTime;
            while (state.mScanRow <= neutralScanRadius && std::chrono::steady_clock::now() < scanEnd)
            {
                state.mNeutralCache->ensure(-neutralScanRadius, state.mScanRow, neutralScanRadius, state.mScanRow);
                for (std::int32_t x = -neutralScanRadius; x <= neutralScanRadius; ++x)
                    if (state.mNeutralCache->get(x, state.mScanRow) != nullptr)
                        state.mLandCells.emplace_back(x, state.mScanRow);
                ++state.mScanRow;
            }
            if (state.mScanRow <= neutralScanRadius)
                return false;
            state.mScanning = false;
            Log(Debug::Info) << "LOD terrain bake: scanned the land of " << state.mWorldName << ": "
                             << state.mLandCells.size() << " cells with land, " << state.mNeutralCache->resident()
                             << " cells held, " << state.mNeutralCache->refusedCells() << " refused.";
            const bool onBorder = std::any_of(state.mLandCells.begin(), state.mLandCells.end(), [](const auto& cell) {
                return std::abs(cell.first) >= neutralScanRadius || std::abs(cell.second) >= neutralScanRadius;
            });
            if (onBorder)
                Log(Debug::Warning) << "LOD terrain bake: land reaches the border of the scan (" << neutralScanRadius
                                    << " cells from the origin); a larger world needs a larger scan.";
            Log(Debug::Info) << "LOD terrain bake: working set " << (Render::processWorkingSetBytes() >> 20)
                             << " MiB after the scan.";
            if (state.mLandCells.empty())
            {
                Log(Debug::Error) << "LOD terrain bake failed, no pack written: the world has no land.";
                state.mFinished = true;
                return true;
            }
            planWork(state);
            return false;
        }

        const auto sliceEnd = std::chrono::steady_clock::now() + sliceTime;
        while (state.mTiles.size() < state.mWork.size() && std::chrono::steady_clock::now() < sliceEnd)
        {
            Lod::BakedTile tile;
            const auto& [id, settings] = state.mWork[state.mTiles.size()];
            if (const Lod::LodError error
                = Lod::bakeTerrainTile(*state.mSource, state.mAlbedo.get(), id, settings, tile))
            {
                Log(Debug::Error) << "LOD terrain bake failed, no pack written: " << error.describe();
                state.mFinished = true;
                return true;
            }
            state.mTiles.push_back(std::move(tile));
        }
        if (state.mTiles.size() < state.mWork.size())
            return false;
        std::sort(state.mTiles.begin(), state.mTiles.end(), [](const Lod::BakedTile& lhs, const Lod::BakedTile& rhs) {
            return Lod::packTileKey(lhs.mId).value_or(0) < Lod::packTileKey(rhs.mId).value_or(0);
        });

        state.mFinished = true;
        {
            // The spread of the tiles' errors (world units), for choosing a threshold.
            std::vector<float> errors;
            errors.reserve(state.mTiles.size());
            for (const Lod::BakedTile& tile : state.mTiles)
                errors.push_back(tile.mData.mGeometricError);
            std::sort(errors.begin(), errors.end());
            // The same, level by level: what the tile selection works from (task 0066 stage d).
            for (const std::uint32_t level : state.mPlan.mLevels)
            {
                std::vector<float> ofLevel;
                for (const Lod::BakedTile& tile : state.mTiles)
                    if (tile.mId.mLevel == level)
                        ofLevel.push_back(tile.mData.mGeometricError);
                std::sort(ofLevel.begin(), ofLevel.end());
                if (!ofLevel.empty())
                {
                    Log(Debug::Info) << "LOD terrain bake: level " << level << ": " << ofLevel.size()
                                     << " tiles, geometric error min " << ofLevel.front() << ", median "
                                     << ofLevel[ofLevel.size() / 2] << ", max " << ofLevel.back();
                }
            }
            if (!errors.empty())
            {
                Log(Debug::Info) << "LOD terrain bake: geometric error of the tiles (world units): min "
                                 << errors.front() << ", median " << errors[errors.size() / 2] << ", 95th percentile "
                                 << errors[errors.size() * 95 / 100] << ", max " << errors.back();
            }
        }
        const EdenArchive::Hash digest = Lod::contentDigest(state.mManifest, state.mTiles);
        const std::filesystem::path path = Lod::terrainPackPath(
            state.mOut / "openeden" / "lod", state.mNeutral ? state.mGameId : std::string(), state.mWorldName);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::uint64_t generation = 1;
        {
            Lod::LodError openError;
            const std::unique_ptr<Lod::TilePack> old = Lod::TilePack::open(path, openError);
            if (old)
            {
                if (old->sourceDigest() == digest && old->manifest() == state.mManifest
                    && old->tileIds().size() == state.mTiles.size())
                {
                    Log(Debug::Info) << "LOD terrain bake: " << Files::pathToUnicodeString(path)
                                     << " is already up to date (generation " << old->generation()
                                     << "), nothing written.";
                    return true;
                }
                Log(Debug::Info)
                    << "LOD terrain bake: the pack on disk is stale (land, land textures or settings changed, or "
                       "an older layout): it is replaced.";
                generation = old->generation() + 1;
            }
        }
        Lod::TilePackOptions options;
        options.mGeneration = generation;
        options.mSourceDigest = digest;
        options.mThreads = 2;
        // Stored, not compressed: the pack is small (a few MiB) and its bytes then depend only on the land data,
        // on any machine and with any zstd version.
        options.mStoreOnly = true;
        if (const Lod::LodError error = Lod::writeTilePack(state.mManifest, state.mTiles, options, path))
        {
            Log(Debug::Error) << "LOD terrain bake failed, the old pack (if any) is unchanged: " << error.describe();
            return true;
        }

        // Read every tile back through the reader the game will use.
        Lod::LodError openError;
        const std::unique_ptr<Lod::TilePack> written = Lod::TilePack::open(path, openError);
        std::size_t good = 0;
        if (written)
        {
            for (const Lod::TileId& id : written->tileIds())
            {
                Lod::TerrainTile tile;
                if (!written->readTile(id, tile))
                    ++good;
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - state.mStart).count();
        Log(Debug::Info) << "LOD terrain bake: working set " << (Render::processWorkingSetBytes() >> 20)
                         << " MiB at the end.";
        Log(Debug::Info) << "LOD terrain bake: wrote " << Files::pathToUnicodeString(path) << " (generation "
                         << generation << "), " << state.mTiles.size() << " tiles, " << good << " read back fine, "
                         << seconds << " s.";
        return true;
    }
}
