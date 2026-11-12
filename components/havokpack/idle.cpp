#include "idle.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace HavokPack
{
    namespace
    {
        // Lower case, forward slashes, no leading "meshes/".
        std::string normalise(std::string_view path)
        {
            std::string out(path);
            for (char& c : out)
            {
                if (c == '\\')
                    c = '/';
                else
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            constexpr std::string_view prefix = "meshes/";
            if (out.compare(0, prefix.size(), prefix) == 0)
                out.erase(0, prefix.size());
            return out;
        }

        bool endsWith(const std::string& s, std::string_view suffix)
        {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }

    std::string skeletonHkxPath(std::string_view skeletonNifPath)
    {
        std::string path = normalise(skeletonNifPath);
        if (!endsWith(path, ".nif") || path.size() == 4)
            return {};
        path.replace(path.size() - 3, 3, "hkx");
        return "meshes/" + path;
    }

    std::string clipHkxPath(std::string_view skeletonNifPath, std::string_view clipName)
    {
        const std::string path = normalise(skeletonNifPath);
        if (!endsWith(path, ".nif") || clipName.empty())
            return {};
        // <actor folder>/<skeleton folder>/<file>: the actor folder is everything before the last two components.
        const std::size_t fileSlash = path.rfind('/');
        if (fileSlash == std::string::npos || fileSlash == 0)
            return {};
        const std::size_t folderSlash = path.rfind('/', fileSlash - 1);
        if (folderSlash == std::string::npos || folderSlash == 0)
            return {};
        std::string name(clipName);
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "meshes/" + path.substr(0, folderSlash) + "/animations/" + name + ".hkx";
    }

    BoundClip bindClip(const Skeleton& skeleton, Clip clip)
    {
        BoundClip bound;
        bound.mTrackBones.reserve(clip.mNumTransformTracks);
        for (std::uint32_t track = 0; track < clip.mNumTransformTracks; ++track)
        {
            const std::int64_t bone = clip.mTrackToBone.empty() ? static_cast<std::int64_t>(track)
                                                                 : static_cast<std::int64_t>(clip.mTrackToBone[track]);
            if (bone < 0 || bone >= static_cast<std::int64_t>(skeleton.mBoneNames.size()))
                throw Error("idle clip: track " + std::to_string(track) + " names bone " + std::to_string(bone)
                    + ", which skeleton '" + skeleton.mName + "' (" + std::to_string(skeleton.mBoneNames.size())
                    + " bones) does not have");
            std::string name = skeleton.mBoneNames[static_cast<std::size_t>(bone)];
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            bound.mTrackBones.push_back(std::move(name));
        }
        bound.mClip = std::move(clip);
        return bound;
    }

    float loopTime(float time, float duration)
    {
        if (!(duration > 0.f) || !std::isfinite(duration) || !std::isfinite(time))
            return 0.f;
        float t = std::fmod(time, duration);
        if (t < 0.f)
            t += duration;
        // fmod of a tiny negative number can round the sum up to the duration itself
        return t >= duration ? 0.f : t;
    }

    void samplePose(const BoundClip& clip, float time, std::span<Transform> out)
    {
        if (out.size() != clip.mClip.mNumTransformTracks)
            return;
        sample(clip.mClip, loopTime(time, clip.mClip.mDuration), out);
    }
}
