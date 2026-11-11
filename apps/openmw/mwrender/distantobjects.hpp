#ifndef OPENEDEN_MWRENDER_DISTANTOBJECTS_H
#define OPENEDEN_MWRENDER_DISTANTOBJECTS_H

#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <vsg/nodes/Group.h>

#include <components/esm3/cellref.hpp>
#include <components/esm3/refnum.hpp>
#include <components/lod/objectcluster.hpp>
#include <components/mwanimation/context.hpp>

#include "animatedchunk.hpp"
#include "distantobjectcells.hpp"
#include "groundcoverchunks.hpp"

namespace vsg
{
    class OperationThreads;
}

namespace MWWorld
{
    class ESMStore;
}

namespace MWRender
{
    /// The references of one cell as the content files leave them, the ones distant objects draw (statics,
    /// activators, doors): the list the cell's chunk is built from, shared with the LOD cluster baker (task 0059)
    /// so that the two cannot disagree about what exists. Reads the store and the content files.
    std::map<ESM::RefNum, ESM::CellRef> collectDistantReferences(
        const std::pair<int, int>& cellIndex, const MWWorld::ESMStore& store);
    /// The model name of a distant object's record (STAT, ACTI or DOOR), empty for other types.
    std::string_view distantObjectModel(int type, const ESM::RefId& id, const MWWorld::ESMStore& store);

    /**
     * @brief Draws the statics, activators and doors of cells beyond the active grid (`[Terrain] object paging`, with
     * `distant terrain`; task 0012, V4n): OpenMW's MWRender::ObjectPaging at 442eec6e2f, for Morrowind's exterior.
     *
     * One chunk per cell, built on this object's own worker thread and compiled and shown by a later update(), as
     * MWRender::Groundcover. Each object is culled past distantObjectMaxDistance (`object paging min size`).
     *
     * Not ported: merging objects into shared geometry, the merge factor settings, LOD meshes, paging the active grid
     * and ESM4. So each object is its own draw, which costs more than OpenMW at the same settings.
     *
     * Models with particles keep their meshes; the particles are switched off, since nothing here animates them. The
     * ring of cells next to the active grid is built but not shown, so an object is drawn as soon as its cell unloads.
     *
     * Owned by RenderManager; every member function runs on the main thread. The store is World's: RenderManager
     * destroys this object when the World goes, which waits for running builds.
     */
    class DistantObjects
    {
    public:
        /// Task 0096 stage d (`[WorldLod] animated objects`): objects whose models have auto-play controllers keep
        /// them, run by updateAnimation() at a rate that falls with the distance. Off (the default) builds the chunks
        /// as before.
        struct AnimationOptions
        {
            bool mEnabled = false;
            /// Beyond this distance (world units) the animation stops.
            float mStopDistance = 24576.f;
            /// The most controllers run in one frame over all chunks.
            unsigned mBudget = 512;
        };

        DistantObjects(const MWAnim::Context& context, const MWWorld::ESMStore& store, float cellSize, float minSize,
            const AnimationOptions& animation);
        ~DistantObjects();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void setActiveGrid(const ActiveCellGrid& grid);
        /// Task 0060: a cell of the active grid whose live objects are still being built (or are built now). Its
        /// distant chunk stays drawn, and staticsReady() stays false, until the live statics are attached.
        void setCellUnbuilt(const std::pair<int, int>& cell, bool unbuilt);
        void update(const vsg::vec3& cameraPosition, float viewDistance);
        /// Runs the controllers of the animated objects of the shown chunks (main thread, once per frame, after
        /// update()); a no-op unless animation is on. `dt` is the frame's game time.
        void updateAnimation(const vsg::vec3& cameraPosition, double dt);
        /// What the last updateAnimation() found, for traces and tests.
        struct AnimationStats
        {
            std::size_t mChunks = 0;
            std::size_t mObjects = 0;
            unsigned mControllersRun = 0;
        };
        const AnimationStats& animationStats() const { return mAnimationStats; }
        /// Hides every chunk (in interiors) and keeps the built ones for the next update().
        void hide();
        using Cells = std::vector<std::pair<int, int>>;
        /// Leaves a disabled reference out, or draws it again once enabled, and rebuilds the chunks of cells if that
        /// changes anything. OpenMW's ObjectPaging::enableObject.
        bool setDisabled(ESM::RefNum ref, bool disabled, const Cells& cells);
        /// Leaves out for good a reference the scene now draws (moved, scaled, rotated or deleted), rebuilding the
        /// chunks of cells if that changes anything. OpenMW's ObjectPaging::blacklistObject.
        bool blacklist(ESM::RefNum ref, const Cells& cells);
        /// The cells a Solid baked object cluster covers (task 0059), each with the sorted references the cluster
        /// draws for it: their chunks are built without exactly those statics, so everything the cluster left out
        /// (activators, doors, small, animated or blended statics) still draws and nothing is drawn twice. A change
        /// rebuilds the chunks of the cells that changed.
        using ClusteredCells = std::map<std::pair<int, int>, std::shared_ptr<const std::vector<Lod::ClusterRef>>>;
        void setClusteredCells(const ClusteredCells& cells);
        /// True when the live objects draw this cell's statics (or have nothing to draw there): the cell is not one a
        /// cluster covers and no rebuild of its chunk is waiting. A cluster stays solid until this is true.
        bool staticsReady(const std::pair<int, int>& cell) const;
        /// Forgets every chunk and reference state, for a new or loaded game.
        void clear();
        void waitForBuilds();

    private:
        struct ChunkBuild;
        using Tracker = GroundcoverChunkTracker<vsg::ref_ptr<vsg::Node>>;

        bool collectBuilds();
        void setShownChunks(vsg::Group::Children chunks);
        void updateWanted();
        void forget(const Cells& cells);
        std::shared_ptr<const std::vector<Lod::ClusterRef>> clusterRefsOf(const std::pair<int, int>& cell) const;

        MWAnim::Context mContext;
        AnimationOptions mAnimation;
        // The animated objects of the chunks in mRoot, rebuilt whenever the shown chunks change.
        std::vector<vsg::ref_ptr<AnimatedChunk>> mAnimated;
        AnimationStats mAnimationStats;
        unsigned mAnimationFrame = 0;
        // For the developer trace only.
        double mAnimationMicroseconds = 0.0;
        std::uint64_t mAnimationControllers = 0;
        const MWWorld::ESMStore& mStore;
        float mCellSize;
        float mMinSize;
        vsg::ref_ptr<vsg::OperationThreads> mThreads;
        vsg::ref_ptr<vsg::Group> mRoot;
        Tracker mTracker;
        std::vector<vsg::ref_ptr<ChunkBuild>> mBuilds;
        std::set<ESM::RefNum> mDisabled;
        std::set<ESM::RefNum> mBlacklist;
        ClusteredCells mClustered;
        // The cells the last updateWanted() asked chunks for (sorted).
        std::vector<std::pair<int, int>> mWantedCells;
        // Cells whose chunk was dropped by forget() and not built again yet, and, for shown chunks, whether they were
        // built without statics.
        std::set<std::pair<int, int>> mRebuilding;
        // For each cell a chunk was built for: whether it was built leaving out a cluster's statics.
        std::map<std::pair<int, int>, bool> mShownSkipsStatics;
        // Builds started before a cell's entry, or before mOldestValidBuild, are out of date and thrown away.
        std::map<std::pair<int, int>, unsigned int> mCellGenerations;
        unsigned int mGeneration = 0;
        unsigned int mOldestValidBuild = 0;
        ActiveCellGrid mActiveGrid;
        std::set<std::pair<int, int>> mUnbuilt;
        vsg::vec3 mCameraPosition;
        float mViewDistance = 0.f;
        bool mHasUpdated = false;
        bool mWantedChanged = false;
    };
}

#endif
