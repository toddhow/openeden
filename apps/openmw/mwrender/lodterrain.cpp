#include "lodterrain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <utility>

#include <vsg/core/Array2D.h>
#include <vsg/state/Sampler.h>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/tilepack.hpp>
#include <components/terrain/builder.hpp>
#include <components/vsgutil/compileop.hpp>

namespace MWRender
{
    void StorageLandSource::sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
        std::vector<float>& heights, std::vector<float>& normals) const
    {
        Terrain::Bounds bounds;
        bounds.size = static_cast<float>(sizeInCells);
        bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
        const Terrain::Storage::VertexData data = mStorage.getVertexData(static_cast<int>(lod), bounds);
        const std::size_t count = static_cast<std::size_t>(data.heights->width()) * data.heights->height();
        heights.assign(data.heights->data(), data.heights->data() + count);
        normals.resize(count * 3);
        for (std::size_t i = 0; i < count; ++i)
        {
            const vsg::vec4& normal = data.normals->data()[i];
            normals[3 * i] = normal.x;
            normals[3 * i + 1] = normal.y;
            normals[3 * i + 2] = normal.z;
        }
    }

    namespace
    {
        struct LoadedTile
        {
            std::uint64_t mKey = 0;
            Lod::TileId mId;
            Lod::TerrainTile mData;
        };

        struct LoadResult
        {
            bool mUsable = false;
            std::string mReason;
            std::vector<LoadedTile> mTiles; // in key order
        };

        // Runs on the worker thread.
        LoadResult loadPackUnchecked(const std::filesystem::path& path, Terrain::Storage& live,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const Lod::TileBakeSettings& settings)
        {
            LoadResult result;
            Lod::LodError error;
            const std::unique_ptr<Lod::TilePack> pack = Lod::TilePack::open(path, error);
            if (!pack)
            {
                result.mReason = "cannot use " + Files::pathToUnicodeString(path) + ": " + error.describe()
                    + " (make one with openmw --bake-lod-terrain)";
                return result;
            }

            // Is the pack made from the land this game has loaded? Bake the same tiles in memory and compare digests.
            StorageLandSource source(live);
            Lod::TileManifest manifest;
            std::vector<Lod::BakedTile> baked;
            error = Lod::bakeTerrain(source, landCells, settings, "Morrowind", manifest, baked);
            if (error)
            {
                result.mReason = "cannot check the pack against the land: " + error.describe();
                return result;
            }
            if (pack->manifest() != manifest || pack->sourceDigest() != Lod::contentDigest(manifest, baked))
            {
                result.mReason
                    = "the pack was made from other land data or settings (stale); make a new one with "
                      "openmw --bake-lod-terrain";
                return result;
            }

            result.mTiles.reserve(pack->tileIds().size());
            for (const Lod::TileId& id : pack->tileIds())
            {
                LoadedTile tile;
                tile.mId = id;
                tile.mKey = Lod::packTileKey(id).value_or(0);
                error = pack->readTile(id, tile.mData);
                if (error)
                {
                    result.mReason = "cannot read a tile: " + error.describe();
                    result.mTiles.clear();
                    return result;
                }
                result.mTiles.push_back(std::move(tile));
            }
            // Lookups below binary-search by key, so the order is made certain here and not taken on trust.
            std::sort(result.mTiles.begin(), result.mTiles.end(),
                [](const LoadedTile& lhs, const LoadedTile& rhs) { return lhs.mKey < rhs.mKey; });
            result.mUsable = true;
            return result;
        }

        // Runs on the worker thread. An exception must not travel through the future to the main thread.
        LoadResult loadPack(const std::filesystem::path& path, Terrain::Storage& live,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, const Lod::TileBakeSettings& settings)
        {
            try
            {
                return loadPackUnchecked(path, live, landCells, settings);
            }
            catch (const std::exception& e)
            {
                LoadResult failed;
                failed.mReason = std::string("an exception while loading the pack: ") + e.what();
                return failed;
            }
        }

        // The live terrain storage, except that the heights and normals of the squares that have a baked tile come
        // from the tile. Colours and blend maps are the live terrain's. Main thread only.
        class BakedStorage final : public Terrain::Storage
        {
        public:
            BakedStorage(Terrain::Storage& live, const std::vector<LoadedTile>& tiles, std::uint32_t level,
                std::uint32_t lod, std::uint8_t worldspace)
                : Terrain::Storage(live.cellWorldSize, live.cellVertices)
                , mLive(live)
                , mTiles(tiles)
                , mLevel(level)
                , mLod(lod)
                , mWorldspace(worldspace)
            {
            }

            VertexData getVertexData(int lodLevel, const Terrain::Bounds& bounds) override
            {
                VertexData data = mLive.getVertexData(lodLevel, bounds);
                if (static_cast<std::uint32_t>(lodLevel) != mLod || bounds.size != static_cast<float>(1u << mLevel))
                    return data;
                const std::optional<Lod::TileId> id
                    = Lod::tileContainingCell(mWorldspace, mLevel, static_cast<std::int32_t>(std::floor(bounds.min.x)),
                        static_cast<std::int32_t>(std::floor(bounds.min.y)));
                if (!id)
                    return data;
                const std::uint64_t key = Lod::packTileKey(*id).value_or(0);
                const auto found = std::lower_bound(mTiles.begin(), mTiles.end(), key,
                    [](const LoadedTile& tile, std::uint64_t wanted) { return tile.mKey < wanted; });
                if (found == mTiles.end() || found->mKey != key)
                    return data;
                const Lod::TerrainTile& tile = found->mData;
                const std::size_t count = std::size_t{ tile.mGridSize } * tile.mGridSize;
                if (data.heights->width() != tile.mGridSize || data.heights->height() != tile.mGridSize
                    || tile.mHeights.size() != count || !tile.mHasNormals || tile.mNormals.size() != 2 * count)
                    return data;
                const double range = static_cast<double>(tile.mMaxHeight) - tile.mMinHeight;
                float* heights = data.heights->data();
                vsg::vec4* normals = data.normals->data();
                for (std::size_t i = 0; i < count; ++i)
                {
                    heights[i]
                        = static_cast<float>(tile.mMinHeight + static_cast<double>(tile.mHeights[i]) / 65535.0 * range);
                    float x = 0, y = 0, z = 1;
                    Lod::decodeNormal(tile.mNormals[2 * i], tile.mNormals[2 * i + 1], x, y, z);
                    normals[i] = vsg::vec4(x, y, z, 0.f);
                }
                return data;
            }

            vsg::ref_ptr<vsg::Data> getBlendmap(const Terrain::Bounds& bounds) override
            {
                return mLive.getBlendmap(bounds);
            }

            std::vector<Terrain::LayerInfo> getLayers() override { return mLive.getLayers(); }

            float getHeightAt(const vsg::vec3& worldPos, ESM::RefId worldspace) override
            {
                return mLive.getHeightAt(worldPos, worldspace);
            }

        private:
            Terrain::Storage& mLive;
            const std::vector<LoadedTile>& mTiles;
            std::uint32_t mLevel;
            std::uint32_t mLod;
            std::uint8_t mWorldspace;
        };

        // Tiles built (and compiled) per frame: each needs a blend map from the live storage.
        constexpr std::size_t maxBuildsPerFrame = 4;
    }

    struct LodTerrain::State
    {
        State(float in_threshold, Terrain::Storage& in_live, vsg::ref_ptr<vsgUtil::CompileContext> in_compile,
            vsg::ref_ptr<const vsg::Options> in_imageOptions, vsg::ref_ptr<const vsg::Options> in_shaderOptions,
            vsg::ref_ptr<vsg::Sampler> in_sampler, Terrain::TerrainMapSettings in_mapSettings)
            : mThreshold(in_threshold)
            , mLive(in_live)
            , mCompile(std::move(in_compile))
            , mImageOptions(std::move(in_imageOptions))
            , mShaderOptions(std::move(in_shaderOptions))
            , mSampler(std::move(in_sampler))
            , mMapSettings(std::move(in_mapSettings))
            , mRoot(vsg::Group::create())
        {
        }

        void start(const std::filesystem::path& pack, std::vector<std::pair<std::int32_t, std::int32_t>> cells)
        {
            mSettings.mCellSize = static_cast<std::uint32_t>(mLive.cellWorldSize);
            mFuture = std::async(
                std::launch::async, [pack, cells = std::move(cells), settings = mSettings, &live = mLive]() {
                    return loadPack(pack, live, cells, settings);
                });
        }

        // Main thread, once the worker is done.
        void adopt()
        {
            mLoaded = mFuture.get();
            mReady = true;
            if (!mLoaded.mUsable)
            {
                Log(Debug::Warning) << "World LOD terrain tiles are not used: " << mLoaded.mReason;
                return;
            }
            std::uint32_t lod = 0;
            Lod::vertexStepLod(mSettings, lod);
            mTileLod = lod;
            mStorage = std::make_unique<BakedStorage>(
                mLive, mLoaded.mTiles, mSettings.mLevel, mTileLod, mSettings.mWorldspace);
            mBuilder = std::make_unique<Terrain::Builder>(mImageOptions, mShaderOptions, mSampler, mMapSettings);
            mBuilder->setStorage(mStorage.get());
            mStates.resize(mLoaded.mTiles.size());
            mNodes.resize(mLoaded.mTiles.size());
            mFailed.assign(mLoaded.mTiles.size(), false);
            for (std::size_t i = 0; i < mStates.size(); ++i)
            {
                const Lod::TerrainTile& tile = mLoaded.mTiles[i].mData;
                Lod::TileBounds& bounds = mStates[i].mBounds;
                bounds.mOriginX = tile.mOriginX;
                bounds.mOriginY = tile.mOriginY;
                bounds.mSide = tile.mSide;
                bounds.mMinHeight = tile.mMinHeight;
                bounds.mMaxHeight = tile.mMaxHeight;
                bounds.mGeometricError = tile.mGeometricError;
            }
            mCoverage.squares.reserve(mStates.size());
            Log(Debug::Info) << "World LOD terrain tiles: " << mStates.size() << " tiles of level " << mSettings.mLevel
                             << " loaded (draft layout version " << Lod::tileKeyLayoutVersion << "), threshold "
                             << mThreshold << " px.";
        }

        bool build(std::size_t index)
        {
            const Lod::TileId& id = mLoaded.mTiles[index].mId;
            std::int32_t cellX = 0;
            std::int32_t cellY = 0;
            Lod::tileOriginCell(id, cellX, cellY);
            Terrain::Bounds bounds;
            bounds.size = static_cast<float>(1u << mSettings.mLevel);
            bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
            const auto lod = static_cast<std::uint8_t>(mTileLod);
            Terrain::Builder::Batch batch = mBuilder->createBatch(bounds, lod);
            batch.leafGroup->addChild(mBuilder->createDraw(batch, bounds, lod, 0));
            vsg::ref_ptr<vsgUtil::CompileOp> compile{ new vsgUtil::CompileOp(batch.root, mCompile) };
            if (!compile->compile())
                return false;
            mRoot->addChild(batch.root);
            mNodes[index] = batch.root;
            return true;
        }

        void remove(std::size_t index)
        {
            vsg::ref_ptr<vsg::Node> node = std::move(mNodes[index]);
            mNodes[index] = {};
            auto& children = mRoot->children;
            children.erase(std::remove(children.begin(), children.end(), node), children.end());
            mCompile->detach(node);
        }

        void rebuildCoverage()
        {
            mCoverage.squares.clear();
            for (std::size_t i = 0; i < mNodes.size(); ++i)
            {
                if (!mNodes[i])
                    continue;
                std::int32_t cellX = 0;
                std::int32_t cellY = 0;
                Lod::tileOriginCell(mLoaded.mTiles[i].mId, cellX, cellY);
                Terrain::CoveredSquare square;
                square.bounds.size = static_cast<float>(1u << mSettings.mLevel);
                square.bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
                square.vertexLod = mTileLod;
                mCoverage.squares.push_back(square);
            }
            ++mCoverage.version;
        }

        float mThreshold;
        Terrain::Storage& mLive;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<const vsg::Options> mImageOptions;
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        vsg::ref_ptr<vsg::Sampler> mSampler;
        Terrain::TerrainMapSettings mMapSettings;
        Lod::TileBakeSettings mSettings;
        std::uint32_t mTileLod = 0;

        std::future<LoadResult> mFuture;
        LoadResult mLoaded;
        bool mReady = false;

        std::unique_ptr<BakedStorage> mStorage;
        std::unique_ptr<Terrain::Builder> mBuilder;
        std::vector<Lod::TileState> mStates;
        std::vector<vsg::ref_ptr<vsg::Node>> mNodes;
        std::vector<bool> mFailed;
        vsg::ref_ptr<vsg::Group> mRoot;
        Terrain::Coverage mCoverage;
        Stats mStats;
    };

    LodTerrain::LodTerrain(std::filesystem::path pack, float threshold, Terrain::Storage& live,
        std::vector<std::pair<std::int32_t, std::int32_t>> landCells, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        vsg::ref_ptr<const vsg::Options> imageOptions, vsg::ref_ptr<const vsg::Options> shaderOptions,
        vsg::ref_ptr<vsg::Sampler> sampler, Terrain::TerrainMapSettings mapSettings)
        : mState(std::make_unique<State>(threshold, live, std::move(compile), std::move(imageOptions),
            std::move(shaderOptions), std::move(sampler), std::move(mapSettings)))
    {
        mState->start(pack, std::move(landCells));
    }

    LodTerrain::~LodTerrain()
    {
        // The worker reads the live storage: wait for it before anything goes.
        if (mState->mFuture.valid())
            mState->mFuture.wait();
    }

    vsg::ref_ptr<vsg::Group> LodTerrain::node() const
    {
        return mState->mRoot;
    }

    LodTerrain::Stats LodTerrain::stats() const
    {
        return mState->mStats;
    }

    const Terrain::Coverage* LodTerrain::update(const Camera& camera)
    {
        State& state = *mState;
        if (!state.mReady)
        {
            if (!state.mFuture.valid() || state.mFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return nullptr;
            state.adopt();
            state.mStats.mReady = true;
            state.mStats.mUsable = state.mLoaded.mUsable;
            state.mStats.mTiles = state.mLoaded.mTiles.size();
        }
        if (!state.mLoaded.mUsable)
            return nullptr;

        Lod::SelectionCamera selection;
        selection.mX = camera.mEye.x;
        selection.mY = camera.mEye.y;
        selection.mZ = camera.mEye.z;
        selection.mForwardX = camera.mForward.x;
        selection.mForwardY = camera.mForward.y;
        selection.mForwardZ = camera.mForward.z;
        selection.mViewportHeight = camera.mViewportHeight;
        selection.mFovY = camera.mFovYDegrees * (3.14159265358979323846 / 180.0);
        selection.mAspect = camera.mAspect;
        selection.mViewDistance = camera.mViewDistance;
        const Lod::SelectionResult result = Lod::selectTiles(state.mStates, selection, state.mThreshold);

        bool changed = false;
        std::size_t builds = 0;
        for (std::size_t i = 0; i < state.mStates.size(); ++i)
        {
            const Lod::TileState& tile = state.mStates[i];
            if (tile.mChoice == Lod::TileChoice::Tile && !state.mNodes[i] && !state.mFailed[i] && tile.mVisible
                && builds < maxBuildsPerFrame)
            {
                ++builds;
                if (state.build(i))
                    changed = true;
                else
                {
                    state.mFailed[i] = true;
                    Log(Debug::Warning) << "World LOD tile " << i << " could not be compiled; the live terrain stays.";
                }
            }
            else if (tile.mChoice == Lod::TileChoice::Live && state.mNodes[i])
            {
                state.remove(i);
                changed = true;
            }
        }
        if (changed)
            state.rebuildCoverage();

        state.mStats.mDrawn = state.mCoverage.squares.size();
        state.mStats.mLive = result.mLive;
        state.mStats.mSkipped = result.mSkipped;
        if (changed)
        {
            Log(Debug::Verbose) << "World LOD tiles: " << state.mStats.mDrawn << " drawn, " << result.mLive
                                << " visible tiles use the live terrain, " << result.mSkipped << " outside the view, "
                                << result.mChanged << " changed their choice.";
        }
        return &state.mCoverage;
    }
}
