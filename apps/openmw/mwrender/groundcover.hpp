#ifndef OPENEDEN_MWRENDER_GROUNDCOVER_H
#define OPENEDEN_MWRENDER_GROUNDCOVER_H

#include <map>
#include <utility>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>

#include "groundcoverinstances.hpp"

namespace MWWorld
{
    class GroundcoverStore;
}

namespace MWRender
{
    /**
     * @brief Draws groundcover around the camera on VSG (task 0012, V4f): OpenMW's MWRender::Groundcover at
     * 442eec6e2f, with one chunk per cell instead of OpenMW's terrain quadtree chunks.
     *
     * Each chunk instances every groundcover model of its cell (collectGroundcoverInstances) with the INSTANCE
     * pipeline mode, which places, rotates and scales each instance and moves it with the wind and the player's steps
     * (files/shaders/lib/groundcover.glsl). Models are read with OpenMW's unified alpha: alpha test at 128/255 and no
     * blending. Chunks of the cells within `[Groundcover] rendering distance` of the camera are shown; a chunk is built
     * the first time its cell comes into range and kept until it leaves it; the cells in range are worked out again
     * after the camera has moved 1024 units, and the shader drops instances beyond the distance (OpenMW's
     * groundcoverFadeEnd).
     *
     * Owned by RenderManager; update() runs on the main thread and builds new chunks there. The store is World's and
     * must outlive this object.
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
        void clear();

    private:
        vsg::ref_ptr<vsg::Node> createChunk(int cellX, int cellY) const;

        MWAnim::Context mContext;
        const MWWorld::GroundcoverStore& mStore;
        float mDensity;
        float mRenderingDistance;
        vsg::ref_ptr<vsg::Group> mRoot;
        // Built chunks by cell; a null node is a cell without groundcover.
        std::map<std::pair<int, int>, vsg::ref_ptr<vsg::Node>> mChunks;
        vsg::vec2 mLastUpdatePosition;
        bool mHasUpdated = false;
    };
}

#endif
