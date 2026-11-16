// Task 0216: measures the skin models of the creatures of docs/tasks/0216-skyrim-creatures-more.md (read only; prints
// bounds with the prefix SKYRIMSE-CREATURE-BOUNDS). The capsule heights of the profiles in creatures.cpp come from these
// lines. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <components/bsa/compressedbsafile.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/vfs/pathutil.hpp>

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return c == '\\' ? '/': static_cast<char>(std::tolower(c));
        });
        return s;
    }

    TEST(SkyrimCreaturesMeasure, boundsOfTheCreatureSkins)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::map<std::string, std::string> wanted{
            { "wolf", "meshes/actors/canine/character assets wolf/wolf.nif" },
            { "dog", "meshes/actors/canine/character assets dog/dog.nif" },
            { "bear", "meshes/actors/bear/character assets/bear.nif" },
            { "skeever", "meshes/actors/skeever/character assets/skeever.nif" },
            { "horse", "meshes/actors/horse/character assets/horse.nif" },
            { "sabrecat", "meshes/actors/sabrecat/character assets/sabrecat.nif" },
            { "deer", "meshes/actors/deer/character assets/elk_maleskin.nif" },
            { "goat", "meshes/actors/goat/character assets/goat.nif" },
            { "mammoth", "meshes/actors/mammoth/character assets/mammothwild.nif" },
            { "giant", "meshes/actors/giant/character assets/giant02.nif" },
        };
        std::map<std::string, bool> done;
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
                    if (name != wantedName || done[label])
                        continue;
                    done[label] = true;
                    auto stream = archive.getFile(&entry);
                    std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                    const NifScene::Scene scene
                        = NifScene::readScene(std::move(input), VFS::Path::Normalized("measure.nif"), {});
                    NifScene::Flat::Limits limits;
                    limits.mSkinnedAtRest = true;
                    const NifScene::Flat::FlatModel model = NifScene::Flat::flatten(scene, limits);
                    std::cout << "SKYRIMSE-CREATURE-BOUNDS " << label << " shapes " << model.mShapes.size()
                              << " skippedSkinned " << model.mReport.mSkippedSkinned << " min "
                              << model.mBoundsMin[0] << " " << model.mBoundsMin[1] << " " << model.mBoundsMin[2]
                              << " max " << model.mBoundsMax[0] << " " << model.mBoundsMax[1] << " "
                              << model.mBoundsMax[2] << std::endl;
                }
            }
        }
        EXPECT_FALSE(done.empty());
    }

    // Where each creature kind stands in the Tamriel exterior near Whiterun (cell x 0..14, y -14..2): the NPC placements
    // and the leveled-list placements, with the creatures a list can resolve to. Prints SKYRIMSE-CREATURE-PLACED lines.
    TEST(SkyrimCreaturesMeasure, whereTheCreaturesStand)
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
        // Which creature kinds an NPC placement can become (the templates pick by the placement's number), and the
        // placements whose base is a leveled list itself.
        std::map<std::string, int> printed, leveledBases;
        std::uint64_t achr = 0, achrNpc = 0, achrLeveled = 0, achrOther = 0;
        for (const ParsedPlugin& full : parsed)
            for (const CellRecord& cell : full.mCells)
            {
                for (const auto* group : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *group)
                    {
                        if (ref.mKind != RefKind::Actor)
                            continue;
                        ++achr;
                        if (const LeveledList* l = index.leveledList(ref.mBase))
                        {
                            ++achrLeveled;
                            ++leveledBases[l->mCommon.mEditorId + (l->mIsNpcList ? " (LVLN)" : " (LVLI)")];
                            continue;
                        }
                        if (index.npc(ref.mBase) == nullptr)
                        {
                            ++achrOther;
                            continue;
                        }
                        ++achrNpc;
                        if (ref.initiallyDisabled())
                            continue; // a disabled placement draws nothing until a script enables it
                        if (cell.interior() || !cell.mGrid || cell.mWorld != 60)
                            continue;
                        const int gx = cell.mGrid->x, gy = cell.mGrid->y;
                        if (gx < -4 || gx > 20 || gy < -20 || gy > 8)
                            continue;
                        std::string folders;
                        for (std::uint32_t pick = 0; pick < 12; ++pick)
                        {
                            ActorIndex::Resolved resolved;
                            if (index.resolve(ref.mBase, 1, pick, resolved) != ActorIndex::ResolveStatus::Ok)
                                continue;
                            const RaceRecord* r = index.race(resolved.mRace);
                            if (r == nullptr)
                                continue;
                            const std::string sk = lower(r->skeleton(Sex::Male));
                            if (sk.find("character/character assets") != std::string::npos)
                                continue;
                            const std::size_t a = sk.find("actors/") + 7;
                            const std::string kind = sk.substr(a, sk.find('/', a) - a);
                            if (folders.find(kind + " ") == std::string::npos)
                                folders += kind + " ";
                        }
                        if (folders.empty()
                            || (printed[folders]++ >= 12 && index.npc(ref.mBase)->mCommon.mEditorId != "Stump"))
                            continue;
                        std::cout << "SKYRIMSE-CREATURE-PLACED npc:" << index.npc(ref.mBase)->mCommon.mEditorId
                                  << " base " << ref.mBase << " ref " << ref.mCommon.mId << " cell " << gx << "," << gy << " at " << int(ref.mPosition.x) << ","
                                  << int(ref.mPosition.y) << "," << int(ref.mPosition.z) << " kinds " << folders
                                  << std::endl;
                    }
            }
        std::cout << "SKYRIMSE-CREATURE-PLACED totals ACHR " << achr << " npc " << achrNpc << " leveled "
                  << achrLeveled << " other " << achrOther << std::endl;
        for (const auto& [k, v] : leveledBases)
            std::cout << "SKYRIMSE-CREATURE-PLACED leveled base " << k << " x" << v << std::endl;
    }
}
