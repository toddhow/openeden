// Task 0156: how many INFO and PACK conditions of the five Skyrim SE masters the neutral world answers from data (the
// `answers` notion of dialogue.hpp), before and after the conversation / fresh-game state, locations and quest
// variables of this task. Prints aggregates and function indexes only (lines starting SKYRIMSE-COND3); never a text.
// Skipped when OPENEDEN_SKYRIMSE_DATA is not set; the quest variables also need "Skyrim - Misc.bsa" and the other
// archives of the masters.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_script.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/speech.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    struct Tally
    {
        std::uint64_t mLists = 0;
        std::uint64_t mFullyAnswered = 0;
        std::uint64_t mConditions = 0;
        std::uint64_t mUnanswered = 0;
        std::map<std::uint16_t, std::uint64_t> mBy;
    };

    void print(const char* label, const Tally& t, std::size_t top)
    {
        std::cout << "SKYRIMSE-COND3 " << label << ": lists " << t.mLists << " fully answered " << t.mFullyAnswered
                  << " conditions " << t.mConditions << " unanswered " << t.mUnanswered << "\n";
        std::vector<std::pair<std::uint64_t, std::uint16_t>> byCount;
        for (const auto& [f, n] : t.mBy)
            byCount.emplace_back(n, f);
        std::sort(byCount.rbegin(), byCount.rend());
        for (std::size_t i = 0; i < byCount.size() && i < top; ++i)
            std::cout << "SKYRIMSE-COND3 " << label << " unanswered fn_" << byCount[i].second << " " << byCount[i].first
                      << "\n";
    }

    /// What `answers` said before this task: the conditions it learned here (and only those) did not count. Every
    /// other path of `answers` is unchanged, so this is exact for the world the caller configured.
    bool oldAnswers(const SkyrimConditionWorld& w, const survey::Ctda& c)
    {
        if (!w.answers(c))
            return false;
        switch (c.mFunction)
        {
            case 310: // GetInWorldspace: the measurement gives no worldspace, so the old world never answered it
            case 77:
            case 84:
            case 606:
            case 314:
            case 131:
            case 130:
            case 629: // no subject
            case 46:
            case 453:
            case 579:
            case 66:
            case 249:
            case 50:
            case 300:
            case 359:
            case 562:
            case 640:
            case 145:
            case 144:
            case 402:
            case 375:
            case 376:
            case 289:
            case 49: // speaker and player state
                return false;
            case 69:
            case 70: // the player's race and sex (run-on 1); the speaker's were answered before
                return c.mRunOn != 1;
            default:
                return true;
        }
    }

    TEST(SkyrimConditionStateRealData, theMastersConditionsAnsweredBeforeAndAfter)
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
        PackageIndex packages;
        LocationIndex locations;
        QuestScriptIndex questScripts;
        std::map<std::string, CellRecord> cellByEditorId;
        std::uint64_t questsWithScripts = 0, locationCount = 0;
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
            PackageRecords pr;
            ASSERT_TRUE(parsePackageRecords(*plugin, order, PackageLimits(), pr));
            LocationRecords lr;
            ASSERT_TRUE(parseLocationRecords(*plugin, order, LocationLimits(), lr));
            EXPECT_EQ(lr.mCount.mFailed, 0u);
            EXPECT_EQ(lr.mOddSubrecords, 0u);
            QuestScriptRecords sr;
            ASSERT_TRUE(parseQuestScripts(*plugin, order, QuestScriptLimits(), sr));
            EXPECT_EQ(sr.mDamaged, 0u) << n;
            questsWithScripts += sr.mWithScripts;
            locationCount += lr.mCount.mParsed;
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
        std::cout << "SKYRIMSE-COND3 LCTN parsed " << locationCount << " (merged " << locations.count()
                  << "), QUST with a VMAD script " << questsWithScripts << " (merged " << questScripts.count() << ")\n";

        // the compiled scripts: the five masters' archives, the latest first
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
        std::map<std::string, FormId> npcByEditorId;
        for (FormId id : actors.npcIds())
            npcByEditorId[actors.npc(id)->mCommon.mEditorId] = id;
        ASSERT_NE(npcByEditorId.find("Hulda"), npcByEditorId.end());

        PlayerStandIn player;
        DialogueSpeaker plain;
        plain.mBase = npcByEditorId["Hulda"];
        plain.mLevel = actors.npc(plain.mBase)->mLevel;
        plain.mCell = mareIt->second.mCommon.mId;
        DialogueSpeaker rich = plain;
        rich.mInterior = mareIt->second.interior();
        rich.mLocation = mareIt->second.mLocation;
        rich.mLocationKnown = true;

        auto contextOf = [&](const DialogInfo* info, const DialogTopic* topic) {
            return info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : kNullFormId);
        };
        auto configure = [&](SkyrimConditionWorld& w, bool after) {
            w.setPlayer(&player);
            w.setQuests(&quests, &state);
            if (after)
            {
                w.setConversationOpen(true);
                w.setRandomSeed(1);
                w.setLocations(&locations);
                w.setQuestVariables(&variables);
                w.setSpeaker(rich);
            }
            else
                w.setSpeaker(plain);
        };

        // ---- every INFO ------------------------------------------------------------------------------------------
        SkyrimConditionWorld after(actors, dialogue);
        configure(after, true);
        Tally tb, ta;
        std::uint64_t var629 = 0, var629known = 0;
        std::map<std::string, std::uint64_t> unknown629;
        std::map<int, std::uint64_t> alias566;
        for (FormId id : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(id);
            for (const DialogInfo* info : dialogue.infosOf(id))
            {
                if (info->mConditions.empty())
                    continue;
                after.setPlugin(&dialogue.resolverOf(*info));
                after.setQuestContext(contextOf(info, topic));
                ++tb.mLists;
                ++ta.mLists;
                bool b = true, a = true;
                for (const survey::Ctda& c : info->mConditions)
                {
                    ++tb.mConditions;
                    ++ta.mConditions;
                    if (!oldAnswers(after, c))
                    {
                        ++tb.mUnanswered;
                        ++tb.mBy[c.mFunction];
                        b = false;
                    }
                    const bool ans = after.answers(c);
                    if (!ans)
                    {
                        ++ta.mUnanswered;
                        ++ta.mBy[c.mFunction];
                        a = false;
                    }
                    if (c.mFunction == 629)
                    {
                        ++var629;
                        var629known += ans ? 1 : 0;
                        if (!ans && unknown629.size() < 400)
                        {
                            const auto q = dialogue.resolverOf(*info).resolve(c.mParam1);
                            const QuestRecord* qr = q ? quests.quest(*q) : nullptr;
                            ++unknown629[(qr != nullptr ? qr->mCommon.mEditorId : std::string("?")) + " " + c.mString2];
                        }
                    }
                    if (!ans && c.mFunction == 566)
                    {
                        const FormId q = contextOf(info, topic);
                        const QuestRecord* qr = quests.quest(q);
                        const QuestAlias* alias = nullptr;
                        if (qr != nullptr)
                            for (const QuestAlias& al : qr->mAliases)
                                if (al.mId == c.mParam1)
                                    alias = &al;
                        ++alias566[alias == nullptr ? -1 : static_cast<int>(alias->mFill)];
                    }
                }
                tb.mFullyAnswered += b ? 1 : 0;
                ta.mFullyAnswered += a ? 1 : 0;
            }
        }
        print("INFO before", tb, 60);
        print("INFO after", ta, 60);
        std::cout << "SKYRIMSE-COND3 INFO fn_629 conditions " << var629 << " answered " << var629known
                  << "; scripts loaded " << variables.scriptsLoaded() << " missing " << variables.scriptsMissing()
                  << "\n";
        for (const auto& [k, v] : unknown629)
            std::cout << "SKYRIMSE-COND3 fn_629 not answered: " << k << " x" << v << "\n";
        for (const auto& [k, v] : alias566)
            std::cout << "SKYRIMSE-COND3 fn_566 not answered, alias fill kind " << k << " (-1 none found): " << v
                      << "\n";
        EXPECT_GT(ta.mFullyAnswered, tb.mFullyAnswered);
        EXPECT_LT(ta.mUnanswered, tb.mUnanswered);

        // ---- every PACK (the package world has the quests and no player; the new answers that apply to it) -------
        SkyrimConditionWorld pa(actors, dialogue);
        pa.setQuests(&quests, &state);
        pa.setLocations(&locations);
        pa.setQuestVariables(&variables);
        pa.setSpeaker(rich);
        Tally pt, pat;
        for (FormId id : packages.packageIds())
        {
            const PackageRecord* p = packages.package(id);
            if (p == nullptr || p->mConditions.empty())
                continue;
            pa.setPlugin(&packages.resolverOf(*p));
            ++pt.mLists;
            ++pat.mLists;
            bool b = true, a = true;
            for (const survey::Ctda& c : p->mConditions)
            {
                ++pt.mConditions;
                ++pat.mConditions;
                if (!oldAnswers(pa, c))
                {
                    ++pt.mUnanswered;
                    ++pt.mBy[c.mFunction];
                    b = false;
                }
                if (!pa.answers(c))
                {
                    ++pat.mUnanswered;
                    ++pat.mBy[c.mFunction];
                    a = false;
                }
            }
            pt.mFullyAnswered += b ? 1 : 0;
            pat.mFullyAnswered += a ? 1 : 0;
        }
        print("PACK before", pt, 30);
        print("PACK after", pat, 30);

        // ---- the Bannered Mare's greeting ----------------------------------------------------------------------
        // "uncertain ahead": the infos the choice walks over before the pick (topics by priority, infos in order, the
        // skips of selectInfo) that hold a condition the world does not answer from data; counted with the old and
        // the new `answers`.
        for (const char* name :
            { "Hulda", "Saadia", "Mikael", "JouaneManette", "Orgnar", "Ysolda", "Sinmir", "Delphine" })
        {
            const auto it = npcByEditorId.find(name);
            if (it == npcByEditorId.end())
                continue;
            DialogueSpeaker s = rich;
            s.mBase = it->second;
            s.mLevel = actors.npc(it->second)->mLevel;
            SelectionOptions o;
            o.mQuestRunning = [&](FormId q) { return !state.exists(q) || state.running(q); };
            o.mQuests = &quests;
            o.mPick = s.mBase;
            SkyrimConditionWorld w(actors, dialogue);
            configure(w, true);
            const Selection pick = pickFromTopics(dialogue, "HELO", w, s, o);
            std::uint64_t oldAhead = 0, newAhead = 0, ahead = 0;
            if (pick.mInfo != nullptr)
            {
                std::vector<FormId> ids = dialogue.topicIds("HELO");
                std::stable_sort(ids.begin(), ids.end(),
                    [&](FormId x, FormId y) { return dialogue.topic(x)->mPriority > dialogue.topic(y)->mPriority; });
                w.setSpeaker(s);
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
                        w.setPlugin(&dialogue.resolverOf(*info));
                        w.setQuestContext(q);
                        ++ahead;
                        bool o1 = false, n1 = false;
                        for (const survey::Ctda& c : info->mConditions)
                        {
                            o1 = o1 || !oldAnswers(w, c);
                            n1 = n1 || !w.answers(c);
                        }
                        oldAhead += o1 ? 1 : 0;
                        newAhead += n1 ? 1 : 0;
                    }
                    if (reached)
                        break;
                }
            }
            std::cout << "SKYRIMSE-COND3 greeting " << name << ": topic "
                      << (pick.mInfo == nullptr ? std::string("none") : pick.mTopic->mCommon.mEditorId)
                      << " considered " << pick.mConsidered << " passing " << pick.mPassing
                      << " infos ahead of the pick " << ahead << " of them uncertain: before " << oldAhead << " after "
                      << newAhead << "\n";
        }
    }
}
