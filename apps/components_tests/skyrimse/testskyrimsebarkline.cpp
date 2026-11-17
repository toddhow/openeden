// Task 0231: DialogueRuntime::barkLine, the line an NPC says unprompted for a DIAL subtype, over generated plugins. No
// game data. The fixtures are copied from testskyrimsedialogueruntime.cpp (which this task does not edit).
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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint32_t runOn = 0,
        std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0)
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
        l.mPlugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), name, error);
        Content::Error e2;
        EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), name, order(), ActorLimits(), l.mActors, e2)) << e2.mMessage;
        return l;
    }

    FormIdResolver resolverOf(const Loaded& l, const std::string& name = "test.esp")
    {
        return FormIdResolver(l.mPlugin->header(), name, order());
    }

    // ---- the conversation ----------------------------------------------------------------------------------------

    constexpr FormId kTopicAttack = kSelf | 0x320;
    constexpr FormId kTopicAttackHigh = kSelf | 0x321;
    constexpr FormId kTopicTaunt = kSelf | 0x322;

    InfoSpec info(std::uint32_t local, const std::string& text, std::uint16_t flags = 0)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mTexts = { text };
        s.mFlags = flags;
        return s;
    }

    Bytes barkPlugin()
    {
        Bytes dials;
        InfoSpec forBob = info(0x20, "Bob, welcome.");
        forBob.mConditions = ctda(72, 0, 1.0f, kNpcB);
        dials.append(
            topicWithInfos(kTopicHello, "Hello", "HELO", 1.0f, kQuest, { forBob, info(0x21, "Hello there.") }));
        dials.append(topicWithInfos(kTopicAttack, "Attack", "ATCK", 1.0f, kQuest, { info(0x22, "Take this!") }));
        dials.append(topicWithInfos(kTopicTaunt, "Taunt", "TAUT", 1.0f, kQuest,
            { info(0x23, "Die!", InfoFlags::Random), info(0x24, "Yield!", InfoFlags::Random),
                info(0x27, "Run!", InfoFlags::Random) }));
        dials.append(topicWithInfos(kTopicAttackHigh, "AttackHigh", "ATCK", 5.0f, kQuest,
            { info(0x25, "For Skyrim!", InfoFlags::SayOnce), info(0x26, "Not today.") }));
        Bytes npcs = npc(kNpcA, "Alice", NpcFlags::Female, 0, 0, {}, kVoice, Bytes());
        npcs.append(npc(kNpcB, "Bob", 0, 0, 0, {}, 0, Bytes()));
        return pluginBytes(masters(), dials, npcs);
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct BarkTalk
    {
        Loaded mLoaded;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        BarkTalk()
        {
            mLoaded = load(barkPlugin());
            mActors.add(mLoaded.mActors);
            mDialogue.add(mLoaded.mDialogue, resolverOf(mLoaded));
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mDialogue, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; });
        }

        static DialogueSpeaker speaker(FormId base)
        {
            DialogueSpeaker s;
            s.mBase = base;
            s.mReference = base | 0x1000;
            return s;
        }
    };

    TEST(SkyrimBarkLine, aHelloBarkIsTheFirstHelloThatPassesForTheSpeaker)
    {
        BarkTalk talk;
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("HELO", BarkTalk::speaker(kNpcA), 0, turn));
        EXPECT_EQ(turn.text, "Hello there.");
        ASSERT_TRUE(talk.mRuntime->barkLine("HELO", BarkTalk::speaker(kNpcB), 0, turn));
        EXPECT_EQ(turn.text, "Bob, welcome.");
    }

    TEST(SkyrimBarkLine, theSubtypeDecidesTheTopicAndAnUnknownOneHasNoLine)
    {
        BarkTalk talk;
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("ATCK", BarkTalk::speaker(kNpcA), 0, turn));
        EXPECT_EQ(turn.text, "For Skyrim!"); // the higher priority topic first
        EXPECT_FALSE(talk.mRuntime->barkLine("DETH", BarkTalk::speaker(kNpcA), 0, turn));
        EXPECT_TRUE(turn.text.empty());
    }

    TEST(SkyrimBarkLine, aSayOnceBarkIsNotRepeated)
    {
        BarkTalk talk;
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("ATCK", BarkTalk::speaker(kNpcA), 0, turn));
        EXPECT_EQ(turn.text, "For Skyrim!");
        ASSERT_TRUE(talk.mRuntime->barkLine("ATCK", BarkTalk::speaker(kNpcA), 0, turn));
        EXPECT_EQ(turn.text, "Not today.");
    }

    TEST(SkyrimBarkLine, thePickChoosesAmongTheRandomInfosAndIsRepeatable)
    {
        BarkTalk talk;
        Turn turn;
        std::set<std::string> seen;
        for (std::uint32_t pick = 0; pick < 6; ++pick)
        {
            ASSERT_TRUE(talk.mRuntime->barkLine("TAUT", BarkTalk::speaker(kNpcA), pick, turn));
            seen.insert(turn.text);
            Turn again;
            ASSERT_TRUE(talk.mRuntime->barkLine("TAUT", BarkTalk::speaker(kNpcA), pick, again));
            EXPECT_EQ(again.text, turn.text);
        }
        EXPECT_EQ(seen, (std::set<std::string>{ "Die!", "Yield!", "Run!" }));
    }

    TEST(SkyrimBarkLine, anOpenConversationIsLeftAlone)
    {
        BarkTalk talk;
        Turn greeting;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(BarkTalk::speaker(kNpcA), greeting, error)) << error;
        ASSERT_TRUE(talk.mRuntime->open());
        Turn turn;
        ASSERT_TRUE(talk.mRuntime->barkLine("HELO", BarkTalk::speaker(kNpcB), 0, turn));
        EXPECT_TRUE(talk.mRuntime->open());
    }
}
