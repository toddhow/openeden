// Task 0311 (Hearthfire): the Falkreath plot quest BYOHHouseFalkreath on the player's own masters. Starts it, fills its
// aliases with the player standing in the Falkreath jarl's longhouse and prints which aliases are filled (editor ids of
// official records only, lines SKYRIMSE-BYOH). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    constexpr FormId kFalkreathQuest = 0x0300305E;
    constexpr FormId kNenyaRef = 0x00019823;

    TEST(SkyrimHearthfireReal, theFalkreathStewardAndLocationAliasesFill)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        ASSERT_EQ(paths.size(), 5u);
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimCellSource source(view);
        const sdkgame::WorldId cellId = source.findCellByEditorId("FalkreathJarlsLonghouse");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mAliases, nullptr);
        QuestAliasFiller::PlayerSite site;
        site.mKnown = true;
        site.mInterior = true;
        site.mCell = static_cast<FormId>(cellId);
        site.mLocation = view.locationOfCell(static_cast<FormId>(cellId));
        view.setPlayerSite([site] { return site; });
        std::cout << "SKYRIMSE-BYOH player location " << std::hex << site.mLocation << std::dec << "\n";

        const QuestRecord* q = access.mQuests->quest(kFalkreathQuest);
        ASSERT_NE(q, nullptr);
        QuestState state = QuestState::initial(*access.mQuests);
        state.start(kFalkreathQuest);
        access.mAliases->fillQuest(kFalkreathQuest, state, true);
        FormId steward = kNullFormId;
        for (const QuestAlias& a : q->mAliases)
        {
            const AliasFill f = state.aliasFill(kFalkreathQuest, a.mId);
            std::cout << "SKYRIMSE-BYOH alias " << a.mId << " " << a.mName << ": "
                      << (f.mKnown && f.mReference != kNullFormId ? "filled" : "empty") << " " << std::hex
                      << (f.mKnown ? f.mReference : kNullFormId) << std::dec << "\n";
            if (a.mName == "Steward" && f.mKnown)
                steward = f.mReference;
        }
        EXPECT_EQ(steward, kNenyaRef);
    }

    // The Steward alias of the plot quest is a find-matching alias whose conditions name the steward by id (GetIsID). At
    // a conversation with her it is filled from the speaker, as the quest's dialogue (GetIsAliasRef) needs.
    TEST(SkyrimHearthfireReal, theStewardAliasIsFilledFromTheSpeakerAtAConversation)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            paths.push_back(data / name);
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mAliases, nullptr);
        SkyrimCellSource source(view);
        const sdkgame::WorldId cellId = source.findCellByEditorId("FalkreathJarlsLonghouse");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;
        QuestAliasFiller::PlayerSite site;
        site.mKnown = true;
        site.mInterior = true;
        site.mCell = static_cast<FormId>(cellId);
        site.mLocation = view.locationOfCell(static_cast<FormId>(cellId));
        view.setPlayerSite([site] { return site; });
        QuestState state = QuestState::initial(*access.mQuests);
        state.start(kFalkreathQuest);
        const QuestRecord* q = access.mQuests->quest(kFalkreathQuest);
        ASSERT_NE(q, nullptr);
        FormId stewardAlias = 0, jarlAlias = 0;
        for (const QuestAlias& a : q->mAliases)
        {
            if (a.mName == "Steward")
                stewardAlias = a.mId;
            if (a.mName == "Jarl")
                jarlAlias = a.mId;
        }
        EXPECT_FALSE(state.aliasFill(kFalkreathQuest, stewardAlias).mReference == kNenyaRef);
        (void)access.mAliases->fillForSpeaker(state, kNenyaRef, 0x00013659);
        EXPECT_EQ(state.aliasFill(kFalkreathQuest, stewardAlias).mReference, kNenyaRef);
        // and the jarl the steward's offer asks about (GetRelationshipRank run on the quest alias Jarl)
        EXPECT_EQ(state.aliasFill(kFalkreathQuest, jarlAlias).mReference, 0x00019822u);
    }

    // The same when the steward alias was filled already (at the start of the quest): the speaker still brings the quest's
    // other empty aliases (the jarl), as the offer's GetRelationshipRank run on the jarl alias needs.
    TEST(SkyrimHearthfireReal, aStewardAliasFilledAtTheStartStillBringsTheJarl)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            paths.push_back(data / name);
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mAliases, nullptr);
        SkyrimCellSource source(view);
        const sdkgame::WorldId cellId = source.findCellByEditorId("FalkreathJarlsLonghouse");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;
        QuestAliasFiller::PlayerSite site;
        site.mKnown = true;
        site.mInterior = true;
        site.mCell = static_cast<FormId>(cellId);
        site.mLocation = view.locationOfCell(static_cast<FormId>(cellId));
        view.setPlayerSite([site] { return site; });
        QuestState state = QuestState::initial(*access.mQuests);
        state.start(kFalkreathQuest);
        const QuestRecord* q = access.mQuests->quest(kFalkreathQuest);
        ASSERT_NE(q, nullptr);
        FormId stewardAlias = 0, jarlAlias = 0;
        for (const QuestAlias& a : q->mAliases)
        {
            if (a.mName == "Steward")
                stewardAlias = a.mId;
            if (a.mName == "Jarl")
                jarlAlias = a.mId;
        }
        AliasFill steward;
        steward.mKnown = true;
        steward.mReference = kNenyaRef;
        state.setAliasFill(kFalkreathQuest, stewardAlias, steward);
        EXPECT_NE(state.aliasFill(kFalkreathQuest, jarlAlias).mReference, 0x00019822u);
        (void)access.mAliases->fillForSpeaker(state, kNenyaRef, 0x00013659);
        EXPECT_EQ(state.aliasFill(kFalkreathQuest, jarlAlias).mReference, 0x00019822u);
    }
}
