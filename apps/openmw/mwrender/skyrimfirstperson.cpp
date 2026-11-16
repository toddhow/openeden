#include "skyrimfirstperson.hpp"

#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/havokpack/idle.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/vfs/manager.hpp>

#include "skyrimidle.hpp"

namespace MWRender::SkyrimFirstPerson
{
    namespace
    {
        // One clip the arms may play: decoded on a worker (SkyrimIdle::loadClipAsync), bound to the arms' bones on
        // the first run after the future is ready.
        struct Slot
        {
            SkyrimIdle::ClipFuture mFuture;
            std::shared_ptr<const HavokPack::BoundClip> mClip;
            std::vector<vsg::ref_ptr<Anim::Transform>> mTargets;
            std::vector<HavokPack::Transform> mScratch;
            std::vector<HavokPack::Transform> mScratchFrom; ///< when the clip is the one a fade comes from
            std::unordered_map<const Anim::Transform*, std::size_t> mTrackOf; ///< bone -> track, for the fade
            bool mResolved = false; ///< the future was read (mClip may be null: the clip failed)
        };
    }

    class ArmsController::Impl
    {
    public:
        Impl(const VFS::Manager& vfs, SkyrimIdle::Controller::BoneMap bones)
            : mVfs(vfs)
            , mBones(std::move(bones))
        {
        }

        const VFS::Manager& mVfs;
        SkyrimIdle::Controller::BoneMap mBones;
        mutable std::mutex mMutex;
        std::map<std::string, Slot> mSlots;
        Timeline mTimeline;
        std::string mLastClip;

        // Caller holds the mutex.
        void ensure(const std::string& name)
        {
            if (name.empty() || mSlots.find(name) != mSlots.end())
                return;
            Slot slot;
            slot.mFuture = SkyrimIdle::loadClipAsync(mVfs, std::string(kSkeletonHkx), clipPath(name));
            mSlots.emplace(name, std::move(slot));
        }

        // Caller holds the mutex. Reads the future when it is ready and binds the clip's tracks to the bones.
        void resolve(Slot& slot)
        {
            if (slot.mResolved || !slot.mFuture.valid()
                || slot.mFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            slot.mResolved = true;
            slot.mClip = slot.mFuture.get();
            slot.mFuture = {};
            if (!slot.mClip)
                return;
            slot.mTargets.resize(slot.mClip->mClip.mNumTransformTracks);
            std::size_t bound = 0;
            for (std::size_t i = 0; i < slot.mTargets.size(); ++i)
            {
                const auto it = mBones.find(slot.mClip->mTrackBones[i]);
                if (it != mBones.end())
                {
                    slot.mTargets[i] = it->second;
                    ++bound;
                }
            }
            slot.mScratch.resize(slot.mTargets.size());
            slot.mScratchFrom.resize(slot.mTargets.size());
            for (std::size_t i = 0; i < slot.mTargets.size(); ++i)
                if (slot.mTargets[i])
                    slot.mTrackOf.emplace(slot.mTargets[i].get(), i);
            if (bound == 0)
                slot.mClip.reset(); // a clip of another skeleton
        }

        // Caller holds the mutex.
        float lengthOf(const std::string& name)
        {
            const auto it = mSlots.find(name);
            if (it == mSlots.end())
                return 0.f;
            resolve(it->second);
            if (!it->second.mResolved)
                return kNotReady;
            return it->second.mClip ? it->second.mClip->mClip.mDuration : 0.f;
        }
    };

    ArmsController::ArmsController(const VFS::Manager& vfs, SkyrimIdle::Controller::BoneMap bones)
        : mImpl(std::make_unique<Impl>(vfs, std::move(bones)))
    {
        hints.autoPlay = true;
    }

    ArmsController::~ArmsController() = default;

    void ArmsController::setIdle(std::string_view name)
    {
        const std::string clip = armsClipName(name);
        if (clip.empty())
            return;
        const std::lock_guard lock(mImpl->mMutex);
        mImpl->ensure(clip);
        mImpl->mTimeline.setIdle(clip);
    }

    void ArmsController::playOnce(std::string_view name)
    {
        const std::string clip = armsClipName(name);
        if (clip.empty())
            return;
        const std::lock_guard lock(mImpl->mMutex);
        mImpl->ensure(clip);
        mImpl->mTimeline.playOnce(clip);
    }

    void ArmsController::prefetch(std::string_view name)
    {
        const std::string clip = armsClipName(name);
        if (clip.empty())
            return;
        const std::lock_guard lock(mImpl->mMutex);
        mImpl->ensure(clip);
    }

    bool ArmsController::playingOnce() const
    {
        const std::lock_guard lock(mImpl->mMutex);
        return mImpl->mTimeline.playingOnce();
    }

    std::string ArmsController::current() const
    {
        const std::lock_guard lock(mImpl->mMutex);
        return mImpl->mLastClip;
    }

    void ArmsController::run(vsg::Object&, float time) const
    {
        Impl& impl = *mImpl;
        const std::lock_guard lock(impl.mMutex);
        const Timeline::Frame frame
            = impl.mTimeline.pick(time, [&impl](const std::string& name) { return impl.lengthOf(name); });
        if (frame.clip.empty())
            return;
        const auto it = impl.mSlots.find(frame.clip);
        if (it == impl.mSlots.end())
            return;
        impl.resolve(it->second);
        Slot& slot = it->second;
        if (!slot.mClip)
            return;
        Misc::LoadTrace::Scope trace("skyrimfirstperson.run");
        impl.mLastClip = frame.clip;
        HavokPack::samplePose(*slot.mClip, frame.time, slot.mScratch);

        // Task 0247: a cross-fade with the clip the arms come from, bone by bone (a bone the old clip does not move
        // takes the new pose as it is). Only when that clip has decoded; otherwise the cut it replaces.
        Slot* from = nullptr;
        if (frame.blend < 1.f && !frame.from.empty())
        {
            const auto fromIt = impl.mSlots.find(frame.from);
            if (fromIt != impl.mSlots.end())
            {
                impl.resolve(fromIt->second);
                if (fromIt->second.mClip)
                {
                    from = &fromIt->second;
                    HavokPack::samplePose(*from->mClip, frame.fromTime, from->mScratchFrom);
                }
            }
        }
        for (std::size_t i = 0; i < slot.mTargets.size(); ++i)
        {
            if (!slot.mTargets[i])
                continue;
            if (from != nullptr)
            {
                const auto track = from->mTrackOf.find(slot.mTargets[i].get());
                if (track != from->mTrackOf.end())
                {
                    SkyrimIdle::apply(
                        blendTransform(from->mScratchFrom[track->second], slot.mScratch[i], frame.blend),
                        *slot.mTargets[i]);
                    continue;
                }
            }
            SkyrimIdle::apply(slot.mScratch[i], *slot.mTargets[i]);
        }
    }
}
