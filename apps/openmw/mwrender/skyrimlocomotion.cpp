#include "skyrimlocomotion.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/misc/loadtrace.hpp>

namespace MWRender::SkyrimLocomotion
{
    namespace
    {
        // A frame longer than this counts as this long: a pause (a loading screen, a breakpoint) must not throw the
        // smoothed speed or the cycle phase.
        constexpr float kMaxStep = 0.1f;
        // A weight below this is not worth sampling a clip for.
        constexpr float kMinWeight = 1e-4f;
    }

    Controller::Controller(SkyrimIdle::ClipFuture idle, SkyrimIdle::ClipFuture walk, SkyrimIdle::ClipFuture run,
        BoneMap bones, float idlePhaseFraction, HavokPack::Gait::Params params)
        : mParams(params)
        , mIdlePhaseFraction(idlePhaseFraction)
    {
        hints.autoPlay = true;
        mIdle.mFuture = std::move(idle);
        mWalk.mFuture = std::move(walk);
        mRun.mFuture = std::move(run);
        // The union of the bones, ordered by name so that the slots do not depend on the hash map's order.
        std::vector<std::pair<std::string, vsg::ref_ptr<Anim::Transform>>> sorted(bones.begin(), bones.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        mTargets.reserve(sorted.size());
        mIndex.reserve(sorted.size());
        for (auto& [name, node] : sorted)
        {
            mIndex.emplace(name, static_cast<int>(mTargets.size()));
            mTargets.push_back(std::move(node));
        }
        mAccum.resize(mTargets.size());
        mAccumWeight.resize(mTargets.size());
    }

    void Controller::poll(Slot& slot) const
    {
        if (!slot.mPending)
            return;
        if (!slot.mFuture.valid())
        {
            slot.mPending = false;
            return;
        }
        if (slot.mFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        slot.mPending = false;
        slot.mClip = slot.mFuture.get();
        slot.mFuture = {};
        if (!slot.mClip)
            return;
        const std::size_t tracks = slot.mClip->mClip.mNumTransformTracks;
        slot.mBone.assign(tracks, -1);
        std::size_t bound = 0;
        for (std::size_t i = 0; i < tracks && i < slot.mClip->mTrackBones.size(); ++i)
        {
            const auto it = mIndex.find(slot.mClip->mTrackBones[i]);
            if (it != mIndex.end())
            {
                slot.mBone[i] = it->second;
                ++bound;
            }
        }
        if (bound == 0)
        {
            slot.mClip.reset(); // a clip of another skeleton: as if it were missing
            return;
        }
        slot.mScratch.resize(tracks);
    }

    void Controller::run(vsg::Object&, float time) const
    {
        Misc::LoadTrace::Scope trace("skyrimlocomotion.run");
        poll(mIdle);
        poll(mWalk);
        poll(mRun);
        mState.mIdleReady = mIdle.ready();
        mState.mWalkReady = mWalk.ready();
        mState.mRunReady = mRun.ready();

        float target = mGroundSpeed.load(std::memory_order_relaxed);
        if (!std::isfinite(target) || target < 0.f)
            target = 0.f;
        float dt = 0.f;
        if (mHaveTime)
            dt = std::clamp(time - mLastTime, 0.f, kMaxStep);
        else
            mState.mSmoothedSpeed = target; // the first frame: no ramp from nothing
        mHaveTime = true;
        mLastTime = time;
        mState.mSmoothedSpeed = HavokPack::Gait::smoothSpeed(mState.mSmoothedSpeed, target, dt, mParams.mSmoothTau);

        HavokPack::Gait::Weights w = HavokPack::Gait::weights(mState.mSmoothedSpeed, mParams);
        // A gait whose clip is missing or not decoded yet hands its weight to the next slower one.
        if (!mRun.ready())
        {
            w.mWalk += w.mRun;
            w.mRun = 0.f;
        }
        if (!mWalk.ready())
        {
            w.mIdle += w.mWalk;
            w.mWalk = 0.f;
        }
        mState.mWeights = w;

        if (w.mCyclesPerSecond > 0.f)
            mState.mPhase = HavokPack::Gait::advancePhase(mState.mPhase, w.mCyclesPerSecond, dt);
        else
            mState.mPhase = 0.f; // a stop; the next start begins with the left foot

        std::fill(mAccumWeight.begin(), mAccumWeight.end(), 0.f);
        const auto add = [this](Slot& slot, float weight, float clipTime) {
            if (weight < kMinWeight || !slot.ready())
                return;
            HavokPack::samplePose(*slot.mClip, clipTime, slot.mScratch);
            for (std::size_t i = 0; i < slot.mBone.size(); ++i)
            {
                const int bone = slot.mBone[i];
                if (bone < 0)
                    continue;
                float& have = mAccumWeight[bone];
                if (have <= 0.f)
                    mAccum[bone] = slot.mScratch[i];
                else
                    mAccum[bone] = HavokPack::Gait::blend(mAccum[bone], slot.mScratch[i], weight / (have + weight));
                have += weight;
            }
        };
        if (mIdle.ready() && !mIdleOffsetSet)
        {
            mIdleOffsetSet = true;
            mIdleOffset = mIdlePhaseFraction * mIdle.mClip->mClip.mDuration;
        }
        add(mIdle, w.mIdle, time + mIdleOffset);
        if (mWalk.ready())
            add(mWalk, w.mWalk,
                HavokPack::Gait::clipTime(mState.mPhase, mParams.mWalkLeftFoot, mWalk.mClip->mClip.mDuration));
        if (mRun.ready())
            add(mRun, w.mRun,
                HavokPack::Gait::clipTime(mState.mPhase, mParams.mRunLeftFoot, mRun.mClip->mClip.mDuration));

        for (std::size_t i = 0; i < mTargets.size(); ++i)
            if (mAccumWeight[i] > 0.f && mTargets[i])
                SkyrimIdle::apply(mAccum[i], *mTargets[i]);

        // Developer trace (one getenv for the process): the blend of every 10th run of a moving actor.
        static const bool sTrace = std::getenv("OPENEDEN_NEUTRAL_LOCO_TRACE") != nullptr;
        // The ground speed the cycle rate implies (cycles a second times the mixed stride): equal to the smoothed speed
        // outside the idle's fade, which is the foot-slide measure of the in-game check.
        const float gait = w.mWalk + w.mRun;
        const float played = w.mCyclesPerSecond
            * (gait > 0.f ? mParams.mWalkStride + (mParams.mRunStride - mParams.mWalkStride) * (w.mRun / gait)
                          : mParams.mWalkStride);
        if (sTrace && (mState.mSmoothedSpeed > 1.f || target > 0.f) && (++mTraceCounter % 10) == 0)
            Log(Debug::Info) << "NLOCO t=" << time << " target=" << target << " smoothed=" << mState.mSmoothedSpeed
                             << " idle=" << w.mIdle << " walk=" << w.mWalk << " run=" << w.mRun
                             << " phase=" << mState.mPhase << " cps=" << w.mCyclesPerSecond << " played=" << played;
    }
}
