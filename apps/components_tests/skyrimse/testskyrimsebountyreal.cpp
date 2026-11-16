// Task 0226: do the bounty quests (BQ01 to BQ04) start on the player's own Skyrim SE masters when he arrives in Whiterun,
// and what fills their Hold, BountyLocation and Bounty? Prints aggregates and official editor ids only (lines starting
// SKYRIMSE-BQREAL). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    TEST(SkyrimBountyReal, arrivingInWhiterunStartsBountyQuestsWithARealHoldDungeonAndBoss)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
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
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mAliases, nullptr);
        ASSERT_NE(access.mLocations, nullptr);
        const FormId mareLocation = view.locationOfCell(static_cast<FormId>(cellId));
        FormId whiterun = kNullFormId;
        for (const FormId id : access.mLocations->ids())
            if (access.mLocations->location(id)->mCommon.mEditorId == "WhiterunLocation")
                whiterun = id;
        ASSERT_NE(whiterun, kNullFormId);
        std::cout << "SKYRIMSE-BQREAL Mare " << std::hex << mareLocation << " Whiterun " << whiterun << std::dec << "\n";

        QuestAliasFiller::PlayerSite site;
        site.mKnown = true;
        site.mInterior = true;
        site.mCell = static_cast<FormId>(cellId);
        site.mLocation = mareLocation;
        view.setPlayerSite([site] { return site; });

        StoryEvent event;
        event.mType = "CLOC";
        event.mLocation1 = mareLocation;
        event.mLocation2 = whiterun;
        std::set<std::string> started;
        for (const FormId q : access.mStory(event))
            started.insert(access.mQuests->quest(q)->mCommon.mEditorId);
        std::cout << "SKYRIMSE-BQREAL the story manager chooses:";
        for (const std::string& n : started)
            std::cout << " " << n;
        std::cout << "\n";

        std::size_t bounties = 0;
        for (const char* name : { "BQ01", "BQ02", "BQ03", "BQ04" })
        {
            FormId id = kNullFormId;
            for (const FormId q : access.mQuests->questIds())
                if (access.mQuests->quest(q)->mCommon.mEditorId == name)
                    id = q;
            ASSERT_NE(id, kNullFormId) << name;
            const QuestRecord* q = access.mQuests->quest(id);
            const bool can = access.mAliases->canStart(id, *access.mState, &event);
            std::cout << "SKYRIMSE-BQREAL " << name << " canStart " << can << "\n";
            if (!can)
                continue;
            ++bounties;
            access.mAliases->fillEventAliases(id, *access.mState, event);
            access.mAliases->fillQuest(id, *access.mState, true);
            for (const QuestAlias& a : q->mAliases)
            {
                const AliasFill f = access.mState->aliasFill(id, a.mId);
                std::cout << "SKYRIMSE-BQREAL   " << name << "." << a.mName << " = " << std::hex << f.mReference << std::dec
                          << " '" << (access.mAliasName ? access.mAliasName(id, a.mName) : std::string()) << "'";
                if (a.mIsLocation && f.mReference != kNullFormId)
                    std::cout << " " << access.mLocations->location(f.mReference)->mCommon.mEditorId;
                std::cout << "\n";
            }
            // the three aliases the earlier task could not fill
            for (const char* aliasName : { "Hold", "BountyLocation", "Bounty" })
                for (const QuestAlias& a : q->mAliases)
                    if (a.mName == aliasName)
                    {
                        EXPECT_NE(access.mState->aliasFill(id, a.mId).mReference, kNullFormId) << name << "." << aliasName;
                    }
        }
        EXPECT_GT(bounties, 0u);
    }
}
