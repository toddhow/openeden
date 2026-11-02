#ifndef OPENEDEN_MWRENDER_DISTANTOBJECTS_H
#define OPENEDEN_MWRENDER_DISTANTOBJECTS_H

#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <vsg/nodes/Group.h>

#include <components/esm3/cellref.hpp>
#include <components/esm3/refnum.hpp>
#include <components/mwanimation/context.hpp>

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
        DistantObjects(const MWAnim::Context& context, const MWWorld::ESMStore& store, float cellSize, float minSize);
        ~DistantObjects();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void setActiveGrid(const ActiveCellGrid& grid);
        void update(const vsg::vec3& cameraPosition, float viewDistance);
        /// Hides every chunk (in interiors) and keeps the built ones for the next update().
        void hide();
        using Cells = std::vector<std::pair<int, int>>;
        /// Leaves a disabled reference out, or draws it again once enabled, and rebuilds the chunks of cells if that
        /// changes anything. OpenMW's ObjectPaging::enableObject.
        bool setDisabled(ESM::RefNum ref, bool disabled, const Cells& cells);
        /// Leaves out for good a reference the scene now draws (moved, scaled, rotated or deleted), rebuilding the
        /// chunks of cells if that changes anything. OpenMW's ObjectPaging::blacklistObject.
        bool blacklist(ESM::RefNum ref, const Cells& cells);
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

        MWAnim::Context mContext;
        const MWWorld::ESMStore& mStore;
        float mCellSize;
        float mMinSize;
        vsg::ref_ptr<vsg::OperationThreads> mThreads;
        vsg::ref_ptr<vsg::Group> mRoot;
        Tracker mTracker;
        std::vector<vsg::ref_ptr<ChunkBuild>> mBuilds;
        std::set<ESM::RefNum> mDisabled;
        std::set<ESM::RefNum> mBlacklist;
        // Builds started before a cell's entry, or before mOldestValidBuild, are out of date and thrown away.
        std::map<std::pair<int, int>, unsigned int> mCellGenerations;
        unsigned int mGeneration = 0;
        unsigned int mOldestValidBuild = 0;
        ActiveCellGrid mActiveGrid;
        vsg::vec3 mCameraPosition;
        float mViewDistance = 0.f;
        bool mHasUpdated = false;
        bool mWantedChanged = false;
    };
}

#endif
