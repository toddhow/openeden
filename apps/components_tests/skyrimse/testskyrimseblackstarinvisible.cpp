// Task 0338 (docs/tasks/0338-skyrim-black-star.md): what "The Black Star" needed of the dialogue, generated plugins, no
// game data. SkyrimBlackStarInvisible: a response with the Invisible Continue flag goes on through its links even when
// the topic linked has a name (the editor's own text "(Invisible Continue)"): the speaker says it and only then offers
// the next choices (Nelacar's four lines in a row). Without the flag the topic stays a choice.
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

    constexpr FormId kTopicStar = kSelf | 0x310; // "They're for enchanting, correct?"
    constexpr FormId kTopicInvisible = kSelf | 0x311; // named "(Invisible Continue)"
    constexpr FormId kTopicQuestion = kSelf | 0x312; // "Why are you asking?"

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(std::uint16_t continueFlag, int chain = 1)
        {
            Bytes dials;
            dials.append(
                topic(kTopicHello, "Hello", "", "HELO", { infoRecord(kSelf | 0x20, "Need something?", {}, 0) }, 5.f));
            // the star response (flagged Invisible Continue, as Nelacar's are) links to the first "(Invisible
            // Continue)" topic, each links to the next (task 0338: Nelacar's answer is four of them in a row)
            dials.append(topic(kTopicStar, "Star", "They're for enchanting, correct?", "CUST",
                { infoRecord(
                    kSelf | 0x21, "They are. Except the gem is always consumed.", { kTopicInvisible }, continueFlag) },
                1.f));
            for (int i = 0; i < chain; ++i)
            {
                const FormId id = i == 0 ? kTopicInvisible : (kSelf | static_cast<FormId>(0x320 + i));
                const FormId next = i + 1 == chain ? kTopicQuestion : (kSelf | static_cast<FormId>(0x320 + i + 1));
                dials.append(topic(id, "Invisible" + std::to_string(i), "(Invisible Continue)", "CUST",
                    { infoRecord(kSelf | static_cast<FormId>(0x40 + i), "Line " + std::to_string(i + 1) + ".", { next },
                        i + 1 == chain ? std::uint16_t(0) : continueFlag) },
                    1.f));
            }
            dials.append(topic(kTopicQuestion, "Question", "Why are you asking?", "CUST",
                { infoRecord(kSelf | 0x23, "Never mind.", {}, kGoodbye) }, 1.f));
            Bytes npc;
            npc.append(edid("Nelacar"));
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
            mRuntime->setQuestGreetings(true);
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

    TEST(SkyrimBlackStarInvisible, aNamedTopicWithTheInvisibleContinueFlagIsSaidAndNotOffered)
    {
        Talk talk(kInvisibleContinue);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicStar, reply, error)) << error;
        EXPECT_EQ(reply.text, "They are. Except the gem is always consumed. Line 1.");
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics[0].text, "Why are you asking?");
    }

    TEST(SkyrimBlackStarInvisible, theSameNamedTopicWithoutTheFlagStaysAChoice)
    {
        Talk talk(0);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicStar, reply, error)) << error;
        EXPECT_EQ(reply.text, "They are. Except the gem is always consumed.");
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics[0].text, "(Invisible Continue)");
    }

    TEST(SkyrimBlackStarInvisible, withoutTheQuestGreetingSwitchTheOldBehaviourStays)
    {
        Talk talk(kInvisibleContinue);
        talk.mRuntime->setQuestGreetings(false);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicStar, reply, error)) << error;
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics[0].text, "(Invisible Continue)");
    }

    TEST(SkyrimBlackStarInvisible, aRunOfSixInvisibleContinuesIsSaidToItsEndBeforeTheNextChoices)
    {
        Talk talk(kInvisibleContinue, 6);
        Turn greeting;
        ASSERT_TRUE(talk.begin(greeting));
        Turn reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->choose(kTopicStar, reply, error)) << error;
        EXPECT_EQ(
            reply.text, "They are. Except the gem is always consumed. Line 1. Line 2. Line 3. Line 4. Line 5. Line 6.");
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics[0].text, "Why are you asking?");
    }
}
