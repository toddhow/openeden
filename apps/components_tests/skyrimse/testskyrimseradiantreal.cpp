// Task 0211: how many vanilla quests with a story event or a find-matching location alias can start now, on the player's own
// Skyrim SE masters. Prints counts and official editor ids only (lines starting SKYRIMSE-RADREAL). Skipped when
// OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/quest_alias.hpp>
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

    TEST(SkyrimRadiantReal, theQuestsThatCanStartInTheBanneredMare)
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
        const FormId mare = static_cast<FormId>(cellId);
        const FormId location = view.locationOfCell(mare);
        std::cout << "SKYRIMSE-RADREAL the Mare's location " << std::hex << location << std::dec << "\n";
        FormId anActor = 0;
        for (const sdkgame::WorldObject& o : cell.objects)
            if (o.kind == sdkgame::ObjectKind::Actor)
            {
                anActor = static_cast<FormId>(o.id);
                break;
            }
        for (const bool withSite : { false, true })
        {
            QuestAliasFiller::PlayerSite site;
            if (withSite)
            {
                site.mKnown = true;
                site.mInterior = true;
                site.mCell = mare;
                site.mLocation = location;
            }
            view.setPlayerSite([site] { return site; });
            std::uint64_t candidates = 0, withBlocking = 0, startable = 0, blocked = 0;
            std::map<std::string, std::uint64_t> startableByEvent, blockedByEvent;
            std::set<std::string> startableNames;
            const auto began = std::chrono::steady_clock::now();
            for (const FormId id : access.mQuests->questIds())
            {
                const QuestRecord* q = access.mQuests->quest(id);
                bool findLocation = false, blockingKind = false;
                for (const QuestAlias& a : q->mAliases)
                {
                    findLocation = findLocation || (a.mIsLocation && a.mFill == AliasFillKind::FindMatching);
                    blockingKind = blockingKind || ((a.mFlags & AliasFlags::Optional) == 0
                        && (a.mFill == AliasFillKind::FindMatching || a.mFill == AliasFillKind::FromEvent
                            || a.mFill == AliasFillKind::InLocationAlias));
                }
                if (q->mEvent.empty() && !findLocation)
                    continue;
                ++candidates;
                withBlocking += blockingKind ? 1 : 0;
                StoryEvent event;
                event.mType = q->mEvent.empty() ? "SCPT" : q->mEvent;
                event.mRef1 = anActor;
                event.mRef2 = 0x14;
                event.mLocation1 = location;
                event.mLocation2 = location;
                const bool ok = access.mAliases->canStart(id, *access.mState, &event);
                const std::string key = q->mEvent.empty() ? "-" : q->mEvent;
                if (ok)
                {
                    ++startable;
                    ++startableByEvent[key];
                    if (findLocation)
                    {
                        startableNames.insert(q->mCommon.mEditorId);
                        if (withSite && access.mText)
                        {
                            const std::string name = access.mText(q->mName, q->mPlugin, StringTableKind::Strings);
                            if (!name.empty())
                            {
                                std::string keywords;
                                for (const survey::Ctda& c : q->mStartConditions)
                                    if (c.mFunction == 576 && ((c.mParam1 >> 16) & 0xFF) == 'K')
                                    {
                                        char buf[16];
                                        std::snprintf(buf, sizeof(buf), " %08X",
                                            access.mQuests->resolverOf(*q).resolve(c.mParam2).value_or(0));
                                        keywords += buf;
                                    }
                                std::cout << "SKYRIMSE-RADREAL   named: " << q->mCommon.mEditorId << " (" << std::hex << id << std::dec << ") event " << key
                                          << " keyword" << keywords << " stages " << q->mStages.size() << "\n";
                            }
                        }
                    }
                }
                else
                {
                    ++blocked;
                    ++blockedByEvent[key];
                }
            }
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - began).count();
            std::cout << "SKYRIMSE-RADREAL site " << (withSite ? "Bannered Mare (interior)" : "unknown") << ": candidates "
                      << candidates << ", with a blocking-kind required alias " << withBlocking << ", can start "
                      << startable << ", blocked " << blocked << ", " << millis << " ms\n";
            for (const auto& [k, v] : startableByEvent)
                std::cout << "SKYRIMSE-RADREAL   can start with event " << k << ": " << v << " (blocked "
                          << blockedByEvent[k] << ")\n";
            for (const std::string& n : startableNames)
                std::cout << "SKYRIMSE-RADREAL   find-location quest that can start: " << n << "\n";
            const QuestAliasFiller::Stats& st = access.mAliases->stats();
            std::cout << "SKYRIMSE-RADREAL   stats: plans " << st.mPlans << " blocked " << st.mStartsBlocked << " evaluated "
                      << st.mEvaluated << "\n";
        }
        for (const FormId id : { 0x020050B9u, 0x00095125u })
        {
            const QuestRecord* q = access.mQuests->quest(id);
            if (q == nullptr)
                continue;
            std::cout << "SKYRIMSE-RADREAL quest " << q->mCommon.mEditorId << " flags " << q->mFlags << " name '"
                      << access.mText(q->mName, q->mPlugin, StringTableKind::Strings) << "'\n";
            for (const QuestStage& st : q->mStages)
            {
                std::cout << "SKYRIMSE-RADREAL    stage " << st.mIndex << " flags " << int(st.mFlags) << ":";
                for (const QuestLogEntry& e : st.mEntries)
                    std::cout << " [" << access.mText(e.mText, q->mPlugin, StringTableKind::DlStrings).substr(0, 60) << "]";
                std::cout << "\n";
            }
            for (const QuestObjective& o : q->mObjectives)
                std::cout << "SKYRIMSE-RADREAL    objective " << o.mIndex << " '"
                          << access.mText(o.mText, q->mPlugin, StringTableKind::Strings).substr(0, 70) << "' targets "
                          << o.mTargets.size() << "\n";
        }
        {
            QuestAliasFiller::PlayerSite site;
            site.mKnown = true;
            site.mInterior = true;
            site.mCell = mare;
            site.mLocation = location;
            view.setPlayerSite([site] { return site; });
            const FormId rv = 0x020050B9;
            const std::size_t n = access.mAliases->fillQuest(rv, *access.mState, true);
            std::cout << "SKYRIMSE-RADREAL DLC1RV02 filled " << n << " aliases from the Mare:";
            for (const QuestAlias& al : access.mQuests->quest(rv)->mAliases)
            {
                const AliasFill f = access.mState->aliasFill(rv, al.mId);
                std::cout << " " << al.mName << "=" << std::hex << f.mReference << std::dec;
                if (!al.mIsLocation && f.mReference != 0 && access.mBaseOf)
                    std::cout << "(base " << std::hex << access.mBaseOf(f.mReference) << std::dec << ")";
            }
            std::cout << "\n";
        }
        // the story manager's answer to entering a few locations (the data's own ids; official editor ids of quests)
        for (const FormId l2 : { 0x00016772u, 0x00018A56u, 0x0001F86Du })
        {
            StoryEvent event;
            event.mType = "CLOC";
            event.mLocation2 = l2;
            const std::vector<FormId> chosen = access.mStory(event);
            std::cout << "SKYRIMSE-RADREAL CLOC to " << std::hex << l2 << std::dec << " chooses " << chosen.size() << ":";
            for (const FormId q : chosen)
                std::cout << " " << access.mQuests->quest(q)->mCommon.mEditorId;
            std::cout << "\n";
        }
        EXPECT_GT(access.mQuests->questCount(), 0u);
    }
}
