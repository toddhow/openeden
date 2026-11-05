#ifndef OPENMW_MWRENDER_LODCLUSTERS_H
#define OPENMW_MWRENDER_LODCLUSTERS_H

// OpenEden world LOD (task 0059, stage d): baked object clusters drawn for the far field, behind
// `[WorldLod] object clusters` (and `enabled`). DRAFT cluster layout version 0 (components/lod), not approved.
//
// What it does, in plain words. At start a worker thread opens <user data>/openeden/lod/objects.edenpak and reads the
// 104-byte header of every cluster (where it is, how big, its error). Each frame, on the main thread, the same
// pixel-error rule as the terrain tiles (Lod::selectTileTree) chooses which clusters to draw. A chosen cluster is
// read, decoded and its textures read on a worker thread, then turned into draws on the main thread (a few a frame)
// and dithered in over the live objects; a cluster that is no longer chosen dithers out and is dropped. The cells a
// cluster covers are reported (clusteredCells) so that DistantObjects stops drawing their statics once the cluster is
// fully in; a cell that is inside the active grid, or holds a reference a script or mod disabled or moved, is masked:
// its part of the cluster is not drawn and the live objects keep it.
//
// Off means unchanged: RenderManager makes this only with the setting on, and nothing here runs otherwise.
//
// Threads: workers read the pack and the textures only (like DistantObjects' worker). The scene graph, the pipelines,
// compile and detach run on the main thread.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <vsg/io/Options.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/esm3/refnum.hpp>
#include <components/lod/objectcluster.hpp>
#include <components/lod/residency.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "distantobjectcells.hpp"

namespace vsg
{
    class Options;
}
namespace Pipeline
{
    class Builder;
}

namespace MWRender
{
    class LodClusters
    {
    public:
        struct Camera
        {
            vsg::dvec3 mEye;
            vsg::dvec3 mForward;
            double mFovYDegrees = 60;
            double mAspect = 16.0 / 9.0;
            double mViewportHeight = 720;
            double mViewDistance = 7168;
            /// How far the live objects page (the live view distance): a cluster wholly inside it can give its cells
            /// back to them, one reaching past it cannot (task 0060).
            double mPagingRange = 7168;
            /// The live distant objects exist (distant terrain and object paging are on): without them nothing else
            /// draws a cluster's statics, so no cluster is ever freed while wanted (task 0060, second review).
            bool mLiveObjectsPage = true;
            /// False in interiors and other worldspaces: every cluster fades out.
            bool mEnabled = true;
            /// The drawn terrain tiles (LodTerrain::drawnGround) and their version: with Setup::mEvictFar a cluster
            /// past the paging range is replaceable when the ground beneath it is drawn (task 0060, far cluster
            /// eviction). Empty: no ground is known, so no cluster is evicted that way.
            std::span<const Lod::GroundSquare> mGround;
            std::uint64_t mGroundVersion = 0;
        };

        struct Setup
        {
            std::filesystem::path mPack;
            float mThreshold = 8;
            float mFadeSeconds = 0.4f;
            /// The longest side of the pictures of the clusters of levels 3 and 4 (the far ones); 0: no further cap.
            int mFarTextureMaxSize = 0;
            /// What the built clusters may hold in memory, in bytes (the tier's budget); 0: no budget.
            std::uint64_t mResidentBudgetBytes = 0;
            /// The highest cluster level read and drawn (`[WorldLod] far levels`); 0: every level of the pack.
            std::uint32_t mMaxLevel = 0;
            /// The tier evicts far clusters (FarTierSettings::mEvictFarClusters): under the budget, a cluster past the
            /// live paging range may be freed when the ground beneath it is drawn. Used only with a budget.
            bool mEvictFar = false;
            double mCellSize = 8192;
            const Pipeline::Builder* mBuilder = nullptr;
            vsg::ref_ptr<const vsg::Options> mImageOptions;
            vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        };
        explicit LodClusters(Setup setup);
        ~LodClusters();

        /// The group to put in the scene beside the live objects.
        vsg::ref_ptr<vsg::Group> node() const;

        /// Main thread, once a frame; `seconds` is the frame time. `liveReady(cell)` says whether the live objects draw
        /// the cell's statics (DistantObjects::staticsReady): a cluster stays solid until it is true for its cells.
        using CellPredicate = std::function<bool(const std::pair<int, int>&)>;
        void update(const Camera& camera, double seconds, const CellPredicate& liveReady);

        /// Task 0080: the memory budget the planner works to, changed while running (main thread, between updates);
        /// 0: no budget. The planner frees the farthest replaceable clusters at its next run when it is lowered.
        void setResidentBudget(std::uint64_t bytes);
        std::uint64_t residentBudget() const;

        /// The cells inside the active grid are masked (the live objects keep them).
        void setActiveGrid(const ActiveCellGrid& grid);
        /// A reference a script disabled, or the scene now draws moved or deleted: the cell it stands in (and the one
        /// it is stored in) is masked in every cluster that lists it.
        void overrideReference(ESM::RefNum ref);
        void clearOverrides();

        /// The cells whose statics a Solid cluster draws (not masked), each with the sorted references the cluster
        /// lists for it: the live objects leave out exactly those statics. Changes only on the main thread inside
        /// update(); the version rises whenever it changes.
        using ClusteredCells = std::map<std::pair<int, int>, std::shared_ptr<const std::vector<Lod::ClusterRef>>>;
        const ClusteredCells& clusteredCells() const;
        std::uint64_t clusteredCellsVersion() const;

        struct Stats
        {
            bool mReady = false;
            bool mUsable = false;
            std::size_t mTiles = 0; // in the pack
            std::size_t mBuilt = 0; // with draws in the scene
            std::size_t mShown = 0; // chosen by the selection
            std::size_t mDraws = 0; // state groups of the shown clusters
            std::size_t mTriangles = 0; // of the shown clusters, masks taken out
            std::size_t mLoads = 0; // tiles read and decoded so far
            std::size_t mBlocked = 0; // clusters left out because the memory budget was reached
            std::uint64_t mHeldBytes
                = 0; // what the built clusters hold after the planner's last run (0 without a budget)
            std::size_t mCooling = 0; // wanted clusters a cooldown held back this frame (third review, finding 1)
            // Far cluster eviction (task 0060): clusters freed while wanted that only the ground rule made replaceable,
            // in all so far; clusters held back this frame until their ground counts; built or wanted clusters past the
            // range with no ground beneath (the sea, a tile that failed); the bytes the budget cannot touch.
            std::size_t mFarEvictions = 0;
            std::size_t mWaitingForGround = 0;
            std::size_t mNoGround = 0;
            std::uint64_t mNoFallbackBytes = 0;
            double mLoadMillisecondsTotal = 0;
            double mLoadMillisecondsWorst = 0;
        };
        Stats stats() const;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
