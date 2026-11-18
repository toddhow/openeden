// Task 0310 (docs/tasks/0310-skyrim-daedric-barbas.md): what "A Daedra's Best Friend" needed of the engine, with generated
// plugins, no game data:
//  * SkyrimDaedricContinue: in an ORDINARY conversation a response that links to a topic with no name goes on by itself
//    (Lod's "There's one out on the road ..." goes on to "If you were willing to retrieve him ..." and only then offers
//    "Sure, I can catch him."), as it already did in a forced one (task 0271) and, since task 0304, behind its switch.
//    Written before the rebase onto task 0304, which has the same mechanism (followNamelessLinks): the two tests were
//    red (2 of 3) against the runtime without it and now pass on that one, which is the only mechanism.
// The real-data facts of the quests are in testskyrimsedaedricreal.cpp (gated).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

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
    constexpr FormId kTopicNoDog = kSelf | 0x301; // "No, sorry. I didn't see a dog."
    constexpr FormId kTopicContinue = kSelf | 0x302; // no name
    constexpr FormId kTopicAccept = kSelf | 0x303;
    constexpr FormId kTopicPersuade = kSelf | 0x304;
    constexpr FormId kTopicForced = kSelf | 0x307; // Barbas's forced greeting
    constexpr FormId kTopicLooking = kSelf | 0x308; // "You were looking for me?"
    constexpr FormId kTopicLookingOn = kSelf | 0x309; // no name: "You see, my name is Barbas."
    constexpr std::uint16_t kInvisibleContinue = 0x0040;
    constexpr std::uint16_t kGoodbye = 0x0001;

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

    Bytes infoRecord(
        std::uint32_t id, const std::string& text, const std::vector<std::uint32_t>& links, std::uint16_t flags)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(flags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        for (const std::uint32_t link : links)
            b.append(sub4("TCLT", Bytes().u32(link)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        return record4("INFO", 0u, id, b);
    }

    Bytes topic(std::uint32_t id, const std::string& editor, const std::string& full, const std::string& subtype,
        const std::vector<Bytes>& infos, float priority)
    {
        Bytes children;
        for (const Bytes& i : infos)
            children.append(i);
        Bytes b;
        b.append(edid(editor));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(priority)));
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
            dials.append(
                topic(kTopicHello, "Hello", "", "HELO", { infoRecord(kSelf | 0x20, "Need something?", {}, 0) }, 5.f));
            // Lod's shape: a named choice whose response goes on by itself through an unnamed topic, then offers two
            dials.append(topic(kTopicNoDog, "NoDog", "No, sorry. I didn't see a dog.", "CUST",
                { infoRecord(kSelf | 0x21, "There's one out on the road.", { kTopicContinue }, kInvisibleContinue) },
                1.f));
            dials.append(topic(kTopicContinue, "Continue", "", "CUST",
                { infoRecord(
                    kSelf | 0x22, "If you were willing to retrieve him.", { kTopicAccept, kTopicPersuade }, 0) },
                1.f));
            dials.append(topic(kTopicAccept, "Accept", "Sure, I can catch him.", "CUST",
                { infoRecord(kSelf | 0x23, "Good! Here is the meat.", {}, kGoodbye) }, 1.f));
            dials.append(topic(kTopicPersuade, "Persuade", "I could be convinced.", "CUST",
                { infoRecord(kSelf | 0x24, "Fine, some gold too.", {}, kGoodbye) }, 1.f));
            // Barbas's forced greeting: a named choice, whose response goes on through a topic with no name
            dials.append(topic(kTopicForced, "Forced", "", "CUST",
                { infoRecord(kSelf | 0x27, "You are exactly what I was looking for.", { kTopicLooking }, 0) }, 1.f));
            dials.append(topic(kTopicLooking, "Looking", "You were looking for me?", "CUST",
                { infoRecord(kSelf | 0x28, "Yes, I think you will be just what I need.", { kTopicLookingOn }, 0) },
                1.f));
            dials.append(topic(kTopicLookingOn, "LookingOn", "", "CUST",
                { infoRecord(kSelf | 0x29, "You see, my name is Barbas.", { kTopicAccept }, 0) }, 1.f));
            Bytes npc;
            npc.append(edid("Lod"));
            Bytes acbs;
            acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
            npc.append(sub4("ACBS", acbs));
            npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
            Bytes race;
            race.append(edid("NordRace"));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, dials));
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
            mRuntime->setQuestGreetings(true); // task 0304's switch: a link with no name is followed by the speaker
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

    TEST(SkyrimDaedricContinue, aResponseFlaggedInvisibleContinueGoesOnThroughItsUnnamedLinkAndOffersTheNextChoices)
    {
        Talk talk;
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicNoDog, reply, error)) << error;
        EXPECT_EQ(reply.text, "There's one out on the road. If you were willing to retrieve him.");
        ASSERT_EQ(reply.topics.size(), 2u);
        EXPECT_EQ(reply.topics[0].text, "Sure, I can catch him.");
        EXPECT_EQ(reply.topics[1].text, "I could be convinced.");
    }

    TEST(SkyrimDaedricContinue, theChoiceAfterTheChainSpeaksAndEndsTheTalkAsItsFlagSays)
    {
        Talk talk;
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicNoDog, reply, error)) << error;
        Turn last;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicAccept, last, error)) << error;
        EXPECT_EQ(last.text, "Good! Here is the meat.");
        EXPECT_TRUE(last.endsConversation);
    }

    // Found by running Barbas's forced greeting: the forced (link) mode follows a link with no name itself, so the
    // follow of task 0304 must leave it alone or the speaker says the continuation twice
    TEST(SkyrimDaedricContinue, inAForcedConversationTheContinuationIsSaidOnceAndTheFragmentsRunBeforeTheNextChoices)
    {
        Talk talk;
        int flushes = 0;
        talk.mRuntime->setFragmentFlush([&flushes]() { ++flushes; });
        DialogueSpeaker speaker;
        speaker.mBase = kNpc;
        speaker.mReference = kNpc | 0x1000;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(speaker, kTopicForced, turn, error)) << error;
        Turn reply;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicLooking, reply, error)) << error;
        EXPECT_EQ(reply.text, "Yes, I think you will be just what I need. You see, my name is Barbas.");
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics[0].text, "Sure, I can catch him.");
        EXPECT_GE(flushes, 1);
    }
}
