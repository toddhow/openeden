// Task 0230: GetIsEditorLocAlias (fn_567) on the player's own Skyrim SE masters. Fills the quests that call it with the
// function unanswered (as before the task) and answered, and prints what changed (aggregates and official editor ids only,
// lines starting SKYRIMSE-BQDLG). Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
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

    constexpr std::uint16_t kFn567 = 567;

    struct Measure
    {
        std::set<std::string> mStarted; ///< what the story manager chooses at the Whiterun arrival
        std::map<std::string, std::map<std::string, std::string>> mFills; ///< quest -> alias -> name of what fills it
        std::map<std::string, std::map<std::string, FormId>> mRefs; ///< quest -> alias -> reference
        std::size_t mQuestsWith567 = 0;
        std::size_t mAliasesWith567 = 0;
        std::chrono::milliseconds mFillTime{ 0 };
        std::map<std::string, bool> mCanStart; ///< every quest with fn_567: does its start gate pass?
        std::vector<std::string> mSpeakerFills; ///< "quest.alias" lines for the conversation start with the innkeeper
    };

    bool measure(bool answer, const fs::path& data, Measure& out, std::string& why)
    {
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        if (paths.empty())
        {
            why = "no masters";
            return false;
        }
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        view.setAnswerEditorLocAlias(answer);
        std::vector<std::string> problems;
        if (view.load(paths, problems) == 0)
        {
            why = "nothing loaded";
            return false;
        }
        SkyrimCellSource source(view);
        const sdkgame::WorldId cellId = source.findCellByEditorId("WhiterunBanneredMare");
        sdkgame::WorldCell cell;
        std::string error;
        if (cellId == sdkgame::kNoWorldId || !source.buildCell(cellId, cell, error))
        {
            why = "no cell " + error;
            return false;
        }
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (access.mAliases == nullptr || access.mLocations == nullptr)
        {
            why = "no papyrus access";
            return false;
        }
        const FormId mareLocation = view.locationOfCell(static_cast<FormId>(cellId));
        FormId whiterun = kNullFormId;
        for (const FormId id : access.mLocations->ids())
            if (access.mLocations->location(id)->mCommon.mEditorId == "WhiterunLocation")
                whiterun = id;
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
        for (const FormId q : access.mStory(event))
            out.mStarted.insert(access.mQuests->quest(q)->mCommon.mEditorId);

        const auto begin = std::chrono::steady_clock::now();
        for (const FormId id : access.mQuests->questIds())
        {
            const QuestRecord* q = access.mQuests->quest(id);
            std::size_t uses = 0;
            for (const QuestAlias& a : q->mAliases)
                for (const survey::Ctda& c : a.mConditions)
                    if (c.mFunction == kFn567)
                        ++uses;
            if (uses == 0)
                continue;
            ++out.mQuestsWith567;
            out.mAliasesWith567 += uses;
            const std::string& name = q->mCommon.mEditorId;
            out.mCanStart[name] = access.mAliases->canStart(id, *access.mState, &event);
            QuestState state = QuestState::initial(*access.mQuests);
            state.start(id);
            access.mAliases->fillEventAliases(id, state, event);
            access.mAliases->fillQuest(id, state, true);
            for (const QuestAlias& a : q->mAliases)
            {
                const AliasFill f = state.aliasFill(id, a.mId);
                out.mRefs[name][a.mName] = f.mKnown ? f.mReference : kNullFormId;
                if (f.mKnown && f.mReference != kNullFormId)
                    out.mFills[name][a.mName]
                        = access.mAliasName ? access.mAliasName(id, a.mName) : std::string("?");
            }
        }
        out.mFillTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);

        // a conversation with Hulda: the lazy fill on the live state
        const FormId hulda = 0x1a66e;
        if (const PlacedEntry* e = access.mAliases != nullptr ? view.placedRefs()->find(hulda) : nullptr)
        {
            QuestState& live = *access.mState;
            for (const FormId id : access.mQuests->questIds())
                if (access.mQuests->quest(id)->mCommon.mEditorId == "BQ01" && !live.running(id))
                {
                    live.start(id);
                    access.mAliases->fillEventAliases(id, live, event);
                    // the Hold, Location and the others, but not the aliases that wait for a conversation
                }
            const std::size_t n = access.mAliases->fillForSpeaker(live, hulda, e->mBase);
            out.mSpeakerFills.push_back("fillForSpeaker(Hulda) filled " + std::to_string(n));
        }
        return true;
    }

    TEST(SkyrimBountyDlgReal, theBountyAliasesFillAndNoOtherStartChanges)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        Measure before, after;
        std::string why;
        ASSERT_TRUE(measure(false, env, before, why)) << why;
        ASSERT_TRUE(measure(true, env, after, why)) << why;

        std::cout << "SKYRIMSE-BQDLG quests that call fn_567: " << after.mQuestsWith567 << " (" << after.mAliasesWith567
                  << " conditions); fill time " << before.mFillTime.count() << " ms before, " << after.mFillTime.count()
                  << " ms after\n";
        std::size_t questsChanged = 0, aliasesChanged = 0, aliasesGained = 0, aliasesLost = 0;
        for (const auto& [quest, aliases] : after.mRefs)
        {
            bool changed = false;
            for (const auto& [alias, ref] : aliases)
            {
                const FormId was = before.mRefs[quest][alias];
                if (was == ref)
                    continue;
                changed = true;
                ++aliasesChanged;
                if (was == kNullFormId)
                    ++aliasesGained;
                else if (ref == kNullFormId)
                    ++aliasesLost;
                std::cout << "SKYRIMSE-BQDLG   " << quest << "." << alias << ": " << std::hex << was << " -> " << ref
                          << std::dec << " '" << after.mFills[quest][alias] << "'\n";
            }
            questsChanged += changed ? 1 : 0;
        }
        std::cout << "SKYRIMSE-BQDLG fills changed: " << questsChanged << " of " << after.mQuestsWith567
                  << " quests, " << aliasesChanged << " aliases (" << aliasesGained << " gained, " << aliasesLost
                  << " lost)\n";
        std::size_t gateChanged = 0;
        for (const auto& [quest, can] : after.mCanStart)
            if (before.mCanStart[quest] != can)
                ++gateChanged;
        std::cout << "SKYRIMSE-BQDLG start gates changed: " << gateChanged << "\n";
        std::cout << "SKYRIMSE-BQDLG started before:";
        for (const std::string& n : before.mStarted)
            std::cout << " " << n;
        std::cout << "\nSKYRIMSE-BQDLG started after:";
        for (const std::string& n : after.mStarted)
            std::cout << " " << n;
        std::cout << "\n";
        for (const std::string& line : after.mSpeakerFills)
            std::cout << "SKYRIMSE-BQDLG " << line << "\n";
        EXPECT_EQ(before.mStarted, after.mStarted); // the quests the story manager starts do not change
        EXPECT_EQ(gateChanged, 0u);
        EXPECT_EQ(aliasesLost, 0u);
        EXPECT_GT(aliasesGained, 0u);
        for (const char* name : { "BQ01", "BQ02", "BQ03" })
            for (const char* alias : { "Innkeeper" })
                EXPECT_NE(after.mRefs[name][alias], kNullFormId) << name << "." << alias;
    }
}
