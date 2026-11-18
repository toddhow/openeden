// Task 0307: the data the Stormcloak join rests on (CW00B and CW01B of the player's Skyrim SE masters). The path itself
// ran with the pieces of task 0298 and nothing new, so this test pins the facts the run depends on: the quests, their
// stages and objectives, Ulfric's forced greeting package and Galmar's alias. Skipped when OPENEDEN_SKYRIMSE_DATA is
// not set.
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

    constexpr FormId kCw00b = 0x0E1B5A;
    constexpr FormId kCw01b = 0x0E2D29;
    constexpr FormId kUlfricForcegreetPackage = 0x0E7A2D;
    constexpr FormId kUlfric = 0x01414D;
    constexpr FormId kGalmar = 0x014128;

    TEST(SkyrimStormcloakReal, theQuestsAndTheirStagesAreThere)
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

        const QuestRecord* intro = access.mQuests->quest(kCw00b);
        ASSERT_NE(intro, nullptr);
        EXPECT_EQ(intro->mCommon.mEditorId, "CW00B");
        EXPECT_TRUE(intro->startsEnabled());
        EXPECT_EQ(intro->mPriority, 71); // equal to CW00A's: the tie goes to the lower quest id (CW00A)
        for (const std::uint16_t stage : { 7, 10, 15, 20 })
            EXPECT_NE(intro->stage(stage), nullptr) << "CW00B stage " << stage;
        ASSERT_NE(intro->alias(2), nullptr); // Ulfric: his forced greeting is a package of the alias
        EXPECT_EQ(intro->alias(2)->mActor, kUlfric);
        ASSERT_EQ(intro->alias(2)->mPackages.size(), 1u);
        EXPECT_EQ(intro->alias(2)->mPackages[0], kUlfricForcegreetPackage);
        ASSERT_NE(intro->alias(0), nullptr);
        EXPECT_EQ(intro->alias(0)->mActor, kGalmar);

        const QuestRecord* join = access.mQuests->quest(kCw01b);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->mCommon.mEditorId, "CW01B");
        EXPECT_FALSE(join->startsEnabled()); // CW00B's stage 20 starts it
        for (const std::uint16_t stage : { 1, 100, 150, 160, 200 })
            EXPECT_NE(join->stage(stage), nullptr) << "CW01B stage " << stage;
        ASSERT_EQ(join->mObjectives.size(), 3u); // kill the Ice Wraith, return to Galmar, take the oath
        EXPECT_EQ(join->mObjectives[0].mIndex, 1);
        EXPECT_EQ(join->mObjectives[1].mIndex, 100);
        EXPECT_EQ(join->mObjectives[2].mIndex, 160);
    }
}
