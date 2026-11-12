// Task 0143, milestone M-S4: the voice of a Skyrim SE conversation. The file name rules (pure), the editor ids the pack
// has to keep (plugin file name, voice type), the speaker's voice type, and the runtime naming the voice files of a
// response through a probe. Generated plugins only; the fixtures are copied from testskyrimsedialogueruntime.cpp.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/voice.hpp>

#include <gtest/gtest.h>

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

    // ---- the file name rules -----------------------------------------------------------------------------------------

    TEST(SkyrimVoiceNames, aShortQuestAndTopicAreJoinedWholeInLowerCase)
    {
        EXPECT_EQ(voiceStem("VoicePowers", "Shout03"), "voicepowers_shout03");
        EXPECT_EQ(voiceStem("DA10", "Hello"), "da10_hello");
    }

    TEST(SkyrimVoiceNames, aLongStemCutsTheQuestToTenAndTheWholeToTwentySixCharacters)
    {
        // Real file names of Skyrim.esm: dialoguege_offerservicesto, da10_da10logrolfdonotfreet.
        EXPECT_EQ(voiceStem("DialogueGeneric", "OfferServicesToPlayer"), "dialoguege_offerservicesto");
        EXPECT_EQ(voiceStem("DA10", "DA10LogrolfDoNotFreeThem"), "da10_da10logrolfdonotfreet");
        EXPECT_EQ(voiceStem("RelationshipMarriageFIN", "RelationshipMarriageAccepted"), "relationsh_relationshipmar");
    }

    TEST(SkyrimVoiceNames, twentySixCharactersAreKeptAndTwentySevenCutTheQuest)
    {
        // "abcdefghijk" + "_" + 14 = 26 characters: whole. One more topic character: 27, so the quest is cut to ten.
        EXPECT_EQ(voiceStem("abcdefghijk", "12345678901234"), "abcdefghijk_12345678901234");
        EXPECT_EQ(voiceStem("abcdefghijk", "123456789012345"), "abcdefghij_123456789012345");
    }

    TEST(SkyrimVoiceNames, aTopicWithoutAnEditorIdLeavesTheQuestAndATrailingUnderscore)
    {
        // Real: wicraftitem03__0009bffd_1 and dialoguegeneric__...
        EXPECT_EQ(voiceStem("WICraftItem03", ""), "wicraftitem03_");
        EXPECT_EQ(voiceStem("DialogueGeneric", ""), "dialoguegeneric_");
    }

    TEST(SkyrimVoiceNames, theFullPathHasThePluginTheVoiceTypeTheStemTheIdAndTheNumber)
    {
        EXPECT_EQ(voicePath("Skyrim.esm", "MaleNord", "dialoguege_hello", 0x00065C4D, 1),
            "voice/skyrim.esm/malenord/dialoguege_hello_00065c4d_1.fuz");
        EXPECT_EQ(voicePath("Dawnguard.esm", "FemaleYoungEager", "x_y", 0x01002F47, 12),
            "voice/dawnguard.esm/femaleyoungeager/x_y_01002f47_12.fuz");
    }

    TEST(SkyrimVoiceNames, namesFromDataThatCouldLeaveTheVoiceFolderAreRefused)
    {
        EXPECT_TRUE(voicePath("", "MaleNord", "a_b", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "", "a_b", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "MaleNord", "", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "Male/Nord", "a_b", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "Male\\Nord", "a_b", 1, 1).empty());
        EXPECT_TRUE(voicePath("..", "MaleNord", "a_b", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "MaleNord", "a/../b", 1, 1).empty());
        EXPECT_TRUE(voicePath("Skyrim.esm", "Male\nNord", "a_b", 1, 1).empty());
    }

    // ---- a generated conversation --------------------------------------------------------------------------------------

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kVoiceMale = kSelf | 0x801;
    constexpr FormId kVoiceFemale = kSelf | 0x802;
    constexpr FormId kVoiceRaceMale = kSelf | 0x803;
    constexpr FormId kVoiceRaceFemale = kSelf | 0x804;
    constexpr FormId kNpcWithVoice = kSelf | 0x200;
    constexpr FormId kNpcFemaleRace = kSelf | 0x201;
    constexpr FormId kNpcMaleRace = kSelf | 0x202;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicAsk = kSelf | 0x301;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kInfoGreeting = kSelf | 0x20;
    constexpr FormId kInfoAnswer = kSelf | 0x21;

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

    Bytes infoRecord(std::uint32_t id, const std::vector<std::string>& texts)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        std::uint8_t n = 1;
        for (const std::string& t : texts)
        {
            Bytes trdt;
            trdt.u32(0).u32(50).u32(0).u8(n++).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
            b.append(sub4("TRDT", trdt));
            b.append(sub4("NAM1", Bytes().zstring(t)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        return record4("INFO", 0, id, b);
    }

    Bytes topic(std::uint32_t id, const std::string& editorId, const std::string& full, const std::string& subtype,
        std::uint8_t category, const Bytes& infos)
    {
        Bytes b;
        if (!editorId.empty())
            b.append(edid(editorId));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, infos));
        return record;
    }

    Bytes acbs(std::uint32_t flags)
    {
        Bytes b;
        b.u32(flags).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        return b;
    }

    Bytes npc(std::uint32_t id, const std::string& name, std::uint32_t flags, std::uint32_t voice)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("ACBS", acbs(flags)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        if (voice != 0)
            b.append(sub4("VTCK", Bytes().u32(voice)));
        return record4("NPC_", 0, id, b);
    }

    Bytes conversationPlugin()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes dials = topic(kTopicHello, "Hello", "", "HELO", 7, infoRecord(kInfoGreeting, { "Well met.", "Be quick." }));
        dials.append(topic(kTopicAsk, "AskAround", "Ask around", "CUST", 0, infoRecord(kInfoAnswer, { "No." })));
        bytes.append(top("DIAL", dials));
        Bytes npcs = npc(kNpcWithVoice, "Hulda", 0, kVoiceMale);
        npcs.append(npc(kNpcFemaleRace, "Greta", NpcFlags::Female, 0));
        npcs.append(npc(kNpcMaleRace, "Gus", 0, 0));
        bytes.append(top("NPC_", npcs));
        Bytes race;
        race.append(edid("NordRace"));
        race.append(sub4("VTCK", Bytes().u32(kVoiceRaceMale).u32(kVoiceRaceFemale)));
        bytes.append(top("RACE", record4("RACE", 0, kRaceNord, race)));
        Bytes voices = record4("VTYP", 0, kVoiceMale, edid("MaleNord"));
        voices.append(record4("VTYP", 0, kVoiceFemale, edid("FemaleNord")));
        voices.append(record4("VTYP", 0, kVoiceRaceMale, edid("MaleRaceDefault")));
        voices.append(record4("VTYP", 0, kVoiceRaceFemale, edid("FemaleRaceDefault")));
        bytes.append(top("VTYP", voices));
        Bytes quest;
        quest.append(edid("DialogueGeneric"));
        // Start game enabled, priority 50, type 0: the quest runs, so its dialogue is heard.
        quest.append(sub4("DNAM", Bytes().u16(1).u8(50).u8(0).u32(0).u32(0)));
        quest.append(sub4("NEXT", Bytes()));
        bytes.append(top("QUST", record4("QUST", 0, kQuest, quest)));
        return bytes;
    }

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        QuestRecords mQuestRecords;
        ActorIndex mActors;
        DialogueIndex mDialogueIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;
        std::set<std::string> mFiles;
        std::vector<std::string> mAsked;

        Talk()
        {
            const Bytes bytes = conversationPlugin();
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
            mRuntime->setVoiceProbe([this](const std::string& path) {
                mAsked.push_back(path);
                return mFiles.count(path) != 0;
            });
        }

        static DialogueSpeaker speaker(FormId base)
        {
            DialogueSpeaker s;
            s.mBase = base;
            s.mReference = base | 0x1000;
            return s;
        }
    };

    using Turn = openeden::sdk::game::DialogueTurn;

    // ---- what the index keeps ---------------------------------------------------------------------------------------------

    TEST(SkyrimVoiceIndex, theIndexKeepsThePluginFileNameAndTheVoiceTypeEditorIds)
    {
        Talk talk;
        const DialogInfo* info = talk.mDialogueIndex.infosOf(kTopicHello).at(0);
        EXPECT_EQ(talk.mDialogueIndex.pluginName(info->mPlugin), "test.esp");
        EXPECT_EQ(talk.mDialogueIndex.voiceTypeName(kVoiceMale), "MaleNord");
        EXPECT_EQ(talk.mDialogueIndex.voiceTypeName(kVoiceRaceFemale), "FemaleRaceDefault");
        EXPECT_TRUE(talk.mDialogueIndex.voiceTypeName(kSelf | 0x999).empty());
        EXPECT_TRUE(talk.mDialogueIndex.pluginName(99).empty());
    }

    TEST(SkyrimVoiceIndex, theSpeakersVoiceTypeIsItsOwnElseItsRacesForItsSex)
    {
        Talk talk;
        SkyrimConditionWorld world(talk.mActors, talk.mDialogueIndex);
        world.setSpeaker(Talk::speaker(kNpcWithVoice));
        EXPECT_EQ(world.speakerVoiceType(), kVoiceMale);
        world.setSpeaker(Talk::speaker(kNpcFemaleRace));
        EXPECT_EQ(world.speakerVoiceType(), kVoiceRaceFemale);
        world.setSpeaker(Talk::speaker(kNpcMaleRace));
        EXPECT_EQ(world.speakerVoiceType(), kVoiceRaceMale);
        world.setSpeaker(DialogueSpeaker());
        EXPECT_EQ(world.speakerVoiceType(), kNullFormId);
    }

    // ---- the runtime ---------------------------------------------------------------------------------------------------------

    TEST(SkyrimVoiceRuntime, aGreetingNamesOneVoiceFilePerResponseLineThatExists)
    {
        Talk talk;
        talk.mFiles = { "voice/test.esp/malenord/dialoguegeneric_hello_01000020_1.fuz",
            "voice/test.esp/malenord/dialoguegeneric_hello_01000020_2.fuz" };
        talk.withProbe();
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcWithVoice), turn, error)) << error;
        EXPECT_EQ(turn.text, "Well met. Be quick.");
        ASSERT_EQ(turn.voice.size(), 2u);
        EXPECT_EQ(turn.voice[0], "voice/test.esp/malenord/dialoguegeneric_hello_01000020_1.fuz");
        EXPECT_EQ(turn.voice[1], "voice/test.esp/malenord/dialoguegeneric_hello_01000020_2.fuz");
        EXPECT_EQ(talk.mRuntime->stats().mVoiceFound, 2u);
        EXPECT_EQ(talk.mRuntime->stats().mVoiceMissing, 0u);
    }

    TEST(SkyrimVoiceRuntime, aLineWithoutAFileIsLeftOutAndCounted)
    {
        Talk talk;
        talk.mFiles = { "voice/test.esp/malenord/dialoguegeneric_hello_01000020_2.fuz" };
        talk.withProbe();
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcWithVoice), turn, error)) << error;
        ASSERT_EQ(turn.voice.size(), 1u);
        EXPECT_EQ(turn.voice[0], "voice/test.esp/malenord/dialoguegeneric_hello_01000020_2.fuz");
        EXPECT_EQ(talk.mRuntime->stats().mVoiceFound, 1u);
        EXPECT_EQ(talk.mRuntime->stats().mVoiceMissing, 1u);
    }

    TEST(SkyrimVoiceRuntime, aChosenTopicsResponseHasItsVoiceToo)
    {
        Talk talk;
        talk.mFiles = { "voice/test.esp/malenord/dialoguegeneric_askaround_01000021_1.fuz" };
        talk.withProbe();
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcWithVoice), turn, error)) << error;
        ASSERT_EQ(turn.topics.size(), 1u);
        ASSERT_TRUE(talk.mRuntime->choose(turn.topics[0].id, turn, error)) << error;
        EXPECT_EQ(turn.text, "No.");
        ASSERT_EQ(turn.voice.size(), 1u);
        EXPECT_EQ(turn.voice[0], "voice/test.esp/malenord/dialoguegeneric_askaround_01000021_1.fuz");
    }

    TEST(SkyrimVoiceRuntime, theRacesVoiceTypeIsUsedWhenTheNpcHasNone)
    {
        Talk talk;
        talk.mFiles = { "voice/test.esp/femaleracedefault/dialoguegeneric_hello_01000020_1.fuz" };
        talk.withProbe();
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcFemaleRace), turn, error)) << error;
        ASSERT_EQ(turn.voice.size(), 1u);
        EXPECT_EQ(turn.voice[0], "voice/test.esp/femaleracedefault/dialoguegeneric_hello_01000020_1.fuz");
    }

    TEST(SkyrimVoiceRuntime, withoutAProbeThereIsNoVoiceAndNoFileIsAsked)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcWithVoice), turn, error)) << error;
        EXPECT_TRUE(turn.voice.empty());
        EXPECT_EQ(talk.mRuntime->stats().mVoiceFound, 0u);
        EXPECT_EQ(talk.mRuntime->stats().mVoiceMissing, 0u);
    }

    TEST(SkyrimVoiceRuntime, anUnknownSpeakerAsksForNoFile)
    {
        Talk talk;
        talk.withProbe();
        Turn turn;
        std::string error;
        // No NPC, so no voice type, so no path.
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kSelf | 0x555), turn, error) || !error.empty());
        EXPECT_TRUE(turn.voice.empty());
        EXPECT_TRUE(talk.mAsked.empty());
    }

    TEST(SkyrimVoiceRuntime, theSubtitleStaysTheJoinedTextWhateverTheVoice)
    {
        Talk talk;
        talk.mFiles = { "voice/test.esp/malenord/dialoguegeneric_hello_01000020_1.fuz" };
        talk.withProbe();
        Turn withVoice;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcWithVoice), withVoice, error)) << error;
        Talk plain;
        Turn without;
        ASSERT_TRUE(plain.mRuntime->begin(Talk::speaker(kNpcWithVoice), without, error)) << error;
        EXPECT_EQ(withVoice.text, without.text);
        EXPECT_EQ(withVoice.topics, without.topics);
    }
}
