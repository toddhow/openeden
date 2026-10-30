#ifndef OPENEDEN_MWRENDER_GROUNDCOVER_H
#define OPENEDEN_MWRENDER_GROUNDCOVER_H

#include <map>
#include <utility>
#include <vector>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>

#include "groundcoverchunks.hpp"
#include "groundcoverinstances.hpp"

namespace vsg
{
    class OperationThreads;
}

namespace MWWorld
{
    class GroundcoverStore;
}

namespace MWRender
{
    /**
     * @brief Draws groundcover around the camera on VSG (task 0012, V4f and V4g): OpenMW's MWRender::Groundcover at
     * 442eec6e2f, with one chunk per cell instead of OpenMW's terrain quadtree chunks.
     *
     * Each chunk instances every groundcover model of its cell (collectGroundcoverInstances) with the INSTANCE
     * pipeline mode, which places, rotates and scales each instance and moves it with the wind and the player's steps
     * (files/shaders/lib/groundcover.glsl). Models are read with OpenMW's unified alpha: alpha test at 128/255 and no
     * blending. Chunks of the cells within `[Groundcover] rendering distance` of the camera are shown; the cells in
     * range are worked out again after the camera has moved 1024 units, and the shader drops instances beyond the
     * distance (OpenMW's groundcoverFadeEnd).
     *
     * A chunk is built on this object's own worker thread (so it never delays cell preloading) the first time its
     * cell comes into range, then compiled and shown by a later update(); chunks out of range are cached
     * (GroundcoverChunkTracker), and all are kept while hidden in interiors. Chunks leaving the scene graph are handed
     * to the compile context's detach, so queued frames can finish drawing them.
     *
     * Owned by RenderManager; every member function runs on the main thread. The store is World's and must outlive
     * every build: the destructor and waitForBuilds() wait for them.
     */
    class Groundcover
    {
    public:
        Groundcover(const MWAnim::Context& context, const MWWorld::GroundcoverStore& store, float density,
            float renderingDistance);
        ~Groundcover();

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void update(const vsg::vec3& cameraPosition);
        /// Hides every chunk (in interiors) and keeps the built ones for the next update().
        void hide();
        /// Waits for the chunks being built, which read the store.
        void waitForBuilds();

    private:
        struct ChunkBuild;
        using Tracker = GroundcoverChunkTracker<vsg::ref_ptr<vsg::Node>>;

        // Takes finished builds, compiling their chunks; true if any finished.
        bool collectBuilds();
        void setShownChunks(vsg::Group::Children chunks);

        MWAnim::Context mContext;
        const MWWorld::GroundcoverStore& mStore;
        float mDensity;
        float mRenderingDistance;
        vsg::ref_ptr<vsg::OperationThreads> mThreads;
        vsg::ref_ptr<vsg::Group> mRoot;
        // A null chunk is a cell without groundcover.
        Tracker mTracker;
        std::vector<vsg::ref_ptr<ChunkBuild>> mBuilds;
        vsg::vec2 mLastUpdatePosition;
        bool mHasUpdated = false;
    };
}

#endif
