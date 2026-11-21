// Task 0325 (Dark Brotherhood): a response that links (TCLT) to its own walk-away topic (TWAT), as every line of
// Astrid's wake-up conversation does, does not offer that topic as a button: DB02AstridWalkAwayTopic showed as a choice
// named by its editor id. Same builders as testskyrimsewalkaway.cpp (task 0315).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
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

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::string mText; ///< empty: no response at all
        std::uint32_t mShared = 0; ///< DNAM
        std::vector<std::uint32_t> mLinks; ///< TCLT
        Bytes mConditions;
        std::string mPrompt; ///< RNAM, empty: none
        std::uint16_t mFlags = 0; ///< ENAM flags (4: say once)
        std::uint32_t mWalkAway = 0; ///< TWAT
        Bytes mVmad; ///< a whole VMAD subrecord, or empty
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        for (const std::uint32_t link : s.mLinks)
            b.append(sub4("TCLT", Bytes().u32(link)));
        if (s.mWalkAway != 0)
            b.append(sub4("TWAT", Bytes().u32(s.mWalkAway)));
        if (s.mVmad.size() != 0)
            b.append(s.mVmad);
        if (s.mShared != 0)
            b.append(sub4("DNAM", Bytes().u32(s.mShared)));
        if (!s.mText.empty())
        {
            Bytes t;
            t.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
            b.append(sub4("TRDT", t));
            b.append(sub4("NAM1", Bytes().zstring(s.mText)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        if (!s.mPrompt.empty())
            b.append(sub4("RNAM", Bytes().zstring(s.mPrompt)));
        b.append(s.mConditions);
        return record4("INFO", 0u, s.mId, b);
    }

    /// A topic with its infos. `category` 0: a player topic (named); 7: Miscellaneous. `branch` 0: none.
    Bytes topic(std::uint32_t id, const std::string& name, const std::string& subtype, std::uint8_t category,
        std::uint32_t branch, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(name));
        if (category == 0)
            b.append(sub4("FULL", Bytes().zstring(name)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        if (branch != 0)
            b.append(sub4("BNAM", Bytes().u32(branch)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category == 0xF0 ? 0 : category).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    Bytes npcRecord()
    {
        Bytes b;
        b.append(edid("Steward"));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        b.append(sub4("ACBS", acbs));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        return record4("NPC_", 0, kNpc, b);
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        return record4("RACE", 0, kRaceNord, b);
    }

    struct Plugin
    {
        Bytes mDials;
        Bytes mBranches;
    };

    Bytes pluginBytes(const Plugin& p)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, p.mDials));
        if (p.mBranches.size() != 0)
            bytes.append(group4(Content::fourCC("DLBR"), kGrupTop, p.mBranches));
        bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, npcRecord()));
        bytes.append(group4(Content::fourCC("RACE"), kGrupTop, raceRecord()));
        return bytes;
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(const Plugin& plugin)
        {
            const Bytes bytes = pluginBytes(plugin);
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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mText = text;
        return s;
    }

    Bytes wstr(Bytes& b, const std::string& s)
    {
        b.u16(static_cast<std::uint16_t>(s.size()));
        for (char c : s)
            b.u8(static_cast<std::uint8_t>(c));
        return b;
    }

    /// An INFO VMAD: one script `name` with one object property `Prop` (form, no alias), then the fragment section.
    Bytes infoVmad(const std::string& name, std::uint32_t propertyForm, int fragmentFlags, bool section = true,
        const std::string& begin = "Fragment_0", const std::string& end = "Fragment_1")
    {
        Bytes b;
        b.u16(5).u16(2).u16(1);
        wstr(b, name);
        b.u8(1).u16(1);
        wstr(b, "Prop");
        b.u8(1).u8(1).u16(0).u16(0xFFFF).u32(propertyForm);
        if (!section)
            return b;
        b.u8(2).u8(static_cast<std::uint8_t>(fragmentFlags));
        wstr(b, name);
        if ((fragmentFlags & 1) != 0)
        {
            b.u8(1);
            wstr(b, name);
            wstr(b, begin);
        }
        if ((fragmentFlags & 2) != 0)
        {
            b.u8(1);
            wstr(b, name);
            wstr(b, end);
        }
        return b;
    }

    constexpr FormId kTopicWalk = kSelf | 0x320;

    Bytes walkVmad()
    {
        return sub4("VMAD", infoVmad("TIF__00000999", kQuest, 3));
    }

    /// A HELO greeting whose INFO names `walk` as its walk-away topic, and the walk-away topic with one fragment INFO.
    Plugin walkPlugin(std::uint32_t walk, bool conditionFails = false)
    {
        Plugin p;
        InfoSpec hello = info(0x20, "Hmm?");
        hello.mWalkAway = walk;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { hello }));
        InfoSpec away = info(0x30, "Wait a moment.");
        away.mVmad = walkVmad();
        if (conditionFails)
            away.mConditions = ctda(72, 0, 1.0f, kSelf | 0x999); // GetIsID of somebody else
        p.mDials.append(topic(kTopicWalk, "Walkaway", "CUST", 7, 0, { away }));
        return p;
    }

    constexpr FormId kTopicOther = kSelf | 0x340;
    constexpr std::uint16_t kInvisible = 0x0100;

    /// A greeting that links to a NAMED player topic and to the walk-away topic, as Astrid's does.
    Plugin menuPlugin(std::uint16_t greetingFlags)
    {
        Plugin p;
        InfoSpec hello = info(0x20, "Sleep well?");
        hello.mFlags = greetingFlags;
        hello.mWalkAway = kTopicWalk;
        hello.mLinks = { kTopicOther, kTopicWalk };
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { hello }));
        p.mDials.append(topic(kTopicOther, "Where am I", "CUST", 0, 0, { info(0x40, "A shack.") }));
        p.mDials.append(topic(kTopicWalk, "WalkawayName", "CUST", 0, 0, { info(0x30, "Cut to it.") }));
        return p;
    }

    bool offers(const Turn& turn, FormId topic)
    {
        for (const auto& t : turn.topics)
            if (t.id == topic)
                return true;
        return false;
    }

    TEST(SkyrimWalkAwayMenu, theWalkAwayTopicAResponseNamesIsNotAChoice)
    {
        Talk talk(menuPlugin(kInvisible));
        talk.mRuntime->setWalkAwayTopics(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_TRUE(offers(turn, kTopicOther));
        EXPECT_FALSE(offers(turn, kTopicWalk));
    }

    TEST(SkyrimWalkAwayMenu, withTheSettingOffTheLinkedTopicIsOfferedAsBefore)
    {
        Talk talk(menuPlugin(kInvisible));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_TRUE(offers(turn, kTopicOther));
        EXPECT_TRUE(offers(turn, kTopicWalk));
    }

    TEST(SkyrimWalkAwayMenu, aTopicThatIsNotTheResponsesWalkAwayTopicIsStillOffered)
    {
        // the greeting names no walk-away topic of its own: both linked topics are choices
        Plugin q;
        InfoSpec hello = info(0x20, "Sleep well?");
        hello.mLinks = { kTopicOther, kTopicWalk };
        q.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { hello }));
        q.mDials.append(topic(kTopicOther, "Where am I", "CUST", 0, 0, { info(0x40, "A shack.") }));
        q.mDials.append(topic(kTopicWalk, "WalkawayName", "CUST", 0, 0, { info(0x30, "Cut to it.") }));
        Talk talk(q);
        talk.mRuntime->setWalkAwayTopics(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_TRUE(offers(turn, kTopicOther));
        EXPECT_TRUE(offers(turn, kTopicWalk));
    }

    TEST(SkyrimWalkAwayMenu, aForcedGreetingKeepsTheWalkAwayTopicOutOfItsChoicesToo)
    {
        Talk talk(menuPlugin(0));
        talk.mRuntime->setWalkAwayTopics(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicHello, turn, error)) << error;
        EXPECT_TRUE(offers(turn, kTopicOther));
        EXPECT_FALSE(offers(turn, kTopicWalk));
    }

    TEST(SkyrimWalkAwayMenu, aForcedGreetingWithTheSettingOffOffersIt)
    {
        Talk talk(menuPlugin(0));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicHello, turn, error)) << error;
        EXPECT_TRUE(offers(turn, kTopicWalk));
    }
}
