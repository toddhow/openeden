// Task 0212: a survey of the creature races of the five Skyrim SE masters (read only; prints race names and aggregates
// with the prefix SKYRIMSE-CREATURES). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    TEST(SkyrimCreaturesRealData, surveyOfCreatureRaces)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
        {
            if (!fs::exists(data / n))
                GTEST_SKIP() << "masters missing";
            ASSERT_TRUE(order.add(n, false, err)) << err;
        }
        ActorIndex index;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            ASSERT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), actors));
            index.add(actors);
            ParsedPlugin records;
            ParseLimits limits;
            limits.mSkipReferences = true;
            ASSERT_TRUE(parseRecords(*plugin, order, limits, records));
            index.addLeveledLists(records.mLeveledLists);
        }
        // Placements: every ACHR of an exterior cell whose base is a creature NPC or a leveled actor list.
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / "Skyrim.esm", error);
            ASSERT_NE(plugin, nullptr);
            ParsedPlugin full;
            ParseLimits limits;
            ASSERT_TRUE(parseRecords(*plugin, order, limits, full));
            std::map<std::string, int> near, all;
            for (const CellRecord& cell : full.mCells)
            {
                if (cell.interior() || !cell.mGrid)
                    continue;
                for (const auto* group : { &cell.mPersistent, &cell.mTemporary, &cell.mVisibleDistant, &cell.mOther })
                    for (const PlacedRef& ref : *group)
                    {
                        if (ref.mKind != RefKind::Actor)
                            continue;
                        std::string label;
                        if (const ActorNpc* n = index.npc(ref.mBase))
                        {
                            const RaceRecord* r = index.race(n->mRace);
                            if (r == nullptr
                                || r->skeleton(Sex::Male).find("Character Assets\\skeleton.nif") != std::string::npos)
                                continue;
                            label = "npc:" + n->mCommon.mEditorId + " (" + r->mCommon.mEditorId + ")";
                        }
                        else if (const LeveledList* l = index.leveledList(ref.mBase))
                            label = "lvln:" + l->mCommon.mEditorId;
                        else
                            continue;
                        ++all[label];
                        const int gx = cell.mGrid->x, gy = cell.mGrid->y;

                        if (gx >= 0 && gx <= 12 && gy >= -12 && gy <= 2)
                            ++near[label + " cell " + std::to_string(gx) + "," + std::to_string(gy) + " at "
                                + std::to_string(int(ref.mPosition.x)) + "," + std::to_string(int(ref.mPosition.y))
                                + " world " + std::to_string(cell.mWorld)];
                    }
            }
            for (const auto& [k, v] : near)
                std::cout << "SKYRIMSE-CREATURES near " << k << " x" << v << "\n";
            for (const auto& [k, v] : all)
                std::cout << "SKYRIMSE-CREATURES placed " << k << " x" << v << "\n";
        }
        std::map<FormId, std::vector<std::string>> byRace;
        for (const FormId id : index.npcIds())
        {
            const ActorNpc* n = index.npc(id);
            byRace[n->mRace].push_back(n->mCommon.mEditorId);
        }
        std::uint64_t creatureNpcs = 0, creatureRaces = 0;
        for (const auto& [raceId, npcs] : byRace)
        {
            const RaceRecord* r = index.race(raceId);
            if (r == nullptr)
                continue;
            const std::string& sk = r->skeleton(Sex::Male);
            const bool humanoid = sk.find("character\\character assets") != std::string::npos
                || sk.find("character/character assets") != std::string::npos;
            if (humanoid)
                continue;
            ++creatureRaces;
            creatureNpcs += npcs.size();
            std::cout << "SKYRIMSE-CREATURES race " << r->mCommon.mEditorId << " npcs " << npcs.size() << " skel " << sk
                      << " | " << r->skeleton(Sex::Female) << " height " << r->mHeight[0] << " hp " << r->mStartHealth
                      << " sta " << r->mStartStamina << "\n";
            for (const RaceModel& b : r->mBehaviourGraphs)
                std::cout << "SKYRIMSE-CREATURES   graph " << int(b.mSex) << " " << b.mPath << "\n";
            if (const ArmorExtra* skin = index.armor(r->mSkin))
            {
                for (FormId a : skin->mArmatures)
                    if (const ArmatureRecord* ar = index.armature(a))
                        std::cout << "SKYRIMSE-CREATURES   skin-addon " << ar->mCommon.mEditorId << " mask " << std::hex
                                  << ar->mSlotMask << std::dec << " model " << ar->mMaleModel << " | "
                                  << ar->mFemaleModel << "\n";
            }
            else
                std::cout << "SKYRIMSE-CREATURES   no race skin\n";
        }
        std::cout << "SKYRIMSE-CREATURES total races " << creatureRaces << " npcs " << creatureNpcs << " of "
                  << index.npcCount() << "\n";
        for (const char* wanted : { "EncWolf", "EncWolfRoaming", "WolfRace" })
            for (const FormId id : index.npcIds())
                if (index.npc(id)->mCommon.mEditorId == wanted)
                {
                    const ActorNpc* n = index.npc(id);
                    std::cout << "SKYRIMSE-CREATURES npc " << wanted << " id " << std::hex << id << std::dec << " race "
                              << std::hex << n->mRace << std::dec << " flags " << std::hex << n->mFlags << std::dec
                              << " style " << std::hex << n->mCombatStyle << std::dec << " level " << n->mLevel
                              << " tmpl " << std::hex << n->mTemplate << std::dec << " tflags " << n->mTemplateFlags
                              << " skin " << std::hex << n->mSkin << std::dec << " outfit " << std::hex
                              << n->mDefaultOutfit << std::dec << " ai " << (n->mAi ? 1 : 0) << "\n";
                }
    }
}
