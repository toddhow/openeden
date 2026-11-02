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
#include <memory>
#include <set>
#include <utility>

#include <vsg/io/Options.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/esm3/refnum.hpp>
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
            /// False in interiors and other worldspaces: every cluster fades out.
            bool mEnabled = true;
        };

        struct Setup
        {
            std::filesystem::path mPack;
            float mThreshold = 8;
            float mFadeSeconds = 0.4f;
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

        /// The cells inside the active grid are masked (the live objects keep them).
        void setActiveGrid(const ActiveCellGrid& grid);
        /// A reference a script disabled, or the scene now draws moved or deleted: the cell it stands in (and the one
        /// it is stored in) is masked in every cluster that lists it.
        void overrideReference(ESM::RefNum ref);
        void clearOverrides();

        /// The cells whose statics a cluster draws, fully faded in and not masked. Changes only on the main thread
        /// inside update(); `version` rises whenever the set changes.
        const std::set<std::pair<int, int>>& clusteredCells() const;
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
