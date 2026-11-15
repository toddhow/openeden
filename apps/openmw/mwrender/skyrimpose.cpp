#include "skyrimpose.hpp"

#include <algorithm>
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
        // The futures this call replaces are moved here and die after the lock is let go (declared first, so they are
        // destroyed last): a future that holds the last reference of a running read must not be waited for under the
        // mutex the update thread needs (review fix of task 0171).
        SkyrimIdle::ClipFuture oldClip;
        SkyrimClipMeta::MetaFuture oldMeta;
        const std::lock_guard lock(mMutex);
        mOnce = false;
        mSampled = false;
        mPlaybackTime = 0.f;
        oldMeta = std::move(mMetaFuture);
        mMetaFuture = {};
        mMeta.reset();
        mActive = !clip.empty();
        if (clip.empty())
        {
            mClip.reset();
            mTargets.clear();
        }
        // Switching from one clip to another (the sit-down to the sitting loop, task 0135 stage S6f) keeps the old clip
        // playing until the new one is decoded and bound: no frame of the idle pose in between.
        oldClip = std::move(mFuture);
        mFuture = std::move(future);
        mPending = mActive;
        mNeedStart = mActive;
    }

    void Controller::playOnce(std::string_view clip, bool perSex)
    {
        if (clip.empty())
        {
            setClip({}, false);
            return;
        }
        SkyrimIdle::ClipFuture future
            = SkyrimIdle::loadIdleAsync(mVfs, mSkeleton, perSex ? (mFemale ? "female" : "male") : "", clip);
        SkyrimClipMeta::MetaFuture metaFuture = SkyrimClipMeta::loadAsync(mVfs, mFemale, std::string(clip), future);
        SkyrimIdle::ClipFuture oldClip; // see setClip: they die after the lock
        SkyrimClipMeta::MetaFuture oldMeta;
        const std::lock_guard lock(mMutex);
        mOnce = true;
        mSampled = false;
        mPlaybackTime = 0.f;
        oldMeta = std::move(mMetaFuture);
        mMetaFuture = std::move(metaFuture);
        mMeta.reset();
        mActive = true;
        oldClip = std::move(mFuture);
        mFuture = std::move(future);
        mPending = true;
        mNeedStart = true;
    }

    HavokPack::ClipProgress Controller::progress() const
    {
        const std::lock_guard lock(mMutex);
        if (!mMeta && mMetaFuture.valid() && mMetaFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            mMeta = mMetaFuture.get();
        return { mMeta, mOnce && mSampled, mPlaybackTime };
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
                        // Task 0171: eight samples over the clip, so that a clip that moves the root or the pelvis shows.
                        for (int step = 0; step <= 8; ++step)
                        {
                            const float at = mClip->mClip.mDuration * static_cast<float>(step) / 8.f;
                            HavokPack::samplePose(*mClip, std::min(at, mClip->mClip.mDuration - 1e-4f), mScratch);
                            for (std::size_t i = 0; i < mTargets.size(); ++i)
                            {
                                const std::string& name = mClip->mTrackBones[i];
                                if (name.find("com") != std::string::npos || name.find("root") != std::string::npos
                                    || name.find("pelvis") != std::string::npos)
                                    Log(Debug::Info)
                                        << "NPOSE " << name << " at " << at << " t " << mScratch[i].mTranslation[0]
                                        << " " << mScratch[i].mTranslation[1] << " " << mScratch[i].mTranslation[2]
                                        << " duration " << mClip->mClip.mDuration << (mTargets[i] ? "" : " (unbound)");
                            }
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
        float at = time - mStart;
        if (mOnce)
        {
            // Combat clips play once at the generator's speed and hold their last pose (samplePose wraps).
            if (!mMeta && mMetaFuture.valid() && mMetaFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                mMeta = mMetaFuture.get();
            const float speed = mMeta ? mMeta->speed() : 1.f;
            const float duration = mClip->mClip.mDuration;
            mSampled = true;
            mPlaybackTime = std::max(0.f, at);
            at = std::max(0.f, at) * speed;
            if (duration > 0.f)
                at = std::min(at, duration - 1e-4f);
        }
        HavokPack::samplePose(*mClip, at, mScratch);
        for (std::size_t i = 0; i < mTargets.size(); ++i)
            if (mTargets[i])
                SkyrimIdle::apply(mScratch[i], *mTargets[i]);
    }
}
