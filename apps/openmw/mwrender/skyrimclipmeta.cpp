#include "skyrimclipmeta.hpp"

#include <map>
#include <mutex>

#include <components/debug/debuglog.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender::SkyrimClipMeta
{
    namespace
    {
        struct Project
        {
            HavokPack::AnimationProject mProject;
            HavokPack::MotionTable mMotion;
            std::string mFailure;
        };

        std::string readText(const VFS::Manager& vfs, const std::string& path)
        {
            Files::IStreamPtr stream = vfs.get(VFS::Path::toNormalized(path));
            if (!stream)
                throw HavokPack::Error("the file '" + path + "' is not in the VFS");
            std::string text;
            char buffer[65536];
            while (stream->read(buffer, sizeof(buffer)) || stream->gcount() > 0)
            {
                text.append(buffer, static_cast<std::size_t>(stream->gcount()));
                if (text.size() > 64u * 1024u * 1024u)
                    throw HavokPack::Error("the file '" + path + "' is too large");
            }
            return text;
        }

        std::mutex sProjectMutex;

        std::shared_ptr<const Project> projectFor(const VFS::Manager& vfs, bool female)
        {
            static std::map<bool, std::shared_ptr<const Project>> sCache;
            const std::lock_guard lock(sProjectMutex);
            const auto found = sCache.find(female);
            if (found != sCache.end())
                return found->second;
            auto project = std::make_shared<Project>();
            const std::string name = female ? "defaultfemale" : "defaultmale";
            try
            {
                project->mProject
                    = HavokPack::readAnimationProject(readText(vfs, "meshes/animationdata/" + name + ".txt"));
                project->mMotion = HavokPack::readMotionTable(
                    readText(vfs, "meshes/animationdata/boundanims/anims_" + name + ".txt"));
            }
            catch (const std::exception& e)
            {
                project->mFailure = e.what();
            }
            sCache.emplace(female, project);
            return project;
        }
    }

    std::shared_ptr<const HavokPack::ClipMeta> makeClipMeta(const HavokPack::AnimationProject& project,
        const HavokPack::MotionTable& motion, std::string_view clipStem, float duration)
    {
        auto meta = std::make_shared<HavokPack::ClipMeta>();
        meta->mDuration = duration;
        const HavokPack::ClipLink link = HavokPack::linkClip(project, motion, clipStem, duration);
        if (!link.mClips.empty())
        {
            const HavokPack::SidecarClip& clip = *link.mClips.front();
            meta->mSpeed = clip.mPlaybackSpeed;
            for (const HavokPack::ClipEvent& event : clip.mEvents)
                meta->mEvents.push_back({ event.mName, event.mTime });
        }
        meta->sortEvents();
        return meta;
    }

    MetaFuture loadAsync(const VFS::Manager& vfs, bool female, std::string clipStem, SkyrimIdle::ClipFuture clip)
    {
        return std::async(std::launch::async, [&vfs, female, stem = std::move(clipStem), clip = std::move(clip)] {
            std::shared_ptr<const HavokPack::ClipMeta> result;
            try
            {
                const auto decoded = clip.get();
                if (!decoded)
                    return result;
                const auto project = projectFor(vfs, female);
                if (!project->mFailure.empty())
                    Log(Debug::Warning) << "Skyrim combat clip " << stem << ": no sidecar events: " << project->mFailure;
                result = makeClipMeta(project->mProject, project->mMotion, stem, decoded->mClip.mDuration);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "Skyrim combat clip " << stem << ": " << e.what();
            }
            return result;
        }).share();
    }
}
