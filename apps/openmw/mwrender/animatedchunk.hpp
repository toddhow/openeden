#ifndef OPENEDEN_MWRENDER_ANIMATEDCHUNK_H
#define OPENEDEN_MWRENDER_ANIMATEDCHUNK_H

#include <cstdint>
#include <utility>
#include <vector>

#include <vsg/core/Inherit.h>
#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <components/animation/autoplay.hpp>
#include <components/animation/controller.hpp>
#include <components/lod/animlod.hpp>

namespace MWRender
{
    /**
     * @brief The moving objects of one object paging chunk (task 0096, stage d, `[WorldLod] animated objects`).
     *
     * DistantObjects::buildChunk gives each object of a chunk whose model has auto-play controllers (texture scroll,
     * alpha and colour, keyframes) a copy of the model of its own and records here the controllers and the nodes and
     * data they move. update() runs them at the rate Lod::planAnimFrame gives the object's distance, within the frame's
     * budget; the animation keeps its time (it is given the time of the frames it skipped).
     *
     * Ownership: the chunk node holds this object (as a user object, key sKey), and this object holds a reference to
     * every controller and every target it runs, so no controller can outlive the node it moves and the raw pointers in
     * the update groups stay valid for as long as this object lives.
     *
     * Threads: addObject and finish are called by the worker that builds the chunk, before the chunk is shared;
     * update() runs on the main thread only, once per frame, and does not allocate after finish().
     */
    class AnimatedChunk : public vsg::Inherit<vsg::Object, AnimatedChunk>
    {
    public:
        static constexpr const char* sKey = "openeden.animatedchunk";

        using Controllers = std::vector<std::pair<vsg::ref_ptr<const Anim::Controller>, vsg::ref_ptr<vsg::Object>>>;

        /// Adds an object standing at `center` (world units), with a stable `id` (its reference number, for the phase
        /// of its animation and its place in the schedule) and the controllers to run. Only controllers with the
        /// auto-play hint are meant to be passed; they are grouped by their phase group. An empty list adds nothing.
        void addObject(const vsg::dvec3& center, std::uint64_t id, const Controllers& controllers);

        /// Reserves what update() needs. Call once, after the last addObject.
        void finish();

        std::size_t objectCount() const { return mObjects.size(); }
        std::size_t controllerCount() const;

        /// Runs this frame's controllers; returns how many ran. `dt` is the frame's game time, `stopDistance` where the
        /// animation stops, `frame` a counter that grows by one each rendered frame, `budget` is shared by all chunks.
        unsigned update(
            const vsg::dvec3& camera, double dt, float stopDistance, unsigned frame, Lod::AnimBudget& budget);

        /// The tier of every object after the last update(), for tests and traces.
        std::size_t countInTier(Lod::AnimTier tier) const;

    private:
        struct Member
        {
            vsg::dvec3 center;
            std::vector<Anim::AutoPlay::Group> groups;
            // The owners of what the raw pointers in `groups` point to.
            Controllers owned;
        };
        std::vector<Member> mObjects;
        std::vector<Lod::AnimItem> mItems;
        std::vector<Lod::AnimRun> mRuns;
    };
}

#endif
