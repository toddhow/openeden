#ifndef OPENEDEN_MWRENDER_POINTSHADOWS_H
#define OPENEDEN_MWRENDER_POINTSHADOWS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsg/app/CommandGraph.h>
#include <vsg/lighting/PointLight.h>
#include <vsg/maths/mat4.h>

#include <components/render/pointshadowcache.hpp>
#include <components/render/pointshadowgpu.hpp>
#include <components/render/pointshadowselect.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/pointshadow.hpp>

namespace vsg
{
    class Context;
}

namespace MWRender
{
    /*
     * Shadows of the nearest point lights (task 0077, docs/tasks/0077-point-light-shadows.md).
     *
     * Owns the maps (View::PointShadow), the slots' state and the data buffer the shaders read. Each frame, on the main
     * thread, `update` takes the lights the main view collected in its last traversal, chooses the ones that get a
     * slot (Render::PointShadow::select), puts the faces' cameras at them, makes each slot's command graph draw its
     * six faces and packs the data buffer in view space of the current camera. Every face of every used slot is drawn
     * every frame, unless `[Shadows] point light shadow cache` is on: then Render::PointShadow::FaceCache decides which
     * faces are drawn again (docs/tasks/0077-point-light-shadows.md, "Caching the maps"). The unchanged faces keep
     * their depth image, because their render graphs are simply not in the slot's command graph that frame.
     */
    class PointShadows
    {
    public:
        using PointLights = std::vector<std::pair<vsg::dmat4, const vsg::PointLight*>>;

        /// The settings of [Shadows] (point light shadow count, resolution, filter taps and bias).
        PointShadows(vsg::Context& ctx, std::size_t maxLights);

        View::PointShadow& maps() { return mMaps; }
        int slotCount() const { return mMaps.slotCount; }

        /// The command graph that draws a slot's faces; its children are replaced each frame by `update`.
        void setGraph(int slot, vsg::ref_ptr<vsg::CommandGraph> graph);

        /// With `gpu timing` on: a timestamp pair round a slot's faces, as the pass "point shadow <slot>" (the table
        /// holds a limited number of passes, so the caller times the first few slots only).
        void setSlotTimer(int slot, vsg::ref_ptr<vsg::Node> begin, vsg::ref_ptr<vsg::Node> end);

        /// `viewMatrix`: world to view of the camera of this frame. `lights` are the main view's last traversal's
        /// (View::CollectLights::snapshots), made with the view matrix that was passed to the previous call (the first
        /// call takes this one). Afterwards `entries()` says which of those lights have a slot.
        /// `worldEpoch`: any number that changes when the casters in the world changed (cells, objects; the cache only
        /// compares it with the last one). With the cache on, the caller fills `motion()` with the moving casters
        /// (begin, add for each, nothing else) before each call.
        void update(
            const vsg::dmat4& viewMatrix, const std::vector<View::LightSnapshot>& lights, std::uint32_t worldEpoch = 0);

        bool cacheEnabled() const { return mCacheEnabled; }
        Render::PointShadow::MotionTracker& motion() { return mMotion; }
        /// The world changed in a way the scene's own counter does not see (the casters' scene was swapped).
        void invalidateAll() { ++mExtraEpoch; }

        /// Faces drawn by the last update and the total they could have been (with the cache off: equal).
        int facesDrawn() const { return mFacesDrawn; }
        int facesPossible() const { return mFacesPossible; }

        /// The lights that have a slot, for View::CollectLights::setShadowEntries.
        const std::vector<View::ShadowEntry>& entries() const { return mEntries; }

        /// How many slots hold a light after the last update.
        int usedSlots() const;

    private:
        View::PointShadow mMaps;
        std::size_t mMaxLights;
        Render::PointShadow::SelectionParams mSelection;
        Render::PointShadow::Params mGpu;
        std::vector<Render::PointShadow::Slot> mSlots;
        // How many frames in a row a holder's light was missing from the collected list (it was culled, or left
        // the view for a moment): it keeps its slot for a few frames so that a turn of the camera does not drop it.
        std::vector<std::uint32_t> mMissing;
        std::vector<View::ShadowEntry> mEntries;
        Render::PointShadow::Selection mResult;
        Render::PointShadow::SelectionScratch mScratch;
        vsg::dmat4 mListView; // the view matrix the last traversal's lights were made with
        bool mHaveListView = false;
        std::vector<vsg::ref_ptr<vsg::CommandGraph>> mGraphs;
        std::vector<vsg::ref_ptr<vsg::Node>> mTimerBegin;
        std::vector<vsg::ref_ptr<vsg::Node>> mTimerEnd;
        // Scratch kept between frames so that update allocates nothing once it has grown.
        std::vector<Render::PointShadow::Candidate> mCandidates;
        std::vector<int> mNoSlots; // empty: the light entries are written by View::CollectLights::pack
        std::vector<float> mNoFades;
        std::vector<vsg::vec4> mSlotPositions;
        std::vector<vsg::vec4> mPacked;
        // The cache (stage e); mCacheEnabled false means every face of every used slot is drawn every frame.
        bool mCacheEnabled = false;
        Render::PointShadow::CacheParams mCacheParams;
        Render::PointShadow::FaceCache mCache;
        Render::PointShadow::MotionTracker mMotion;
        std::vector<Render::PointShadow::Disturbance> mDisturbances;
        std::vector<Render::PointShadow::CacheLight> mCacheLights;
        std::vector<std::uint8_t> mDraw;
        std::uint32_t mExtraEpoch = 0;
        std::uint32_t mCacheLogCounter = 0;
        int mFacesDrawn = 0;
        int mFacesPossible = 0;
    };
}

#endif
