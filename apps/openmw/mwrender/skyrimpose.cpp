#include "skyrimpose.hpp"

#include <chrono>
#include <cstdlib>

#include <components/debug/debuglog.hpp>

#include <components/havokpack/idle.hpp>

namespace MWRender::SkyrimPose
{
    Controller::Controller(const VFS::Manager& vfs, std::string skeletonNifPath, bool female, BoneMap bones)
        : mVfs(vfs)
        , mSkeleton(std::move(skeletonNifPath))
        , mFemale(female)
        , mBones(std::move(bones))
    {
        hints.autoPlay = true;
    }

    void Controller::setClip(std::string_view clip, bool perSex)
    {
        SkyrimIdle::ClipFuture future;
        if (!clip.empty())
            future = SkyrimIdle::loadIdleAsync(mVfs, mSkeleton, perSex ? (mFemale ? "female" : "male") : "", clip);
        const std::lock_guard lock(mMutex);
        mActive = !clip.empty();
        if (clip.empty())
        {
            mClip.reset();
            mTargets.clear();
        }
        // Switching from one clip to another (the sit-down to the sitting loop, task 0135 stage S6f) keeps the old clip
        // playing until the new one is decoded and bound: no frame of the idle pose in between.
        mFuture = std::move(future);
        mPending = mActive;
        mNeedStart = mActive;
    }

    bool Controller::active() const
    {
        const std::lock_guard lock(mMutex);
        return mActive;
    }

    bool Controller::drawing() const
    {
        const std::lock_guard lock(mMutex);
        return mActive && mClip != nullptr && !mPending;
    }

    void Controller::run(vsg::Object&, float time) const
    {
        const std::lock_guard lock(mMutex);
        if (!mActive)
            return;
        if (mPending)
        {
            if (!mFuture.valid())
            {
                mPending = false;
                return;
            }
            if (mFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                if (!mClip)
                    return;
            }
            else
            {
                mPending = false;
                mClip = mFuture.get();
                mFuture = {};
                if (mClip)
                {
                    mTargets.assign(mClip->mClip.mNumTransformTracks, nullptr);
                    std::size_t bound = 0;
                    for (std::size_t i = 0; i < mTargets.size(); ++i)
                    {
                        const auto it = mBones.find(mClip->mTrackBones[i]);
                        if (it != mBones.end())
                        {
                            mTargets[i] = it->second;
                            ++bound;
                        }
                    }
                    mScratch.resize(mTargets.size());
                    if (bound == 0)
                        mClip.reset();
                    static const bool trace = std::getenv("OPENEDEN_NEUTRAL_ACTOR_TRACE") != nullptr;
                    if (trace && mClip)
                    {
                        // Developer trace: the clip's first pose for the root and centre-of-mass tracks, where the
                        // actor's origin sits relative to the figure (task 0135 stage S6b).
                        HavokPack::samplePose(*mClip, 0.f, mScratch);
                        for (std::size_t i = 0; i < mTargets.size(); ++i)
                        {
                            const std::string& name = mClip->mTrackBones[i];
                            if (name.find("com") != std::string::npos || name.find("root") != std::string::npos
                                || name.find("pelvis") != std::string::npos)
                                Log(Debug::Info)
                                    << "NPOSE " << name << " t " << mScratch[i].mTranslation[0] << " "
                                    << mScratch[i].mTranslation[1] << " " << mScratch[i].mTranslation[2] << " duration "
                                    << mClip->mClip.mDuration << (mTargets[i] ? "" : " (unbound)");
                        }
                    }
                }
            }
        }
        if (!mClip)
            return;
        if (mNeedStart && !mPending)
        {
            mNeedStart = false;
            mStart = time;
        }
        HavokPack::samplePose(*mClip, time - mStart, mScratch);
        for (std::size_t i = 0; i < mTargets.size(); ++i)
            if (mTargets[i])
                SkyrimIdle::apply(mScratch[i], *mTargets[i]);
    }
}
