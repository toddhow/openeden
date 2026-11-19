#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

// Task 0346 (The Wolf Queen Awakened, MS06Start): Falk Firebeard's request is a TOP-LEVEL branch whose starting topic has no name
// (MS06FalkInvestigate0): the player reads the prompt (RNAM) of its INFO. Task 0270 made a nameless topic only ever a choice,
// so it was never listed. With `setPromptStartsAreMenus(true)` a nameless top-level start with a prompt is a menu button; the start of
// a BLOCKING branch (a forced greeting) never is.

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using Turn = openeden::sdk::game::DialogueTurn;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicAsk = kSelf | 0x310; // no name, the INFO has a prompt
    constexpr FormId kTopicSilent = kSelf | 0x311; // no name and no prompt
    constexpr FormId kTopicBlocking = kSelf | 0x312; // no name, a prompt, the start of a Blocking branch
    constexpr FormId kBranchBlocking = kSelf | 0x500;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes infoRecord(std::uint32_t id, const std::string& text, const std::string& prompt)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        if (!prompt.empty())
            b.append(sub4("RNAM", Bytes().zstring(prompt)));
        return record4("INFO", 0u, id, b);
    }

    Bytes topic(std::uint32_t id, const std::string& editor, const std::string& subtype, const std::vector<Bytes>& infos,
        std::uint32_t branch)
    {
        Bytes children;
        for (const Bytes& i : infos)
            children.append(i);
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring(editor)));
        if (branch != 0)
            b.append(sub4("BNAM", Bytes().u32(branch)));
        b.append(sub4("PNAM", Bytes().f32(1.f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
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
            Bytes dials;
            dials.append(topic(kTopicHello, "Hello", "HELO", { infoRecord(kSelf | 0x20, "Yeah?", "") }, 0));
            dials.append(topic(kTopicAsk, "Ask", "CUST",
                { infoRecord(kSelf | 0x30, "Wolfskull Cave? Clear it out.", "Ask Falk about Wolfskull Cave") }, 0));
            dials.append(topic(kTopicSilent, "Silent", "CUST", { infoRecord(kSelf | 0x31, "No prompt.", "") }, 0));
            dials.append(topic(kTopicBlocking, "Blocking", "CUST",
                { infoRecord(kSelf | 0x32, "A forced greeting.", "Never a button") }, kBranchBlocking));
            Bytes branch;
            branch.append(sub4("EDID", Bytes().zstring("BlockingBranch")));
            branch.append(sub4("QNAM", Bytes().u32(kQuest)));
            branch.append(sub4("TNAM", Bytes().u32(0)));
            branch.append(sub4("DNAM", Bytes().u32(2))); // Blocking
            branch.append(sub4("SNAM", Bytes().u32(kTopicBlocking)));
            Bytes npc;
            npc.append(sub4("EDID", Bytes().zstring("Falk")));
            Bytes acbs;
            acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
            npc.append(sub4("ACBS", acbs));
            npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
            Bytes race;
            race.append(sub4("EDID", Bytes().zstring("NordRace")));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, dials));
            bytes.append(group4(Content::fourCC("DLBR"), kGrupTop, record4("DLBR", 0, kBranchBlocking, branch)));
            bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, record4("NPC_", 0, kNpc, npc)));
            bytes.append(group4(Content::fourCC("RACE"), kGrupTop, record4("RACE", 0, kRaceNord, race)));
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
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mIndex, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; });
        }

        bool begin(Turn& turn)
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            s.mReference = kNpc | 0x1000;
            std::string error;
            return mRuntime->begin(s, turn, error);
        }
    };

    std::vector<std::string> texts(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    TEST(SkyrimSolitudePrompt, aNamelessTopLevelStartIsNotAButtonByDefault)
    {
        Talk talk;
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        EXPECT_TRUE(texts(greeting).empty()); // task 0270's rule: a nameless topic is only ever a choice
    }

    TEST(SkyrimSolitudePrompt, withTheSwitchAPromptedNamelessStartIsAButtonAndAnsweredAsItsOwnTopic)
    {
        Talk talk;
        talk.mRuntime->setPromptStartsAreMenus(true);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        EXPECT_EQ(texts(greeting), (std::vector<std::string>{ "Ask Falk about Wolfskull Cave" }));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicAsk, reply, error)) << error;
        EXPECT_EQ(reply.text, "Wolfskull Cave? Clear it out.");
    }

    TEST(SkyrimSolitudePrompt, theStartOfABlockingBranchIsNeverAButton)
    {
        Talk talk;
        EXPECT_TRUE(talk.mIndex.isBlockingStart(kTopicBlocking));
        EXPECT_FALSE(talk.mIndex.isBlockingStart(kTopicAsk));
        EXPECT_FALSE(talk.mIndex.isBlockingStart(kTopicSilent));
        talk.mRuntime->setPromptStartsAreMenus(true);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        for (const std::string& t : texts(greeting))
        {
            EXPECT_NE(t, "Never a button");
            EXPECT_NE(t, "");
        }
    }
}
