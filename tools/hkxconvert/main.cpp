// hkxconvert (task 0119, research note 0054): reads the player's own Havok .hkx files (Skyrim Special Edition
// packfiles) and writes what the clean-room decoder in components/havokpack makes of them. A developer tool, built only
// with -DOPENEDEN_BUILD_HKXCONVERT=ON.
//
//   hkxconvert info  <file.hkx>
//   hkxconvert gltf  <clip.hkx> <skeleton.hkx> <out.gltf> [--clip N] [--skeleton N] [--sidecar <project.txt>]
//   hkxconvert pose  <clip.hkx> <skeleton.hkx> <out.txt> <t0,t1,...> [--clip N] [--skeleton N]
//
// Nothing is written beside the input. The outputs are derived from the user's game data, so a path inside a git work
// tree is refused: keep them in a work folder outside the repository.

#include <components/havokpack/animation.hpp>
#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/export.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/havokpack/skeleton.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot open " + path.string());
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    void refuseInsideGitWorkTree(const std::filesystem::path& out)
    {
        std::filesystem::path dir = std::filesystem::absolute(out).parent_path();
        while (!dir.empty())
        {
            if (std::filesystem::exists(dir / ".git"))
                throw std::runtime_error("refusing to write inside a git work tree (" + dir.string()
                    + "): output derived from game data stays outside the repository");
            const auto parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }
    }

    std::size_t optionValue(const std::vector<std::string>& args, const std::string& name)
    {
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == name)
                return static_cast<std::size_t>(std::stoul(args[i + 1]));
        return 0;
    }

    std::string optionString(const std::vector<std::string>& args, const std::string& name)
    {
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == name)
                return args[i + 1];
        return {};
    }

    std::string readText(const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        return std::string(bytes.begin(), bytes.end());
    }

    // The sidecar data of a clip file: the generators of the project named like the file whose animation has the clip's
    // duration, and the root motion beside the project file (boundanims/anims_<project>.txt) when that exists.
    HavokPack::ClipLink linkSidecar(const std::filesystem::path& projectFile, const std::filesystem::path& clipFile,
        float duration, HavokPack::AnimationProject& project, HavokPack::MotionTable& motion)
    {
        project = HavokPack::readAnimationProject(readText(projectFile));
        const auto motionFile = projectFile.parent_path() / "boundanims" / ("anims_" + projectFile.filename().string());
        if (std::filesystem::exists(motionFile))
            motion = HavokPack::readMotionTable(readText(motionFile));
        else
            std::cout << "no " << motionFile.string() << ": root motion skipped\n";
        return HavokPack::linkClip(project, motion, clipFile.stem().string(), duration);
    }

    int info(const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        const auto pack = HavokPack::Packfile::parse(bytes);
        std::map<std::string, std::size_t> counts;
        for (const auto& o : pack.objects())
            ++counts[std::string(pack.className(o))];
        std::cout << path.filename().string() << ": content version " << pack.versionString() << ", "
                  << pack.objects().size() << " objects\n";
        for (const auto& [name, n] : counts)
            std::cout << "  " << n << " x " << name << "\n";
        for (const auto& s : HavokPack::readSkeletons(pack))
            std::cout << "skeleton '" << s.mName << "': " << s.mBoneNames.size() << " bones\n";
        for (const auto& c : HavokPack::readClips(pack))
            std::cout << "clip: " << c.mNumTransformTracks << " transform tracks, " << c.mNumFloatTracks
                      << " float tracks, " << c.mNumFrames << " frames, " << c.mDuration << " s, " << c.mBlocks.size()
                      << " blocks, skeleton '" << c.mSkeletonName << "'\n";
        return 0;
    }

    struct Loaded
    {
        HavokPack::Skeleton mSkeleton;
        HavokPack::Clip mClip;
    };

    Loaded load(const std::vector<std::string>& args)
    {
        const auto clipBytes = readFile(args[1]);
        const auto clips = HavokPack::readClips(HavokPack::Packfile::parse(clipBytes));
        const std::size_t clipIndex = optionValue(args, "--clip");
        if (clipIndex >= clips.size())
            throw std::runtime_error("the clip file has " + std::to_string(clips.size()) + " clips");
        const auto skeletonBytes = readFile(args[2]);
        const auto skeletons = HavokPack::readSkeletons(HavokPack::Packfile::parse(skeletonBytes));
        const std::size_t skeletonIndex = optionValue(args, "--skeleton");
        if (skeletonIndex >= skeletons.size())
            throw std::runtime_error("the skeleton file has " + std::to_string(skeletons.size()) + " skeletons");
        return { skeletons[skeletonIndex], clips[clipIndex] };
    }
}

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    try
    {
        if (args.size() >= 2 && args[0] == "info")
            return info(args[1]);
        if (args.size() >= 4 && args[0] == "gltf")
        {
            refuseInsideGitWorkTree(args[3]);
            const Loaded l = load(args);
            const std::string name = std::filesystem::path(args[1]).stem().string();
            HavokPack::AnimationProject project;
            HavokPack::MotionTable motion;
            HavokPack::ClipLink link;
            const std::string sidecar = optionString(args, "--sidecar");
            if (!sidecar.empty())
            {
                link = linkSidecar(sidecar, args[1], l.mClip.mDuration, project, motion);
                std::cout << "sidecar: " << link.mClips.size() << " generator(s) named '" << name
                          << "' with the clip's duration, " << (link.mMotion ? "root motion found" : "no root motion")
                          << "\n";
            }
            std::ofstream out(args[3], std::ios::binary);
            out << HavokPack::writeGltf(l.mSkeleton, l.mClip, name, link);
            std::cout << "wrote " << args[3] << " (" << l.mClip.mNumFrames << " frames, " << l.mClip.mNumTransformTracks
                      << " tracks)\n";
            return 0;
        }
        if (args.size() >= 5 && args[0] == "pose")
        {
            refuseInsideGitWorkTree(args[3]);
            const Loaded l = load(args);
            std::ofstream out(args[3]);
            std::stringstream times(args[4]);
            std::string item;
            while (std::getline(times, item, ','))
            {
                const float t = std::stof(item);
                const auto world = HavokPack::toWorld(l.mSkeleton, HavokPack::poseAt(l.mSkeleton, l.mClip, t));
                out << "time " << t << "\n";
                for (std::size_t i = 0; i < world.size(); ++i)
                    out << l.mSkeleton.mBoneNames[i] << "|" << l.mSkeleton.mParents[i] << "|"
                        << world[i].mTranslation[0] << "|" << world[i].mTranslation[1] << "|"
                        << world[i].mTranslation[2] << "\n";
            }
            return 0;
        }
        std::cerr
            << "usage: hkxconvert info <file.hkx>\n"
               "       hkxconvert gltf <clip.hkx> <skeleton.hkx> <out.gltf> [--clip N] [--skeleton N] [--sidecar "
               "<project.txt>]\n"
               "       hkxconvert pose <clip.hkx> <skeleton.hkx> <out.txt> <t0,t1,...> [--clip N] [--skeleton N]\n";
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "hkxconvert: " << e.what() << "\n";
        return 1;
    }
}
