// Task 0270 (marriage, first slice): what the pack adds to the game's own courting scripts. Written before the code.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/marriage.hpp>
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

    TEST(SkyrimMarriageList, anEmptyTextIsAnEmptyList)
    {
        std::vector<FormId> ids{ 5 };
        EXPECT_TRUE(marriage::parseBaseIdList("", ids));
        EXPECT_TRUE(ids.empty());
        EXPECT_TRUE(marriage::parseBaseIdList("  ", ids));
        EXPECT_TRUE(ids.empty());
    }

    TEST(SkyrimMarriageList, hexIdsSeparatedByCommasOrSpacesAreRead)
    {
        std::vector<FormId> ids;
        ASSERT_TRUE(marriage::parseBaseIdList("a2c8e, 0x13bab 1A692", ids));
        ASSERT_EQ(ids.size(), 3u);
        EXPECT_EQ(ids[0], 0x000A2C8Eu);
        EXPECT_EQ(ids[1], 0x00013BABu);
        EXPECT_EQ(ids[2], 0x0001A692u);
    }

    TEST(SkyrimMarriageList, aBadEntryAZeroIdOrTooManyEntriesFailAndLeaveNothing)
    {
        std::vector<FormId> ids;
        EXPECT_FALSE(marriage::parseBaseIdList("a2c8e,lydia", ids));
        EXPECT_TRUE(ids.empty());
        EXPECT_FALSE(marriage::parseBaseIdList("0", ids));
        EXPECT_TRUE(ids.empty());
        std::string many;
        for (std::size_t i = 0; i <= marriage::kMaxFriends; ++i)
            many += "a2c8e ";
        EXPECT_FALSE(marriage::parseBaseIdList(many, ids));
        EXPECT_TRUE(ids.empty());
    }

    struct MarriageWorldTest : ::testing::Test
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        static constexpr conditions::Subject kPlayer = SkyrimConditionWorld::kPlayer;
    };

    TEST_F(MarriageWorldTest, thePlayerWearsNothingUntilASourceSaysSo)
    {
        EXPECT_FALSE(mWorld.isEquipped(kPlayer, marriage::kAmuletOfMara));
        mWorld.setPlayerWornSource([](FormId id) { return id == marriage::kAmuletOfMara; });
        EXPECT_TRUE(mWorld.isEquipped(kPlayer, marriage::kAmuletOfMara));
        EXPECT_FALSE(mWorld.isEquipped(kPlayer, 0x00012345));
        mWorld.setPlayerWornSource({});
        EXPECT_FALSE(mWorld.isEquipped(kPlayer, marriage::kAmuletOfMara));
    }

    TEST_F(MarriageWorldTest, theCarriedCountOfAnItemComesFromTheItemSource)
    {
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, marriage::kAmuletOfMara), 0.f);
        mWorld.setPlayerItemSource([](FormId id) { return id == marriage::kAmuletOfMara ? 2.f : 0.f; });
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, marriage::kAmuletOfMara), 2.f);
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, 0x00012345), 0.f);
    }

    TEST_F(MarriageWorldTest, theItemSourceIsReadAtTheMomentOfTheCheckAndNeverNegative)
    {
        float count = 1.f;
        mWorld.setPlayerItemSource([&](FormId) { return count; });
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, 0x00012345), 1.f);
        count = -4.f;
        EXPECT_FLOAT_EQ(mWorld.itemCount(kPlayer, 0x00012345), 0.f);
    }

    // ---- the dialogue (helpers copied from testskyrimsehouses.cpp: its own anonymous namespace) ----

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kBranch = kSelf | 0x500;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicOffer = kSelf | 0x310; // the start topic of the branch
    constexpr FormId kTopicTake = kSelf | 0x311; // a member of the branch: only a choice
    constexpr FormId kTopicLeave = kSelf | 0x312; // another member
    constexpr FormId kTopicShared = kSelf | 0x313; // a Misc topic that holds responses other INFOs share
    constexpr FormId kTopicPlain = kSelf | 0x314; // an ordinary topic, no branch

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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0,
        const std::string& string2 = std::string())
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        Bytes out = sub4("CTDA", b);
        if (!string2.empty())
            out.append(sub4("CIS2", Bytes().zstring(string2)));
        return out;
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::string mText; ///< empty: no response at all
        std::uint32_t mShared = 0; ///< DNAM
        std::vector<std::uint32_t> mLinks; ///< TCLT
        Bytes mConditions;
        std::string mPrompt; ///< RNAM, empty: none
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        for (const std::uint32_t link : s.mLinks)
            b.append(sub4("TCLT", Bytes().u32(link)));
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

    Bytes branchRecord(std::uint32_t id, std::uint32_t start, std::uint32_t flags)
    {
        Bytes b;
        b.append(edid("Branch"));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DNAM", Bytes().u32(flags)));
        b.append(sub4("SNAM", Bytes().u32(start)));
        return record4("DLBR", 0, id, b);
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
            mRuntime = std::make_unique<DialogueRuntime>(mActors, mIndex, mQuests, QuestState::initial(mQuests),
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
            if (t.text == name)
                return t.id;
        ADD_FAILURE() << "topic not offered: " << name;
        return 0;
    }


    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mText = text;
        return s;
    }

    /// The marriage offer's shape: a named start topic whose response links to two topics that have NO name of their own
    /// (no FULL) and whose INFOs carry the choice's words as their prompt (RNAM).
    Plugin offerPlugin(bool promptOnYes)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec offer = info(0x21, "Well, yes. Are you interested in me?");
        offer.mLinks = { kTopicTake, kTopicLeave };
        p.mDials.append(topic(kTopicOffer, "Interested in me, are you?", "CUST", 0, kBranch, { offer }));
        InfoSpec yes = info(0x22, "It's settled then.");
        if (promptOnYes)
            yes.mPrompt = "Yes. Yes I am.";
        InfoSpec no = info(0x23, "Oh. I'm sorry I said anything.");
        no.mPrompt = "No, I'm not.";
        // category 0xF0 here: a player topic (category 0) with no FULL, as the choices of the marriage branch
        p.mDials.append(topic(kTopicTake, "YesTopic", "CUST", 0xF0, kBranch, { yes }));
        p.mDials.append(topic(kTopicLeave, "NoTopic", "CUST", 0xF0, kBranch, { no }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 1);
        return p;
    }

    TEST(SkyrimMarriageDialogue, linkedTopicsWithNoNameAreChoicesTheirPromptNames)
    {
        Talk talk(offerPlugin(true));
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Interested in me, are you?"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Well, yes. Are you interested in me?");
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "Yes. Yes I am.", "No, I'm not." }));
    }

    TEST(SkyrimMarriageDialogue, aLinkedTopicWithNeitherANameNorAPromptIsNotAChoice)
    {
        Talk talk(offerPlugin(false));
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Interested in me, are you?"), reply, error)) << error;
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "No, I'm not." }));
    }

    TEST(SkyrimMarriageDialogue, aNamelessTopicIsNeverInTheNormalList)
    {
        Talk talk(offerPlugin(true));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Interested in me, are you?" }));
    }

    // ---- a keyword is a model record (a script calls Keyword.SendStoryEvent on a property that holds one) ----

    TEST(SkyrimMarriageRecords, aKeywordIsReadAsAModelRecordWithItsEditorId)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("RelationshipMarriageWeddingKeyword")));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("KYWD"), 0, record4("KYWD", 0, 0x01000A10, subs)));
        LoadOrder o;
        std::string err;
        ASSERT_TRUE(o.add("Skyrim.esm", false, err));
        ASSERT_TRUE(o.add("test.esp", false, err));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(bytes.str(), "test.esp", o, ParseLimits(), plugin, error)) << error.describe();
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_EQ(plugin.mModels[0].mType, Content::fourCC("KYWD"));
        EXPECT_EQ(plugin.mModels[0].mCommon.mEditorId, "RelationshipMarriageWeddingKeyword");
    }
}
