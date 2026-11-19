// Task 0331 (docs/tasks/0331-skyrim-bards-college-2.md): what Viarmo's poem (MS05 "Tending the Flames") needed of the
// dialogue runtime, with generated plugins, no game data:
//  * SkyrimBardsPoem: the verse-2 prompt is a response with no name whose links are the player's choices AND a nameless
//    topic ("You're leaving? Okay, we'll just work on the verse in a bit.") that the response itself names as its
//    walk-away topic (TWAT). The speaker goes on through a link with no name (task 0304), but the walk-away topic is
//    what is said when the player LEAVES (task 0315, 0325): it is not a continuation, so the choices stay and the
//    line is not spoken.
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
    constexpr FormId kTopicVerse1 = kSelf | 0x310; // "We can do this. What's the first verse?"
    constexpr FormId kTopicPrompt = kSelf | 0x311; // no name: asks what really happened, links the choices
    constexpr FormId kTopicIncompetent = kSelf | 0x312;
    constexpr FormId kTopicEvil = kSelf | 0x313;
    constexpr FormId kTopicExit = kSelf | 0x314; // no name: the walk-away line
    constexpr std::uint16_t kWalkAway = 0x0080;
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

    Bytes infoRecord(std::uint32_t id, const std::string& text, const std::vector<std::uint32_t>& links,
        std::uint16_t flags, std::uint32_t walkAwayTopic = 0)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(flags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        for (const std::uint32_t link : links)
            b.append(sub4("TCLT", Bytes().u32(link)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        if (walkAwayTopic != 0)
            b.append(sub4("TWAT", Bytes().u32(walkAwayTopic)));
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
                topic(kTopicHello, "Hello", "", "HELO", { infoRecord(kSelf | 0x20, "I'm all ears.", {}, 0) }, 5.f));
            dials.append(topic(kTopicVerse1, "Verse1", "We can do this. What's the first verse?", "CUST",
                { infoRecord(kSelf | 0x30, "O, Olaf, our subjugator.", { kTopicPrompt }, kInvisibleContinue) }, 1.f));
            dials.append(topic(kTopicPrompt, "Prompt", "", "CUST",
                { infoRecord(kSelf | 0x31, "What do we say really happened?",
                    { kTopicIncompetent, kTopicEvil, kTopicExit }, kWalkAway, kTopicExit) },
                1.f));
            dials.append(topic(kTopicIncompetent, "Incompetent", "Olaf was incompetent.", "CUST",
                { infoRecord(kSelf | 0x32, "A fine start.", {}, 0) }, 1.f));
            dials.append(topic(kTopicEvil, "Evil", "Olaf was Numinex. (Persuade)", "CUST",
                { infoRecord(kSelf | 0x33, "That might work.", {}, 0) }, 1.f));
            dials.append(topic(kTopicExit, "Exit", "", "CUST",
                { infoRecord(kSelf | 0x34, "You're leaving? Okay, we'll just work on the verse in a bit.", {},
                    0) },
                1.f));
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
            mRuntime->setWalkAwayTopics(true); // task 0315's: the TWAT topic is said as the player leaves
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

    TEST(SkyrimBardsPoem, theVersePromptIsFollowedAndItsChoicesAreOfferedWithoutTheWalkAwayLine)
    {
        Talk talk;
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicVerse1, reply, error)) << error;
        EXPECT_EQ(reply.text, "O, Olaf, our subjugator. What do we say really happened?");
        EXPECT_EQ(reply.text.find("leaving"), std::string::npos);
        ASSERT_EQ(reply.topics.size(), 2u);
        EXPECT_EQ(reply.topics[0].text, "Olaf was incompetent.");
        EXPECT_EQ(reply.topics[1].text, "Olaf was Numinex. (Persuade)");
        EXPECT_FALSE(reply.endsConversation);
    }

    TEST(SkyrimBardsPoem, aChoiceAfterThePromptAnswersAsItsOwnTopic)
    {
        Talk talk;
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicVerse1, reply, error)) << error;
        Turn chosen;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicEvil, chosen, error)) << error;
        EXPECT_EQ(chosen.text, "That might work.");
    }
}
