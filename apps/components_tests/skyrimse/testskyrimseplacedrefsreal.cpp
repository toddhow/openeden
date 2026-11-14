// Task 0160: the placed-reference table, relationships, quest aliases that name references, and the package choice,
// measured on the five Skyrim SE masters. Same harness as task 0156's SkyrimConditionStateRealData: INFO and PACK lists
// fully answered, unanswered conditions by function, the Bannered Mare's greeting picks; plus the table's build time
// and memory, and how NPC package choice changes when the package world sees locations, quest variables and other
// references. Prints aggregates and function indexes only (lines SKYRIMSE-COND4). Skipped without
// OPENEDEN_SKYRIMSE_DATA.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_script.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/speech.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
// psapi.h needs windows.h before it
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#pragma comment(lib, "psapi.lib")
#endif

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::uint64_t workingSetBytes()
    {
#ifdef _MSC_VER
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return counters.WorkingSetSize;
#endif
        return 0;
    }

    struct Tally
    {
        std::uint64_t mLists = 0;
        std::uint64_t mFullyAnswered = 0;
        std::uint64_t mConditions = 0;
        std::uint64_t mUnanswered = 0;
        std::map<std::uint16_t, std::uint64_t> mBy;
        std::map<std::uint32_t, std::uint64_t> mByRunOn;
    };

    void print(const char* label, const Tally& t, std::size_t top)
    {
        std::cout << "SKYRIMSE-COND4 " << label << ": lists " << t.mLists << " fully answered " << t.mFullyAnswered
                  << " conditions " << t.mConditions << " unanswered " << t.mUnanswered << "\n";
        std::vector<std::pair<std::uint64_t, std::uint16_t>> byCount;
        for (const auto& [f, n] : t.mBy)
            byCount.emplace_back(n, f);
        std::sort(byCount.rbegin(), byCount.rend());
        for (std::size_t i = 0; i < byCount.size() && i < top; ++i)
            std::cout << "SKYRIMSE-COND4 " << label << " unanswered fn_" << byCount[i].second << " " << byCount[i].first
                      << "\n";
        for (const auto& [runOn, n] : t.mByRunOn)
            std::cout << "SKYRIMSE-COND4 " << label << " unanswered run-on " << runOn << ": " << n << "\n";
    }

    /// What `answers` said at the end of task 0156 (reconstructed: this task's additions that need no placed-reference
    /// table are switched off): the speaker's own GetDisabled, IsSneaking, GetItemCount and GetEquipped, and a
    /// condition run on the player's own reference (run-on 2 with 0x14).
    bool baseline156(const SkyrimConditionWorld& w, const survey::Ctda& c)
    {
        if (!w.answers(c))
            return false;
        if (c.mRunOn != 1 && (c.mFunction == 35 || c.mFunction == 286 || c.mFunction == 47 || c.mFunction == 182))
            return false;
        if (c.mRunOn == 2 && w.resolveRunOn(2, c.mReference) == SkyrimConditionWorld::kPlayer)
            return false;
        return true;
    }

    const std::vector<std::string>& masters()
    {
        static const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        return names;
    }

    TEST(SkyrimPlacedRefsRealData, theMastersConditionsAnsweredBeforeAndAfter)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        for (const std::string& n : masters())
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : masters())
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ActorIndex actors;
        DialogueIndex dialogue;
        QuestIndex quests;
        PackageIndex packages;
        LocationIndex locations;
        QuestScriptIndex questScripts;
        PlacedRefIndex placed;
        std::map<std::string, CellRecord> cellByEditorId;

        // the table alone: time and memory
        const std::uint64_t memoryBefore = workingSetBytes();
        const auto placedStart = std::chrono::steady_clock::now();
        std::uint64_t refRecords = 0, failed = 0, odd = 0;
        for (const std::string& n : masters())
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            PlacedRefRecords pr;
            ASSERT_TRUE(parsePlacedRefs(*plugin, order, PlacedRefLimits(), pr));
            refRecords += pr.mRefs.size();
            failed += pr.mFailed;
            odd += pr.mOddSubrecords;
            placed.add(std::move(pr));
        }
        placed.finish();
        const auto placedMillis
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - placedStart)
                  .count();
        const std::uint64_t memoryAfter = workingSetBytes();
        std::uint64_t actorsPlaced = 0, dead = 0, disabled = 0;
        for (const PlacedEntry& r : placed.all())
        {
            actorsPlaced += (r.mFlags & PlacedFlags::IsActor) != 0 ? 1 : 0;
            dead += (r.mFlags & PlacedFlags::Dead) != 0 ? 1 : 0;
            disabled += placed.initiallyDisabled(r) ? 1 : 0;
        }
        std::cout << "SKYRIMSE-COND4 placed table: " << refRecords << " reference records read, " << placed.size()
                  << " after the merge (" << actorsPlaced << " actors, " << dead << " start dead, " << disabled
                  << " initially disabled), " << placed.containerCount() << " containers, " << placed.relationCount()
                  << " relationships, failed " << failed << " odd " << odd << "; build " << placedMillis
                  << " ms (parse and merge, one thread, includes reading the five files), table bytes "
                  << placed.memoryBytes() << " (" << placed.memoryBytes() / (1024 * 1024) << " MiB), working set "
                  << memoryBefore / (1024 * 1024) << " -> " << memoryAfter / (1024 * 1024) << " MiB\n";
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(odd, 0u);

        for (const std::string& n : masters())
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            ActorRecords ar;
            ASSERT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), ar));
            QuestRecords qr;
            ASSERT_TRUE(parseQuestRecords(*plugin, order, QuestLimits(), qr));
            PackageRecords pr;
            ASSERT_TRUE(parsePackageRecords(*plugin, order, PackageLimits(), pr));
            LocationRecords lr;
            ASSERT_TRUE(parseLocationRecords(*plugin, order, LocationLimits(), lr));
            QuestScriptRecords sr;
            ASSERT_TRUE(parseQuestScripts(*plugin, order, QuestScriptLimits(), sr));
            ParsedPlugin pp;
            Content::Error rerr;
            ParseLimits skip;
            skip.mSkipReferences = true;
            ASSERT_TRUE(parseRecordsFile(data / n, order, skip, pp, rerr)) << rerr.mMessage;
            actors.add(ar);
            actors.addLeveledLists(pp.mLeveledLists);
            for (const CellRecord& c : pp.mCells)
                if (!c.mCommon.mEditorId.empty())
                    cellByEditorId[c.mCommon.mEditorId] = c;
            const FormIdResolver resolver(plugin->header(), n, order);
            dialogue.add(std::move(dr), resolver);
            quests.add(std::move(qr), resolver);
            packages.add(std::move(pr), resolver);
            locations.add(std::move(lr));
            questScripts.add(std::move(sr));
        }

        std::vector<fs::path> archives;
        for (const char* a :
            { "Dragonborn.bsa", "HearthFires.bsa", "Dawnguard.bsa", "Update.bsa", "Skyrim - Misc.bsa" })
            if (fs::exists(data / a))
                archives.push_back(data / a);
        QuestVariables variables(questScripts, [&](const std::string& script, std::vector<std::uint8_t>& bytes) {
            for (const fs::path& a : archives)
            {
                std::string raw, e;
                if (readArchiveEntry(a, "scripts/" + script + ".pex", raw, e))
                {
                    bytes.assign(reinterpret_cast<const std::uint8_t*>(raw.data()),
                        reinterpret_cast<const std::uint8_t*>(raw.data()) + raw.size());
                    return true;
                }
            }
            return false;
        });

        const QuestState state = QuestState::initial(quests);
        const auto mareIt = cellByEditorId.find("WhiterunBanneredMare");
        ASSERT_NE(mareIt, cellByEditorId.end());
        const FormId mareCell = mareIt->second.mCommon.mId;
        std::map<std::string, FormId> npcByEditorId;
        for (FormId id : actors.npcIds())
            npcByEditorId[actors.npc(id)->mCommon.mEditorId] = id;
        ASSERT_NE(npcByEditorId.find("Hulda"), npcByEditorId.end());

        // the placed reference of an NPC base in the Mare, or 0
        auto mareReferenceOf = [&](FormId base) -> FormId {
            const PlacedRefIndex::Range range = placed.byBase(base);
            for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                if ((*it)->mCell == mareCell)
                    return (*it)->mId;
            return kNullFormId;
        };

        PlayerStandIn player;
        DialogueSpeaker plain;
        plain.mBase = npcByEditorId["Hulda"];
        plain.mLevel = actors.npc(plain.mBase)->mLevel;
        plain.mCell = mareCell;
        DialogueSpeaker rich = plain;
        rich.mInterior = mareIt->second.interior();
        rich.mLocation = mareIt->second.mLocation;
        rich.mLocationKnown = true;
        DialogueSpeaker richRef = rich;
        richRef.mReference = mareReferenceOf(plain.mBase);
        std::cout << "SKYRIMSE-COND4 Hulda's placed reference in the Mare: "
                  << (richRef.mReference != kNullFormId ? "found" : "NOT FOUND") << "\n";
        EXPECT_NE(richRef.mReference, kNullFormId);

        auto contextOf = [&](const DialogInfo* info, const DialogTopic* topic) {
            return info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : kNullFormId);
        };
        // before: the world as task 0156 left it. after: with the placed references (and the speaker's reference).
        auto configure = [&](SkyrimConditionWorld& w, bool after) {
            w.setPlayer(&player);
            w.setQuests(&quests, &state);
            w.setConversationOpen(true);
            w.setRandomSeed(1);
            w.setLocations(&locations);
            w.setQuestVariables(&variables);
            if (after)
            {
                w.setPlacedRefs(&placed);
                w.setPackages(&packages);
                w.setSpeaker(richRef);
            }
            else
                w.setSpeaker(rich);
        };

        // ---- every INFO ------------------------------------------------------------------------------------------
        SkyrimConditionWorld before(actors, dialogue), after(actors, dialogue);
        configure(before, false);
        configure(after, true);
        Tally tb, ta, ts;
        std::map<int, std::uint64_t> alias566;
        for (FormId id : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(id);
            for (const DialogInfo* info : dialogue.infosOf(id))
            {
                if (info->mConditions.empty())
                    continue;
                for (SkyrimConditionWorld* w : { &before, &after })
                {
                    w->setPlugin(&dialogue.resolverOf(*info));
                    w->setQuestContext(contextOf(info, topic));
                }
                ++tb.mLists;
                ++ta.mLists;
                ++ts.mLists;
                bool b = true, a = true, sp = true;
                for (const survey::Ctda& c : info->mConditions)
                {
                    ++tb.mConditions;
                    ++ta.mConditions;
                    ++ts.mConditions;
                    if (!baseline156(before, c))
                    {
                        ++tb.mUnanswered;
                        ++tb.mBy[c.mFunction];
                        ++tb.mByRunOn[c.mRunOn];
                        b = false;
                    }
                    if (!before.answers(c))
                    {
                        ++ts.mUnanswered;
                        ++ts.mBy[c.mFunction];
                        sp = false;
                    }
                    if (!after.answers(c))
                    {
                        ++ta.mUnanswered;
                        ++ta.mBy[c.mFunction];
                        ++ta.mByRunOn[c.mRunOn];
                        a = false;
                        if (c.mFunction == 566)
                        {
                            const QuestRecord* qr = quests.quest(contextOf(info, topic));
                            const QuestAlias* alias = nullptr;
                            if (qr != nullptr)
                                for (const QuestAlias& al : qr->mAliases)
                                    if (al.mId == c.mParam1)
                                        alias = &al;
                            ++alias566[alias == nullptr ? -1 : static_cast<int>(alias->mFill)];
                        }
                    }
                }
                tb.mFullyAnswered += b ? 1 : 0;
                ta.mFullyAnswered += a ? 1 : 0;
                ts.mFullyAnswered += sp ? 1 : 0;
            }
        }
        print("INFO 0156 baseline (reconstructed)", tb, 40);
        print("INFO speaker-only additions, no table", ts, 12);
        print("INFO after", ta, 40);
        for (const auto& [k, v] : alias566)
            std::cout << "SKYRIMSE-COND4 fn_566 still not answered, alias fill kind " << k << " (-1 none): " << v
                      << "\n";
        EXPECT_GE(ta.mFullyAnswered, tb.mFullyAnswered);
        EXPECT_LE(ta.mUnanswered, tb.mUnanswered);

        // ---- every PACK (the package world: no player, no conversation; wired = locations, quest variables and the
        // placed references, with the actor in a cell) -----------------------------------------------------------
        SkyrimConditionWorld pold(actors, dialogue), pnew(actors, dialogue);
        for (SkyrimConditionWorld* w : { &pold, &pnew })
        {
            w->setQuests(&quests, &state);
            w->setSpeaker(plain);
        }
        pnew.setLocations(&locations);
        pnew.setQuestVariables(&variables);
        pnew.setPlacedRefs(&placed);
        pnew.setSpeaker(richRef);
        Tally pt, pat;
        for (FormId id : packages.packageIds())
        {
            const PackageRecord* p = packages.package(id);
            if (p == nullptr || p->mConditions.empty())
                continue;
            pold.setPlugin(&packages.resolverOf(*p));
            pnew.setPlugin(&packages.resolverOf(*p));
            ++pt.mLists;
            ++pat.mLists;
            bool b = true, a = true;
            for (const survey::Ctda& c : p->mConditions)
            {
                ++pt.mConditions;
                ++pat.mConditions;
                if (!baseline156(pold, c))
                {
                    ++pt.mUnanswered;
                    ++pt.mBy[c.mFunction];
                    b = false;
                }
                if (!pnew.answers(c))
                {
                    ++pat.mUnanswered;
                    ++pat.mBy[c.mFunction];
                    a = false;
                }
            }
            pt.mFullyAnswered += b ? 1 : 0;
            pat.mFullyAnswered += a ? 1 : 0;
        }
        print("PACK 0156 baseline (no wiring)", pt, 20);
        print("PACK after (wired)", pat, 20);

        // ---- the Bannered Mare's greeting, before and after, with the person's own reference ----------------------
        for (const char* name :
            { "Hulda", "Saadia", "Mikael", "JouaneManette", "Orgnar", "Ysolda", "Sinmir", "Delphine" })
        {
            const auto it = npcByEditorId.find(name);
            if (it == npcByEditorId.end())
                continue;
            DialogueSpeaker s = rich;
            s.mBase = it->second;
            s.mLevel = actors.npc(it->second)->mLevel;
            DialogueSpeaker sRef = s;
            sRef.mReference = mareReferenceOf(s.mBase);
            std::string pickedBefore, pickedAfter;
            std::uint64_t aheadOld = 0, aheadNew = 0, ahead = 0;
            for (const bool afterRun : { false, true })
            {
                SelectionOptions o;
                o.mQuestRunning = [&](FormId q) { return !state.exists(q) || state.running(q); };
                o.mQuests = &quests;
                o.mPick = s.mBase;
                SkyrimConditionWorld w(actors, dialogue);
                configure(w, afterRun);
                const Selection pick = pickFromTopics(dialogue, "HELO", w, afterRun ? sRef : s, o);
                (afterRun ? pickedAfter : pickedBefore) = pick.mInfo == nullptr
                    ? std::string("none")
                    : pick.mTopic->mCommon.mEditorId + "/" + std::to_string(pick.mInfo->mCommon.mId);
                if (!afterRun || pick.mInfo == nullptr)
                    continue;
                std::vector<FormId> ids = dialogue.topicIds("HELO");
                std::stable_sort(ids.begin(), ids.end(),
                    [&](FormId x, FormId y) { return dialogue.topic(x)->mPriority > dialogue.topic(y)->mPriority; });
                SkyrimConditionWorld oldW(actors, dialogue);
                configure(oldW, false);
                oldW.setSpeaker(s);
                w.setSpeaker(sRef);
                bool reached = false;
                for (FormId id : ids)
                {
                    for (const DialogInfo* info : dialogue.infosOf(id))
                    {
                        if (info == pick.mInfo)
                        {
                            reached = true;
                            break;
                        }
                        const FormId q = contextOf(info, dialogue.topic(id));
                        if (q != kNullFormId && state.exists(q) && !state.running(q))
                            continue;
                        if (info->mSpeaker != kNullFormId && info->mSpeaker != s.mBase)
                            continue;
                        for (SkyrimConditionWorld* x : { &oldW, &w })
                        {
                            x->setPlugin(&dialogue.resolverOf(*info));
                            x->setQuestContext(q);
                        }
                        ++ahead;
                        bool o1 = false, n1 = false;
                        for (const survey::Ctda& c : info->mConditions)
                        {
                            o1 = o1 || !baseline156(oldW, c);
                            n1 = n1 || !w.answers(c);
                        }
                        aheadOld += o1 ? 1 : 0;
                        aheadNew += n1 ? 1 : 0;
                    }
                    if (reached)
                        break;
                }
            }
            std::cout << "SKYRIMSE-COND4 greeting " << name << ": pick before " << pickedBefore << " after "
                      << pickedAfter << (pickedBefore == pickedAfter ? " (same)" : " (DIFFERENT)")
                      << "; infos ahead of the pick " << ahead << " uncertain: before " << aheadOld << " after "
                      << aheadNew << "\n";
        }
    }

    // How NPC package choice changes when the package world sees locations, quest variables and other references:
    // every placed actor of the five masters, 24 hours, a weekday and a weekend day, through the real world view.
    TEST(SkyrimPlacedRefsRealData, packageChoiceWithAndWithoutTheWiring)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        std::vector<fs::path> paths;
        for (const std::string& n : masters())
        {
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
            paths.push_back(data / n);
        }
        DataFolderFiles files(data);
        SkyrimWorldView view(SkyrimWorldView::Options(), &files);
        std::vector<std::string> problems;
        ASSERT_EQ(view.load(paths, problems), paths.size());

        const auto start = std::chrono::steady_clock::now();
        bool ready = false;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(240))
        {
            if (view.ready())
            {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(ready) << "the dialogue worker did not finish";
        std::cout
            << "SKYRIMSE-COND4 dialogue worker ready after "
            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()
            << " ms (the reading of every record family on the worker; the placed-reference part alone took "
            << view.placedRefsBuildMillis() << " ms of it)\n";
        const PlacedRefIndex* placed = view.placedRefs();
        ASSERT_NE(placed, nullptr);

        struct Result
        {
            std::uint64_t mActors = 0;
            std::uint64_t mSamples = 0;
            std::uint64_t mChanged = 0; ///< (actor, hour, day) with another package
            std::uint64_t mChangedToNone = 0;
            std::uint64_t mChangedFromNone = 0;
            std::uint64_t mActorsAffected = 0;
            std::map<std::string, std::uint64_t> mByEditorId; ///< the new winner of a changed sample, top ones only
        } r;
        for (const PlacedEntry& ref : placed->all())
        {
            if ((ref.mFlags & PlacedFlags::IsActor) == 0 || (ref.mFlags & PlacedFlags::Dead) != 0
                || ref.mBase == kNullFormId)
                continue;
            std::vector<const PackageRecord*> list;
            if (!view.packagesFor(ref.mBase, list) || list.empty())
                continue;
            ++r.mActors;
            bool affected = false;
            for (const std::int32_t day : { 0, 1, 6 })
                for (int hour = 0; hour < 24; ++hour)
                {
                    view.setPackageConditionsWired(false);
                    const PackageRecord* oldChoice
                        = selectPackage(list, static_cast<float>(hour) + 0.25f, day, true, view, ref.mBase, ref.mId);
                    view.setPackageConditionsWired(true);
                    const PackageRecord* newChoice
                        = selectPackage(list, static_cast<float>(hour) + 0.25f, day, true, view, ref.mBase, ref.mId);
                    ++r.mSamples;
                    if (oldChoice != newChoice)
                    {
                        ++r.mChanged;
                        affected = true;
                        r.mChangedToNone += newChoice == nullptr ? 1 : 0;
                        r.mChangedFromNone += oldChoice == nullptr ? 1 : 0;
                        if (newChoice != nullptr && r.mByEditorId.size() < 4000)
                            ++r.mByEditorId[newChoice->mCommon.mEditorId];
                    }
                }
            r.mActorsAffected += affected ? 1 : 0;
        }
        view.setPackageConditionsWired(true);
        std::cout << "SKYRIMSE-COND4 package choice: " << r.mActors << " placed living actors with packages, "
                  << r.mSamples << " samples (24 hours x 3 days), changed " << r.mChanged << " (to no package "
                  << r.mChangedToNone << ", from no package " << r.mChangedFromNone << "), actors with any change "
                  << r.mActorsAffected << "\n";
        std::vector<std::pair<std::uint64_t, std::string>> top;
        for (const auto& [name, n] : r.mByEditorId)
            top.emplace_back(n, name);
        std::sort(top.rbegin(), top.rend());
        for (std::size_t i = 0; i < top.size() && i < 8; ++i)
            std::cout << "SKYRIMSE-COND4 package choice changed to: " << top[i].second << " x" << top[i].first << "\n";
        EXPECT_GT(r.mActors, 0u);
    }
}
