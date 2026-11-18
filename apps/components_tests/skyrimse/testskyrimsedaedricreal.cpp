// Task 0310: the data "A Daedra's Best Friend" rests on (DA03Start and DA03 of the player's Skyrim SE masters). The pieces
// are the game's own quests, dialogue and Papyrus; this test pins the facts the run depends on (the start quest runs from
// the first frame and its guard aliases carry the forced-greeting package, DA03 is not start-game enabled and its stages,
// objectives and Barbas's packages are as read). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    constexpr FormId kDa03Start = 0x0D7939;
    constexpr FormId kDa03 = 0x01BFC4;
    constexpr FormId kGuardGreetPackage = 0x0E0D53;
    constexpr FormId kLod = 0x013650;
    constexpr FormId kBarbas = 0x01BFC5;

    TEST(SkyrimDaedricReal, theStartQuestAndTheBarbasQuestAreAsTheRunNeedsThem)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0')
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        if (paths.empty())
            GTEST_SKIP() << "no masters in " << data;
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mQuests, nullptr);

        const QuestRecord* start = access.mQuests->quest(kDa03Start);
        ASSERT_NE(start, nullptr);
        EXPECT_EQ(start->mCommon.mEditorId, "DA03Start");
        EXPECT_TRUE(start->startsEnabled()); // it runs from the first frame: the guards greet at once
        for (const std::uint16_t stage : { 5, 10, 15, 20, 200 })
            EXPECT_NE(start->stage(stage), nullptr) << "DA03Start stage " << stage;
        std::size_t guardsWithPackage = 0;
        bool lod = false;
        for (const QuestAlias& alias : start->mAliases)
        {
            if (alias.mReference != kNullFormId && alias.mPackages.size() == 1 && alias.mPackages[0] == kGuardGreetPackage)
                ++guardsWithPackage;
            if (alias.mActor == kLod)
                lod = true;
        }
        EXPECT_EQ(guardsWithPackage, 4u); // the Imperial and Sons gate guards of Falkreath
        EXPECT_TRUE(lod);

        const QuestRecord* quest = access.mQuests->quest(kDa03);
        ASSERT_NE(quest, nullptr);
        EXPECT_EQ(quest->mCommon.mEditorId, "DA03");
        EXPECT_FALSE(quest->startsEnabled()); // DA03Start's stage 10 starts it
        for (const std::uint16_t stage : { 5, 7, 10, 20, 25, 50, 70, 75, 100, 105, 110, 150, 155, 200 })
            EXPECT_NE(quest->stage(stage), nullptr) << "DA03 stage " << stage;
        EXPECT_EQ(quest->mObjectives.size(), 9u);
        bool barbas = false;
        for (const QuestAlias& alias : quest->mAliases)
            if (alias.mActor == kBarbas)
            {
                barbas = true;
                EXPECT_EQ(alias.mPackages.size(), 8u); // forcegreet, wait, follow, escort, shrine waits ...
            }
        EXPECT_TRUE(barbas);
    }
}
