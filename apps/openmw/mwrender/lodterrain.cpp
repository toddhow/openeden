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
#include <components/lod/texturedecode.hpp>
#include <components/lod/tilepack.hpp>
#include <components/terrain/builder.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgutil/compileop.hpp>
#include <components/vsgutil/readimage.hpp>

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

    Lod::TerrainBakePlan terrainBakePlan()
    {
        return Lod::TerrainBakePlan{};
    }

    Lod::TileBakeSettings terrainBakeSettings(std::uint32_t cellSize)
    {
        Lod::TileBakeSettings settings;
        settings.mCellSize = cellSize;
        return settings;
    }

    // ---- the ground colour inputs ----

    namespace
    {
        // Maps what the readers give for a land texture to a CPU decode format.
        std::optional<Lod::SourceFormat> sourceFormatOf(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_SRGB:
                    return Lod::SourceFormat::Rgba8;
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_B8G8R8A8_SRGB:
                    return Lod::SourceFormat::Bgra8;
                case VK_FORMAT_R8G8B8_UNORM:
                case VK_FORMAT_R8G8B8_SRGB:
                    return Lod::SourceFormat::Rgb8;
                case VK_FORMAT_B8G8R8_UNORM:
                case VK_FORMAT_B8G8R8_SRGB:
                    return Lod::SourceFormat::Bgr8;
                case VK_FORMAT_R8_UNORM:
                case VK_FORMAT_R8_SRGB:
                    return Lod::SourceFormat::Grey8;
                case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
                case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
                case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
                case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
                    return Lod::SourceFormat::Bc1;
                case VK_FORMAT_BC2_UNORM_BLOCK:
                case VK_FORMAT_BC2_SRGB_BLOCK:
                    return Lod::SourceFormat::Bc2;
                case VK_FORMAT_BC3_UNORM_BLOCK:
                case VK_FORMAT_BC3_SRGB_BLOCK:
                    return Lod::SourceFormat::Bc3;
                default:
                    return std::nullopt;
            }
        }

        // The terrain's layers are read under the terrain's texture cap (components/terrain/builder.cpp).
        vsgUtil::ImageUse terrainLayerUse()
        {
            vsgUtil::ImageUse use;
            use.category = vsgUtil::TextureCap::Category::Terrain;
            use.kind = vsgUtil::TextureCap::Kind::Colour;
            return use;
        }
    }

    struct StorageAlbedoSource::Caches
    {
        bool mImagesRead = false;
        std::vector<Lod::Rgba8Image> mImages;
        std::vector<bool> mImageOk;
        std::vector<std::optional<EdenArchive::Hash>> mDigests;
    };

    StorageAlbedoSource::StorageAlbedoSource(Terrain::Storage& storage, std::vector<Terrain::LayerInfo> layers,
        vsg::ref_ptr<const vsg::Options> imageOptions, const VFS::Manager* vfs)
        : mStorage(storage)
        , mLayers(std::move(layers))
        , mImageOptions(std::move(imageOptions))
        , mVfs(vfs)
        , mCaches(std::make_unique<Caches>())
    {
        // The shader's layer array holds at most 256 layers (Terrain::Builder::setStorage).
        if (mLayers.size() > 256)
            mLayers.resize(256);
        mCaches->mDigests.resize(mLayers.size());
    }

    StorageAlbedoSource::~StorageAlbedoSource() = default;

    std::size_t StorageAlbedoSource::layerCount() const
    {
        return mLayers.size();
    }

    const Lod::Rgba8Image* StorageAlbedoSource::layerImage(std::size_t index) const
    {
        Caches& caches = *mCaches;
        if (!caches.mImagesRead)
        {
            caches.mImagesRead = true;
            caches.mImages.resize(mLayers.size());
            caches.mImageOk.assign(mLayers.size(), false);
            const std::vector<std::string> paths(mLayers.begin(), mLayers.end());
            const auto images = vsgUtil::readArrayImages(paths, mImageOptions, terrainLayerUse(), false);
            for (std::size_t i = 0; i < images.size() && i < mLayers.size(); ++i)
            {
                const vsg::ref_ptr<vsg::Data>& data = images[i];
                if (!data)
                    continue;
                const std::optional<Lod::SourceFormat> format = sourceFormatOf(data->properties.format);
                if (!format)
                    continue;
                const std::uint32_t width = data->width() * std::max<std::uint32_t>(1u, data->properties.blockWidth);
                const std::uint32_t height = data->height() * std::max<std::uint32_t>(1u, data->properties.blockHeight);
                const auto* bytes = static_cast<const std::uint8_t*>(data->dataPointer());
                if (Lod::decodeTexture(*format, width, height, std::span<const std::uint8_t>(bytes, data->dataSize()),
                        caches.mImages[i]))
                    caches.mImageOk[i] = true;
            }
        }
        if (index >= mLayers.size() || !caches.mImageOk[index])
            return nullptr;
        return &caches.mImages[index];
    }

    EdenArchive::Hash StorageAlbedoSource::layerDigest(std::size_t index) const
    {
        if (index >= mLayers.size())
            return {};
        std::optional<EdenArchive::Hash>& cached = mCaches->mDigests[index];
        if (cached)
            return *cached;
        EdenArchive::Hasher hasher;
        const std::string& name = mLayers[index];
        hasher.update(name.data(), name.size());
        std::string resolved = name;
        if (mImageOptions && mImageOptions->findFileCallback)
            resolved = mImageOptions->findFileCallback(name, mImageOptions.get()).string();
        if (mVfs != nullptr)
        {
            const VFS::Path::Normalized path(resolved);
            if (mVfs->exists(path))
            {
                const Files::IStreamPtr stream = mVfs->get(path);
                char buffer[16384];
                while (stream && *stream)
                {
                    stream->read(buffer, sizeof(buffer));
                    const std::streamsize got = stream->gcount();
                    if (got <= 0)
                        break;
                    hasher.update(buffer, static_cast<std::size_t>(got));
                }
            }
        }
        cached = hasher.finalize();
        return *cached;
    }

    void StorageAlbedoSource::blendMap(
        std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::vector<std::uint8_t>& indices) const
    {
        Terrain::Bounds bounds;
        bounds.size = static_cast<float>(sizeInCells);
        bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
        const vsg::ref_ptr<vsg::Data> data = mStorage.getBlendmap(bounds);
        indices.clear();
        if (!data)
            return;
        const std::size_t bytes = static_cast<std::size_t>(data->width()) * data->height() * 4;
        const auto* source = static_cast<const std::uint8_t*>(data->dataPointer());
        indices.assign(source, source + bytes);
    }

    void StorageAlbedoSource::vertexColours(
        std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::vector<std::uint8_t>& rgb) const
    {
        Terrain::Bounds bounds;
        bounds.size = static_cast<float>(sizeInCells);
        bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
        const Terrain::Storage::VertexData data = mStorage.getVertexData(0, bounds);
        const std::size_t count = static_cast<std::size_t>(data.colors->width()) * data.colors->height();
        rgb.resize(count * 3);
        for (std::size_t i = 0; i < count; ++i)
        {
            const vsg::ubvec4& colour = data.colors->data()[i];
            rgb[3 * i] = colour.r;
            rgb[3 * i + 1] = colour.g;
            rgb[3 * i + 2] = colour.b;
        }
    }

    // ---- loading and drawing ----

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
            std::vector<LoadedTile> mTiles; // in tree order: biggest level first, then key
        };

        LoadResult loadPackUnchecked(const std::filesystem::path& path, Terrain::Storage& live,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, StorageAlbedoSource& albedo,
            const Lod::TileBakeSettings& base, const Lod::TerrainBakePlan& plan)
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

            // Is the pack made from the land and the land textures this game has loaded? Work out the same tiles'
            // digests (heights, and the albedo inputs without blending) and compare.
            StorageLandSource source(live);
            Lod::TileManifest manifest;
            std::vector<Lod::BakedTile> baked;
            Lod::TileBakeSettings settings = base;
            settings.mDigestOnly = true;
            error = Lod::bakeTerrainLevels(source, &albedo, landCells, settings, plan, "Morrowind", manifest, baked);
            if (error)
            {
                result.mReason = "cannot check the pack against the land: " + error.describe();
                return result;
            }
            if (pack->manifest() != manifest || pack->sourceDigest() != Lod::contentDigest(manifest, baked))
            {
                result.mReason
                    = "the pack was made from other land, land textures or settings (stale); make a new one "
                      "with openmw --bake-lod-terrain";
                return result;
            }

            const std::vector<Lod::TileId> ordered = Lod::orderTilesForTree(pack->tileIds());
            result.mTiles.reserve(ordered.size());
            for (const Lod::TileId& id : ordered)
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
            result.mUsable = true;
            return result;
        }

        // Runs on the worker thread. An exception must not travel through the future to the main thread.
        LoadResult loadPack(const std::filesystem::path& path, Terrain::Storage& live,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& landCells, StorageAlbedoSource& albedo,
            const Lod::TileBakeSettings& base, const Lod::TerrainBakePlan& plan)
        {
            try
            {
                return loadPackUnchecked(path, live, landCells, albedo, base, plan);
            }
            catch (const std::exception& e)
            {
                LoadResult failed;
                failed.mReason = std::string("an exception while loading the pack: ") + e.what();
                return failed;
            }
        }

        // The live terrain storage, except that the heights and normals of the squares that have a baked tile come
        // from the tile, and a tile with a baked albedo is drawn with it alone. Main thread only.
        class BakedStorage final : public Terrain::Storage
        {
        public:
            BakedStorage(Terrain::Storage& live, std::vector<Terrain::LayerInfo> layers,
                const std::vector<LoadedTile>& tiles, const Lod::TileBakeSettings& base, bool useAlbedo)
                : Terrain::Storage(live.cellWorldSize, live.cellVertices)
                , mLive(live)
                , mLayers(std::move(layers))
                , mTiles(tiles)
                , mBase(base)
                , mUseAlbedo(useAlbedo)
            {
                mKeys.reserve(tiles.size());
                for (std::size_t i = 0; i < tiles.size(); ++i)
                    mKeys.emplace_back(tiles[i].mKey, i);
                std::sort(mKeys.begin(), mKeys.end());
            }

            VertexData getVertexData(int lodLevel, const Terrain::Bounds& bounds) override
            {
                VertexData data = mLive.getVertexData(lodLevel, bounds);
                const LoadedTile* found = find(bounds);
                if (found == nullptr)
                    return data;
                const Lod::TerrainTile& tile = found->mData;
                if (hasAlbedo(tile))
                {
                    // The picture (getDiffuseOverride) already holds the vertex colours, whatever happens to the rest.
                    const std::size_t colours = static_cast<std::size_t>(data.colors->width()) * data.colors->height();
                    std::fill(data.colors->data(), data.colors->data() + colours, vsg::ubvec4(255, 255, 255, 255));
                }
                std::uint32_t lod = 0;
                Lod::TileBakeSettings settings = mBase;
                settings.mLevel = found->mId.mLevel;
                if (!Lod::vertexStepLod(settings, lod) || static_cast<std::uint32_t>(lodLevel) != lod)
                    return data;
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

            vsg::ref_ptr<vsg::Data> getDiffuseOverride(const Terrain::Bounds& bounds) override
            {
                const LoadedTile* found = find(bounds);
                if (found == nullptr || !hasAlbedo(found->mData))
                    return {};
                const Lod::TerrainTile& tile = found->mData;
                auto image = vsg::ubvec4Array2D::create(tile.mAlbedoWidth, tile.mAlbedoHeight, vsg::ubvec4(),
                    vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
                std::copy(tile.mAlbedo.begin(), tile.mAlbedo.end(), static_cast<std::uint8_t*>(image->dataPointer()));
                return image;
            }

            std::vector<Terrain::LayerInfo> getLayers() override { return mLayers; }

            float getHeightAt(const vsg::vec3& worldPos, ESM::RefId worldspace) override
            {
                return mLive.getHeightAt(worldPos, worldspace);
            }

        private:
            bool hasAlbedo(const Lod::TerrainTile& tile) const
            {
                return mUseAlbedo && tile.mHasAlbedo && tile.mAlbedoFormat == Lod::AlbedoFormat::Rgba8
                    && tile.mAlbedo.size() == std::size_t{ tile.mAlbedoWidth } * tile.mAlbedoHeight * 4;
            }

            const LoadedTile* find(const Terrain::Bounds& bounds) const
            {
                // A square of 2^level cells: its level and its south-west cell name the tile.
                const float size = bounds.size;
                if (!(size >= 1.f) || size != std::floor(size))
                    return nullptr;
                std::uint32_t level = 0;
                while ((1u << level) < static_cast<std::uint32_t>(size) && level < Lod::maxLevel)
                    ++level;
                if ((1u << level) != static_cast<std::uint32_t>(size))
                    return nullptr;
                const std::optional<Lod::TileId> id = Lod::tileContainingCell(mBase.mWorldspace, level,
                    static_cast<std::int32_t>(std::floor(bounds.min.x)),
                    static_cast<std::int32_t>(std::floor(bounds.min.y)));
                if (!id)
                    return nullptr;
                const std::uint64_t key = Lod::packTileKey(*id).value_or(0);
                const auto found = std::lower_bound(mKeys.begin(), mKeys.end(), std::make_pair(key, std::size_t{ 0 }));
                if (found == mKeys.end() || found->first != key)
                    return nullptr;
                return &mTiles[found->second];
            }

            Terrain::Storage& mLive;
            std::vector<Terrain::LayerInfo> mLayers;
            const std::vector<LoadedTile>& mTiles;
            std::vector<std::pair<std::uint64_t, std::size_t>> mKeys;
            Lod::TileBakeSettings mBase;
            bool mUseAlbedo;
        };

        // Tiles built (and compiled) per frame: each needs a blend map or a picture.
        constexpr std::size_t maxBuildsPerFrame = 4;
    }

    struct LodTerrain::State
    {
        explicit State(Setup in_setup)
            : mSetup(std::move(in_setup))
            , mRoot(vsg::Group::create())
        {
        }

        void start()
        {
            mPlan = terrainBakePlan();
            mBase = terrainBakeSettings(static_cast<std::uint32_t>(mSetup.mLive->cellWorldSize));
            // The worker has its own colour source (the caches are not locked) over the same live storage.
            mWorkerAlbedo = std::make_unique<StorageAlbedoSource>(
                *mSetup.mLive, mSetup.mLayers, mSetup.mImageOptions, mSetup.mVfs);
            mFuture = std::async(std::launch::async, [this]() {
                return loadPack(mSetup.mPack, *mSetup.mLive, mSetup.mLandCells, *mWorkerAlbedo, mBase, mPlan);
            });
        }

        // Main thread, once the worker is done.
        void adopt()
        {
            mLoaded = mFuture.get();
            mWorkerAlbedo.reset();
            mReady = true;
            if (!mLoaded.mUsable)
            {
                Log(Debug::Warning) << "World LOD terrain tiles are not used: " << mLoaded.mReason;
                return;
            }
            // Baked albedo pictures draw through the terrain pipeline without its maps; with the maps on, every tile
            // uses the live layers and blend map instead.
            const bool useAlbedo = !mSetup.mMapSettings.enabled();
            mStorage = std::make_unique<BakedStorage>(*mSetup.mLive, mSetup.mLayers, mLoaded.mTiles, mBase, useAlbedo);
            mBuilder = std::make_unique<Terrain::Builder>(
                mSetup.mImageOptions, mSetup.mShaderOptions, mSetup.mSampler, mSetup.mMapSettings);
            mBuilder->setStorage(mStorage.get());

            const std::size_t count = mLoaded.mTiles.size();
            std::vector<Lod::TileId> ids;
            ids.reserve(count);
            for (const LoadedTile& tile : mLoaded.mTiles)
                ids.push_back(tile.mId);
            mParents = Lod::nearestAncestors(ids);
            mStates.resize(count);
            mNodes.resize(count);
            mBatches.resize(count);
            mFlags.assign(count, 0);
            mLods.assign(count, 0);
            mFailed.assign(count, false);
            for (std::size_t i = 0; i < count; ++i)
            {
                const Lod::TerrainTile& tile = mLoaded.mTiles[i].mData;
                Lod::TileBounds& bounds = mStates[i].mBounds;
                bounds.mOriginX = tile.mOriginX;
                bounds.mOriginY = tile.mOriginY;
                bounds.mSide = tile.mSide;
                bounds.mMinHeight = tile.mMinHeight;
                bounds.mMaxHeight = tile.mMaxHeight;
                bounds.mGeometricError = tile.mGeometricError;
                Lod::TileBakeSettings settings = mBase;
                settings.mLevel = mLoaded.mTiles[i].mId.mLevel;
                Lod::vertexStepLod(settings, mLods[i]);
            }
            mCoverage.squares.reserve(count);
            Log(Debug::Info) << "World LOD terrain tiles: " << count << " tiles of levels " << mPlan.mLevels.front()
                             << " to " << mPlan.mLevels.back() << " loaded (layout version "
                             << Lod::tileKeyLayoutVersion << "), threshold " << mSetup.mThreshold << " px, "
                             << (useAlbedo ? "baked ground pictures from level 2 up"
                                           : "live layers (terrain maps are on)")
                             << ".";
        }

        Terrain::Bounds boundsOf(std::size_t index) const
        {
            const Lod::TileId& id = mLoaded.mTiles[index].mId;
            std::int32_t cellX = 0;
            std::int32_t cellY = 0;
            Lod::tileOriginCell(id, cellX, cellY);
            Terrain::Bounds bounds;
            bounds.size = static_cast<float>(1u << id.mLevel);
            bounds.min = vsg::vec2(static_cast<float>(cellX), static_cast<float>(cellY));
            return bounds;
        }

        bool build(std::size_t index)
        {
            const Terrain::Bounds bounds = boundsOf(index);
            const auto lod = static_cast<std::uint8_t>(mLods[index]);
            Terrain::Builder::Batch batch = mBuilder->createBatch(bounds, lod);
            batch.leafGroup->addChild(mBuilder->createDraw(batch, bounds, lod, 0));
            vsg::ref_ptr<vsgUtil::CompileOp> compile{ new vsgUtil::CompileOp(batch.root, mSetup.mCompile) };
            if (!compile->compile())
                return false;
            mRoot->addChild(batch.root);
            mNodes[index] = batch.root;
            mBatches[index] = std::move(batch);
            mFlags[index] = 0;
            return true;
        }

        void remove(std::size_t index)
        {
            vsg::ref_ptr<vsg::Node> node = std::move(mNodes[index]);
            mNodes[index] = {};
            mBatches[index] = {};
            auto& children = mRoot->children;
            children.erase(std::remove(children.begin(), children.end(), node), children.end());
            mSetup.mCompile->detach(node);
        }

        // Is the tile that covers this one drawn (so that this one may go without a hole)?
        bool ancestorDrawn(std::size_t index) const
        {
            std::int32_t parent = mParents[index];
            while (parent >= 0)
            {
                const auto p = static_cast<std::size_t>(parent);
                if (mStates[p].mChoice == Lod::TileChoice::Tile)
                    return mNodes[p] != nullptr;
                parent = mParents[p];
            }
            return false;
        }

        void rebuildCoverage()
        {
            mCoverage.squares.clear();
            for (std::size_t i = 0; i < mNodes.size(); ++i)
            {
                if (!mNodes[i])
                    continue;
                Terrain::CoveredSquare square;
                square.bounds = boundsOf(i);
                square.vertexLod = mLods[i];
                mCoverage.squares.push_back(square);
            }
            ++mCoverage.version;
        }

        // A tile cannot stitch its edges to a coarser neighbour of the live terrain (those are split instead), but it
        // can to a coarser drawn tile: set each drawn tile's flags the way Paging does for live chunks.
        void updateStitching()
        {
            static const vsg::vec2 directions[4] = { { 0.f, 1.f }, { 1.f, 0.f }, { 0.f, -1.f }, { -1.f, 0.f } };
            for (std::size_t i = 0; i < mNodes.size(); ++i)
            {
                if (!mNodes[i])
                    continue;
                const Terrain::Bounds bounds = boundsOf(i);
                std::uint32_t flags = 0;
                for (std::uint32_t d = 0; d < 4; ++d)
                {
                    const vsg::vec2 point = bounds.center() + directions[d] * bounds.size;
                    for (std::size_t j = 0; j < mNodes.size(); ++j)
                    {
                        if (j == i || !mNodes[j])
                            continue;
                        const Terrain::Bounds other = boundsOf(j);
                        if (other.size > bounds.size && other.contains(point) && mLods[j] > mLods[i])
                        {
                            const std::uint32_t shift = 4 * d;
                            const std::uint32_t wanted
                                = std::max((mLods[j] - mLods[i]) << shift, flags & (15u << shift));
                            flags = (flags & ~(15u << shift)) | wanted;
                        }
                    }
                }
                if (flags == mFlags[i])
                    continue;
                const auto lod = static_cast<std::uint8_t>(mLods[i]);
                vsg::ref_ptr<vsg::Node> draw = mBuilder->createDraw(mBatches[i], bounds, lod, flags);
                vsg::ref_ptr<vsgUtil::CompileOp> compile{ new vsgUtil::CompileOp(draw, mSetup.mCompile) };
                if (!compile->compile())
                    continue;
                for (const auto& old : mBatches[i].leafGroup->children)
                    mSetup.mCompile->detach(old);
                mBatches[i].leafGroup->children = { draw };
                mFlags[i] = flags;
            }
        }

        Setup mSetup;
        Lod::TerrainBakePlan mPlan;
        Lod::TileBakeSettings mBase;

        std::unique_ptr<StorageAlbedoSource> mWorkerAlbedo;
        std::future<LoadResult> mFuture;
        LoadResult mLoaded;
        bool mReady = false;

        std::unique_ptr<BakedStorage> mStorage;
        std::unique_ptr<Terrain::Builder> mBuilder;
        std::vector<Lod::TileState> mStates;
        std::vector<std::int32_t> mParents;
        std::vector<vsg::ref_ptr<vsg::Node>> mNodes;
        std::vector<Terrain::Builder::Batch> mBatches;
        std::vector<std::uint32_t> mFlags;
        std::vector<std::uint32_t> mLods;
        std::vector<bool> mFailed;
        vsg::ref_ptr<vsg::Group> mRoot;
        Terrain::Coverage mCoverage;
        Stats mStats;
    };

    LodTerrain::LodTerrain(Setup setup)
        : mState(std::make_unique<State>(std::move(setup)))
    {
        mState->start();
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
        const Lod::SelectionResult result
            = Lod::selectTileTree(state.mStates, state.mParents, selection, state.mSetup.mThreshold);

        bool changed = false;
        std::size_t builds = 0;
        // First the tiles that are now wanted: a tile that has to go waits until what replaces it is drawn.
        for (std::size_t i = 0; i < state.mStates.size(); ++i)
        {
            const Lod::TileState& tile = state.mStates[i];
            if (tile.mChoice != Lod::TileChoice::Tile || state.mNodes[i] || state.mFailed[i] || !tile.mVisible
                || builds >= maxBuildsPerFrame)
                continue;
            ++builds;
            if (state.build(i))
                changed = true;
            else
            {
                state.mFailed[i] = true;
                Log(Debug::Warning) << "World LOD tile " << i << " could not be compiled; the live terrain stays.";
            }
        }
        for (std::size_t i = 0; i < state.mStates.size(); ++i)
        {
            if (!state.mNodes[i] || state.mStates[i].mChoice == Lod::TileChoice::Tile)
                continue;
            if (state.mStates[i].mChoice == Lod::TileChoice::Covered && !state.ancestorDrawn(i))
                continue;
            state.remove(i);
            changed = true;
        }
        if (changed)
        {
            state.rebuildCoverage();
            state.updateStitching();
        }

        state.mStats.mDrawn = state.mCoverage.squares.size();
        state.mStats.mLive = result.mLive;
        state.mStats.mSkipped = result.mSkipped;
        if (changed)
        {
            Log(Debug::Verbose) << "World LOD tiles: " << state.mStats.mDrawn << " drawn, " << result.mLive
                                << " visible tiles use the live terrain, " << result.mCovered << " covered, "
                                << result.mSkipped << " outside the view, " << result.mChanged
                                << " changed their choice.";
        }
        return &state.mCoverage;
    }
}
