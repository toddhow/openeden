#include "lodclusters.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/utils/SharedObjects.h>

#include <vsg/nodes/VertexIndexDraw.h>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/clusterfade.hpp>
#include <components/lod/clusterpack.hpp>
#include <components/lod/clusterplan.hpp>
#include <components/lod/fartier.hpp>
#include <components/lod/residency.hpp>
#include <components/lod/tileselection.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/processmemory.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/shareimage.hpp>

namespace MWRender
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Tiles read and decoded at once (each on its own thread), and tiles turned into draws in one frame.
        constexpr std::size_t maxLoadsInFlight = 2;
        constexpr std::size_t maxBuildsPerFrame = 1;
        // Frames before a tile that failed to read or compile is tried again (about ten seconds).
        constexpr int retryFrames = 600;
        // Frames before a tile that the residency planner freed for a closer one may be asked for again (about one
        // second): the closer tile must have had its turn, and two tiles of about the same urgency must not trade
        // places every frame (the planner's swap margin does the rest, task 0060).
        constexpr int evictedFrames = 60;
        // What loading a tile is expected to hold before one of its level has been read, in bytes: the planner needs
        // a number to admit a load against the budget. After the first read of a level the mean of that level's
        // tiles replaces it. A starting value (stage e measured clusters of 0.2 to 4 MiB).
        constexpr std::uint64_t firstClusterEstimate = 1024ull * 1024;
        constexpr std::size_t levelSlots = 8;

        // Clusters of this level and above (levels 3 and 4: thousands of statics, hundreds of textures) get the far
        // picture cap.
        constexpr std::uint32_t farTextureMinLevel = 1;

        // Everything a worker brings back for one cluster.
        struct Loaded
        {
            Lod::ObjectCluster mCluster;
            std::vector<vsg::ref_ptr<vsg::Data>> mImages; // one per group, null when unreadable
            bool mOk = false;
            double mMilliseconds = 0;
        };

        struct OpenResult
        {
            std::shared_ptr<Lod::ClusterPack> mPack;
            std::vector<Lod::TileId> mOrdered;
            std::vector<Lod::ClusterHeaderInfo> mHeaders; // parallel to mOrdered
            std::string mError;
        };

        // The alpha test mode of a NiAlphaProperty that keeps what is above the cutoff (GL_GREATER); the baker stores
        // only the cutoff.
        constexpr std::uint32_t alphaTestGreater = 4;

        struct Tile
        {
            Lod::TileId mId;
            enum class Phase
            {
                Idle,
                Loading,
                Built,
            } mPhase
                = Phase::Idle;
            std::future<Loaded> mLoad;
            std::shared_ptr<Loaded> mData;
            std::uint64_t mBytes
                = 0; // what the built cluster holds in memory (Lod::clusterResidentBytes); valid with mData
            vsg::ref_ptr<vsg::Node> mNode;
            std::vector<std::unique_ptr<Pipeline::Material>> mMaterials;
            float mFade = 0; // the fade the materials hold (mirrors the cover tile's)
            int mCooldown = 0; // frames before a tile that failed to compile is tried again
            int mIdleFrames = 0; // frames since the tile was last wanted
            int mSuppressed = 0; // frames the planner keeps the tile unwanted after it freed it for a closer one
            bool mFreeWhenHidden = false; // the planner freed it: go as soon as the fade has ended
            bool mLoggedFailure = false;
            bool mWanted = false;
            std::uint64_t mMaskVersion = 0;
            std::set<int> mMasked; // the cell slots the draws leave out
            std::size_t mDraws = 0;
            std::size_t mTriangles = 0;
            // The slots (into the cluster's cell table) of the cells that are not masked and that the draws cover.
            std::vector<int> mFreeSlots;
        };
    }

    struct LodClusters::State
    {
        explicit State(Setup setup)
            : mSetup(std::move(setup))
        {
        }

        Setup mSetup;
        vsg::ref_ptr<vsg::Group> mRoot = vsg::Group::create();
        std::future<OpenResult> mOpen;
        bool mOpened = false;
        bool mUsable = false;
        std::shared_ptr<Lod::ClusterPack> mPack;

        std::vector<Tile> mTiles; // in tree order
        std::vector<std::int32_t> mParents;
        std::vector<Lod::TileState> mStates;

        ActiveCellGrid mActive;
        std::set<std::pair<std::uint32_t, std::uint32_t>> mOverrides; // (index, content file)
        std::uint64_t mMaskVersion = 1;
        vsg::ref_ptr<vsg::Sampler> mSampler;
        bool mClusteredDirty = false;
        bool mLoggedBudget = false;

        // What loading a tile of this level is expected to hold: the mean of the tiles of the level read so far.
        std::uint64_t estimateBytes(std::uint32_t level) const
        {
            const std::size_t slot = std::min<std::size_t>(level, levelSlots - 1);
            return mLevelCount[slot] > 0 ? mLevelBytes[slot] / mLevelCount[slot] : firstClusterEstimate;
        }

        std::vector<Lod::CoverTile> mCover; // parallel to mTiles
        // The residency planner (task 0060): its input and output are reused every frame.
        std::vector<Lod::ResidencyItem> mItems;
        Lod::ResidencyPlan mPlan;
        std::array<std::uint64_t, levelSlots> mLevelBytes{}; // sum of the bytes of the tiles read, per level
        std::array<std::uint32_t, levelSlots> mLevelCount{};
        LodClusters::ClusteredCells mClustered;
        std::uint64_t mClusteredVersion = 0;
        Stats mStats;
        std::size_t mLastLoggedBuilt = static_cast<std::size_t>(-1);
        std::size_t mLastLoggedShown = static_cast<std::size_t>(-1);
    };

    namespace
    {
        // Runs on a worker: opens the pack and reads every header.
        OpenResult openPackUnchecked(const std::filesystem::path& path, std::uint32_t maxLevel)
        {
            OpenResult result;
            Lod::LodError error;
            std::unique_ptr<Lod::ClusterPack> pack = Lod::ClusterPack::open(path, error);
            if (!pack)
            {
                result.mError = error.describe();
                return result;
            }
            result.mPack = std::move(pack);
            std::vector<Lod::TileId> ids = result.mPack->tileIds();
            if (maxLevel > 0)
            {
                // `far levels` (task 0066 stage e): the clusters above it are not read, drawn or kept.
                ids.erase(std::remove_if(ids.begin(), ids.end(),
                              [maxLevel](const Lod::TileId& id) { return id.mLevel > maxLevel; }),
                    ids.end());
            }
            result.mOrdered = Lod::orderTilesForTree(std::move(ids));
            for (const Lod::TileId& id : result.mOrdered)
            {
                Lod::ClusterHeaderInfo info;
                error = result.mPack->readHeader(id, info);
                if (error)
                {
                    result.mError = "cluster header: " + error.describe();
                    result.mPack.reset();
                    return result;
                }
                result.mHeaders.push_back(info);
            }
            return result;
        }

        // A worker must not let an exception reach the frame loop through future::get().
        OpenResult openPack(const std::filesystem::path& path, std::uint32_t maxLevel)
        {
            try
            {
                return openPackUnchecked(path, maxLevel);
            }
            catch (const std::exception& e)
            {
                OpenResult result;
                result.mError = e.what();
                return result;
            }
        }

        // Runs on a worker: reads and decodes one cluster and the pictures of its groups.
        Loaded loadClusterUnchecked(const std::shared_ptr<Lod::ClusterPack>& pack, const Lod::TileId& id,
            vsg::ref_ptr<const vsg::Options> images, int textureCap)
        {
            Loaded loaded;
            const auto start = Clock::now();
            EdenArchive::DecodeBudget budget{ std::uint64_t{ 512 } << 20 };
            if (const Lod::LodError error = pack->readCluster(id, loaded.mCluster, &budget))
            {
                Log(Debug::Warning) << "LOD clusters: cannot read a cluster: " << error.describe();
                return loaded;
            }
            vsgUtil::ImageUse use;
            use.category = vsgUtil::TextureCap::Category::Object;
            use.kind = vsgUtil::TextureCap::Kind::Colour;
            use.maxSize = textureCap;
            for (const Lod::ClusterGroup& group : loaded.mCluster.mGroups)
            {
                vsgUtil::ImageUse groupUse = use;
                if ((group.mFlags & Lod::clusterFlagAlphaTest) != 0)
                    groupUse.alphaCutoff = group.mAlphaCutoff;
                loaded.mImages.push_back(vsgUtil::readOptionalImage(group.mTexture, images, groupUse));
            }
            loaded.mOk = true;
            loaded.mMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            return loaded;
        }

        Loaded loadCluster(const std::shared_ptr<Lod::ClusterPack>& pack, const Lod::TileId& id,
            vsg::ref_ptr<const vsg::Options> images, int textureCap)
        {
            try
            {
                return loadClusterUnchecked(pack, id, images, textureCap);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "LOD clusters: cannot load a cluster: " << e.what();
                return Loaded{};
            }
        }

        std::pair<int, int> cellOf(const Lod::ClusterCell& cell)
        {
            return { cell.mCellX, cell.mCellY };
        }
    }

    LodClusters::LodClusters(Setup setup)
        : mState(std::make_unique<State>(std::move(setup)))
    {
        const std::filesystem::path path = mState->mSetup.mPack;
        const std::uint32_t maxLevel = mState->mSetup.mMaxLevel;
        mState->mOpen = std::async(std::launch::async, [path, maxLevel] { return openPack(path, maxLevel); });
    }

    LodClusters::~LodClusters()
    {
        // Workers hold the pack; wait for every one before the pack goes.
        if (mState->mOpen.valid())
            mState->mOpen.wait();
        for (Tile& tile : mState->mTiles)
            if (tile.mLoad.valid())
                tile.mLoad.wait();
        for (Tile& tile : mState->mTiles)
            if (tile.mNode)
                mState->mSetup.mCompile->detach(tile.mNode);
    }

    vsg::ref_ptr<vsg::Group> LodClusters::node() const
    {
        return mState->mRoot;
    }

    void LodClusters::setActiveGrid(const ActiveCellGrid& grid)
    {
        State& state = *mState;
        if (state.mActive.centre != grid.centre || state.mActive.halfGrid != grid.halfGrid)
        {
            state.mActive = grid;
            ++state.mMaskVersion;
        }
    }

    void LodClusters::overrideReference(ESM::RefNum ref)
    {
        State& state = *mState;
        if (state.mOverrides.emplace(ref.mIndex, static_cast<std::uint32_t>(ref.mContentFile)).second)
            ++state.mMaskVersion;
    }

    void LodClusters::clearOverrides()
    {
        State& state = *mState;
        if (!state.mOverrides.empty())
        {
            state.mOverrides.clear();
            ++state.mMaskVersion;
        }
    }

    const LodClusters::ClusteredCells& LodClusters::clusteredCells() const
    {
        return mState->mClustered;
    }

    std::uint64_t LodClusters::clusteredCellsVersion() const
    {
        return mState->mClusteredVersion;
    }

    LodClusters::Stats LodClusters::stats() const
    {
        return mState->mStats;
    }

    namespace
    {
        // The cells of a cluster that are masked: inside the active grid, or listing a reference that was overridden.
        std::set<int> maskedSlots(const Lod::ObjectCluster& cluster, const ActiveCellGrid& active,
            const std::set<std::pair<std::uint32_t, std::uint32_t>>& overrides)
        {
            std::set<int> masked;
            for (std::size_t slot = 0; slot < cluster.mCells.size(); ++slot)
            {
                const Lod::ClusterCell& cell = cluster.mCells[slot];
                if (active.contains(cellOf(cell)))
                {
                    masked.insert(static_cast<int>(slot));
                    continue;
                }
                if (overrides.empty())
                    continue;
                for (std::uint32_t k = 0; k < cell.mRefCount; ++k)
                {
                    const Lod::ClusterRef& ref = cluster.mRefs[cell.mFirstRef + k];
                    if (overrides.contains({ ref.mIndex, ref.mContentFile }))
                    {
                        masked.insert(static_cast<int>(slot));
                        break;
                    }
                }
            }
            return masked;
        }

        // Builds the draws of one cluster for the cells that are not masked. Main thread.
        vsg::ref_ptr<vsg::Node> buildNode(Tile& tile, const LodClusters::Setup& setup, const std::set<int>& masked,
            vsg::ref_ptr<vsg::Sampler>& sampler)
        {
            const Lod::ObjectCluster& cluster = tile.mData->mCluster;
            tile.mMaterials.clear();
            tile.mDraws = 0;
            tile.mTriangles = 0;

            constexpr std::uint32_t noVertex = 0xFFFFFFFFu;
            std::vector<std::uint32_t> remap(cluster.mVertices.size(), noVertex);
            auto group = vsg::Group::create();
            for (std::size_t g = 0; g < cluster.mGroups.size(); ++g)
            {
                const Lod::ClusterGroup& source = cluster.mGroups[g];
                std::vector<std::uint32_t> indices;
                for (const Lod::ClusterRange& range : cluster.mRanges)
                {
                    if (range.mGroup != g || masked.contains(static_cast<int>(range.mCellSlot)))
                        continue;
                    indices.insert(indices.end(), cluster.mIndices.begin() + range.mFirstIndex,
                        cluster.mIndices.begin() + range.mFirstIndex + range.mIndexCount);
                }
                if (indices.empty())
                    continue;

                Pipeline::Options options;
                options.shader.addMode(Pipeline::Mode::VERTEX);
                options.shader.addMode(Pipeline::Mode::NORMAL);
                options.shader.addMode(Pipeline::Mode::COLOR);
                options.shader.addMode(Pipeline::Mode::TEXCOORD);
                options.shader.addMode(Pipeline::Mode::MATERIAL);
                options.shader.addMode(Pipeline::Mode::LOD_FADE);
                options.numUvSets = 1;
                options.colorMode = 2; // ambient and diffuse from the vertex colour, as most Morrowind models
                if ((source.mFlags & Lod::clusterFlagAlphaTest) != 0)
                    options.alphaTestMode = alphaTestGreater;
                if ((source.mFlags & Lod::clusterFlagTwoSided) != 0)
                    options.cullMode = VK_CULL_MODE_NONE;

                auto material = std::make_unique<Pipeline::Material>();
                if ((source.mFlags & Lod::clusterFlagAlphaTest) != 0)
                    material->value().alphaTestCutoff = source.mAlphaCutoff;
                material->value().specular.a = tile.mFade;
                vsg::Descriptors descriptors{ material->descriptor() };

                if (g < tile.mData->mImages.size() && tile.mData->mImages[g])
                {
                    options.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
                    if (!sampler)
                    {
                        sampler = setup.mBuilder->createSampler();
                        sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                        sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    }
                    descriptors.push_back(vsgUtil::sharedDescriptorImage(setup.mImageOptions->sharedObjects, sampler,
                        tile.mData->mImages[g], Pipeline::Descriptors::DIFFUSE_UNIT));
                }

                auto bindPipeline = setup.mBuilder->graphics->getOrCreate(options);
                vsg::ref_ptr<vsg::PipelineLayout> layout = bindPipeline->pipeline->layout;
                auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
                auto bindDescriptors = vsg::BindDescriptorSet::create(
                    VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);

                // Only the vertices this group's triangles use, in their own arrays (one buffer per array for each
                // draw; arrays shared between draws made the vertices of later draws come out wrong).
                std::vector<std::uint32_t> used;
                for (std::uint32_t& index : indices)
                {
                    if (remap[index] == noVertex)
                    {
                        remap[index] = static_cast<std::uint32_t>(used.size());
                        used.push_back(index);
                    }
                    index = remap[index];
                }
                const auto count = static_cast<std::uint32_t>(used.size());
                auto positions = vsg::vec3Array::create(count);
                auto normals = vsg::vec3Array::create(count);
                auto colours = vsg::vec4Array::create(count);
                auto uvs = vsg::vec2Array::create(count);
                for (std::uint32_t k = 0; k < count; ++k)
                {
                    const Lod::ClusterVertex& v = cluster.mVertices[used[k]];
                    (*positions)[k] = vsg::vec3(v.mPosition[0], v.mPosition[1], v.mPosition[2]);
                    const vsg::vec3 n(v.mNormal[0] / 127.f, v.mNormal[1] / 127.f, v.mNormal[2] / 127.f);
                    const float length = vsg::length(n);
                    (*normals)[k] = length > 1e-4f ? n / length : vsg::vec3(0, 0, 1);
                    (*colours)[k]
                        = vsg::vec4(v.mColor[0] / 255.f, v.mColor[1] / 255.f, v.mColor[2] / 255.f, v.mColor[3] / 255.f);
                    (*uvs)[k] = vsg::vec2(v.mUv[0], v.mUv[1]);
                }
                for (const std::uint32_t old : used)
                    remap[old] = noVertex;

                auto indexArray = vsg::uintArray::create(static_cast<std::uint32_t>(indices.size()));
                std::copy(indices.begin(), indices.end(), indexArray->begin());
                auto draw = vsg::VertexIndexDraw::create();
                draw->assignArrays(vsg::DataList{ positions, normals, colours, uvs });
                draw->assignIndices(indexArray);
                draw->indexCount = static_cast<std::uint32_t>(indices.size());
                draw->instanceCount = 1;

                auto stateGroup = vsg::StateGroup::create();
                stateGroup->stateCommands = { bindPipeline, bindDescriptors };
                stateGroup->children = { draw };
                group->addChild(stateGroup);
                tile.mMaterials.push_back(std::move(material));
                ++tile.mDraws;
                tile.mTriangles += indices.size() / 3;
            }
            if (group->children.empty())
                return {};

            const double side = cluster.mSide;
            auto transform = vsg::MatrixTransform::create(
                vsg::translate(static_cast<double>(cluster.mOriginX), static_cast<double>(cluster.mOriginY), 0.0));
            transform->addChild(group);
            const vsg::dvec3 centre(
                cluster.mOriginX + side * 0.5, cluster.mOriginY + side * 0.5, (cluster.mMinZ + cluster.mMaxZ) * 0.5);
            const double radius = std::sqrt(side * side * 0.5
                                      + 0.25 * (cluster.mMaxZ - cluster.mMinZ) * (cluster.mMaxZ - cluster.mMinZ))
                + cluster.mOverhang;
            return vsg::CullNode::create(vsg::dsphere(centre, radius), transform);
        }
    }

    namespace
    {
        // Builds the draws of a tile for the cells that are not masked, compiles them and adds them to the scene. False
        // when the compile failed: the tile then covers no cell, so the live objects keep every static. A tile whose
        // every cell is masked has no draws and succeeds.
        bool attachNode(Tile& tile, const LodClusters::Setup& setup, const std::set<int>& masked,
            vsg::ref_ptr<vsg::Sampler>& sampler, vsg::Group& root)
        {
            tile.mNode = buildNode(tile, setup, masked, sampler);
            tile.mFreeSlots.clear();
            if (tile.mNode)
            {
                if (!setup.mCompile->compile(tile.mNode))
                {
                    tile.mNode = {};
                    tile.mMaterials.clear();
                    return false;
                }
                root.addChild(tile.mNode);
                for (std::size_t slot = 0; slot < tile.mData->mCluster.mCells.size(); ++slot)
                    if (!masked.contains(static_cast<int>(slot)))
                        tile.mFreeSlots.push_back(static_cast<int>(slot));
            }
            return true;
        }

        // A tile that could not be drawn: forget it (its cells stay live), log once, and try again later.
        void dropFailedTile(Tile& tile, Lod::CoverTile& cover)
        {
            if (!tile.mLoggedFailure)
            {
                tile.mLoggedFailure = true;
                Log(Debug::Warning)
                    << "LOD clusters: a cluster could not be compiled; the live objects keep its cells.";
            }
            tile.mNode = {};
            tile.mMaterials.clear();
            tile.mData.reset();
            tile.mFreeSlots.clear();
            tile.mFade = 0;
            tile.mCooldown = retryFrames;
            tile.mPhase = Tile::Phase::Idle;
            cover = {};
        }
    }

    void LodClusters::update(const Camera& camera, double seconds, const CellPredicate& liveReady)
    {
        State& state = *mState;
        const Setup& setup = state.mSetup;

        if (!state.mOpened)
        {
            if (state.mOpen.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            state.mOpened = true;
            OpenResult result = state.mOpen.get();
            state.mStats.mReady = true;
            if (!result.mPack)
            {
                Log(Debug::Warning) << "LOD clusters: " << setup.mPack.string() << " is not usable: " << result.mError
                                    << " (make one with openmw --bake-lod-objects); the live objects draw everything.";
                return;
            }
            state.mPack = result.mPack;
            state.mParents = Lod::nearestAncestors(result.mOrdered);
            for (std::size_t i = 0; i < result.mOrdered.size(); ++i)
            {
                Tile tile;
                tile.mId = result.mOrdered[i];
                state.mTiles.push_back(std::move(tile));
                Lod::TileState tileState;
                const Lod::ClusterHeaderInfo& header = result.mHeaders[i];
                tileState.mBounds.mOriginX = header.mOriginX;
                tileState.mBounds.mOriginY = header.mOriginY;
                tileState.mBounds.mSide = header.mSide;
                tileState.mBounds.mMinHeight = header.mMinZ;
                tileState.mBounds.mMaxHeight = header.mMaxZ;
                tileState.mBounds.mGeometricError = header.mGeometricError;
                state.mStates.push_back(tileState);
            }
            state.mCover.assign(state.mTiles.size(), Lod::CoverTile{});
            state.mUsable = true;
            state.mStats.mUsable = true;
            state.mStats.mTiles = state.mTiles.size();
            Log(Debug::Info) << "LOD clusters: " << state.mTiles.size() << " clusters in " << setup.mPack.string();
        }
        if (!state.mUsable)
            return;

        // Which clusters are wanted.
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
        Lod::selectTileTree(state.mStates, state.mParents, selection, setup.mThreshold);

        // What the built clusters hold, against the budget of the tier (task 0066 stage e).
        std::uint64_t residentBytes = 0;
        for (const Tile& tile : state.mTiles)
        {
            if (tile.mData)
            {
                residentBytes += tile.mBytes;
            }
        }

        // The residency planner decides what is loaded and freed (task 0060): the clusters the selection wants load
        // nearest first and within the memory budget of the tier, a closer cluster may take the room of a farther one,
        // and what the camera has left goes at once. A cluster freed while it is still wanted leaves its cells to the
        // live objects; the hand-over below fades it out first, so no frame has a hole.
        const std::size_t tileCount = state.mTiles.size();
        state.mItems.resize(tileCount);
        for (std::size_t i = 0; i < tileCount; ++i)
        {
            Tile& tile = state.mTiles[i];
            const Lod::CoverTile& cover = state.mCover[i];
            if (tile.mCooldown > 0)
                --tile.mCooldown;
            const bool suppressed = tile.mSuppressed > 0;
            if (suppressed)
                --tile.mSuppressed;
            tile.mWanted = camera.mEnabled && state.mStates[i].mChoice == Lod::TileChoice::Tile && !suppressed;
            tile.mIdleFrames = tile.mWanted ? 0 : std::min(tile.mIdleFrames + 1, 1 << 20);
            if (tile.mWanted)
                tile.mFreeWhenHidden = false;
            const bool built = tile.mPhase == Tile::Phase::Built;
            Lod::ResidencyItem& item = state.mItems[i];
            item = Lod::ResidencyItem{};
            item.mHeld = built;
            item.mLoading = tile.mPhase == Tile::Phase::Loading;
            item.mWanted = tile.mWanted;
            item.mPinned = built && !tile.mWanted && cover.mFade.mPhase != Lod::FadePhase::Hidden;
            item.mIdleFrames = tile.mIdleFrames;
            item.mCooldown = tile.mCooldown;
            if (built || item.mLoading)
                item.mBytes = tile.mBytes > 0 ? tile.mBytes : state.estimateBytes(tile.mId.mLevel);
            else
                item.mBytes = state.estimateBytes(tile.mId.mLevel);
            item.mPriority = static_cast<float>(Lod::distanceToTile(selection, state.mStates[i].mBounds));
        }
        Lod::ResidencyParams params;
        params.mBudgetBytes = setup.mResidentBudgetBytes;
        params.mKeepFrames = 0; // a cluster that is not wanted keeps nothing: re-reading is the price of coming back
        params.mMaxLoadsInFlight = static_cast<std::uint32_t>(maxLoadsInFlight);
        params.mMaxLoadsPerCall = static_cast<std::uint32_t>(maxLoadsInFlight);
        Lod::planResidency(state.mItems, params, state.mPlan);
        state.mStats.mBlocked = state.mPlan.mBlocked;
        if (state.mPlan.mBlocked > 0 && !state.mLoggedBudget)
        {
            state.mLoggedBudget = true;
            Log(Debug::Info) << "LOD clusters: the memory budget of " << (setup.mResidentBudgetBytes >> 20)
                             << " MiB is reached; the farther clusters wait or give way to closer ones.";
        }
        for (const std::uint32_t index : state.mPlan.mFree)
        {
            Tile& tile = state.mTiles[index];
            if (tile.mWanted)
            {
                // Freed for a closer cluster (or because the budget shrank): the live objects take its cells back
                // once the cluster has faded out, and it is not asked for again for a while.
                tile.mWanted = false;
                tile.mSuppressed = evictedFrames;
                tile.mCooldown = 2 * evictedFrames;
            }
            tile.mFreeWhenHidden = true;
        }
        for (const std::uint32_t index : state.mPlan.mLoad)
        {
            Tile& tile = state.mTiles[index];
            tile.mPhase = Tile::Phase::Loading;
            const std::shared_ptr<Lod::ClusterPack> pack = state.mPack;
            const Lod::TileId id = tile.mId;
            const vsg::ref_ptr<const vsg::Options> images = setup.mImageOptions;
            // The far levels gather many textures that are seen from far: their pictures are capped (task 0066
            // stage e).
            const int textureCap = id.mLevel >= farTextureMinLevel ? setup.mFarTextureMaxSize : 0;
            tile.mLoad = std::async(std::launch::async,
                [pack, id, images, textureCap] { return loadCluster(pack, id, images, textureCap); });
        }

        std::size_t builds = 0;
        for (std::size_t i = 0; i < state.mTiles.size(); ++i)
        {
            Tile& tile = state.mTiles[i];
            Lod::CoverTile& cover = state.mCover[i];

            if (tile.mPhase == Tile::Phase::Loading
                && tile.mLoad.wait_for(std::chrono::seconds(0)) == std::future_status::ready
                && builds < maxBuildsPerFrame)
            {
                tile.mData = std::make_shared<Loaded>(tile.mLoad.get());
                ++state.mStats.mLoads;
                state.mStats.mLoadMillisecondsTotal += tile.mData->mMilliseconds;
                state.mStats.mLoadMillisecondsWorst
                    = std::max(state.mStats.mLoadMillisecondsWorst, tile.mData->mMilliseconds);
                if (tile.mData->mOk)
                {
                    // The planner's estimates for the next tile of this level.
                    const std::uint64_t bytes = Lod::clusterResidentBytes(
                        tile.mData->mCluster.mVertices.size(), tile.mData->mCluster.mIndices.size());
                    const std::size_t slot = std::min<std::size_t>(tile.mId.mLevel, levelSlots - 1);
                    state.mLevelBytes[slot] += bytes;
                    ++state.mLevelCount[slot];
                    tile.mBytes = bytes;
                }
                if (!tile.mData->mOk || !tile.mWanted)
                {
                    // Failed, or no longer wanted by the time it arrived: forget it (a failed read logs and waits a
                    // while: the pack is derived data and may be damaged).
                    if (!tile.mData->mOk)
                        tile.mCooldown = retryFrames;
                    tile.mBytes = 0;
                    tile.mData.reset();
                    tile.mPhase = Tile::Phase::Idle;
                }
                else
                {
                    residentBytes += tile.mBytes;
                    const std::set<int> masked = maskedSlots(tile.mData->mCluster, state.mActive, state.mOverrides);
                    tile.mFade = 0;
                    tile.mMaskVersion = state.mMaskVersion;
                    tile.mMasked = masked;
                    ++builds;
                    if (attachNode(tile, setup, masked, state.mSampler, *state.mRoot))
                        tile.mPhase = Tile::Phase::Built;
                    else
                        dropFailedTile(tile, cover);
                }
            }
            if (tile.mPhase != Tile::Phase::Built)
                continue;

            // The mask changed: rebuild the draws for the new set of cells (only when this tile's own mask changed).
            if (tile.mMaskVersion != state.mMaskVersion && builds < maxBuildsPerFrame + 1)
            {
                const std::set<int> masked = maskedSlots(tile.mData->mCluster, state.mActive, state.mOverrides);
                tile.mMaskVersion = state.mMaskVersion;
                if (masked != tile.mMasked)
                {
                    vsg::ref_ptr<vsg::Node> old = tile.mNode;
                    tile.mMasked = masked;
                    ++builds;
                    state.mClusteredDirty = true;
                    if (old)
                    {
                        auto& children = state.mRoot->children;
                        children.erase(std::remove(children.begin(), children.end(), old), children.end());
                        setup.mCompile->detach(old);
                    }
                    if (!attachNode(tile, setup, masked, state.mSampler, *state.mRoot))
                    {
                        dropFailedTile(tile, cover);
                        continue;
                    }
                }
            }

            // What the hand-over sees of this tile: wanted, and the cells whose statics its draws cover (none when it
            // has no draws).
            cover.mWanted = tile.mWanted;
            cover.mCells.clear();
            if (tile.mNode)
                for (const int slot : tile.mFreeSlots)
                    cover.mCells.push_back(cellOf(tile.mData->mCluster.mCells[static_cast<std::size_t>(slot)]));
        }

        // The hand-over order is Lod::stepCoverage's: a cluster dithers in over the live objects, is solid while they
        // leave out its statics, and when it is no longer wanted stays solid until they draw the statics again (or
        // another solid cluster covers the cell).
        static const Lod::LiveReadyFn alwaysReady = [](const Lod::Cell&) { return true; };
        const Lod::LiveReadyFn& ready = liveReady ? liveReady : alwaysReady;
        if (Lod::stepCoverage(state.mCover, static_cast<float>(seconds), setup.mFadeSeconds, ready))
            state.mClusteredDirty = true;

        std::size_t built = 0, shown = 0, draws = 0, triangles = 0;
        for (std::size_t i = 0; i < state.mTiles.size(); ++i)
        {
            Tile& tile = state.mTiles[i];
            const Lod::CoverTile& cover = state.mCover[i];
            if (tile.mPhase != Tile::Phase::Built)
                continue;
            ++built;
            if (tile.mFade != cover.mFade.mFade)
            {
                tile.mFade = cover.mFade.mFade;
                for (auto& material : tile.mMaterials)
                    material->update(material->value().specular.a, tile.mFade);
            }
            if (tile.mWanted)
            {
                ++shown;
                draws += tile.mDraws;
                triangles += tile.mTriangles;
            }
            if (!tile.mWanted && cover.mFade.mPhase == Lod::FadePhase::Hidden && tile.mFreeWhenHidden)
            {
                tile.mFreeWhenHidden = false;
                tile.mBytes = 0;
                if (tile.mNode)
                {
                    auto& children = state.mRoot->children;
                    children.erase(std::remove(children.begin(), children.end(), tile.mNode), children.end());
                    setup.mCompile->detach(tile.mNode);
                }
                tile.mNode = {};
                tile.mMaterials.clear();
                tile.mData.reset();
                tile.mFreeSlots.clear();
                state.mCover[i] = {};
                tile.mFade = 0;
                tile.mPhase = Tile::Phase::Idle;
            }
        }

        // The cells the clusters cover, with the references each draws: worked out again only when a tile became or
        // stopped being Solid or a mask changed a Solid tile's cells, not every frame.
        if (state.mClusteredDirty)
        {
            state.mClusteredDirty = false;
            LodClusters::ClusteredCells clustered;
            for (std::size_t i = 0; i < state.mTiles.size(); ++i)
            {
                const Tile& tile = state.mTiles[i];
                if (tile.mPhase != Tile::Phase::Built || state.mCover[i].mFade.mPhase != Lod::FadePhase::Solid
                    || !tile.mNode)
                    continue;
                const Lod::ObjectCluster& cluster = tile.mData->mCluster;
                for (const int slot : tile.mFreeSlots)
                {
                    const Lod::ClusterCell& cell = cluster.mCells[static_cast<std::size_t>(slot)];
                    auto& refs = clustered[cellOf(cell)];
                    std::vector<Lod::ClusterRef> merged;
                    if (refs)
                        merged = *refs;
                    merged.insert(merged.end(), cluster.mRefs.begin() + cell.mFirstRef,
                        cluster.mRefs.begin() + cell.mFirstRef + cell.mRefCount);
                    std::sort(merged.begin(), merged.end());
                    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
                    refs = std::make_shared<const std::vector<Lod::ClusterRef>>(std::move(merged));
                }
            }
            const bool same = clustered.size() == state.mClustered.size()
                && std::equal(clustered.begin(), clustered.end(), state.mClustered.begin(),
                    [](const auto& a, const auto& b) { return a.first == b.first && *a.second == *b.second; });
            if (!same)
            {
                state.mClustered = std::move(clustered);
                ++state.mClusteredVersion;
            }
        }
        state.mStats.mBuilt = built;
        state.mStats.mShown = shown;
        state.mStats.mDraws = draws;
        state.mStats.mTriangles = triangles;
        if (built != state.mLastLoggedBuilt || shown != state.mLastLoggedShown)
        {
            state.mLastLoggedBuilt = built;
            state.mLastLoggedShown = shown;
            Log(Debug::Info) << "LOD clusters: " << shown << " shown, " << built << " built, " << draws << " draws, "
                             << triangles << " triangles; " << state.mStats.mLoads << " loads, "
                             << (state.mStats.mLoads > 0 ? state.mStats.mLoadMillisecondsTotal / state.mStats.mLoads
                                                         : 0.0)
                             << " ms mean, " << state.mStats.mLoadMillisecondsWorst << " ms worst"
                             << "; about " << (residentBytes >> 20) << " MiB held, working set "
                             << (Render::processWorkingSetBytes() >> 20) << " MiB";
        }
    }
}
