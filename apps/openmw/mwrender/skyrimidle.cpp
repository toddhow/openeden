#include "skyrimidle.hpp"

#include <chrono>
#include <map>
#include <mutex>

#include <components/debug/debuglog.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/misc/loadtrace.hpp>
#include <components/misc/resourcehelpers.hpp>

namespace MWRender::SkyrimIdle
{
    namespace
    {
        struct CacheEntry
        {
            std::shared_ptr<const HavokPack::BoundClip> mClip; // null for a remembered failure
            std::string mFailure;
        };

        std::mutex sCacheMutex;
        std::map<std::string, CacheEntry>& cache()
        {
            static std::map<std::string, CacheEntry> sCache;
            return sCache;
        }

        std::vector<std::uint8_t> readAll(const VFS::Manager& vfs, const std::string& path)
        {
            Files::IStreamPtr stream = vfs.get(VFS::Path::toNormalized(path));
            if (!stream)
                throw HavokPack::Error("the file '" + path + "' is not in the VFS");
            std::vector<std::uint8_t> bytes;
            char buffer[65536];
            while (stream->read(buffer, sizeof(buffer)) || stream->gcount() > 0)
            {
                bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(buffer),
                    reinterpret_cast<const std::uint8_t*>(buffer) + stream->gcount());
                if (bytes.size() > 64u * 1024u * 1024u)
                    throw HavokPack::Error("the file '" + path + "' is too large");
            }
            return bytes;
        }
    }

    namespace
    {
        // The files are read and decoded here; cached by (skeleton, clip) path, failures too. Never throws.
        std::shared_ptr<const HavokPack::BoundClip> decodeIdle(const VFS::Manager& vfs, LoadReport& report)
        {
            const std::string key = report.mSkeletonPath + "|" + report.mClipPath;
            {
                const std::lock_guard lock(sCacheMutex);
                const auto it = cache().find(key);
                if (it != cache().end())
                {
                    report.mFailure = it->second.mFailure;
                    if (it->second.mClip)
                        report.mTracks = it->second.mClip->mClip.mNumTransformTracks;
                    return it->second.mClip;
                }
            }
            CacheEntry entry;
            try
            {
                const HavokPack::Packfile skeletonFile = HavokPack::Packfile::parse(readAll(vfs, report.mSkeletonPath));
                const std::vector<HavokPack::Skeleton> skeletons = HavokPack::readSkeletons(skeletonFile);
                if (skeletons.empty())
                    throw HavokPack::Error("'" + report.mSkeletonPath + "' holds no skeleton");
                const HavokPack::Packfile clipFile = HavokPack::Packfile::parse(readAll(vfs, report.mClipPath));
                std::vector<HavokPack::Clip> clips = HavokPack::readClips(clipFile);
                if (clips.empty())
                    throw HavokPack::Error("'" + report.mClipPath + "' holds no spline clip");
                entry.mClip = std::make_shared<const HavokPack::BoundClip>(
                    HavokPack::bindClip(skeletons.front(), std::move(clips.front())));
                report.mTracks = entry.mClip->mClip.mNumTransformTracks;
            }
            catch (const std::exception& e)
            {
                entry.mFailure = e.what();
            }
            report.mFailure = entry.mFailure;
            const std::lock_guard lock(sCacheMutex);
            cache().emplace(key, entry);
            return entry.mClip;
        }

        std::mutex sFutureMutex;
        std::map<std::string, ClipFuture>& futures()
        {
            static std::map<std::string, ClipFuture> sFutures;
            return sFutures;
        }
    }

    std::shared_ptr<const HavokPack::BoundClip> loadIdle(
        const VFS::Manager& vfs, const std::string& skeletonNifPath, LoadReport& report)
    {
        report = {};
        report.mSkeletonPath = HavokPack::skeletonHkxPath(skeletonNifPath);
        report.mClipPath = HavokPack::clipHkxPath(skeletonNifPath);
        if (report.mSkeletonPath.empty() || report.mClipPath.empty())
        {
            report.mFailure = "no idle clip path for '" + skeletonNifPath + "'";
            return {};
        }
        return decodeIdle(vfs, report);
    }

    ClipFuture loadIdleAsync(const VFS::Manager& vfs, const std::string& skeletonNifPath, std::string_view sexFolder,
        std::string_view clipName)
    {
        const std::string skeletonPath = HavokPack::skeletonHkxPath(skeletonNifPath);
        const std::string clipPath = HavokPack::clipHkxPath(skeletonNifPath, clipName, sexFolder);
        if (skeletonPath.empty() || clipPath.empty())
        {
            std::promise<std::shared_ptr<const HavokPack::BoundClip>> none;
            none.set_value(nullptr);
            return none.get_future().share();
        }
        const std::string key = skeletonPath + "|" + clipPath;
        const std::lock_guard lock(sFutureMutex);
        const auto it = futures().find(key);
        if (it != futures().end())
            return it->second;
        // Decoded once on a worker (about the first actor's frame), shared by every actor of this skeleton and sex.
        ClipFuture future = std::async(std::launch::async, [&vfs, skeletonPath, clipPath] {
            LoadReport report;
            report.mSkeletonPath = skeletonPath;
            report.mClipPath = clipPath;
            auto clip = decodeIdle(vfs, report);
            if (clip)
                Log(Debug::Info) << "Skyrim idle clip '" << clipPath << "' decoded: " << report.mTracks << " tracks, "
                                 << clip->mClip.mDuration << " s";
            else
                Log(Debug::Info) << "Skyrim actors keep their rest pose: " << report.mFailure;
            return clip;
        }).share();
        futures().emplace(key, future);
        return future;
    }

    void apply(const HavokPack::Transform& from, Anim::Transform& to)
    {
        to.translation = vsg::vec3(from.mTranslation[0], from.mTranslation[1], from.mTranslation[2]);
        to.setAttitude(vsg::quat(from.mRotation[0], from.mRotation[1], from.mRotation[2], from.mRotation[3]));
        to.scale = vsg::vec3(from.mScale[0], from.mScale[1], from.mScale[2]);
    }

    Controller::Controller(std::shared_ptr<const HavokPack::BoundClip> clip,
        std::vector<vsg::ref_ptr<Anim::Transform>> targets, float phase)
        : mClip(std::move(clip))
        , mTargets(std::move(targets))
        , mPhase(phase)
    {
        hints.autoPlay = true;
        if (mClip)
            mTargets.resize(mClip->mClip.mNumTransformTracks);
        for (const auto& target : mTargets)
            if (target)
                ++mBound;
        mScratch.resize(mTargets.size());
    }

    Controller::Controller(ClipFuture clip, BoneMap bones, float phaseFraction)
        : mPending(true)
        , mFuture(std::move(clip))
        , mBones(std::move(bones))
        , mPhaseFraction(phaseFraction)
    {
        hints.autoPlay = true;
    }

    bool Controller::resolve() const
    {
        if (!mFuture.valid())
        {
            mPending = false;
            return false;
        }
        if (mFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        mPending = false;
        mClip = mFuture.get();
        mFuture = {};
        if (mClip)
        {
            mTargets.resize(mClip->mClip.mNumTransformTracks);
            for (std::size_t i = 0; i < mTargets.size(); ++i)
            {
                const auto it = mBones.find(mClip->mTrackBones[i]);
                if (it != mBones.end())
                {
                    mTargets[i] = it->second;
                    ++mBound;
                }
            }
            mScratch.resize(mTargets.size());
            mPhase = mPhaseFraction * mClip->mClip.mDuration;
        }
        mBones = {};
        return mClip && mBound > 0;
    }

    void Controller::run(vsg::Object&, float time) const
    {
        if (mPending && !resolve())
            return;
        if (!mClip || mBound == 0)
            return;
        Misc::LoadTrace::Scope trace("skyrimidle.run");
        HavokPack::samplePose(*mClip, time + mPhase, mScratch);
        for (std::size_t i = 0; i < mTargets.size(); ++i)
            if (mTargets[i])
                apply(mScratch[i], *mTargets[i]);
    }
}
