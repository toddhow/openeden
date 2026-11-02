#include "lodterrainbaker.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadland.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/tilebake.hpp>
#include <components/lod/tilepack.hpp>
#include <components/lod/tileselection.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/terrain/bounds.hpp>

#include "mwbase/environment.hpp"
#include "mwrender/lodterrain.hpp"
#include "mwrender/terrainstorage.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    namespace
    {
        // Time spent baking between two frames; the window keeps drawing in between.
        constexpr std::chrono::milliseconds sliceTime{ 10 };
        // The name Morrowind's one exterior world gets in the manifest (worldspace index 0).
        constexpr const char* worldspaceName = "Morrowind";
    }

    struct LodTerrainBaker::State
    {
        State(std::filesystem::path out, Resource::ResourceSystem& resources)
            : mOut(std::move(out))
            , mResources(resources)
            , mStorage(&resources)
            , mSource(mStorage)
        {
        }

        std::filesystem::path mOut;
        Resource::ResourceSystem& mResources;
        MWRender::TerrainStorage mStorage;
        MWRender::StorageLandSource mSource;
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
            state.mStorage.setStore(store);
            // The cell size comes from the storage (8192 for Morrowind), never from a literal here.
            state.mSettings = MWRender::terrainBakeSettings(static_cast<std::uint32_t>(state.mStorage.cellWorldSize));
            state.mPlan = MWRender::terrainBakePlan();
            std::vector<std::pair<std::int32_t, std::int32_t>> cells;
            for (const ESM::Land& land : store->get<ESM::Land>())
                cells.emplace_back(land.mX, land.mY);
            for (const std::uint32_t level : state.mPlan.mLevels)
            {
                Lod::TileBakeSettings settings = state.mSettings;
                settings.mLevel = level;
                settings.mAlbedoSize = level >= state.mPlan.mAlbedoFromLevel ? state.mPlan.mAlbedoSize : 0;
                const std::vector<Lod::TileId> ids = Lod::tilesCoveringCells(cells, settings);
                for (const Lod::TileId& id : ids)
                    state.mWork.emplace_back(id, settings);
                Log(Debug::Info) << "LOD terrain bake: level " << level << ": " << ids.size() << " tiles"
                                 << (settings.mAlbedoSize != 0 ? " with a ground picture" : "");
            }
            state.mAlbedo = std::make_unique<MWRender::StorageAlbedoSource>(
                state.mStorage, state.mStorage.getLayers(), state.mResources.textureOptions, state.mResources.getVFS());
            state.mManifest = Lod::makeTerrainManifest(state.mSettings, state.mPlan, true, worldspaceName);
            state.mTiles.reserve(state.mWork.size());
            Log(Debug::Info) << "LOD terrain bake: " << cells.size() << " land cells, " << state.mWork.size()
                             << " tiles in all (layout version " << Lod::tileKeyLayoutVersion << ").";
        }

        const auto sliceEnd = std::chrono::steady_clock::now() + sliceTime;
        while (state.mTiles.size() < state.mWork.size() && std::chrono::steady_clock::now() < sliceEnd)
        {
            Lod::BakedTile tile;
            const auto& [id, settings] = state.mWork[state.mTiles.size()];
            if (const Lod::LodError error
                = Lod::bakeTerrainTile(state.mSource, state.mAlbedo.get(), id, settings, tile))
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
            if (!errors.empty())
            {
                Log(Debug::Info) << "LOD terrain bake: geometric error of the tiles (world units): min "
                                 << errors.front() << ", median " << errors[errors.size() / 2] << ", 95th percentile "
                                 << errors[errors.size() * 95 / 100] << ", max " << errors.back();
            }
        }
        const EdenArchive::Hash digest = Lod::contentDigest(state.mManifest, state.mTiles);
        const std::filesystem::path path = state.mOut / "openeden" / "lod" / "terrain.edenpak";
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
        Log(Debug::Info) << "LOD terrain bake: wrote " << Files::pathToUnicodeString(path) << " (generation "
                         << generation << "), " << state.mTiles.size() << " tiles, " << good << " read back fine, "
                         << seconds << " s.";
        return true;
    }
}
