// Task 0141, M-S4 slice 1: the conversation with the actors of the Bannered Mare on the player's own Skyrim SE masters.
// Prints counts only (lines starting SKYRIMSE-TALK): whether a greeting was found, how many topics are offered, never a
// response text. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    TEST(SkyrimDialogueRuntimeRealData, theActorsOfTheBanneredMareGreetAndOfferTopics)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            if (fs::exists(data / name))
                paths.push_back(data / name);
        }
        ASSERT_FALSE(paths.empty());

        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimCellSource source(view);

        const sdkgame::WorldId cellId = source.findCellByEditorId("WhiterunBanneredMare");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;

        std::uint64_t actors = 0, greeted = 0, noGreeting = 0, totalTopics = 0, maxTopics = 0, withName = 0;
        for (const sdkgame::WorldObject& o : cell.objects)
        {
            if (o.kind != sdkgame::ObjectKind::Actor)
                continue;
            ++actors;
            sdkgame::DialogueTurn turn;
            const auto start = std::chrono::steady_clock::now();
            const bool ok = source.beginConversation(o.base, o.id, cellId, turn, error);
            const auto ms
                = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                      .count();
            if (!ok)
            {
                ++noGreeting;
                std::cout << "SKYRIMSE-TALK " << o.baseEditorId << ": no greeting (" << error << ") in " << ms
                          << " ms\n";
                continue;
            }
            ++greeted;
            totalTopics += turn.topics.size();
            maxTopics = std::max<std::uint64_t>(maxTopics, turn.topics.size());
            withName += !turn.speakerName.empty();
            std::cout << "SKYRIMSE-TALK " << o.baseEditorId << ": greeting " << turn.text.size() << " bytes, name '"
                      << turn.speakerName << "', " << turn.topics.size() << " topics, ends " << turn.endsConversation
                      << ", in " << ms << " ms\n";
            // A first topic must answer.
            if (!turn.topics.empty())
            {
                sdkgame::DialogueTurn reply;
                EXPECT_TRUE(source.chooseTopic(turn.topics.front().id, reply, error)) << error;
            }
            source.endConversation();
        }
        std::cout << "SKYRIMSE-TALK summary: actors " << actors << " greeted " << greeted << " no greeting "
                  << noGreeting << " topics total " << totalTopics << " max " << maxTopics << " with a name "
                  << withName << "\n";
        EXPECT_GT(actors, 0u);
    }
}
