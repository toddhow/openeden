#ifndef OPENMW_MWRENDER_SKYRIMIDLE_HPP
#define OPENMW_MWRENDER_SKYRIMIDLE_HPP

// Task 0132 (M-S3, first slice, `[Game] neutral actor idles`): a neutral Skyrim actor loops an idle clip decoded from
// the user's own .hkx files (components/havokpack, clean-room) instead of standing in the rest pose.
//
// The clip is read through the VFS the first time a skeleton asks and shared by every actor of that skeleton
// (shared_ptr<const BoundClip>, immutable). The controller is an ordinary auto-play Anim::Controller of the actor's
// object: each frame it samples the clip at its own looping time and writes the translation, rotation and scale of
// every bound bone's Anim::Transform. Root motion is not applied (the clip is sampled for the bones only).
//
// Threads: the loader may be called from the main thread only (Scene::insertNeutralActor); the cache is locked anyway.
// run() is called by the object's update (an update worker for some objects), one object at a time; the scratch
// buffer belongs to the controller, which belongs to one actor.

#include <memory>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/havokpack/idle.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender::SkyrimIdle
{
    /// What loading did, for the log and for tests.
    struct LoadReport
    {
        std::string mSkeletonPath;
        std::string mClipPath;
        std::string mFailure; // set when no clip was made
        std::uint32_t mTracks = 0;
    };

    /// The idle clip for an actor whose skeleton model is `skeletonNifPath`, bound to that skeleton's bone names; null
    /// with report.mFailure set when a file is missing or damaged. Cached by (skeleton, clip) path, failures too, so a
    /// cell of actors reads each file once. Never throws.
    [[nodiscard]] std::shared_ptr<const HavokPack::BoundClip> loadIdle(
        const VFS::Manager& vfs, const std::string& skeletonNifPath, LoadReport& report);

    /// Writes one Havok transform into a scene graph transform (translation, rotation, scale). The scene transform
    /// stores the transposed rotation matrix; Anim::Transform::setAttitude takes care of it.
    void apply(const HavokPack::Transform& from, Anim::Transform& to);

    /// Loops `clip` on `targets` (one per track, null to skip a track). Starts `phase` seconds into the clip.
    class Controller : public Anim::Controller
    {
    public:
        Controller(std::shared_ptr<const HavokPack::BoundClip> clip, std::vector<vsg::ref_ptr<Anim::Transform>> targets,
            float phase);

        void run(vsg::Object& target, float time) const override;

        std::size_t boundTracks() const { return mBound; }

    private:
        std::shared_ptr<const HavokPack::BoundClip> mClip;
        std::vector<vsg::ref_ptr<Anim::Transform>> mTargets;
        float mPhase;
        std::size_t mBound = 0;
        mutable std::vector<HavokPack::Transform> mScratch;
    };
}

#endif
