// Task 0139, M-S4 slice 2: the aggregate check of the quest records, the initial quest state and its effect on the
// response choice, on the player's own Skyrim SE masters. Prints counts and official editor ids only (lines starting
// SKYRIMSE-QUEST), never a text, a path or a mod name, and writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    const char* fillName(AliasFillKind k)
    {
        switch (k)
        {
            case AliasFillKind::FindMatching:
                return "FindMatching";
            case AliasFillKind::SpecificReference:
                return "SpecificReference";
            case AliasFillKind::UniqueActor:
                return "UniqueActor";
            case AliasFillKind::SpecificLocation:
                return "SpecificLocation";
            case AliasFillKind::ExternalAlias:
                return "ExternalAlias";
            case AliasFillKind::CreatedObject:
                return "CreatedObject";
            case AliasFillKind::FromEvent:
                return "FromEvent";
            case AliasFillKind::NearAlias:
                return "NearAlias";
            case AliasFillKind::InLocationAlias:
                return "InLocationAlias";
        }
        return "?";
    }

    TEST(SkyrimQuestRealData, theFiveMastersDecodeAndTheQuestStateChangesTheHelloChoice)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ActorIndex actors;
        DialogueIndex dialogue;
        QuestIndex quests;
        std::map<std::string, FormId> cellByEditorId;
        std::uint64_t seen = 0, parsed = 0, failed = 0, unresolved = 0, odd = 0, errors = 0;
        std::uint64_t rawStartEnabled = 0, rawStages = 0, rawAliases = 0, rawRefFill = 0, rawActorFill = 0,
                      rawObjectives = 0, rawHeadConditions = 0, rawBadConditions = 0, rawStartUp = 0,
                      rawCompleteEntries = 0, rawFailEntries = 0;
        std::map<std::string, std::uint64_t> oddBy;
        for (const std::string& n : names)
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
            ParsedPlugin pp;
            Content::Error rerr;
            ParseLimits skip;
            skip.mSkipReferences = true;
            ASSERT_TRUE(parseRecordsFile(data / n, order, skip, pp, rerr)) << rerr.mMessage;
            actors.add(ar);
            actors.addLeveledLists(pp.mLeveledLists);
            for (const CellRecord& c : pp.mCells)
                if (!c.mCommon.mEditorId.empty())
                    cellByEditorId[c.mCommon.mEditorId] = c.mCommon.mId;

            const TypeCount& c = qr.mCounts[Content::fourCC("QUST")];
            seen += c.mSeen;
            parsed += c.mParsed;
            failed += c.mFailed;
            unresolved += qr.mUnresolvedFormIds;
            odd += qr.mOddSubrecords;
            errors += qr.mErrors.size() + qr.mErrorsDropped;
            for (const auto& [k, v] : qr.mOddBy)
                oddBy[k] += v;
            for (const QuestRecord& q : qr.mQuests)
            {
                rawStartEnabled += q.startsEnabled() ? 1 : 0;
                rawStages += q.mStages.size();
                rawObjectives += q.mObjectives.size();
                rawHeadConditions += q.mConditions.size();
                rawBadConditions += q.mConditionsOk ? 0 : 1;
                for (const QuestStage& s : q.mStages)
                {
                    rawStartUp += s.startUp() ? 1 : 0;
                    for (const QuestLogEntry& e : s.mEntries)
                    {
                        rawCompleteEntries += (e.mFlags & LogEntryFlags::CompleteQuest) != 0 ? 1 : 0;
                        rawFailEntries += (e.mFlags & LogEntryFlags::FailQuest) != 0 ? 1 : 0;
                    }
                }
                for (const QuestAlias& a : q.mAliases)
                {
                    ++rawAliases;
                    rawRefFill += a.mFill == AliasFillKind::SpecificReference ? 1 : 0;
                    rawActorFill += a.mFill == AliasFillKind::UniqueActor ? 1 : 0;
                }
            }
            const FormIdResolver resolver(plugin->header(), n, order);
            dialogue.add(std::move(dr), resolver);
            quests.add(std::move(qr), resolver);
        }
        std::cout << "SKYRIMSE-QUEST records QUST seen " << seen << " parsed " << parsed << " failed " << failed
                  << "; odd subrecords " << odd << "; unresolved ids " << unresolved << "; errors " << errors << "\n";
        for (const auto& [k, v] : oddBy)
            std::cout << "SKYRIMSE-QUEST odd " << k << " " << v << "\n";
        std::cout << "SKYRIMSE-QUEST per-plugin sums (before the merge): start-game-enabled " << rawStartEnabled
                  << " stages " << rawStages << " start-up stages " << rawStartUp << " complete-quest entries "
                  << rawCompleteEntries << " fail-quest entries " << rawFailEntries << " objectives " << rawObjectives
                  << " aliases " << rawAliases << " (specific reference " << rawRefFill << ", unique actor "
                  << rawActorFill << ") dialogue-condition CTDA " << rawHeadConditions << " damaged lists "
                  << rawBadConditions << "\n";
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(errors, 0u);
        EXPECT_EQ(parsed, seen);
        EXPECT_EQ(unresolved, 0u);
        // independent walker (a throw-away Python survey of the same files): 2,391 records, 455 start enabled,
        // 17,491 aliases, 3,752 with ALFR, 3,831 with ALUA
        EXPECT_EQ(seen, 2391u);
        EXPECT_EQ(rawStartEnabled, 455u);
        EXPECT_EQ(rawAliases, 17491u);
        EXPECT_EQ(rawRefFill, 3752u);
        EXPECT_EQ(rawActorFill, 3831u);

        InitialStateReport report;
        const QuestState state = QuestState::initial(quests, &report);
        std::cout << "SKYRIMSE-QUEST merged quests " << quests.questCount() << " start enabled (running) "
                  << report.mStartEnabled << " of which with a start-up stage " << report.mStartEnabledWithStartUpStage
                  << " already completed " << report.mAlreadyCompleted << "\n";
        std::cout << "SKYRIMSE-QUEST aliases " << report.mAliases << " reference " << report.mReferenceAliases
                  << " location " << report.mLocationAliases << " filled from data " << report.mAliasesDataFillable
                  << " left empty (conditional or not a reference) " << report.mAliasesConditional << "\n";
        for (const auto& [k, v] : report.mByFill)
            std::cout << "SKYRIMSE-QUEST alias fill " << fillName(k) << " " << v << "\n";

        // ---- the Hello topics: how much the quests settle ------------------------------------------------------
        auto contextOf = [&](const DialogInfo* info, const DialogTopic* topic) {
            return info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : kNullFormId);
        };
        SkyrimConditionWorld without(actors, dialogue);
        SkyrimConditionWorld with(actors, dialogue);
        with.setQuests(&quests, &state);
        DialogueSpeaker nobody;
        without.setSpeaker(nobody);
        with.setSpeaker(nobody);
        std::uint64_t helloInfos = 0, uncertainWithout = 0, uncertainWith = 0, topicsWithInfos = 0;
        std::map<std::uint16_t, std::uint64_t> byWithout, byWith;
        for (FormId id : dialogue.topicIds("HELO"))
        {
            const auto infos = dialogue.infosOf(id);
            if (infos.empty())
                continue;
            ++topicsWithInfos;
            const DialogTopic* topic = dialogue.topic(id);
            for (const DialogInfo* info : infos)
            {
                ++helloInfos;
                without.setPlugin(&dialogue.resolverOf(*info));
                with.setPlugin(&dialogue.resolverOf(*info));
                with.setQuestContext(contextOf(info, topic));
                bool a = false, b = false;
                for (const survey::Ctda& c : info->mConditions)
                {
                    if (!without.answers(c))
                    {
                        ++byWithout[c.mFunction];
                        a = true;
                    }
                    if (!with.answers(c))
                    {
                        ++byWith[c.mFunction];
                        b = true;
                    }
                }
                uncertainWithout += a ? 1 : 0;
                uncertainWith += b ? 1 : 0;
            }
        }
        std::cout << "SKYRIMSE-QUEST hello topics with infos " << topicsWithInfos << " infos " << helloInfos
                  << ": depend on a query the data cannot answer: slice 1 " << uncertainWithout << ", with quest state "
                  << uncertainWith << "\n";
        for (const int fn : { 58, 59, 56, 543, 566, 629 })
        {
            const std::uint16_t f = static_cast<std::uint16_t>(fn);
            std::cout << "SKYRIMSE-QUEST hello unanswered fn_" << fn << ": slice 1 " << byWithout[f]
                      << ", with quest state " << byWith[f] << "\n";
        }
        // fn_566 names an alias; how many of those the data fills (whoever the speaker is)
        {
            std::uint64_t aliasConditions = 0, fillable = 0, unique = 0, reference = 0;
            for (FormId id : dialogue.topicIds("HELO"))
            {
                const DialogTopic* topic = dialogue.topic(id);
                for (const DialogInfo* info : dialogue.infosOf(id))
                    for (const survey::Ctda& c : info->mConditions)
                        if (c.mFunction == 566)
                        {
                            ++aliasConditions;
                            const FormId q = contextOf(info, topic);
                            const AliasFill f = state.aliasFill(q, c.mParam1);
                            fillable += f.mKnown ? 1 : 0;
                            unique += f.mKnown && f.mActor != kNullFormId ? 1 : 0;
                            reference += f.mKnown && f.mReference != kNullFormId ? 1 : 0;
                        }
            }
            std::cout << "SKYRIMSE-QUEST hello fn_566 conditions " << aliasConditions
                      << ", naming an alias the data fills " << fillable << " (unique actor " << unique
                      << ", specific reference " << reference << ")\n";
        }
        {
            std::vector<std::pair<std::uint64_t, std::uint16_t>> topn;
            for (const auto& [f, n] : byWith)
                topn.emplace_back(n, f);
            std::sort(topn.rbegin(), topn.rend());
            for (std::size_t i = 0; i < topn.size() && i < 8; ++i)
                std::cout << "SKYRIMSE-QUEST hello still unanswered fn_" << topn[i].second << " " << topn[i].first
                          << "\n";
        }
        EXPECT_LT(uncertainWith, uncertainWithout);

        // ---- the Bannered Mare's people, slice 1 against the quest state ---------------------------------------
        const auto mare = cellByEditorId.find("WhiterunBanneredMare");
        std::map<std::string, FormId> npcByEditorId;
        for (FormId id : actors.npcIds())
            npcByEditorId[actors.npc(id)->mCommon.mEditorId] = id;
        std::uint64_t asked = 0, same = 0, differ = 0, picked = 0;
        for (const char* name :
            { "Hulda", "Saadia", "Mikael", "JouaneManette", "Orgnar", "Ysolda", "Sinmir", "Delphine", "Eorlund" })
        {
            const auto it = npcByEditorId.find(name);
            if (it == npcByEditorId.end())
                continue;
            DialogueSpeaker s;
            s.mBase = it->second;
            s.mLevel = actors.npc(it->second)->mLevel;
            if (mare != cellByEditorId.end())
                s.mCell = mare->second;
            SkyrimConditionWorld w1(actors, dialogue);
            conditions::Evaluator e1;
            const Selection a = pickFromTopics(dialogue, "HELO", w1, s, SelectionOptions(), &e1);
            SkyrimConditionWorld w2(actors, dialogue);
            w2.setQuests(&quests, &state);
            conditions::Evaluator e2;
            SelectionOptions o2;
            o2.mQuestRunning = [&](FormId q) { return !state.exists(q) || state.running(q); };
            o2.mQuests = &quests;
            const Selection b = pickFromTopics(dialogue, "HELO", w2, s, o2, &e2);
            ++asked;
            const bool same1 = a.mInfo == b.mInfo;
            same += same1 ? 1 : 0;
            differ += same1 ? 0 : 1;
            picked += b.mInfo != nullptr ? 1 : 0;
            std::cout << "SKYRIMSE-QUEST npc " << name << " hello: slice 1 considered " << a.mConsidered << " passing "
                      << a.mPassing << " uncertain-before-pick " << a.mUncertainBefore << "; with quests considered "
                      << b.mConsidered << " passing " << b.mPassing << " uncertain-before-pick " << b.mUncertainBefore
                      << "; same pick " << (same1 ? "yes" : "no");
            if (b.mInfo != nullptr)
            {
                w2.setPlugin(&dialogue.resolverOf(*b.mInfo));
                w2.setQuestContext(contextOf(b.mInfo, b.mTopic));
            }
            if (b.mInfo != nullptr)
                std::cout << "; picked topic " << b.mTopic->mCommon.mEditorId << " priority " << b.mTopic->mPriority
                          << " unanswered-in-pick " << unansweredConditions(*b.mInfo, w2);
            std::cout << "\n";
            if (std::string(name) == "Hulda")
            {
                // what is still unanswered ahead of the pick (topics by priority, infos in order, as the choice walks)
                std::vector<FormId> ids = dialogue.topicIds("HELO");
                std::stable_sort(ids.begin(), ids.end(),
                    [&](FormId x, FormId y) { return dialogue.topic(x)->mPriority > dialogue.topic(y)->mPriority; });
                std::map<std::uint16_t, std::uint64_t> left;
                bool reached = false;
                w2.setSpeaker(s);
                for (FormId id : ids)
                {
                    for (const DialogInfo* info : dialogue.infosOf(id))
                    {
                        if (info == b.mInfo)
                            reached = true;
                        if (reached)
                            break;
                        const FormId q = contextOf(info, dialogue.topic(id));
                        if (q != kNullFormId && state.exists(q) && !state.running(q))
                            continue;
                        if (info->mSpeaker != kNullFormId && info->mSpeaker != s.mBase)
                            continue;
                        w2.setPlugin(&dialogue.resolverOf(*info));
                        w2.setQuestContext(q);
                        for (const survey::Ctda& c : info->mConditions)
                            if (!w2.answers(c))
                                ++left[c.mFunction];
                    }
                    if (reached)
                        break;
                }
                for (const auto& [f, n] : left)
                    std::cout << "SKYRIMSE-QUEST npc Hulda still unanswered ahead of the pick fn_" << f << " " << n
                              << "\n";
            }
        }
        std::cout << "SKYRIMSE-QUEST mare npcs asked " << asked << " with a hello " << picked
                  << " same pick as slice 1 " << same << " different " << differ << "\n";
    }
}
