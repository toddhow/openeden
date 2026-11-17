// Task 0259 (werewolf): read-only survey of the installed data for the werewolf beast: every clip of its folder, the
// sidecar link by the generators `WW <clip>.hkx<nn>`, events, root motion, and the skin bounds. Prints SKYRIMSE-WW-*
// lines. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <components/bsa/compressedbsafile.hpp>
#include <components/havokpack/animation.hpp>
#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/havokpack/skeleton.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/vfs/pathutil.hpp>

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    std::string lowered(std::string s)
    {
        for (char& c : s)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // The generator of a clip file `ww_<name>`: the sidecar names it `WW <Name>.hkx`, `.hkx00` ... `.hkx10`. Case is
    // ignored.
    std::vector<std::string> aliasesOf(const std::string& stem)
    {
        std::vector<std::string> out;
        if (stem.rfind("ww_", 0) != 0)
            return out;
        const std::string rest = stem.substr(3);
        out.push_back("ww " + rest + ".hkx");
        for (int i = 0; i <= 10; ++i)
            out.push_back("ww " + rest + ".hkx" + (i < 10 ? "0" : "") + std::to_string(i));
        return out;
    }

    TEST(SkyrimWerewolfSurvey, everyClipOfTheBeastFolder)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path path = fs::path(env) / "Skyrim - Animations.bsa";
        if (!fs::exists(path))
            GTEST_SKIP() << "Skyrim - Animations.bsa is missing";
        Bsa::CompressedBSAFile archive;
        archive.open(path);
        const auto& list = archive.getList();
        std::map<std::string, std::size_t> files;
        for (std::size_t i = 0; i < list.size(); ++i)
            files[lowered(std::string(list[i].name()))] = i;
        const auto read = [&](const std::string& name) -> std::string {
            const auto it = files.find(name);
            if (it == files.end())
                return {};
            auto stream = archive.getFile(&list[it->second]);
            return std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        };
        const auto span = [](const std::string& bytes) {
            return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        };
        const std::string skeletonBytes = read("meshes/actors/werewolfbeast/character assets/skeleton.hkx");
        if (!skeletonBytes.empty())
        {
            const HavokPack::Packfile file = HavokPack::Packfile::parse(span(skeletonBytes));
            const auto skeletons = HavokPack::readSkeletons(file);
            std::cout << "SKYRIMSE-WW-SKELETON bones "
                      << (skeletons.empty() ? std::size_t(0) : skeletons.front().mBoneNames.size()) << std::endl;
        }
        const std::string sidecar = read("meshes/animationdata/werewolfbeastproject.txt");
        const std::string motionText = read("meshes/animationdata/boundanims/anims_werewolfbeastproject.txt");
        ASSERT_FALSE(sidecar.empty());
        const HavokPack::AnimationProject project = HavokPack::readAnimationProject(sidecar);
        const HavokPack::MotionTable motion = HavokPack::readMotionTable(motionText);
        std::cout << "SKYRIMSE-WW-GENS generators " << project.mClips.size() << " motion blocks "
                  << motion.mBlocks.size() << std::endl;
        const std::string prefix = "meshes/actors/werewolfbeast/animations/";
        for (const auto& [name, index] : files)
        {
            if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 5)
                continue;
            const std::string stem = name.substr(prefix.size(), name.size() - prefix.size() - 4);
            float duration = 0.f;
            std::uint32_t tracks = 0;
            try
            {
                const std::string bytes = read(name);
                const HavokPack::Packfile file = HavokPack::Packfile::parse(span(bytes));
                const auto clips = HavokPack::readClips(file);
                if (!clips.empty())
                {
                    duration = clips.front().mDuration;
                    tracks = clips.front().mNumTransformTracks;
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "SKYRIMSE-WW-CLIP " << stem << " UNREADABLE " << e.what() << std::endl;
                continue;
            }
            HavokPack::ClipLink link = HavokPack::linkClip(project, motion, stem, duration);
            std::string via = "own name";
            if (link.mClips.empty())
                for (const std::string& alias : aliasesOf(stem))
                {
                    link = HavokPack::linkClip(project, motion, alias, duration);
                    if (!link.mClips.empty())
                    {
                        via = alias;
                        break;
                    }
                }
            std::string events;
            for (const HavokPack::SidecarClip* generator : link.mClips)
                for (const HavokPack::ClipEvent& event : generator->mEvents)
                    events += event.mName + "@" + std::to_string(event.mTime).substr(0, 5) + " ";
            float stride = 0.f;
            if (link.mMotion != nullptr && !link.mMotion->mTranslation.empty())
            {
                const auto& last = link.mMotion->mTranslation.back();
                stride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
            }
            std::cout << "SKYRIMSE-WW-CLIP " << stem << " dur " << duration << " tracks " << tracks << " via " << via
                      << " gens " << link.mClips.size() << " stride " << stride << " ev " << events << std::endl;
        }
    }

    TEST(SkyrimWerewolfSurvey, boundsOfTheSkin)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::map<std::string, std::string> wanted{
            { "body", "meshes/actors/werewolfbeast/character assets/malebodywerewolf_1.nif" },
            { "head", "meshes/actors/werewolfbeast/character assets/maleheadwerewolff.nif" },
        };
        for (const char* archiveName : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" })
        {
            const fs::path path = fs::path(env) / archiveName;
            if (!fs::exists(path))
                continue;
            Bsa::CompressedBSAFile archive;
            archive.open(path);
            for (const auto& entry : archive.getList())
            {
                const std::string name = lowered(std::string(entry.name()));
                for (const auto& [label, wantedName] : wanted)
                {
                    if (name != wantedName)
                        continue;
                    auto stream = archive.getFile(&entry);
                    std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                    const NifScene::Scene scene
                        = NifScene::readScene(std::move(input), VFS::Path::Normalized("measure.nif"), {});
                    NifScene::Flat::Limits limits;
                    limits.mSkinnedAtRest = true;
                    const NifScene::Flat::FlatModel model = NifScene::Flat::flatten(scene, limits);
                    const auto& lo = model.mBoundsMin;
                    const auto& hi = model.mBoundsMax;
                    float slabLo = 1e9f, slabHi = -1e9f, slabYLo = 1e9f, slabYHi = -1e9f;
                    const float zLow = lo[2] + (hi[2] - lo[2]) * 0.2f, zHigh = lo[2] + (hi[2] - lo[2]) * 0.6f;
                    for (const auto& shape : model.mShapes)
                        for (const auto& v : shape.mPositions)
                            if (v[2] >= zLow && v[2] <= zHigh)
                            {
                                slabLo = std::min(slabLo, v[0]);
                                slabHi = std::max(slabHi, v[0]);
                                slabYLo = std::min(slabYLo, v[1]);
                                slabYHi = std::max(slabYHi, v[1]);
                            }
                    std::cout << "SKYRIMSE-WW-BOUNDS " << label << " shapes " << model.mShapes.size() << " x "
                              << hi[0] - lo[0] << " y " << hi[1] - lo[1] << " z " << lo[2] << " " << hi[2] << " ext "
                              << hi[2] - lo[2] << " slab x " << slabHi - slabLo << " y " << slabYHi - slabYLo
                              << std::endl;
                }
            }
        }
    }
}
