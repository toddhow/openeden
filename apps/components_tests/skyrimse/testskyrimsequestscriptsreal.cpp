// Task 0187: the dialogue lines with script fragments that the actors of the Bannered Mare offer on the player's own
// Skyrim SE masters, and the story manager's tree. Prints ids and counts only (lines starting SKYRIMSE-QS). Skipped
// when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    std::string hex8(FormId id)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%08X", id);
        return buffer;
    }

    TEST(SkyrimQuestScriptsRealData, theMareActorsOfferLinesWithFragmentsAndTheStoryTreeDecodes)
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
        std::vector<InfoFragmentCall> calls;
        view.setInfoFragmentSink([&calls](const InfoFragmentCall& c) { calls.push_back(c); });

        const sdkgame::WorldId cellId = source.findCellByEditorId("WhiterunBanneredMare");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;

        SkyrimWorldView::PapyrusAccess access;
        std::uint64_t spins = 0;
        while (!view.papyrusAccess(access) && spins++ < 600)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mStory, nullptr);

        std::uint64_t actors = 0, linesWithFragments = 0;
        for (const sdkgame::WorldObject& o : cell.objects)
        {
            if (o.kind != sdkgame::ObjectKind::Actor)
                continue;
            ++actors;
            sdkgame::DialogueTurn turn;
            if (!source.beginConversation(o.base, o.id, cellId, turn, error))
                continue;
            std::cout << "SKYRIMSE-QS " << o.baseEditorId << ": " << turn.topics.size() << " topics\n";
            const std::vector<sdkgame::DialogueTopicChoice> topics = turn.topics;
            std::size_t index = 0;
            for (const sdkgame::DialogueTopicChoice& topic : topics)
            {
                ++index;
                calls.clear();
                sdkgame::DialogueTurn reply;
                if (!source.chooseTopic(topic.id, reply, error))
                    continue;
                for (const InfoFragmentCall& c : calls)
                {
                    ++linesWithFragments;
                    std::cout << "SKYRIMSE-QS   " << o.baseEditorId << " button " << index - 1 << " topic "
                              << hex8(topic.id) << " info " << hex8(c.mInfo) << " quest "
                              << hex8(view.questOfInfo(c.mInfo)) << " script " << c.mVmad->mInfo.mBeginScript
                              << c.mVmad->mInfo.mEndScript << " begin '" << c.mVmad->mInfo.mBeginFunction << "' end '"
                              << c.mVmad->mInfo.mEndFunction << "'\n";
                }
                if (reply.endsConversation)
                    break;
            }
            source.endConversation();
        }
        std::cout << "SKYRIMSE-QS summary: actors " << actors << " lines with fragments " << linesWithFragments << "\n";
        EXPECT_GT(actors, 0u);
    }
}
