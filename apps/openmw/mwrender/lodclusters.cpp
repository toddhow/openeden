#include "lodclusters.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
#include <components/lod/clusterpack.hpp>
#include <components/lod/tileselection.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/sets.hpp>
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
            vsg::ref_ptr<vsg::Node> mNode;
            std::vector<std::unique_ptr<Pipeline::Material>> mMaterials;
            float mFade = 0;
            bool mWanted = false;
            std::uint64_t mMaskVersion = 0;
            std::size_t mDraws = 0;
            std::size_t mTriangles = 0;
            // The cells of the cluster that are not masked.
            std::set<std::pair<int, int>> mFreeCells;
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

        std::set<std::pair<int, int>> mClustered;
        std::uint64_t mClusteredVersion = 0;
        Stats mStats;
        std::size_t mLastLoggedBuilt = static_cast<std::size_t>(-1);
        std::size_t mLastLoggedShown = static_cast<std::size_t>(-1);
    };

    namespace
    {
        // Runs on a worker: opens the pack and reads every header.
        OpenResult openPack(const std::filesystem::path& path)
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
            result.mOrdered = Lod::orderTilesForTree(result.mPack->tileIds());
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

        // Runs on a worker: reads and decodes one cluster and the pictures of its groups.
        Loaded loadCluster(
            const std::shared_ptr<Lod::ClusterPack>& pack, const Lod::TileId& id, vsg::ref_ptr<const vsg::Options> images)
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

        std::pair<int, int> cellOf(const Lod::ClusterCell& cell)
        {
            return { cell.mCellX, cell.mCellY };
        }
    }

    LodClusters::LodClusters(Setup setup)
        : mState(std::make_unique<State>(std::move(setup)))
    {
        const std::filesystem::path path = mState->mSetup.mPack;
        mState->mOpen = std::async(std::launch::async, [path] { return openPack(path); });
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

    const std::set<std::pair<int, int>>& LodClusters::clusteredCells() const
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
        vsg::ref_ptr<vsg::Node> buildNode(Tile& tile, const LodClusters::Setup& setup, const std::set<int>& masked)
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
                    auto sampler = setup.mBuilder->createSampler();
                    sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
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
            const vsg::dvec3 centre(cluster.mOriginX + side * 0.5, cluster.mOriginY + side * 0.5,
                (cluster.mMinZ + cluster.mMaxZ) * 0.5);
            const double radius = std::sqrt(side * side * 0.5 + 0.25 * (cluster.mMaxZ - cluster.mMinZ)
                                      * (cluster.mMaxZ - cluster.mMinZ))
                + cluster.mOverhang;
            return vsg::CullNode::create(vsg::dsphere(centre, radius), transform);
        }
    }

    void LodClusters::update(const Camera& camera, double seconds)
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

        std::size_t inFlight = 0;
        for (const Tile& tile : state.mTiles)
            if (tile.mPhase == Tile::Phase::Loading)
                ++inFlight;

        const float step = setup.mFadeSeconds > 0.f ? static_cast<float>(seconds / setup.mFadeSeconds) : 1.f;
        std::size_t builds = 0;
        for (std::size_t i = 0; i < state.mTiles.size(); ++i)
        {
            Tile& tile = state.mTiles[i];
            tile.mWanted = camera.mEnabled && state.mStates[i].mChoice == Lod::TileChoice::Tile;

            if (tile.mPhase == Tile::Phase::Idle && tile.mWanted && inFlight < maxLoadsInFlight)
            {
                tile.mPhase = Tile::Phase::Loading;
                ++inFlight;
                const std::shared_ptr<Lod::ClusterPack> pack = state.mPack;
                const Lod::TileId id = tile.mId;
                const vsg::ref_ptr<const vsg::Options> images = setup.mImageOptions;
                tile.mLoad = std::async(std::launch::async, [pack, id, images] { return loadCluster(pack, id, images); });
            }
            if (tile.mPhase == Tile::Phase::Loading && tile.mLoad.wait_for(std::chrono::seconds(0)) == std::future_status::ready
                && builds < maxBuildsPerFrame)
            {
                tile.mData = std::make_shared<Loaded>(tile.mLoad.get());
                ++state.mStats.mLoads;
                state.mStats.mLoadMillisecondsTotal += tile.mData->mMilliseconds;
                state.mStats.mLoadMillisecondsWorst = std::max(state.mStats.mLoadMillisecondsWorst, tile.mData->mMilliseconds);
                --inFlight;
                if (!tile.mData->mOk || !tile.mWanted)
                {
                    // Failed, or no longer wanted by the time it arrived: forget it (a failed one is retried when
                    // wanted again, which logs again; the pack is derived data and may be damaged).
                    tile.mData.reset();
                    tile.mPhase = Tile::Phase::Idle;
                }
                else
                {
                    const std::set<int> masked = maskedSlots(tile.mData->mCluster, state.mActive, state.mOverrides);
                    tile.mFade = 0;
                    tile.mNode = buildNode(tile, setup, masked);
                    tile.mMaskVersion = state.mMaskVersion;
                    ++builds;
                    if (tile.mNode)
                    {
                        setup.mCompile->compile(tile.mNode);
                        state.mRoot->addChild(tile.mNode);
                    }
                    tile.mFreeCells.clear();
                    for (std::size_t slot = 0; slot < tile.mData->mCluster.mCells.size(); ++slot)
                        if (!masked.contains(static_cast<int>(slot)))
                            tile.mFreeCells.insert(cellOf(tile.mData->mCluster.mCells[slot]));
                    tile.mPhase = Tile::Phase::Built;
                }
            }
            if (tile.mPhase != Tile::Phase::Built)
                continue;

            // The mask changed: rebuild the draws for the new set of cells.
            if (tile.mMaskVersion != state.mMaskVersion && builds < maxBuildsPerFrame + 1)
            {
                const std::set<int> masked = maskedSlots(tile.mData->mCluster, state.mActive, state.mOverrides);
                vsg::ref_ptr<vsg::Node> old = tile.mNode;
                tile.mNode = buildNode(tile, setup, masked);
                tile.mMaskVersion = state.mMaskVersion;
                ++builds;
                if (old)
                {
                    auto& children = state.mRoot->children;
                    children.erase(std::remove(children.begin(), children.end(), old), children.end());
                    setup.mCompile->detach(old);
                }
                if (tile.mNode)
                {
                    setup.mCompile->compile(tile.mNode);
                    state.mRoot->addChild(tile.mNode);
                }
                tile.mFreeCells.clear();
                for (std::size_t slot = 0; slot < tile.mData->mCluster.mCells.size(); ++slot)
                    if (!masked.contains(static_cast<int>(slot)))
                        tile.mFreeCells.insert(cellOf(tile.mData->mCluster.mCells[slot]));
            }

            // Dither in while wanted, out otherwise; a fade in progress runs to the end (the selection's own hysteresis
            // keeps the wanted flag from flickering).
            const float target = tile.mWanted ? 1.f : 0.f;
            const float before = tile.mFade;
            if (tile.mFade < target)
                tile.mFade = std::min(target, tile.mFade + step);
            else if (tile.mFade > target)
                tile.mFade = std::max(target, tile.mFade - step);
            if (tile.mFade != before)
                for (auto& material : tile.mMaterials)
                    material->update(material->value().specular.a, tile.mFade);

            if (!tile.mWanted && tile.mFade <= 0.f)
            {
                if (tile.mNode)
                {
                    auto& children = state.mRoot->children;
                    children.erase(std::remove(children.begin(), children.end(), tile.mNode), children.end());
                    setup.mCompile->detach(tile.mNode);
                }
                tile.mNode = {};
                tile.mMaterials.clear();
                tile.mData.reset();
                tile.mFreeCells.clear();
                tile.mPhase = Tile::Phase::Idle;
            }
        }

        // The cells whose statics the clusters draw: wanted, fully in, not masked.
        std::set<std::pair<int, int>> clustered;
        std::size_t built = 0, shown = 0, draws = 0, triangles = 0;
        for (const Tile& tile : state.mTiles)
        {
            if (tile.mPhase != Tile::Phase::Built)
                continue;
            ++built;
            if (!tile.mWanted)
                continue;
            ++shown;
            draws += tile.mDraws;
            triangles += tile.mTriangles;
            if (tile.mFade >= 1.f)
                clustered.insert(tile.mFreeCells.begin(), tile.mFreeCells.end());
        }
        if (clustered != state.mClustered)
        {
            state.mClustered = std::move(clustered);
            ++state.mClusteredVersion;
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
                             << (state.mStats.mLoads > 0 ? state.mStats.mLoadMillisecondsTotal / state.mStats.mLoads : 0.0)
                             << " ms mean, " << state.mStats.mLoadMillisecondsWorst << " ms worst";
        }
    }
}
