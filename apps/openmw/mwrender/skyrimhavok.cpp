#include "skyrimhavok.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/havokpack/animation.hpp>
#include <components/havokpack/idle.hpp>
#include <components/havokpack/objectproject.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/nifscene/sequence.hpp>

namespace MWRender::SkyrimHavok
{
    namespace
    {
        bool equalNoCase(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        std::vector<std::uint8_t> readAll(const VFS::Manager& vfs, const std::string& path)
        {
            Files::IStreamPtr stream = vfs.get(VFS::Path::toNormalized(path));
            if (!stream)
                throw HavokPack::Error("the file '" + path + "' is not in the VFS");
            std::vector<std::uint8_t> bytes;
            char buffer[16384];
            while (stream->read(buffer, sizeof(buffer)) || stream->gcount() > 0)
            {
                bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(buffer),
                    reinterpret_cast<const std::uint8_t*>(buffer) + stream->gcount());
                if (bytes.size() > 4u * 1024u * 1024u)
                    throw HavokPack::Error("the file '" + path + "' is too large");
            }
            return bytes;
        }
    }

    NifScene::Sequence sequenceFromSampler(std::string name, float duration,
        const std::vector<std::string>& nodeNames, const Sampler& sampler)
    {
        NifScene::Sequence sequence;
        sequence.mName = std::move(name);
        sequence.mStart = 0.f;
        sequence.mFrequency = 1.f;
        sequence.mCycleType = 2; // clamp: the animation plays once and holds its end
        if (!std::isfinite(duration) || duration <= 0.f || duration > kMaxClipSeconds || !sampler)
            return sequence;
        sequence.mStop = duration;
        std::vector<std::size_t> tracks; // the track index of each sequence track
        for (std::size_t i = 0; i < nodeNames.size(); ++i)
        {
            if (nodeNames[i].empty())
                continue;
            NifScene::Track track;
            track.mNodeName = nodeNames[i];
            sequence.mTracks.push_back(std::move(track));
            tracks.push_back(i);
        }
        if (tracks.empty())
            return sequence;
        std::vector<float> times;
        for (float t = 0.f; t < duration; t += kKeyStep)
            times.push_back(t);
        times.push_back(duration);
        std::vector<HavokPack::Transform> pose(nodeNames.size());
        for (float t : times)
        {
            sampler(t, pose);
            for (std::size_t k = 0; k < tracks.size(); ++k)
            {
                const HavokPack::Transform& at = pose[tracks[k]];
                NifScene::Track& track = sequence.mTracks[k];
                NifScene::KeyVec3 translation;
                translation.mTime = t;
                translation.mValue = { at.mTranslation[0], at.mTranslation[1], at.mTranslation[2] };
                track.mTranslations.push_back(translation);
                NifScene::KeyQuat rotation;
                rotation.mTime = t;
                rotation.mValue = { at.mRotation[0], at.mRotation[1], at.mRotation[2], at.mRotation[3] };
                track.mRotations.push_back(rotation);
            }
        }
        return sequence;
    }

    std::size_t addSequences(NifScene::Scene& scene, const VFS::Manager& vfs, const std::string& modelPath)
    {
        std::size_t added = 0;
        try
        {
            const std::string projectPath = HavokPack::objectProjectPath(modelPath);
            if (projectPath.empty() || !vfs.exists(VFS::Path::toNormalized(projectPath)))
                return 0;
            const std::vector<std::uint8_t> projectBytes = readAll(vfs, projectPath);
            HavokPack::ObjectProject project;
            if (!HavokPack::parseObjectProject(
                    std::string_view(reinterpret_cast<const char*>(projectBytes.data()), projectBytes.size()), project))
            {
                Log(Debug::Verbose) << "Skyrim Havok object " << modelPath << ": the project text is not usable";
                return 0;
            }
            const std::string folder = HavokPack::objectFolder(modelPath);
            const HavokPack::Packfile skeletonFile = HavokPack::Packfile::parse(readAll(vfs, folder + project.mSkeleton));
            const std::vector<HavokPack::Skeleton> skeletons = HavokPack::readSkeletons(skeletonFile);
            if (skeletons.empty())
                return 0;
            for (const std::string& clipPath : project.mClips)
            {
                if (scene.mSequences.size() >= kMaxSequences)
                    break;
                const HavokPack::Packfile clipFile = HavokPack::Packfile::parse(readAll(vfs, folder + clipPath));
                std::vector<HavokPack::Clip> clips = HavokPack::readClips(clipFile);
                if (clips.empty())
                    continue;
                const HavokPack::BoundClip bound = HavokPack::bindClip(skeletons.front(), std::move(clips.front()));
                std::vector<std::string> nodeNames;
                for (const std::string& bone : bound.mTrackBones)
                {
                    std::string node;
                    for (const NifScene::Node& candidate : scene.mNodes)
                        if (!candidate.mName.empty() && equalNoCase(candidate.mName, bone))
                        {
                            node = candidate.mName;
                            break;
                        }
                    nodeNames.push_back(std::move(node));
                }
                const Sampler sampler = [&bound](float time, std::span<HavokPack::Transform> out) {
                    HavokPack::sample(bound.mClip, time, out);
                };
                for (const std::string& event : HavokPack::eventNamesOfClip(HavokPack::clipStem(clipPath)))
                {
                    if (NifScene::Sequences::find(scene, event) != NifScene::Sequences::sNoSequence)
                        continue;
                    NifScene::Sequence sequence = sequenceFromSampler(event, bound.mClip.mDuration, nodeNames, sampler);
                    if (sequence.mTracks.empty())
                        continue;
                    // What the clip's first pose differs from the node's own rest pose by: a unit or axis mismatch
                    // between the clip files and the model shows up here (translation in units, rotation as the
                    // largest difference of the matrix entries).
                    for (const NifScene::Track& track : sequence.mTracks)
                        for (const NifScene::Node& node : scene.mNodes)
                            if (node.mName == track.mNodeName)
                            {
                                const NifScene::Transform at
                                    = NifScene::Sequences::sampleLocal(track, node.mTransform, 0.f);
                                float rotation = 0.f;
                                for (std::size_t i = 0; i < 9; ++i)
                                    rotation = std::max(rotation, std::abs(at.mRotation[i] - node.mTransform.mRotation[i]));
                                float translation = 0.f;
                                for (std::size_t i = 0; i < 3; ++i)
                                    translation = std::max(
                                        translation, std::abs(at.mTranslation[i] - node.mTransform.mTranslation[i]));
                                Log(Debug::Info) << "Skyrim Havok object " << modelPath << ": '" << sequence.mName
                                                 << "' drives " << node.mName << ", " << sequence.mStop
                                                 << " s, first pose differs from the node by " << translation
                                                 << " units and " << rotation << " in the rotation matrix";
                                break;
                            }
                    scene.mSequences.push_back(std::move(sequence));
                    ++added;
                }
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Verbose) << "Skyrim Havok object " << modelPath << " is not animated: " << e.what();
        }
        return added;
    }
}
