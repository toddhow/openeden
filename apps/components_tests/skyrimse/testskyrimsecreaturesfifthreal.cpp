// Task 0301: read-only surveys of the installed data for the creatures that kept the rest pose (task 0297 item 11).
// Prints lines with the prefixes SKYRIMSE-CREATURE5-*. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
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
#include <memory>
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

    struct Archives
    {
        std::vector<std::unique_ptr<Bsa::CompressedBSAFile>> mOpen;
        std::map<std::string, std::pair<Bsa::CompressedBSAFile*, std::size_t>> mFiles;

        bool load(const char* env)
        {
            if (env == nullptr || *env == '\0' || !fs::is_directory(env))
                return false;
            for (const char* name : { "Skyrim - Animations.bsa", "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa" })
            {
                const fs::path path = fs::path(env) / name;
                if (!fs::exists(path))
                    continue;
                mOpen.push_back(std::make_unique<Bsa::CompressedBSAFile>());
                mOpen.back()->open(path);
                const auto& list = mOpen.back()->getList();
                for (std::size_t i = 0; i < list.size(); ++i)
                    mFiles[lower(std::string(list[i].name()))] = { mOpen.back().get(), i };
            }
            return !mFiles.empty();
        }

        std::string read(const std::string& name) const
        {
            const auto it = mFiles.find(name);
            if (it == mFiles.end())
                return {};
            auto stream = it->second.first->getFile(&it->second.first->getList()[it->second.second]);
            return std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
        }

        HavokPack::Packfile parse(const std::string& name) const
        {
            const std::string bytes = read(name);
            return HavokPack::Packfile::parse(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        }
    };

    struct Candidate
    {
        const char* mFolder; // under meshes/actors/
        const char* mAssets; // "character assets" or "characterassets"
        const char* mProject; // sidecar project or ""
        const char* mSkin; // the skin model under the assets folder
        std::vector<std::string> mGait; // clip stems whose foot excursion is printed
    };

    const std::vector<Candidate>& candidates()
    {
        static const std::vector<Candidate> list{
            { "mudcrab", "character assets", "mudcrabproject", "mcrab1.nif", { "forward_walk", "forward_run" } },
            { "ambient/hare", "character assets", "hareproject", "rabbit.nif", { "walkforward", "runforward" } },
            { "ambient/chicken", "character assets", "chickenproject", "chicken.nif", { "walkforward", "runforward" } },
            { "cow", "character assets", "highlandcowproject", "highlandcow.nif", { "walkforward", "runforward" } },
            { "horker", "character assets", "horkerproject", "horker.nif", { "walkforward", "runforward" } },
            { "slaughterfish", "character assets", "slaughterfishproject", "slaughterfish.nif",
                { "swim_forward", "swim_fastforward" } },
            { "icewraith", "character assets", "icewraithproject", "icewraith.nif", { "runf" } },
            { "dlc02/riekling", "character assets", "", "riekling01.nif", { "mt_walkf", "mt_runf" } },
            { "dlc02/scrib", "character assets", "", "scrib.nif", { "walkforward", "runforward" } },
            { "dlc02/netch", "characterassets", "", "netch.nif", { "netch_runforward", "netch_sprintforward" } },
            { "dragonpriest", "character assets", "dragon_priest", "dragon_priest.nif", { "mt_walk_f", "mt_run_f" } },
            { "vampirelord", "character assets", "", "vampirelord.nif", { "mt_walkforward", "mt_runforward" } },
            { "dlc01/vampirebrute", "character assets", "", "vampirebrute.nif", { "walkforward", "runforward" } },
            { "dwarvenspider", "character assets", "dwarvenspidercenturionproject", "dwarvenspidercenturion.nif",
                { "forward_walk", "forward_run" } },
            { "chaurus", "character assets", "chaurusproject", "", {} },
            { "draugr", "character assets", "draugrproject", "", {} },
        };
        return list;
    }

    bool skipStem(const std::string& stem)
    {
        for (const char* prefix : { "mag_", "magic_", "lip_", "staff_", "levitate_", "crossbow_", "spear_" })
            if (stem.rfind(prefix, 0) == 0)
                return true;
        return false;
    }

    TEST(SkyrimCreaturesFifthSurvey, theClipsOfTheCandidates)
    {
        Archives archives;
        if (!archives.load(std::getenv("OPENEDEN_SKYRIMSE_DATA")))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        for (const Candidate& c : candidates())
        {
            const std::string project = c.mProject;
            const std::string sidecar
                = project.empty() ? std::string() : archives.read("meshes/animationdata/" + project + ".txt");
            const std::string motionText = project.empty()
                ? std::string()
                : archives.read("meshes/animationdata/boundanims/anims_" + project + ".txt");
            HavokPack::AnimationProject parsed;
            HavokPack::MotionTable motion;
            if (!sidecar.empty())
            {
                parsed = HavokPack::readAnimationProject(sidecar);
                motion = HavokPack::readMotionTable(motionText);
            }
            std::cout << "SKYRIMSE-CREATURE5-PROJECT " << c.mFolder << " project " << project << " sidecar "
                      << sidecar.size() << " generators " << parsed.mClips.size() << " blocks "
                      << motion.mBlocks.size() << std::endl;
            for (const HavokPack::SidecarClip& generator : parsed.mClips)
            {
                bool hit = false;
                for (const HavokPack::ClipEvent& event : generator.mEvents)
                    hit = hit || event.mName == "HitFrame";
                const HavokPack::MotionBlock* block = motion.find(generator.mCacheIndex);
                float stride = 0.f;
                if (block != nullptr && !block->mTranslation.empty())
                {
                    const auto& last = block->mTranslation.back();
                    stride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                }
                std::string events;
                const std::string gname = lower(generator.mName);
                for (const char* key : { "spit", "cast", "release", "mag", "fire", "sleep", "alcove", "sarcoph", "sit" })
                    if (gname.find(key) != std::string::npos)
                    {
                        for (const HavokPack::ClipEvent& event : generator.mEvents)
                            events += event.mName + "@" + std::to_string(event.mTime).substr(0, 5) + " ";
                        break;
                    }
                std::cout << "SKYRIMSE-CREATURE5-GEN " << c.mFolder << " " << generator.mName << " ev " << events << " dur "
                          << (block != nullptr ? block->mDuration : -1.f) << " stride " << stride << " hit "
                          << (hit ? "Y" : "n") << std::endl;
            }
            const std::string prefix = std::string("meshes/actors/") + c.mFolder + "/animations/";
            for (const auto& [name, entry] : archives.mFiles)
            {
                if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 5
                    || name.compare(name.size() - 4, 4, ".hkx") != 0)
                    continue;
                const std::string stem = name.substr(prefix.size(), name.size() - prefix.size() - 4);
                if (skipStem(stem))
                    continue;
                try
                {
                    const auto clips = HavokPack::readClips(archives.parse(name));
                    const float duration = clips.empty() ? 0.f : clips.front().mDuration;
                    float rootY = 0.f, rootX = 0.f;
                    if (!clips.empty() && clips.front().mNumTransformTracks > 0)
                    {
                        std::vector<HavokPack::Transform> a(clips.front().mNumTransformTracks), z = a;
                        HavokPack::sample(clips.front(), 0.f, a);
                        HavokPack::sample(clips.front(), duration, z);
                        rootX = z[0].mTranslation[0] - a[0].mTranslation[0];
                        rootY = z[0].mTranslation[1] - a[0].mTranslation[1];
                    }
                    const HavokPack::ClipLink link = HavokPack::linkClip(parsed, motion, stem, duration);
                    bool hit = false;
                    for (const HavokPack::SidecarClip* generator : link.mClips)
                        for (const HavokPack::ClipEvent& event : generator->mEvents)
                            hit = hit || event.mName == "HitFrame";
                    float stride = 0.f;
                    if (link.mMotion != nullptr && !link.mMotion->mTranslation.empty())
                    {
                        const auto& last = link.mMotion->mTranslation.back();
                        stride = std::sqrt(last[1] * last[1] + last[2] * last[2]);
                    }
                    std::cout << "SKYRIMSE-CREATURE5-CLIP " << c.mFolder << " " << stem << " dur " << duration
                              << " gens " << link.mClips.size() << " stride " << stride << " hit "
                              << (hit ? "Y" : "n") << " own " << rootX << "," << rootY << std::endl;
                }
                catch (const std::exception& e)
                {
                    std::cout << "SKYRIMSE-CREATURE5-CLIP " << c.mFolder << " " << stem << " UNREADABLE " << e.what()
                              << std::endl;
                }
            }
        }
    }

    // Without a sidecar the only measure of the gait is how far each foot bone travels along the model's y axis over
    // one cycle (the method of task 0290's boar survey).
    TEST(SkyrimCreaturesFifthSurvey, theFootExcursions)
    {
        Archives archives;
        if (!archives.load(std::getenv("OPENEDEN_SKYRIMSE_DATA")))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        for (const Candidate& c : candidates())
        {
            try
            {
                const auto skeletons = HavokPack::readSkeletons(archives.parse(
                    std::string("meshes/actors/") + c.mFolder + "/" + c.mAssets + "/skeleton.hkx"));
                if (skeletons.empty())
                    continue;
                const HavokPack::Skeleton& skeleton = skeletons.front();
                for (const std::string& stem : c.mGait)
                {
                    const auto clips = HavokPack::readClips(
                        archives.parse(std::string("meshes/actors/") + c.mFolder + "/animations/" + stem + ".hkx"));
                    if (clips.empty())
                        continue;
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
                    float best = 0.f;
                    std::string bestName;
                    for (const auto& [name, r] : range)
                        if (name.find("hoof") != std::string::npos || name.find("foot") != std::string::npos
                            || name.find("toe") != std::string::npos || name.find("paw") != std::string::npos
                            || name.find("hand") != std::string::npos || name.find("claw") != std::string::npos
                            || name.find("calf") != std::string::npos)
                            if (r.second - r.first > best)
                            {
                                best = r.second - r.first;
                                bestName = name;
                            }
                    std::cout << "SKYRIMSE-CREATURE5-FOOT " << c.mFolder << " " << stem << " dur " << clip.mDuration
                              << " bone " << bestName << " excursion " << best << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "SKYRIMSE-CREATURE5-FOOT " << c.mFolder << " FAILED " << e.what() << std::endl;
            }
        }
    }

    TEST(SkyrimCreaturesFifthSurvey, boundsOfEverySkin)
    {
        Archives archives;
        if (!archives.load(std::getenv("OPENEDEN_SKYRIMSE_DATA")))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        for (const Candidate& c : candidates())
        {
            if (std::string(c.mSkin).empty())
                continue;
            const std::string path = std::string("meshes/actors/") + c.mFolder + "/" + c.mAssets + "/" + c.mSkin;
            try
            {
                std::string bytes = archives.read(path);
                if (bytes.empty())
                {
                    std::cout << "SKYRIMSE-CREATURE5-BOUNDS " << c.mFolder << " MISSING " << path << std::endl;
                    continue;
                }
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
                std::cout << "SKYRIMSE-CREATURE5-BOUNDS " << c.mFolder << " shapes " << model.mShapes.size()
                          << " xext " << hi[0] - lo[0] << " yext " << hi[1] - lo[1] << " zext " << hi[2] - lo[2]
                          << " zmin " << lo[2] << " slabx " << slabHi - slabLo << " slaby " << slabYHi - slabYLo
                          << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cout << "SKYRIMSE-CREATURE5-BOUNDS " << c.mFolder << " FAILED " << e.what() << std::endl;
            }
        }
    }

    TEST(SkyrimCreaturesFifthSurvey, whereTheyStand)
    {
        using namespace game_skyrimse;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0 || !fs::is_directory(env))
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
        const std::vector<std::string> wantedFolders{ "/mudcrab/", "/ambient/hare/", "/ambient/chicken/", "/cow/",
            "/horker/", "/slaughterfish/", "/icewraith/", "/dlc02/riekling/", "/dlc02/scrib/", "/dlc02/netch/",
            "/dragonpriest/", "/vampirelord/", "/dlc01/vampirebrute/", "/dwarvenspider/", "/draugr/" };
        std::map<std::string, int> printed;
        std::set<std::string> seenNpc;
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
                        const std::string sk = "/" + lower(r->skeleton(Sex::Male));
                        std::string kind;
                        for (const std::string& f : wantedFolders)
                            if (sk.find(f) != std::string::npos)
                                kind = f;
                        if (kind.empty() || printed[kind] >= 8
                            || !seenNpc.insert(index.npc(ref.mBase)->mCommon.mEditorId).second)
                            continue;
                        ++printed[kind];
                        std::cout << "SKYRIMSE-CREATURE5-PLACED " << kind
                                  << " npc:" << index.npc(ref.mBase)->mCommon.mEditorId << " base " << ref.mBase
                                  << " cell " << (cell.interior() ? cell.mCommon.mEditorId : "exterior") << " world "
                                  << cell.mWorld << " at " << int(ref.mPosition.x) << "," << int(ref.mPosition.y) << ","
                                  << int(ref.mPosition.z) << " skeleton " << sk << std::endl;
                    }
    }
}
