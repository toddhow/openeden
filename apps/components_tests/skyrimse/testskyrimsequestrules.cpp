// Task 0356: the quest rules switch and the dialogue's fragment order. Written before the code.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_rules.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    std::string manifestText()
    {
        std::ifstream in(
            std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "game_packs" / "skyrimse" / "extension.json",
            std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    TEST(SkyrimQuestRules, theSettingIsDeclaredInTheManifestAndIsOnByDefault)
    {
        const std::string text = manifestText();
        const std::size_t at = text.find("\"key\": \"" + std::string(questrules::kKey) + "\"");
        ASSERT_NE(at, std::string::npos);
        const std::size_t end = text.find('}', at);
        const std::string entry = text.substr(at, end - at);
        EXPECT_NE(entry.find("\"type\": \"bool\""), std::string::npos);
        EXPECT_NE(entry.find("\"default\": true"), std::string::npos);
    }

    TEST(SkyrimQuestRules, theRuleNamesAreUniqueAndNotEmpty)
    {
        std::set<std::string_view> seen;
        for (const std::string_view name : questrules::kRules)
        {
            EXPECT_FALSE(name.empty());
            EXPECT_TRUE(seen.insert(name).second) << name;
        }
    }

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kTopicHello = kSelf | 0x300;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    // Bersi's greeting (task 0347): two Hello lines, told apart by a quest script variable (pTG01BersiWarned) that the
    // fragment of an EARLIER line sets.
    Bytes infoRecord(std::uint32_t id, const std::string& text, float flagWanted)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        Bytes t;
        t.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", t));
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        Bytes cond = ctda(629, 0, flagWanted, kQuest);
        cond.append(sub4("CIS2", Bytes().zstring("::Flag_var")));
        b.append(cond);
        return record4("INFO", 0u, kSelf | id, b);
    }

    Bytes pluginBytes()
    {
        Bytes children;
        children.append(infoRecord(0x20, "Fresh.", 0.0f));
        children.append(infoRecord(0x21, "Warned.", 1.0f));
        Bytes b;
        b.append(edid("Hello"));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(7).u16(0)));
        b.append(sub4("SNAM", Bytes().text("HELO")));
        b.append(sub4("TIFC", Bytes().u32(2)));
        Bytes dial = record4("DIAL", 0, kTopicHello, b);
        dial.append(group4(kTopicHello, kGrupTopicChildren, children));

        Bytes npc;
        npc.append(edid("Steward"));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        npc.append(sub4("ACBS", acbs));
        npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        npc.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        Bytes race;
        race.append(edid("NordRace"));
        race.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));

        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, dial));
        bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, record4("NPC_", 0, kNpc, npc)));
        bytes.append(group4(Content::fourCC("RACE"), kGrupTop, record4("RACE", 0, kRaceNord, race)));
        return bytes;
    }

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        Talk()
        {
            const Bytes bytes = pluginBytes();
            Content::Error error;
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), "test.esp", order(), DialogueLimits(), mDialogue, error))
                << error.mMessage;
            mPlugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            Content::Error e2;
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", order(), ActorLimits(), mActorRecords, e2))
                << e2.mMessage;
            mActors.add(mActorRecords);
            mIndex.add(mDialogue, FormIdResolver(mPlugin->header(), "test.esp", order()));
            mRuntime = std::make_unique<DialogueRuntime>(
                mActors, mIndex, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; },
                DialogueRuntimeLimits());
        }

        static DialogueSpeaker speaker()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            s.mReference = kNpc | 0x1000;
            return s;
        }
    };

    TEST(SkyrimQuestRulesFlush, theGreetingIsJudgedAfterTheFragmentsOfTheEarlierLineRan)
    {
        Talk talk;
        auto flag = std::make_shared<bool>(false);
        talk.mRuntime->setLiveQuestVariable(
            [flag](FormId, const std::string&) -> std::optional<float> { return *flag ? 1.0f : 0.0f; });
        talk.mRuntime->setFragmentFlush([flag]() { *flag = true; }); // a fragment queued by the line before
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Warned.");
    }

    TEST(SkyrimQuestRulesFlush, withNoFlushTheGreetingSeesTheOldValue)
    {
        Talk talk;
        auto flag = std::make_shared<bool>(false);
        talk.mRuntime->setLiveQuestVariable(
            [flag](FormId, const std::string&) -> std::optional<float> { return *flag ? 1.0f : 0.0f; });
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Fresh.");
    }

    TEST(SkyrimQuestRulesFlush, theFlushRunsBeforeEveryGreetingNotOnce)
    {
        Talk talk;
        int flushes = 0;
        talk.mRuntime->setFragmentFlush([&flushes]() { ++flushes; });
        openeden::sdk::game::DialogueTurn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        const int first = flushes;
        EXPECT_GE(first, 1);
        talk.mRuntime->end();
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_GT(flushes, first);
    }
}
