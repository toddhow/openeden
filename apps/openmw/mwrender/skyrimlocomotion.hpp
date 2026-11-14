#ifndef OPENMW_MWRENDER_SKYRIMLOCOMOTION_HPP
#define OPENMW_MWRENDER_SKYRIMLOCOMOTION_HPP

// Task 0148 (M-S3, locomotion, `[Game] neutral actor locomotion`): a neutral Skyrim actor blends the standing idle with
// the walk and run cycles of its sex (mt_walkforward / mt_runforward, decoded on workers like the idle of task 0145) by
// its measured ground speed. The maths is in components/havokpack/locomotion.hpp (Gait::weights and friends).
//
// Why not CreativeMotion's blend (task 0124)? CreativeMotion's runtime converts clips to opas::anim::Clip and drives
// them from Morrowind's group table (MWAnim::Play); a Skyrim neutral actor is a bare MWAnim::ObjectAnimation with no
// Play and no CharacterController, and its clips are Havok clips read by components/havokpack. Taking this slice
// through CM would need the havok-to-opas clip conversion plus the AnimSet of task 0124 stage C1, neither of which
// exists. So this slice extends the 0145 controller: the same clip futures, the same bone writes, three clips instead
// of one, and a pure blend that a CM graph can replace later (Gait::weights is the whole decision).
//
// Threads: the controller is created on the main thread (Scene::insertNeutralActor); setGroundSpeed is called on the
// main thread (World::doPhysics) and only stores a relaxed atomic float; run() is called by the object's update (an
// update worker for some objects), one object at a time. run() allocates nothing once a clip is resolved: scratch and
// accumulator buffers are sized when the clips are resolved. Nothing in it blocks (futures are polled with a zero
// wait).

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/havokpack/locomotion.hpp>

#include "skyrimidle.hpp"

namespace MWRender::SkyrimLocomotion
{
    /// The clips a walking person plays (animations/<male|female>/<name>.hkx; task 0148 chose them from the listing of
    /// Skyrim - Animations.bsa: the other mt_walk* / mt_run* clips are the strafe and backward variants).
    inline constexpr std::string_view sWalkClip = "mt_walkforward";
    inline constexpr std::string_view sRunClip = "mt_runforward";

    class Controller : public Anim::Controller
    {
    public:
        using BoneMap = SkyrimIdle::Controller::BoneMap;

        /// `idlePhaseFraction` (0..1) is the idle's start, as in the idle controller. A null future is a clip that does
        /// not exist (its weight goes to the next slower gait, finally to the idle).
        Controller(SkyrimIdle::ClipFuture idle, SkyrimIdle::ClipFuture walk, SkyrimIdle::ClipFuture run, BoneMap bones,
            float idlePhaseFraction, HavokPack::Gait::Params params = {});

        /// Main thread. The actor's measured ground speed in units a second (0 when it stands or is blocked).
        void setGroundSpeed(float unitsPerSecond) { mGroundSpeed.store(unitsPerSecond, std::memory_order_relaxed); }

        void run(vsg::Object& target, float time) const override;

        /// What the last run did, for tests and the trace.
        struct State
        {
            HavokPack::Gait::Weights mWeights;
            float mSmoothedSpeed = 0.f;
            float mPhase = 0.f;
            float mRunMix = 0.f; ///< 0 the walk cycle, 1 the run cycle (cross-fading)
            bool mIdleReady = false;
            bool mWalkReady = false;
            bool mRunReady = false;
        };
        const State& state() const { return mState; }

    private:
        struct Slot
        {
            SkyrimIdle::ClipFuture mFuture;
            std::shared_ptr<const HavokPack::BoundClip> mClip;
            std::vector<int> mBone; // per track: index into mTargets, -1 for none
            std::vector<HavokPack::Transform> mScratch;
            bool mPending = true;
            bool ready() const { return mClip != nullptr; }
        };
        void poll(Slot& slot) const;

        std::atomic<float> mGroundSpeed{ 0.f };
        HavokPack::Gait::Params mParams;
        float mIdlePhaseFraction;

        mutable Slot mIdle, mWalk, mRun;
        mutable std::vector<vsg::ref_ptr<Anim::Transform>> mTargets;
        mutable std::unordered_map<std::string, int> mIndex; // bone name -> slot in mTargets
        mutable std::vector<HavokPack::Transform> mAccum;
        mutable std::vector<float> mAccumWeight;
        mutable float mIdleOffset = 0.f;
        mutable bool mIdleOffsetSet = false;
        mutable float mLastTime = 0.f;
        mutable bool mHaveTime = false;
        mutable bool mHaveGait = false;
        mutable bool mIsRun = false;
        mutable unsigned mTraceCounter = 0;
        mutable State mState;
    };
}

#endif
