// Task 0290: read-only surveys of the installed data for the Dwarven centurions and the DLC2 boar riekling. Prints
// lines with the prefixes SKYRIMSE-CREATURE4-*. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <components/bsa/compressedbsafile.hpp>
#include <components/havokpack/animation.hpp>
#include <components/havokpack/animationdata.hpp>
#include <components/havokpack/export.hpp>
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
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    std::string lower(std::string s)
    {
        for (char& c : s)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    TEST(SkyrimCreaturesFourthSurvey, theClipsOfTheCandidateProjects)
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
            files[lower(std::string(list[i].name()))] = i;
        const auto read = [&](const std::string& name) -> std::string {
            const auto it = files.find(name);
            if (it == files.end())
                return {};
            auto stream = archive.getFile(&list[it->second]);
            return std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        };
        struct Candidate
        {
            const char* mFolder;
            const char* mProject;
        };
        const Candidate candidates[]
            = { { "dwarvensteamcenturion", "steamproject" }, { "dwarvenspherecenturion", "spherecenturion" },
                  { "dwarvenspidercenturion", "dwarvenspidercenturionproject" },
                  { "dlc02/boarriekling", "boarproject" }, { "dlc02/riekling", "rieklingproject" } };
        for (const Candidate& c : candidates)
        {
            const std::string sidecar = read(std::string("meshes/animationdata/") + c.mProject + ".txt");
            const std::string motionText
                = read(std::string("meshes/animationdata/boundanims/anims_") + c.mProject + ".txt");
            std::cout << "SKYRIMSE-CREATURE4-CLIPS project " << c.mProject << " sidecar " << sidecar.size()
                      << " motion " << motionText.size() << std::endl;
            HavokPack::AnimationProject project;
            HavokPack::MotionTable motion;
            if (!sidecar.empty())
            {
                project = HavokPack::readAnimationProject(sidecar);
                motion = HavokPack::readMotionTable(motionText);
            }
            std::cout << "SKYRIMSE-CREATURE4-CLIPS project " << c.mProject << " generators " << project.mClips.size()
                      << " motion blocks " << motion.mBlocks.size() << std::endl;
            for (const HavokPack::SidecarClip& generator : project.mClips)
            {
                bool generatorHit = false;
                for (const HavokPack::ClipEvent& event : generator.mEvents)
                    generatorHit = generatorHit || event.mName == "HitFrame";
                const HavokPack::MotionBlock* block = motion.find(generator.mCacheIndex);
                float blockStride = 0.f;
                if (block != nullptr && !block->mTranslation.empty())
                {
                    const auto& last = block->mTranslation.back();
                    blockStride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                }
                std::cout << "SKYRIMSE-CREATURE4-GEN " << c.mProject << " " << generator.mName << " cache "
                          << generator.mCacheIndex << " blockdur " << (block != nullptr ? block->mDuration : -1.f)
                          << " stride " << blockStride << " speed " << generator.mPlaybackSpeed << " events "
                          << generator.mEvents.size() << " hit " << (generatorHit ? "Y" : "n") << std::endl;
            }
            const std::string prefix = std::string("meshes/actors/") + c.mFolder + "/animations/";
            for (const auto& [name, index] : files)
            {
                if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 5)
                    continue;
                const std::string stem = name.substr(prefix.size(), name.size() - prefix.size() - 4);
                float duration = 0.f;
                try
                {
                    const std::string bytes = read(name);
                    const HavokPack::Packfile file = HavokPack::Packfile::parse(std::span<const std::uint8_t>(
                        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
                    const auto clips = HavokPack::readClips(file);
                    duration = clips.empty() ? 0.f : clips.front().mDuration;
                    if (!clips.empty() && clips.front().mNumTransformTracks > 0)
                    {
                        std::vector<HavokPack::Transform> a(clips.front().mNumTransformTracks), z = a;
                        HavokPack::sample(clips.front(), 0.f, a);
                        HavokPack::sample(clips.front(), duration, z);
                        std::cout << "SKYRIMSE-CREATURE4-OWNROOT " << c.mProject << " " << stem << " dx "
                                  << z[0].mTranslation[0] - a[0].mTranslation[0] << " dy "
                                  << z[0].mTranslation[1] - a[0].mTranslation[1] << std::endl;
                    }
                }
                catch (const std::exception& e)
                {
                    std::cout << "SKYRIMSE-CREATURE4-CLIPS " << c.mProject << " " << stem << " UNREADABLE " << e.what()
                              << std::endl;
                    continue;
                }
                const HavokPack::ClipLink link = HavokPack::linkClip(project, motion, stem, duration);
                bool hit = false;
                std::string events;
                for (const HavokPack::SidecarClip* generator : link.mClips)
                    for (const HavokPack::ClipEvent& event : generator->mEvents)
                    {
                        hit = hit || event.mName == "HitFrame";
                        if (events.size() < 120)
                            events += event.mName + "@" + std::to_string(event.mTime).substr(0, 5) + " ";
                    }
                float stride = 0.f;
                if (link.mMotion != nullptr && !link.mMotion->mTranslation.empty())
                {
                    const auto& last = link.mMotion->mTranslation.back();
                    stride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                }
                std::cout << "SKYRIMSE-CREATURE4-CLIPS " << c.mProject << " " << stem << " dur " << duration << " gens "
                          << link.mClips.size() << " stride " << stride << " hit " << (hit ? "Y" : "n") << " ev "
                          << events << std::endl;
            }
        }
    }

    TEST(SkyrimCreaturesFourthSurvey, boundsOfEverySkinForTheRadius)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::map<std::string, std::string> wanted{
            { "steam", "meshes/actors/dwarvensteamcenturion/character assets/steamcenturion.nif" },
            { "sphere", "meshes/actors/dwarvenspherecenturion/character assets/spherecenturion.nif" },
            { "boar", "meshes/actors/dlc02/boarriekling/character assets/boarriekling.nif" },
            { "boar2", "meshes/actors/dlc02/boarriekling/character assets/boar.nif" },
            { "riekling", "meshes/actors/dlc02/riekling/character assets/riekling01.nif" },
        };
        std::set<std::string> done;
        for (const char* archiveName : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" })
        {
            const fs::path path = fs::path(env) / archiveName;
            if (!fs::exists(path))
                continue;
            Bsa::CompressedBSAFile archive;
            archive.open(path);
            for (const auto& entry : archive.getList())
            {
                const std::string name = lower(std::string(entry.name()));
                for (const auto& [label, wantedName] : wanted)
                {
                    if (name != wantedName || done.count(label) != 0)
                        continue;
                    done.insert(label);
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
                    // The sideways width of the mid-body: the vertices between 20 and 60 percent of the height.
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
                    std::cout << "SKYRIMSE-CREATURE4-SLAB " << label << " xext " << slabHi - slabLo << " yext "
                              << slabYHi - slabYLo << std::endl;
                    std::cout << "SKYRIMSE-CREATURE4-BOUNDS " << label << " shapes " << model.mShapes.size() << " x "
                              << lo[0] << " " << hi[0] << " ext " << hi[0] - lo[0] << " y " << lo[1] << " " << hi[1]
                              << " ext " << hi[1] - lo[1] << " z " << lo[2] << " " << hi[2] << " ext " << hi[2] - lo[2]
                              << std::endl;
                }
            }
        }
        EXPECT_FALSE(done.empty());
    }

    // The boar riekling has no sidecar, so no root-motion stride: how far each foot bone travels along the model's y
    // axis over one cycle (peak to peak, model space) is the survey's only measure of its gait.
    TEST(SkyrimCreaturesFourthSurvey, theBoarFootExcursion)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0 || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        std::map<std::string, std::string> found;
        for (const char* archiveName : { "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa", "Skyrim - Animations.bsa" })
        {
            const fs::path path = fs::path(env) / archiveName;
            if (!fs::exists(path))
                continue;
            Bsa::CompressedBSAFile archive;
            archive.open(path);
            for (const auto& entry : archive.getList())
            {
                const std::string name = lower(std::string(entry.name()));
                if (name.rfind("meshes/actors/dlc02/boarriekling/", 0) != 0 || name.size() < 4
                    || name.compare(name.size() - 4, 4, ".hkx") != 0)
                    continue;
                auto stream = archive.getFile(&entry);
                found[name] = std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            }
        }
        const auto parse = [&](const std::string& name) {
            const std::string& bytes = found.at(name);
            return HavokPack::Packfile::parse(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        };
        const auto skeletons
            = HavokPack::readSkeletons(parse("meshes/actors/dlc02/boarriekling/character assets/skeleton.hkx"));
        ASSERT_FALSE(skeletons.empty());
        const HavokPack::Skeleton& skeleton = skeletons.front();
        std::cout << "SKYRIMSE-CREATURE4-BOARSKEL bones " << skeleton.mBoneNames.size() << std::endl;
        for (const char* stem : { "walkforward", "runforward", "trotforward", "mt_walkf" })
        {
            const auto clips = HavokPack::readClips(
                parse(std::string("meshes/actors/dlc02/boarriekling/animations/") + stem + ".hkx"));
            ASSERT_FALSE(clips.empty());
            const HavokPack::Clip& clip = clips.front();
            std::map<std::string, std::pair<float, float>> range;
            for (int step = 0; step < 32; ++step)
            {
                std::vector<HavokPack::Transform> local = skeleton.mReferencePose;
                std::vector<HavokPack::Transform> sampled(clip.mNumTransformTracks);
                HavokPack::sample(clip, clip.mDuration * static_cast<float>(step) / 32.f, sampled);
                for (std::size_t t = 0; t < sampled.size(); ++t)
                {
                    const std::size_t bone = clip.mTrackToBone.empty() ? t : clip.mTrackToBone[t];
                    if (bone < local.size())
                        local[bone] = sampled[t];
                }
                const auto world = HavokPack::toWorld(skeleton, local);
                for (std::size_t b = 0; b < world.size(); ++b)
                {
                    const std::string name = lower(skeleton.mBoneNames[b]);
                    const float y = world[b].mTranslation[1];
                    auto it = range.find(name);
                    if (it == range.end())
                        range[name] = { y, y };
                    else
                    {
                        it->second.first = std::min(it->second.first, y);
                        it->second.second = std::max(it->second.second, y);
                    }
                }
            }
            for (const auto& [name, r] : range)
                if (name.find("hoof") != std::string::npos || name.find("foot") != std::string::npos
                    || name.find("toe") != std::string::npos || name.find("paw") != std::string::npos
                    || name.find("hand") != std::string::npos)
                    std::cout << "SKYRIMSE-CREATURE4-BOARFOOT " << stem << " dur " << clip.mDuration << " " << name
                              << " ymin " << r.first << " ymax " << r.second << " excursion " << r.second - r.first
                              << std::endl;
        }
    }

    TEST(SkyrimCreaturesFourthSurvey, whereTheyStand)
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
        const std::vector<std::string> wantedFolders{ "dwarvensteamcenturion/", "dwarvenspherecenturion/",
            "boarriekling/", "skeletonbeast" };
        std::map<std::string, int> printed;
        for (const ParsedPlugin& full : parsed)
            for (const CellRecord& cell : full.mCells)
                for (const auto* group : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *group)
                    {
                        if (ref.mKind != RefKind::Actor || ref.initiallyDisabled() || index.npc(ref.mBase) == nullptr)
                            continue;
                        ActorIndex::Resolved resolved;
                        if (index.resolve(ref.mBase, 1, 0, resolved) != ActorIndex::ResolveStatus::Ok)
                            continue;
                        const RaceRecord* r = index.race(resolved.mRace);
                        if (r == nullptr)
                            continue;
                        const std::string sk = lower(r->skeleton(Sex::Male));
                        std::string kind;
                        for (const std::string& f : wantedFolders)
                            if (sk.find(f) != std::string::npos)
                                kind = f;
                        if (kind.empty() || printed[kind]++ >= 40)
                            continue;
                        std::cout << "SKYRIMSE-CREATURE4-PLACED " << kind
                                  << " npc:" << index.npc(ref.mBase)->mCommon.mEditorId << " base " << ref.mBase
                                  << " cell " << (cell.interior() ? cell.mCommon.mEditorId : "exterior") << " world "
                                  << cell.mWorld << " at " << int(ref.mPosition.x) << "," << int(ref.mPosition.y) << ","
                                  << int(ref.mPosition.z) << " skeleton " << sk << std::endl;
                    }
    }
}
