#ifndef OPENMW_MWRENDER_SKYRIMPOSE_HPP
#define OPENMW_MWRENDER_SKYRIMPOSE_HPP

// Task 0135 stage S6b (furniture use, `[Game] neutral actor idles`): the pose a neutral Skyrim actor holds while it sits or
// sleeps. One controller per drawn actor, added AFTER the idle (or locomotion) controller so that, while it is active, its
// bone writes win. While inactive it does nothing (one mutex lock and a flag test a frame). The clip is a Havok clip
// (components/havokpack) decoded on a worker through SkyrimIdle::loadIdleAsync, looped from the moment it became active.
//
// Threads: setClip is called on the main thread (World::doPhysics, via Scene::setNeutralActorPose); run() is called by
// the object's update (an update worker for some objects). The few members both touch are guarded by one mutex; run()
// allocates nothing once the clip is resolved.

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>

#include "skyrimidle.hpp"

namespace MWRender::SkyrimPose
{
    class Controller : public Anim::Controller
    {
    public:
        using BoneMap = SkyrimIdle::Controller::BoneMap;

        /// `vfs` must outlive the controller (the game's VFS does). `skeletonNifPath` and `female` name the clip files.
        Controller(const VFS::Manager& vfs, std::string skeletonNifPath, bool female, BoneMap bones);

        /// Main thread. Starts looping `clip` (a file name under the animations folder, no extension); `perSex` says it is
        /// in the male/female sub-folder. An empty name switches the pose off (the idle shows again).
        void setClip(std::string_view clip, bool perSex);

        /// True between a setClip with a name and the next empty one.
        [[nodiscard]] bool active() const;
        /// True once the active clip is decoded and bound (it is drawing).
        [[nodiscard]] bool drawing() const;

        void run(vsg::Object& target, float time) const override;

    private:
        const VFS::Manager& mVfs;
        std::string mSkeleton;
        bool mFemale;
        BoneMap mBones;

        mutable std::mutex mMutex;
        bool mActive = false;
        mutable bool mNeedStart = false; ///< the next run records the group time as the clip's start
        mutable float mStart = 0.f;
        mutable SkyrimIdle::ClipFuture mFuture;
        mutable bool mPending = false;
        mutable std::shared_ptr<const HavokPack::BoundClip> mClip;
        mutable std::vector<vsg::ref_ptr<Anim::Transform>> mTargets;
        mutable std::vector<HavokPack::Transform> mScratch;
    };
}

#endif
