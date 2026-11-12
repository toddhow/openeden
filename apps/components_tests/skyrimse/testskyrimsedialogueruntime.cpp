// Task 0140, milestone M-S4 "talk to an NPC", slice 1: the conversation runtime (greeting, topic list, response,
// say once, goodbye) over generated plugins. No game data. The fixtures are copied from testskyrimsedialogue.cpp
// (which this task does not edit).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>

#include <gtest/gtest.h>

#include <cstring>
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

    // load order: Skyrim.esm = 0, test.esp = 1, other.esp = 2. Ids 0x01xxxxxx are test.esp's own.
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kFaction = kSelf | 0x701;
    constexpr FormId kKeywordNpc = kSelf | 0x601;
    constexpr FormId kKeywordRace = kSelf | 0x602;
    constexpr FormId kVoice = kSelf | 0x801;
    constexpr FormId kVoiceList = kSelf | 0x802;
    constexpr FormId kGlobal = kSelf | 0x901;
    constexpr FormId kList = kSelf | 0xA01;
    constexpr FormId kNpcA = kSelf | 0x200;
    constexpr FormId kNpcB = kSelf | 0x201;
    constexpr FormId kTemplateNpc = kSelf | 0x202;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicHigh = kSelf | 0x301;
    constexpr FormId kQuest = kSelf | 0x400;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        EXPECT_TRUE(o.add("other.esp", false, err));
        return o;
    }

    Bytes header(const std::vector<Master>& masters)
    {
        return tes4Header(1.7f, 0, "A", "B", 1, masters, 24, 44);
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0,
        std::uint32_t runOn = 0, std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(reference).u32(0);
        return sub4("CTDA", b);
    }

    Bytes trdt(std::uint8_t number, std::uint32_t emotionType = 0, std::uint32_t emotionValue = 50)
    {
        Bytes b;
        b.u32(emotionType).u32(emotionValue).u32(0).u8(number).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        return sub4("TRDT", b);
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::uint16_t mFlags = 0;
        Bytes mConditions;
        std::vector<std::string> mTexts{ "Hello." };
        std::uint32_t mQuest = 0;
        std::uint32_t mSpeaker = 0;
        bool mDeleted = false;
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        if (s.mQuest != 0)
            b.append(sub4("QNAM", Bytes().u32(s.mQuest)));
        if (s.mSpeaker != 0)
            b.append(sub4("ANAM", Bytes().u32(s.mSpeaker)));
        std::uint8_t n = 1;
        for (const std::string& t : s.mTexts)
        {
            b.append(trdt(n++));
            b.append(sub4("NAM1", Bytes().zstring(t)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        b.append(s.mConditions);
        return record4("INFO", s.mDeleted ? 0x20u : 0u, s.mId, b);
    }

    Bytes topicRecord(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, std::uint8_t category = 7)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("PNAM", Bytes().f32(priority)));
        b.append(sub4("QNAM", Bytes().u32(quest)));
        b.append(sub4("BNAM", Bytes().u32(kSelf | 0x500)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(73)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        return record4("DIAL", 0, id, b);
    }

    /// DIAL + its type 7 group holding the infos.
    Bytes topicWithInfos(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b = topicRecord(id, name, subtype, priority, quest);
        b.append(group4(id, kGrupTopicChildren, children));
        return b;
    }

    Bytes acbs(std::uint32_t flags, std::uint16_t templateFlags)
    {
        Bytes b;
        b.u32(flags).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(templateFlags).u16(0).u16(0);
        return b;
    }

    Bytes npc(std::uint32_t id, const std::string& name, std::uint32_t flags, std::uint16_t templateFlags,
        std::uint32_t templ, const std::vector<std::uint32_t>& keywords, std::uint32_t voice, const Bytes& factions)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("ACBS", acbs(flags, templateFlags)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        if (voice != 0)
            b.append(sub4("VTCK", Bytes().u32(voice)));
        if (templ != 0)
            b.append(sub4("TPLT", Bytes().u32(templ)));
        b.append(factions);
        if (!keywords.empty())
        {
            Bytes k;
            for (std::uint32_t w : keywords)
                k.u32(w);
            b.append(sub4("KWDA", k));
        }
        return record4("NPC_", 0, id, b);
    }

    Bytes faction(std::uint32_t id, std::int8_t rank)
    {
        return sub4("SNAM", Bytes().u32(id).u8(static_cast<std::uint8_t>(rank)).u8(0).u8(0).u8(0));
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        b.append(sub4("KWDA", Bytes().u32(kKeywordRace)));
        return record4("RACE", 0, kRaceNord, b);
    }

    Bytes pluginBytes(const std::vector<Master>& masters, const Bytes& dials, const Bytes& npcs = Bytes(),
        const Bytes& extras = Bytes())
    {
        Bytes bytes = header(masters);
        if (dials.size() != 0)
            bytes.append(top("DIAL", dials));
        if (npcs.size() != 0)
        {
            bytes.append(top("NPC_", npcs));
            bytes.append(top("RACE", raceRecord()));
        }
        bytes.append(extras);
        return bytes;
    }

    std::vector<Master> masters()
    {
        return { { "Skyrim.esm", 0 } };
    }

    struct Loaded
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActors;
        bool mOk = false;
    };

    Loaded load(const Bytes& bytes, const std::string& name = "test.esp", DialogueLimits limits = DialogueLimits())
    {
        Loaded l;
        Content::Error error;
        l.mOk = parseDialogueRecordsBytes(bytes.str(), name, order(), limits, l.mDialogue, error);
        l.mPlugin = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), name,
            error);
        Content::Error e2;
        EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), name, order(), ActorLimits(), l.mActors, e2)) << e2.mMessage;
        return l;
    }

    FormIdResolver resolverOf(const Loaded& l, const std::string& name = "test.esp")
    {
        return FormIdResolver(l.mPlugin->header(), name, order());
    }


    // ---- the conversation ----------------------------------------------------------------------------------------

    constexpr FormId kTopicRumors = kSelf | 0x310;
    constexpr FormId kTopicSecret = kSelf | 0x311;
    constexpr FormId kTopicOnce = kSelf | 0x312;
    constexpr FormId kTopicBye = kSelf | 0x313;
    constexpr FormId kTopicScript = kSelf | 0x314;
    constexpr FormId kTopicNoName = kSelf | 0x315;

    InfoSpec info(std::uint32_t local, const std::string& text, std::uint16_t flags = 0)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mTexts = { text };
        s.mFlags = flags;
        return s;
    }

    /// A player topic: category 0 with a FULL name.
    Bytes playerTopic(std::uint32_t id, const std::string& name, const std::vector<InfoSpec>& infos, bool named = true)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(name));
        if (named)
            b.append(sub4("FULL", Bytes().zstring(name)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text("CUST")));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    constexpr FormId kNpcC = kSelf | 0x203;

    Bytes namedNpc(std::uint32_t id, const std::string& editorId, const std::string& full)
    {
        Bytes b;
        b.append(edid(editorId));
        b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("ACBS", acbs(0, 0)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        return record4("NPC_", 0, id, b);
    }

    Bytes conversationPlugin()
    {
        Bytes dials;
        InfoSpec forBob = info(0x20, "Welcome, Bob.");
        forBob.mConditions = ctda(72, 0, 1.0f, kNpcB);
        dials.append(topicWithInfos(kTopicHello, "Hello", "HELO", 1.0f, kQuest, { forBob, info(0x21, "Hello there.") }));
        dials.append(playerTopic(kTopicRumors, "Rumors", { info(0x22, "Dragons, they say.") }));
        InfoSpec secret = info(0x23, "The cellar key is under the stone.");
        secret.mConditions = ctda(72, 0, 1.0f, kNpcB);
        dials.append(playerTopic(kTopicSecret, "Secret", { secret }));
        dials.append(playerTopic(kTopicOnce, "Work",
            { info(0x24, "I have a job for you.", InfoFlags::SayOnce), info(0x25, "I told you already.") }));
        dials.append(playerTopic(kTopicBye, "Farewell", { info(0x26, "Safe travels.", InfoFlags::Goodbye) }));
        InfoSpec scripted = info(0x27, "Take this.");
        scripted.mConditions = sub4("VMAD", Bytes().u16(5).u16(2).u16(0));
        dials.append(playerTopic(kTopicScript, "Gift", { scripted }));
        dials.append(playerTopic(kTopicNoName, "Hidden", { info(0x28, "Nobody sees this.") }, false));
        Bytes npcs = npc(kNpcA, "Alice", NpcFlags::Female, 0, 0, {}, kVoice, faction(kFaction, 1));
        npcs.append(npc(kNpcB, "Bob", 0, 0, 0, {}, 0, Bytes()));
        npcs.append(namedNpc(kNpcC, "Carl", "Carl the Smith"));
        return pluginBytes(masters(), dials, npcs);
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct Talk
    {
        Loaded mLoaded;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(const Bytes& bytes, DialogueRuntimeLimits limits = {})
        {
            mLoaded = load(bytes);
            mActors.add(mLoaded.mActors);
            mDialogue.add(mLoaded.mDialogue, resolverOf(mLoaded));
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mDialogue, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; }, limits);
        }

        static DialogueSpeaker speaker(FormId base)
        {
            DialogueSpeaker s;
            s.mBase = base;
            s.mReference = base | 0x1000;
            return s;
        }
    };

    std::vector<std::string> names(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    std::uint32_t idOf(const Turn& turn, const std::string& name)
    {
        for (const auto& t : turn.topics)
        {
            if (t.text == name)
                return t.id;
        }
        ADD_FAILURE() << "topic not offered: " << name;
        return 0;
    }

    TEST(SkyrimDialogueRuntime, theGreetingIsTheFirstHelloThatPassesForTheSpeaker)
    {
        Talk talk(conversationPlugin());
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcB), turn, error)) << error;
        EXPECT_EQ(turn.text, "Welcome, Bob.");
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error)) << error;
        EXPECT_EQ(turn.text, "Hello there.");
        EXPECT_FALSE(turn.endsConversation);
    }

    TEST(SkyrimDialogueRuntime, theTurnCarriesTheSpeakersFullNameWhenTheRecordHasOne)
    {
        Talk talk(conversationPlugin());
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcC), turn, error)) << error;
        EXPECT_EQ(turn.speakerName, "Carl the Smith");
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        EXPECT_TRUE(turn.speakerName.empty()); // Alice has no FULL: the caller's own name is used
    }

    TEST(SkyrimDialogueRuntime, onlyNamedPlayerTopicsThatPassForTheSpeakerAreOffered)
    {
        Talk talk(conversationPlugin());
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Rumors", "Work", "Farewell", "Gift" }));
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcB), turn, error));
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Rumors", "Secret", "Work", "Farewell", "Gift" }));
    }

    TEST(SkyrimDialogueRuntime, choosingATopicGivesItsResponseAndTheListAgain)
    {
        Talk talk(conversationPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Rumors"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Dragons, they say.");
        EXPECT_FALSE(reply.endsConversation);
        EXPECT_EQ(names(reply), names(turn));
    }

    TEST(SkyrimDialogueRuntime, aSayOnceResponseIsFollowedByTheNextOne)
    {
        Talk talk(conversationPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        const std::uint32_t work = idOf(turn, "Work");
        ASSERT_TRUE(talk.mRuntime->choose(work, reply, error));
        EXPECT_EQ(reply.text, "I have a job for you.");
        ASSERT_TRUE(talk.mRuntime->choose(work, reply, error));
        EXPECT_EQ(reply.text, "I told you already.");
    }

    TEST(SkyrimDialogueRuntime, aGoodbyeResponseEndsTheConversation)
    {
        Talk talk(conversationPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        const std::uint32_t rumors = idOf(turn, "Rumors");
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Farewell"), reply, error));
        EXPECT_EQ(reply.text, "Safe travels.");
        EXPECT_TRUE(reply.endsConversation);
        EXPECT_FALSE(talk.mRuntime->open());
        EXPECT_FALSE(talk.mRuntime->choose(rumors, reply, error));
    }

    TEST(SkyrimDialogueRuntime, aTopicThatWasNotOfferedOrNoConversationIsRefused)
    {
        Talk talk(conversationPlugin());
        Turn turn, reply;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->choose(kTopicRumors, reply, error));
        EXPECT_FALSE(error.empty());
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        error.clear();
        EXPECT_FALSE(talk.mRuntime->choose(kTopicSecret, reply, error)); // Alice does not get it
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(talk.mRuntime->open());
        talk.mRuntime->end();
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimDialogueRuntime, noGreetingMeansNoConversation)
    {
        Talk talk(pluginBytes(masters(), playerTopic(kTopicRumors, "Rumors", { info(0x22, "Dragons.") }),
            npc(kNpcA, "Alice", 0, 0, 0, {}, 0, Bytes())));
        Turn turn;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimDialogueRuntime, aResponseWithAScriptIsShownAndCounted)
    {
        Talk talk(conversationPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Gift"), reply, error));
        EXPECT_EQ(reply.text, "Take this.");
        EXPECT_TRUE(reply.hadUnrunEffects);
        EXPECT_EQ(talk.mRuntime->stats().mScriptsNotRun, 1u);
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Rumors"), reply, error));
        EXPECT_FALSE(reply.hadUnrunEffects);
    }

    TEST(SkyrimDialogueRuntime, theTopicLimitCutsTheListAndCountsWhatItCut)
    {
        DialogueRuntimeLimits limits;
        limits.mMaxTopics = 2;
        Talk talk(conversationPlugin(), limits);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(kNpcA), turn, error));
        EXPECT_EQ(turn.topics.size(), 2u);
        EXPECT_EQ(talk.mRuntime->stats().mTopicsCut, 2u);
    }
}
