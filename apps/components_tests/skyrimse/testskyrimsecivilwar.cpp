// Task 0298 (Civil War, joining a side): a conversation may start with the line of a Blocking dialogue branch instead
// of the Hello greeting. Written before the code.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_priority.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
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

    std::vector<std::string> names(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mText = text;
        return s;
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    constexpr FormId kBranch2 = kSelf | 0x501;
    constexpr FormId kTopicBlock = kSelf | 0x320; // the start topic of a blocking branch, no name of its own
    constexpr FormId kTopicBlock2 = kSelf | 0x321;
    constexpr FormId kTopicMember = kSelf | 0x322; // a named choice in the blocking branch

    struct Variant
    {
        std::uint32_t mFlags = 2; ///< DLBR DNAM: 1 top-level, 2 blocking
        bool mBlockingPasses = true;
        bool mSecond = false; ///< a second blocking branch, with a higher form id for its branch but a lower topic id
    };

    Plugin blockingPlugin(const Variant& v)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec block = info(0x30, "You survived Helgen?");
        block.mLinks = { kTopicMember };
        block.mConditions = ctda(72, 0, v.mBlockingPasses ? 1.0f : 0.0f, kNpc);
        p.mDials.append(topic(kTopicBlock, "Block", "CUST", 0xF0, kBranch, { block }));
        p.mDials.append(topic(kTopicMember, "I'm going alone?", "CUST", 0, kBranch, { info(0x31, "Yes.") }));
        p.mBranches = branchRecord(kBranch, kTopicBlock, v.mFlags);
        if (v.mSecond)
        {
            InfoSpec other = info(0x32, "Are you lost?");
            p.mDials.append(topic(kTopicBlock2, "Block2", "CUST", 0xF0, kBranch2, { other }));
            p.mBranches.append(branchRecord(kBranch2, kTopicBlock2, 2));
        }
        return p;
    }

    TEST(SkyrimBlockingBranch, offTheGreetingIsHelloAndTheBranchIsNeverStarted)
    {
        Talk talk(blockingPlugin(Variant()));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimBlockingBranch, onTheBlockingBranchStartsTheConversationAndItsLinksAreTheChoices)
    {
        Talk talk(blockingPlugin(Variant()));
        talk.mRuntime->setBlockingBranches(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "You survived Helgen?");
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "I'm going alone?" }));
    }

    TEST(SkyrimBlockingBranch, aLineWhoseConditionsFailLeavesTheHelloGreeting)
    {
        Variant v;
        v.mBlockingPasses = false;
        Talk talk(blockingPlugin(v));
        talk.mRuntime->setBlockingBranches(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimBlockingBranch, aTopLevelBranchThatDoesNotBlockDoesNotStartTheConversation)
    {
        Variant v;
        v.mFlags = 1;
        Talk talk(blockingPlugin(v));
        talk.mRuntime->setBlockingBranches(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimBlockingBranch, withTwoBlockingBranchesTheLowestTopicIdWinsEveryTime)
    {
        Variant v;
        v.mSecond = true;
        for (int run = 0; run < 3; ++run)
        {
            Talk talk(blockingPlugin(v));
            talk.mRuntime->setBlockingBranches(true);
            Turn turn;
            std::string error;
            ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
            EXPECT_EQ(turn.text, "You survived Helgen?");
        }
    }

    TEST(SkyrimBlockingBranch, theIndexListsOnlyTheStartTopicsOfBlockingBranchesSortedById)
    {
        Variant v;
        v.mSecond = true;
        Talk talk(blockingPlugin(v));
        EXPECT_EQ(talk.mIndex.blockingStarts(), (std::vector<FormId>{ kTopicBlock, kTopicBlock2 }));
        v.mFlags = 1;
        Talk other(blockingPlugin(v));
        EXPECT_EQ(other.mIndex.blockingStarts(), (std::vector<FormId>{ kTopicBlock2 }));
    }
}

namespace
{
    using namespace game_skyrimse;

    TEST(SkyrimQuestPriority, aHigherPriorityQuestComesFirst)
    {
        EXPECT_TRUE(questPriorityBefore(71, 0x000D3C5F, 70, 0x00019E53)); // CW00A before CW
        EXPECT_FALSE(questPriorityBefore(70, 0x00019E53, 71, 0x000D3C5F));
    }

    TEST(SkyrimQuestPriority, equalPrioritiesGoByTheLowerQuestIdAndAQuestIsNeverBeforeItself)
    {
        EXPECT_TRUE(questPriorityBefore(70, 0x100, 70, 0x200));
        EXPECT_FALSE(questPriorityBefore(70, 0x200, 70, 0x100));
        EXPECT_FALSE(questPriorityBefore(70, 0x100, 70, 0x100));
    }

    TEST(SkyrimQuestPriority, sortingByItIsTheSameWhateverTheStartingOrder)
    {
        const std::vector<std::pair<std::uint8_t, FormId>> base{ { 70, 0x300 }, { 71, 0x200 }, { 70, 0x100 } };
        std::vector<std::pair<std::uint8_t, FormId>> a = base;
        std::vector<std::pair<std::uint8_t, FormId>> b(base.rbegin(), base.rend());
        const auto less = [](const auto& x, const auto& y) {
            return questPriorityBefore(x.first, x.second, y.first, y.second);
        };
        std::sort(a.begin(), a.end(), less);
        std::sort(b.begin(), b.end(), less);
        EXPECT_EQ(a, b);
        EXPECT_EQ(a.front().second, 0x200u);
        EXPECT_EQ(a.back().second, 0x300u);
    }
}

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kWorldL = 0x0000003C;
    constexpr FormId kFort = 0x01000100;
    constexpr FormId kFortRefType = 0x01000300; // the Civil War's CWFortMonster
    constexpr FormId kOtherRefType = 0x01000301;
    constexpr FormId kBandit1 = 0x01000401;
    constexpr FormId kBandit2 = 0x01000402;
    constexpr FormId kBandit3 = 0x01000403;

    struct FortLocations
    {
        FortLocations()
        {
            Bytes special;
            const auto add = [&](FormId type, FormId ref) {
                special.u32(type).u32(ref).u32(kWorldL).u16(0).u16(0);
            };
            add(kFortRefType, kBandit1);
            add(kFortRefType, kBandit2);
            add(kOtherRefType, kBandit3);
            Bytes record;
            record.append(sub4("EDID", Bytes().zstring("Fort")));
            record.append(sub4("LCSR", special));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("LCTN"), 0, record4("LCTN", 0u, kFort, record)));
            Content::Error error;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            LoadOrder o;
            std::string err;
            EXPECT_TRUE(o.add("Skyrim.esm", false, err));
            EXPECT_TRUE(o.add("test.esp", false, err));
            if (plugin == nullptr)
                return;
            EXPECT_TRUE(parseLocationRecords(*plugin, o, LocationLimits(), mRecords));
            LocationRecords copy = mRecords;
            mIndex.add(std::move(copy));
        }
        LocationRecords mRecords;
        LocationIndex mIndex;
    };

    TEST(SkyrimFortCount, everyListedReferenceOfTheTypeCountsWhenAllAreAlive)
    {
        const FortLocations f;
        EXPECT_EQ(f.mIndex.countSpecialRefs(kFort, kFortRefType, [](FormId) { return true; }), 2u);
        EXPECT_EQ(f.mIndex.countSpecialRefs(kFort, kOtherRefType, [](FormId) { return true; }), 1u);
    }

    TEST(SkyrimFortCount, aReferenceTheCallerSaysDoesNotCountIsLeftOut)
    {
        const FortLocations f;
        EXPECT_EQ(f.mIndex.countSpecialRefs(kFort, kFortRefType, [](FormId ref) { return ref != kBandit1; }), 1u);
        EXPECT_EQ(f.mIndex.countSpecialRefs(kFort, kFortRefType, [](FormId) { return false; }), 0u);
    }

    TEST(SkyrimFortCount, anUnknownLocationOrTypeCountsNothing)
    {
        const FortLocations f;
        EXPECT_EQ(f.mIndex.countSpecialRefs(0x01000999, kFortRefType, [](FormId) { return true; }), 0u);
        EXPECT_EQ(f.mIndex.countSpecialRefs(kFort, 0x01000999, [](FormId) { return true; }), 0u);
    }
}
