// Task 0253 (dragons): read-only survey of the installed data for the dragon: every clip of the dragon folder
// (duration, track count, skeleton, sidecar link, events, root motion) and the skin bounds. Prints SKYRIMSE-DRAGON-*
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

    TEST(SkyrimDragonSurvey, everyClipOfTheDragonFolder)
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
        const std::string skeletonBytes = read("meshes/actors/dragon/character assets/skeleton.hkx");
        if (!skeletonBytes.empty())
        {
            const HavokPack::Packfile file = HavokPack::Packfile::parse(span(skeletonBytes));
            const auto skeletons = HavokPack::readSkeletons(file);
            std::cout << "SKYRIMSE-DRAGON-SKELETON bones "
                      << (skeletons.empty() ? std::size_t(0) : skeletons.front().mBoneNames.size()) << " name "
                      << (skeletons.empty() ? std::string() : skeletons.front().mName) << std::endl;
        }
        const std::string sidecar = read("meshes/animationdata/dragonproject.txt");
        const std::string motionText = read("meshes/animationdata/boundanims/anims_dragonproject.txt");
        ASSERT_FALSE(sidecar.empty());
        const HavokPack::AnimationProject project = HavokPack::readAnimationProject(sidecar);
        const HavokPack::MotionTable motion = HavokPack::readMotionTable(motionText);
        std::cout << "SKYRIMSE-DRAGON-GENS generators " << project.mClips.size() << " motion blocks "
                  << motion.mBlocks.size() << std::endl;
        const std::string prefix = "meshes/actors/dragon/animations/";
        for (const auto& [name, index] : files)
        {
            if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 5)
                continue;
            const std::string stem = name.substr(prefix.size(), name.size() - prefix.size() - 4);
            if (stem.rfind("phoneme_", 0) == 0 || stem.rfind("mag_", 0) == 0 || stem.rfind("staff_", 0) == 0)
                continue;
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
                std::cout << "SKYRIMSE-DRAGON-CLIP " << stem << " UNREADABLE " << e.what() << std::endl;
                continue;
            }
            const HavokPack::ClipLink link = HavokPack::linkClip(project, motion, stem, duration);
            std::string events;
            for (const HavokPack::SidecarClip* generator : link.mClips)
                for (const HavokPack::ClipEvent& event : generator->mEvents)
                    events += event.mName + "@" + std::to_string(event.mTime).substr(0, 5) + " ";
            float stride = 0.f, rise = 0.f;
            if (link.mMotion != nullptr && !link.mMotion->mTranslation.empty())
            {
                const auto& last = link.mMotion->mTranslation.back();
                stride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                rise = last[3];
            }
            std::cout << "SKYRIMSE-DRAGON-CLIP " << stem << " dur " << duration << " tracks " << tracks << " gens "
                      << link.mClips.size() << " stride " << stride << " rise " << rise << " ev " << events
                      << std::endl;
        }
    }

    TEST(SkyrimDragonSurvey, boundsOfTheSkin)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::map<std::string, std::string> wanted{
            { "dragon", "meshes/actors/dragon/character assets/dragon.nif" },
            { "farskin", "meshes/actors/dragon/character assets/farskin.nif" },
            { "bossdragon", "meshes/actors/dragon/character assets/bossdragon.nif" },
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
                    std::cout << "SKYRIMSE-DRAGON-BOUNDS " << label << " shapes " << model.mShapes.size() << " x "
                              << hi[0] - lo[0] << " y " << hi[1] - lo[1] << " z " << lo[2] << " " << hi[2] << " ext "
                              << hi[2] - lo[2] << " slab x " << slabHi - slabLo << " y " << slabYHi - slabYLo
                              << std::endl;
                }
            }
        }
    }

    // Where the dragons stand: every ACHR whose NPC resolves to the dragon skeleton, with its cell, position and
    // whether it starts disabled. Prints SKYRIMSE-DRAGON-PLACED lines.
    TEST(SkyrimDragonSurvey, whereTheDragonsStand)
    {
        using namespace game_skyrimse;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;
        ActorIndex index;
        std::vector<ParsedPlugin> parsed;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            ASSERT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), actors));
            index.add(actors);
            ParsedPlugin full;
            ASSERT_TRUE(parseRecords(*plugin, order, ParseLimits(), full));
            index.addLeveledLists(full.mLeveledLists);
            parsed.push_back(std::move(full));
        }
        int printed = 0;
        for (const ParsedPlugin& full : parsed)
            for (const CellRecord& cell : full.mCells)
                for (const auto* group : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *group)
                    {
                        if (ref.mKind != RefKind::Actor || index.npc(ref.mBase) == nullptr)
                            continue;
                        ActorIndex::Resolved resolved;
                        if (index.resolve(ref.mBase, 1, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                            continue;
                        const RaceRecord* r = index.race(resolved.mRace);
                        if (r == nullptr)
                            continue;
                        // Also: every enabled creature in the box round Whiterun's plain (for a test place).
                        if (!cell.interior() && cell.mWorld == 60 && !ref.initiallyDisabled()
                            && lowered(r->skeleton(Sex::Male)).find("actors/character/") == std::string::npos
                            && ref.mPosition.x > 12000 && ref.mPosition.x < 42000 && ref.mPosition.y > -36000
                            && ref.mPosition.y < -4000)
                            std::cout << "SKYRIMSE-DRAGON-NEAR npc:" << index.npc(ref.mBase)->mCommon.mEditorId
                                      << " at " << int(ref.mPosition.x) << "," << int(ref.mPosition.y) << ","
                                      << int(ref.mPosition.z) << std::endl;
                        if (lowered(r->skeleton(Sex::Male)).find("actors/dragon/") == std::string::npos)
                            continue;
                        if (printed++ >= 80)
                            continue;
                        std::cout << "SKYRIMSE-DRAGON-PLACED npc:" << index.npc(ref.mBase)->mCommon.mEditorId
                                  << " base " << ref.mBase << " disabled " << (ref.initiallyDisabled() ? "Y" : "n")
                                  << " cell " << (cell.interior() ? cell.mCommon.mEditorId : "exterior") << " world "
                                  << cell.mWorld << " at " << int(ref.mPosition.x) << "," << int(ref.mPosition.y) << ","
                                  << int(ref.mPosition.z) << std::endl;
                    }
    }
}
