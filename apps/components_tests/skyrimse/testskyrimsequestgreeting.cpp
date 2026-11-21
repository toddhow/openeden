// Task 0304 (College of Winterhold, second slice): a quest's nameless branch-start topic is the greeting. Written
// before the code. Faralda's MG01FaraldaInitialBranchTopic (a CUST topic with no FULL, the start of a Blocking branch,
// whose INFO has no prompt) is no HELO topic, no package's topic and no TCLT link of any INFO; the only way the game
// reaches it is the conversation's own start.
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
    constexpr FormId kBranch = kSelf | 0x500;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicOffer = kSelf | 0x310; // the start topic of the branch
    constexpr FormId kTopicTake = kSelf | 0x311; // a member of the branch: only a choice
    constexpr FormId kTopicLeave = kSelf | 0x312; // another member
    constexpr FormId kTopicShared = kSelf | 0x313;
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
        std::uint16_t mFlags = 0; ///< ENAM flags (4: say once)
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
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

    std::uint32_t idOf(const Turn& turn, const std::string& name)
    {
        for (const auto& t : turn.topics)
            if (t.text == name)
                return t.id;
        ADD_FAILURE() << "topic not offered: " << name;
        return 0;
    }

    constexpr FormId kBranchB = kSelf | 0x501;
    constexpr FormId kTopicB = kSelf | 0x330; // the higher id

    std::vector<std::string> names(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    /// The INFO names its speaker (GetIsID, function 72): a line of a quest for one actor
    InfoSpec speaks(InfoSpec spec)
    {
        spec.mConditions = ctda(72, 0, 1.0f, kNpc);
        return spec;
    }

    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mText = text;
        return s;
    }

    /// The College's shape: a HELO greeting, a nameless start topic of a Blocking branch with an INFO that links to
    /// two named topics, and one nameless topic that is only a member of the branch.
    Plugin collegePlugin(bool sayOnce)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec welcome = speaks(info(0x21, "Welcome to the College."));
        welcome.mLinks = { kTopicTake, kTopicLeave };
        (void)sayOnce;
        p.mDials.append(topic(kTopicOffer, "StartTopic", "CUST", 0xF0, kBranch, { welcome }));
        p.mDials.append(topic(kTopicTake, "What is this place?", "CUST", 0, kBranch, { info(0x22, "A college.") }));
        p.mDials.append(topic(kTopicLeave, "Why are you out here?", "CUST", 0, kBranch, { info(0x23, "Guarding.") }));
        // a nameless topic inside the branch that is not its start
        p.mDials.append(topic(kTopicShared, "Member", "CUST", 0xF0, kBranch, { info(0x24, "Member line.") }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2); // Blocking
        return p;
    }

    TEST(SkyrimQuestGreeting, offByDefaultTheHelloGreetsAndTheNamelessStartIsNotOffered)
    {
        Talk talk(collegePlugin(false));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
        EXPECT_TRUE(turn.topics.empty());
    }

    TEST(SkyrimQuestGreeting, onTheNamelessBranchStartIsTheGreetingAndItsLinksAreTheChoices)
    {
        Talk talk(collegePlugin(false));
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Welcome to the College.");
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "What is this place?", "Why are you out here?" }));
    }

    TEST(SkyrimQuestGreeting, aNamelessTopicThatIsOnlyAMemberOfABranchIsNeverTheGreeting)
    {
        Plugin p = collegePlugin(false);
        // the branch starts at the named topic: both nameless topics are then only members
        p.mBranches = branchRecord(kBranch, kTopicTake, 2);
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimQuestGreeting, aTopicWithAPromptOrANameIsNotAGreeting)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec named = info(0x21, "Named start.");
        p.mDials.append(topic(kTopicOffer, "Named", "CUST", 0, kBranch, { named }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimQuestGreeting, aGreetingSaidOnceIsNotSaidAgain)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec welcome = speaks(info(0x21, "Welcome to the College."));
        welcome.mFlags = InfoFlags::SayOnce;
        p.mDials.append(topic(kTopicOffer, "StartTopic", "CUST", 0xF0, kBranch, { welcome }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Welcome to the College.");
        talk.mRuntime->end();
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimQuestGreeting, greetingsOfEqualPriorityTheHigherFormIdSpeaksFirstThenTheOtherOnceItHasSaidItsPiece)
    {
        // Faralda's Initial branch (0AF0DD) and the Nirya greeting branch (01F80F, a catch-all that passes from stage
        // 0) both have priority 50: the data wants the Initial one first, and the other once the first says no more.
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        p.mDials.append(topic(kTopicOffer, "LowId", "CUST", 0xF0, kBranch, { speaks(info(0x21, "Low id line.")) }));
        InfoSpec first = speaks(info(0x31, "High id line."));
        first.mFlags = InfoFlags::SayOnce;
        p.mDials.append(topic(kTopicB, "HighId", "CUST", 0xF0, kBranchB, { first }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        p.mBranches.append(branchRecord(kBranchB, kTopicB, 2));
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "High id line.");
        talk.mRuntime->end();
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Low id line.");
    }

    TEST(SkyrimQuestGreeting, aNamelessBranchStartThatAnotherInfoLinksToIsReachedByThatLinkNotByGreeting)
    {
        // MG01FaraldaYoureInBranchTopic: the start of a branch, no name, and the TCLT target of three INFOs: it is a
        // reply in a conversation, never the greeting.
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec linker = info(0x21, "A line that links on.");
        linker.mLinks = { kTopicB };
        p.mDials.append(topic(kTopicOffer, "Named", "CUST", 0, kBranch, { linker }));
        p.mDials.append(topic(kTopicB, "Linked", "CUST", 0xF0, kBranchB, { speaks(info(0x31, "Reached by link.")) }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        p.mBranches.append(branchRecord(kBranchB, kTopicB, 2));
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    TEST(SkyrimQuestGreeting, aNamelessBranchStartWhoseInfosNameNoSpeakerIsNotAGreeting)
    {
        // WESharedDialoguePrisonerSetFree: a shared quest's start, no GetIsID or GetIsAliasRef on any INFO. It
        // passes for everybody and would greet everybody.
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        p.mDials.append(topic(kTopicOffer, "Shared", "CUST", 0xF0, kBranch, { info(0x21, "For anybody.") }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        Talk talk(p);
        EXPECT_TRUE(talk.mIndex.questGreetingTopics().empty());
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Yes?");
    }

    // MG01FaraldaEntryTakeTest answers "Excellent." and links to the nameless MG01NiryaGreetingBranchTopic, whose INFO
    // has no prompt: the speaker goes on with it, then the player's choices are that INFO's own links.
    Plugin followPlugin()
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec reply = info(0x21, "Excellent.");
        reply.mLinks = { kTopicB };
        p.mDials.append(topic(kTopicOffer, "Take the test", "CUST", 0, kBranch, { reply }));
        InfoSpec follow = info(0x31, "Here is the spell.");
        follow.mLinks = { kTopicTake };
        p.mDials.append(topic(kTopicB, "Follow", "CUST", 0xF0, kBranchB, { follow }));
        p.mDials.append(topic(kTopicTake, "Okay, I will.", "CUST", 0, kBranchB, { info(0x32, "Good.") }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2);
        p.mBranches.append(branchRecord(kBranchB, kTopicB, 2));
        return p;
    }

    TEST(SkyrimQuestGreeting, aReplyThatLinksToANamelessTopicGoesOnWithIt)
    {
        Talk talk(followPlugin());
        talk.mRuntime->setQuestGreetings(true);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Take the test"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Excellent. Here is the spell.");
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "Okay, I will." }));
    }

    TEST(SkyrimQuestGreeting, withoutTheSettingTheNamelessLinkIsNotFollowed)
    {
        Talk talk(followPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Take the test"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Excellent.");
    }
}
