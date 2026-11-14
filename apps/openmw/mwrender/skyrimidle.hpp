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

#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/havokpack/idle.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender::SkyrimIdle
{
    /// The clip an ambient standing person loops (`animations/<male|female>/mt_idle.hkx` of the actor folder; chosen
    /// from the archive listing of Skyrim - Animations.bsa, task 0145).
    inline constexpr std::string_view sAmbientIdle = "mt_idle";

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

    /// Task 0145: the same load on a worker thread, started at the first request for the (skeleton, sex, clip) and
    /// shared by every later one (a ready future once decoded). The future holds null with the reason in the log when
    /// the files are missing or damaged. `sexFolder` is "male" or "female" (the ambient idles' sub-folder); `clipName`
    /// has no extension. Never throws; `vfs` must outlive the worker (the game's VFS does).
    using ClipFuture = std::shared_future<std::shared_ptr<const HavokPack::BoundClip>>;
    [[nodiscard]] ClipFuture loadIdleAsync(const VFS::Manager& vfs, const std::string& skeletonNifPath,
        std::string_view sexFolder, std::string_view clipName);

    /// Writes one Havok transform into a scene graph transform (translation, rotation, scale). The scene transform
    /// stores the transposed rotation matrix; Anim::Transform::setAttitude takes care of it.
    void apply(const HavokPack::Transform& from, Anim::Transform& to);

    /// Loops `clip` on `targets` (one per track, null to skip a track). Starts `phase` seconds into the clip.
    class Controller : public Anim::Controller
    {
    public:
        Controller(std::shared_ptr<const HavokPack::BoundClip> clip, std::vector<vsg::ref_ptr<Anim::Transform>> targets,
            float phase);

        /// Task 0145: the clip is still being decoded on a worker. Until the future is ready the bones stay as they
        /// are; the first run after that names the targets from `bones` (lower-case bone name to its transform; moved
        /// from the actor's bones when the controller is made) and starts `phaseFraction` (0..1) of the way into the
        /// clip.
        using BoneMap = std::unordered_map<std::string, vsg::ref_ptr<Anim::Transform>>;
        Controller(ClipFuture clip, BoneMap bones, float phaseFraction);

        void run(vsg::Object& target, float time) const override;

        std::size_t boundTracks() const { return mBound; }
        /// False while the worker has not finished (always true for the synchronous constructor).
        bool resolved() const { return !mPending; }

    private:
        bool resolve() const;

        mutable std::shared_ptr<const HavokPack::BoundClip> mClip;
        mutable std::vector<vsg::ref_ptr<Anim::Transform>> mTargets;
        mutable float mPhase = 0.f;
        mutable std::size_t mBound = 0;
        mutable std::vector<HavokPack::Transform> mScratch;
        // Only set by the asynchronous constructor; resolved on the first run after the future is ready.
        mutable bool mPending = false;
        mutable ClipFuture mFuture;
        mutable BoneMap mBones;
        float mPhaseFraction = 0.f;
    };
}

#endif
