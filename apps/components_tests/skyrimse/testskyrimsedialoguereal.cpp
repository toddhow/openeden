// Task 0137, M-S4 slice 1: the aggregate check of the dialogue records and of the response choice on the player's own
// Skyrim SE masters. Prints counts and the editor ids of official topics and NPCs (lines starting SKYRIMSE-DIALOGUE),
// never a response text, a path or a mod name, and writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/pack_core.hpp>
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

    TEST(SkyrimDialogueRealData, theFiveMastersDecodeAndTheMareSpeaksHello)
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
        std::map<std::uint32_t, TypeCount> counts;
        std::uint64_t unresolved = 0, odd = 0, errors = 0, outside = 0, localisedTexts = 0, ilFound = 0, ilChecked = 0;
        std::uint64_t promptsChecked = 0, promptsInIl = 0, promptsInStrings = 0;
        std::uint64_t ticMismatch = 0;
        std::map<std::string, std::uint64_t> oddBy;
        std::map<std::string, FormId> cellByEditorId;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            DialogueRecords dr;
            ASSERT_TRUE(parseDialogueRecords(*plugin, order, DialogueLimits(), dr));
            ActorRecords ar;
            ASSERT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), ar));
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

            for (const auto& [type, c] : dr.mCounts)
            {
                TypeCount& sum = counts[type];
                sum.mSeen += c.mSeen;
                sum.mParsed += c.mParsed;
                sum.mFailed += c.mFailed;
                sum.mDeleted += c.mDeleted;
            }
            unresolved += dr.mUnresolvedFormIds;
            odd += dr.mOddSubrecords;
            errors += dr.mErrors.size() + dr.mErrorsDropped;
            outside += dr.mInfosOutsideTopicGroup;
            for (const auto& [k, v] : dr.mOddBy)
                oddBy[k] += v;

            // The response texts are in the plugin's .ILSTRINGS, the prompts and names in .STRINGS or .ILSTRINGS.
            if (plugin->header().isLocalized())
            {
                StringTable il, st;
                std::string bytes, e;
                Content::Error te;
                if (readArchiveEntry(data / "Skyrim - Interface.bsa",
                        stringTablePath(n, "english", StringTableKind::IlStrings), bytes, e))
                {
                    ASSERT_TRUE(StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                        StringTableKind::IlStrings, n, il, te));
                }
                if (readArchiveEntry(data / "Skyrim - Interface.bsa",
                        stringTablePath(n, "english", StringTableKind::Strings), bytes, e))
                {
                    ASSERT_TRUE(StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                        StringTableKind::Strings, n, st, te));
                }
                for (const DialogInfo& info : dr.mInfos)
                {
                    for (const DialogResponse& r : info.mResponses)
                        if (r.mText.mPresent && r.mText.mLocalized)
                        {
                            ++localisedTexts;
                            ++ilChecked;
                            if (il.find(r.mText.mId) != nullptr)
                                ++ilFound;
                        }
                    if (info.mPrompt.mPresent && info.mPrompt.mLocalized)
                    {
                        ++promptsChecked;
                        promptsInIl += il.find(info.mPrompt.mId) != nullptr ? 1 : 0;
                        promptsInStrings += st.find(info.mPrompt.mId) != nullptr ? 1 : 0;
                    }
                }
            }
            dialogue.add(std::move(dr), FormIdResolver(plugin->header(), n, order));
        }

        // TIFC against the merged group sizes can differ where a later plugin adds or deletes; count topics whose
        // count differs, as information.
        std::uint64_t topics = 0, withInfos = 0;
        std::map<std::string, std::uint64_t> bySubtype;
        for (FormId id : dialogue.topicIds())
        {
            const DialogTopic* t = dialogue.topic(id);
            ++topics;
            ++bySubtype[t->mSubtypeName];
            const std::size_t n = dialogue.infosOf(id).size();
            withInfos += n != 0;
            if (n != t->mInfoCount)
                ++ticMismatch;
        }
        std::cout << "SKYRIMSE-DIALOGUE records DIAL seen " << counts[Content::fourCC("DIAL")].mSeen << " parsed "
                  << counts[Content::fourCC("DIAL")].mParsed << " failed " << counts[Content::fourCC("DIAL")].mFailed
                  << "; INFO seen " << counts[Content::fourCC("INFO")].mSeen << " parsed "
                  << counts[Content::fourCC("INFO")].mParsed << " failed " << counts[Content::fourCC("INFO")].mFailed
                  << "; GLOB parsed " << counts[Content::fourCC("GLOB")].mParsed << "; FLST parsed "
                  << counts[Content::fourCC("FLST")].mParsed << "; odd subrecords " << odd << "; unresolved ids "
                  << unresolved << "; errors " << errors << "; INFO outside a topic group " << outside << "\n";
        for (const auto& [k, v] : oddBy)
            std::cout << "SKYRIMSE-DIALOGUE odd " << k << " " << v << "\n";
        std::cout << "SKYRIMSE-DIALOGUE merged topics " << topics << " with infos " << withInfos << " infos "
                  << dialogue.infoCount() << " topics whose TIFC differs from the merged count " << ticMismatch << "\n";
        std::cout << "SKYRIMSE-DIALOGUE response texts localised " << localisedTexts << " found in ILSTRINGS " << ilFound
                  << " of " << ilChecked << "; prompts " << promptsChecked << " found in ILSTRINGS " << promptsInIl
                  << " in STRINGS " << promptsInStrings << "\n";
        for (const char* sub : { "HELO", "GBYE", "CUST", "SCEN", "IDAT" })
            std::cout << "SKYRIMSE-DIALOGUE subtype " << sub << " topics " << bySubtype[sub] << "\n";

        EXPECT_EQ(counts[Content::fourCC("DIAL")].mFailed, 0u);
        EXPECT_EQ(counts[Content::fourCC("INFO")].mFailed, 0u);
        EXPECT_EQ(counts[Content::fourCC("INFO")].mParsed, counts[Content::fourCC("INFO")].mSeen);
        EXPECT_EQ(outside, 0u);
        EXPECT_EQ(errors, 0u);
        EXPECT_EQ(ilFound, ilChecked);
        EXPECT_GT(dialogue.infoCount(), 30000u);

        // ---- the Hello topics: how much of each INFO the data alone can answer ---------------------------------
        SkyrimConditionWorld probe(actors, dialogue);
        DialogueSpeaker nobody;
        probe.setSpeaker(nobody);
        std::uint64_t helloInfos = 0, helloUncertain = 0, helloTopicsWithInfos = 0;
        std::uint64_t maxPerTopic = 0;
        std::map<std::uint16_t, std::uint64_t> unansweredBy;
        for (FormId id : dialogue.topicIds("HELO"))
        {
            const auto infos = dialogue.infosOf(id);
            if (infos.empty())
                continue;
            ++helloTopicsWithInfos;
            std::uint64_t uncertain = 0;
            for (const DialogInfo* info : infos)
            {
                ++helloInfos;
                bool any = false;
                for (const survey::Ctda& c : info->mConditions)
                    if (!probe.answers(c))
                    {
                        ++unansweredBy[c.mFunction];
                        any = true;
                    }
                uncertain += any ? 1 : 0;
            }
            helloUncertain += uncertain;
            maxPerTopic = std::max(maxPerTopic, uncertain);
        }
        std::cout << "SKYRIMSE-DIALOGUE hello topics with infos " << helloTopicsWithInfos << " infos " << helloInfos
                  << " of which depend on a query the data cannot answer " << helloUncertain
                  << " (most in one topic " << maxPerTopic << ")\n";
        {
            std::vector<std::pair<std::uint64_t, std::uint16_t>> top;
            for (const auto& [f, n] : unansweredBy)
                top.emplace_back(n, f);
            std::sort(top.rbegin(), top.rend());
            for (std::size_t i = 0; i < top.size() && i < 8; ++i)
                std::cout << "SKYRIMSE-DIALOGUE hello unanswered fn_" << top[i].second << " " << top[i].first << "\n";
        }

        // ---- the Bannered Mare's people -------------------------------------------------------------------------
        const auto mare = cellByEditorId.find("WhiterunBanneredMare");
        std::cout << "SKYRIMSE-DIALOGUE mare cell " << (mare != cellByEditorId.end() ? "found" : "missing") << "\n";
        std::map<std::string, FormId> npcByEditorId;
        for (FormId id : actors.npcIds())
            npcByEditorId[actors.npc(id)->mCommon.mEditorId] = id;
        std::set<FormId> pickedInfos;
        std::uint64_t picked = 0, asked = 0;
        for (const char* name : { "Hulda", "Saadia", "Mikael", "Jouane", "JouaneManette", "Orgnar", "Ysolda", "Sinmir",
                 "Delphine", "Eorlund" })
        {
            const auto it = npcByEditorId.find(name);
            if (it == npcByEditorId.end())
                continue;
            DialogueSpeaker s;
            s.mBase = it->second;
            s.mLevel = actors.npc(it->second)->mLevel;
            if (mare != cellByEditorId.end())
                s.mCell = mare->second;
            SkyrimConditionWorld world(actors, dialogue);
            conditions::Evaluator eval;
            const Selection sel = pickFromTopics(dialogue, "HELO", world, s, SelectionOptions(), &eval);
            ++asked;
            std::cout << "SKYRIMSE-DIALOGUE npc " << name << " hello: considered " << sel.mConsidered << " passing "
                      << sel.mPassing << " uncertain-before-pick " << sel.mUncertainBefore;
            if (sel.mInfo != nullptr)
            {
                ++picked;
                pickedInfos.insert(sel.mInfo->mCommon.mId);
                std::cout << " picked topic " << sel.mTopic->mCommon.mEditorId << " priority " << sel.mTopic->mPriority
                          << " info responses " << sel.mInfo->mResponses.size() << " flags 0x" << std::hex
                          << sel.mInfo->mFlags << std::dec << " unanswered-in-pick "
                          << unansweredConditions(*sel.mInfo, world);
            }
            else
                std::cout << " nothing";
            std::cout << " failed-closed lists " << eval.mListsFailedClosed << "\n";
        }
        std::cout << "SKYRIMSE-DIALOGUE mare npcs asked " << asked << " with a hello " << picked << "\n";
    }
}
