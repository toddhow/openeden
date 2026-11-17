// Task 0243: which line a bark takes (DialogueRuntime::barkLine). An INFO with no response is not a line, a line whose
// voice file does not exist is not marked said, and a conversation is unchanged. Generated plugins only. Written
// against stubs first and run red (see the stage log of the task).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kVoiceMale = kSelf | 0x801;
    constexpr FormId kNpcHulda = kSelf | 0x200;
    constexpr FormId kTopicNotice = kSelf | 0x300;
    constexpr FormId kTopicHello = kSelf | 0x301;
    constexpr FormId kTopicSteal = kSelf | 0x302;
    constexpr FormId kQuest = kSelf | 0x400;

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

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    struct Spec
    {
        std::uint32_t mLocal = 0;
        std::uint16_t mFlags = 0;
        std::vector<std::string> mTexts{ "What was that?" };
        bool mNoResponse = false; ///< no TRDT / NAM1 at all
        Bytes mConditions;
    };

    Bytes infoRecord(const Spec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        std::uint8_t n = 1;
        if (!s.mNoResponse)
            for (const std::string& t : s.mTexts)
            {
                Bytes trdt;
                trdt.u32(0).u32(50).u32(0).u8(n++).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
                b.append(sub4("TRDT", trdt));
                b.append(sub4("NAM1", Bytes().zstring(t)));
                b.append(sub4("NAM2", Bytes().u8(0)));
            }
        b.append(s.mConditions);
        return record4("INFO", 0, kSelf | s.mLocal, b);
    }

    Bytes topic(
        std::uint32_t id, const std::string& editorId, const std::string& subtype, const std::vector<Spec>& infos)
    {
        Bytes children;
        for (const Spec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(editorId));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(7).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    Bytes plugin(const std::vector<Spec>& notice, const std::vector<Spec>& hello = { Spec() },
        const std::vector<Spec>& pickpocket = { Spec() })
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes dials = topic(kTopicNotice, "Notice", "NOTA", notice);
        dials.append(topic(kSelf | 0x303, "Caught", "PICN", pickpocket));
        dials.append(topic(kTopicHello, "Hello", "HELO", hello));
        dials.append(topic(kTopicSteal, "Steal", "STEA", { Spec() }));
        bytes.append(top("DIAL", dials));
        Bytes npcBody;
        npcBody.append(edid("Hulda"));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        npcBody.append(sub4("ACBS", acbs));
        npcBody.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        npcBody.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        npcBody.append(sub4("VTCK", Bytes().u32(kVoiceMale)));
        bytes.append(top("NPC_", record4("NPC_", 0, kNpcHulda, npcBody)));
        Bytes race;
        race.append(edid("NordRace"));
        race.append(sub4("VTCK", Bytes().u32(kVoiceMale).u32(kVoiceMale)));
        bytes.append(top("RACE", record4("RACE", 0, kRaceNord, race)));
        bytes.append(top("VTYP", record4("VTYP", 0, kVoiceMale, edid("MaleNord"))));
        Bytes quest;
        quest.append(edid("DialogueGeneric"));
        quest.append(sub4("DNAM", Bytes().u16(1).u8(50).u8(0).u32(0).u32(0)));
        quest.append(sub4("NEXT", Bytes()));
        bytes.append(top("QUST", record4("QUST", 0, kQuest, quest)));
        return bytes;
    }

    std::string voiceOf(const char* topicName, std::uint32_t local)
    {
        char tail[48];
        std::snprintf(tail, sizeof(tail), "_%08x_1.fuz", static_cast<unsigned>(kSelf | local));
        return std::string("voice/test.esp/malenord/dialoguegeneric_") + topicName + tail;
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct SelectionTalk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        QuestRecords mQuestRecords;
        ActorIndex mActors;
        DialogueIndex mDialogueIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;
        std::set<std::string> mFiles; ///< the voice files that exist

        explicit SelectionTalk(const Bytes& bytes)
        {
            Content::Error error;
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), "test.esp", order(), DialogueLimits(), mDialogue, error))
                << error.mMessage;
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", order(), ActorLimits(), mActorRecords, error))
                << error.mMessage;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), mQuestRecords, error))
                << error.mMessage;
            mPlugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            EXPECT_NE(mPlugin, nullptr);
            const FormIdResolver resolver(mPlugin->header(), "test.esp", order());
            mActors.add(mActorRecords);
            mDialogueIndex.add(mDialogue, resolver);
            mQuests.add(mQuestRecords, resolver);
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mDialogueIndex, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; });
        }

        void withProbe()
        {
            mRuntime->setVoiceProbe([this](const std::string& path) { return mFiles.count(path) != 0; });
        }

        static DialogueSpeaker speaker()
        {
            DialogueSpeaker s;
            s.mBase = kNpcHulda;
            s.mReference = kNpcHulda | 0x1000;
            return s;
        }
    };

    // ---- 1. an INFO with no response is not a line

    TEST(SkyrimBarkSelection, anInfoWithNoResponseIsSkippedForTheNextOne)
    {
        Spec empty;
        empty.mLocal = 0x20;
        empty.mNoResponse = true;
        Spec spoken;
        spoken.mLocal = 0x21;
        spoken.mTexts = { "Who is there?" };
        SelectionTalk talk(plugin({ empty, spoken }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "Who is there?");
    }

    TEST(SkyrimBarkSelection, aResponseWhoseTextIsEmptyIsNotALineEither)
    {
        Spec empty;
        empty.mLocal = 0x20;
        empty.mTexts = { "" };
        Spec spoken;
        spoken.mLocal = 0x21;
        SelectionTalk talk(plugin({ empty, spoken }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "What was that?");
    }

    TEST(SkyrimBarkSelection, aTopicWhoseInfosAllHaveNoResponseHasNoLine)
    {
        Spec a;
        a.mLocal = 0x20;
        a.mNoResponse = true;
        Spec b = a;
        b.mLocal = 0x21;
        SelectionTalk talk(plugin({ a, b }));
        Turn turn;
        EXPECT_FALSE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_TRUE(turn.text.empty());
    }

    TEST(SkyrimBarkSelection, aRandomPickNeverChoosesAnInfoWithNoResponse)
    {
        std::vector<Spec> specs;
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            Spec s;
            s.mLocal = 0x20 + i;
            s.mFlags = InfoFlags::Random;
            s.mNoResponse = (i % 2) == 0;
            s.mTexts = { "Line " + std::to_string(i) };
            specs.push_back(s);
        }
        SelectionTalk talk(plugin(specs));
        std::set<std::string> seen;
        for (std::uint32_t pick = 0; pick < 16; ++pick)
        {
            Turn turn;
            ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), pick, turn)) << pick;
            EXPECT_FALSE(turn.text.empty()) << pick;
            seen.insert(turn.text);
        }
        EXPECT_EQ(seen, (std::set<std::string>{ "Line 1", "Line 3" }));
    }

    TEST(SkyrimBarkSelection, aOneSpaceTextIsStillALine)
    {
        Spec blank;
        blank.mLocal = 0x20;
        blank.mTexts = { " " };
        SelectionTalk talk(plugin({ blank }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, " ");
    }

    TEST(SkyrimBarkSelection, aConversationGreetingIsChosenAsBefore)
    {
        // The shared selection is not changed: a greeting INFO with no response is still the greeting.
        Spec empty;
        empty.mLocal = 0x30;
        empty.mNoResponse = true;
        Spec spoken;
        spoken.mLocal = 0x31;
        SelectionTalk talk(plugin({ Spec() }, { empty, spoken }));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(SelectionTalk::speaker(), turn, error)) << error;
        EXPECT_TRUE(turn.text.empty());
    }

    // ---- 2. a line whose voice file is missing is not marked said

    TEST(SkyrimBarkSelection, aSayOnceLineWithNoVoiceFileIsNotSpentAndTheNextLineWithOneIsTaken)
    {
        Spec silent;
        silent.mLocal = 0x20;
        silent.mFlags = InfoFlags::SayOnce;
        silent.mTexts = { "No file." };
        Spec voiced;
        voiced.mLocal = 0x21;
        voiced.mTexts = { "Has a file." };
        SelectionTalk talk(plugin({ silent, voiced }));
        talk.mFiles = { voiceOf("notice", 0x21) };
        talk.withProbe();
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "Has a file.");
        ASSERT_EQ(turn.voice.size(), 1u);
        // the file of the first appears later: the say-once line was not used up
        talk.mFiles.insert(voiceOf("notice", 0x20));
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "No file.");
    }

    TEST(SkyrimBarkSelection, aTopicWhoseLinesAllLackAVoiceFileHasNoLineAndSpendsNothing)
    {
        Spec a;
        a.mLocal = 0x20;
        a.mFlags = InfoFlags::SayOnce;
        Spec b = a;
        b.mLocal = 0x21;
        SelectionTalk talk(plugin({ a, b }));
        talk.withProbe(); // no file exists
        Turn turn;
        EXPECT_FALSE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        talk.mFiles = { voiceOf("notice", 0x20), voiceOf("notice", 0x21) };
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "What was that?");
        EXPECT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn))
            << "both say-once lines were still unspent";
    }

    TEST(SkyrimBarkSelection, aSpokenSayOnceLineIsMarkedSaid)
    {
        Spec once;
        once.mLocal = 0x20;
        once.mFlags = InfoFlags::SayOnce;
        once.mTexts = { "Only once." };
        SelectionTalk talk(plugin({ once }));
        talk.mFiles = { voiceOf("notice", 0x20) };
        talk.withProbe();
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_FALSE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
    }

    TEST(SkyrimBarkSelection, aRandomPickSkipsTheInfosWithNoVoiceFile)
    {
        std::vector<Spec> specs;
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            Spec s;
            s.mLocal = 0x20 + i;
            s.mFlags = InfoFlags::Random;
            s.mTexts = { "Line " + std::to_string(i) };
            specs.push_back(s);
        }
        SelectionTalk talk(plugin(specs));
        talk.mFiles = { voiceOf("notice", 0x21) };
        talk.withProbe();
        for (std::uint32_t pick = 0; pick < 9; ++pick)
        {
            Turn turn;
            ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), pick, turn)) << pick;
            EXPECT_EQ(turn.text, "Line 1") << pick;
        }
    }

    TEST(SkyrimBarkSelection, withNoVoiceProbeALineIsStillChosen)
    {
        SelectionTalk talk(plugin({ Spec() }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_TRUE(turn.voice.empty());
    }

    TEST(SkyrimBarkSelection, theProbingOfABarkLeavesTheVoiceStatsOfConversationsAlone)
    {
        Spec silent;
        silent.mLocal = 0x20;
        SelectionTalk talk(plugin({ silent }));
        talk.withProbe();
        Turn turn;
        EXPECT_FALSE(talk.mRuntime->barkLine("NOTA", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(talk.mRuntime->stats().mVoiceMissing, 0u);
    }

    // ---- 3. IsActorAVictim for the speaker that is the victim of the crime

    TEST(SkyrimBarkSelection, aVictimLineIsTakenOnlyForTheSpeakerThatIsTheVictim)
    {
        Spec victimLine;
        victimLine.mLocal = 0x40;
        victimLine.mTexts = { "My purse!" };
        victimLine.mConditions = ctda(314, 0, 1.0f);
        Spec bystanderLine;
        bystanderLine.mLocal = 0x41;
        bystanderLine.mTexts = { "He robbed him." };
        bystanderLine.mConditions = ctda(314, 0, 0.0f);
        SelectionTalk talk(plugin({ Spec() }, { Spec() }, { victimLine, bystanderLine }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("PICN", SelectionTalk::speaker(), 0, turn, true));
        EXPECT_EQ(turn.text, "My purse!");
        ASSERT_TRUE(talk.mRuntime->barkLine("PICN", SelectionTalk::speaker(), 0, turn, false));
        EXPECT_EQ(turn.text, "He robbed him.");
        ASSERT_TRUE(talk.mRuntime->barkLine("PICN", SelectionTalk::speaker(), 0, turn));
        EXPECT_EQ(turn.text, "He robbed him.") << "no victim by default";
    }

    TEST(SkyrimBarkSelection, theVictimIsForgottenAfterTheBark)
    {
        Spec victimLine;
        victimLine.mLocal = 0x40;
        victimLine.mConditions = ctda(314, 0, 1.0f);
        SelectionTalk talk(plugin({ Spec() }, { Spec() }, { victimLine }));
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("PICN", SelectionTalk::speaker(), 0, turn, true));
        EXPECT_FALSE(talk.mRuntime->barkLine("PICN", SelectionTalk::speaker(), 0, turn));
    }
}
