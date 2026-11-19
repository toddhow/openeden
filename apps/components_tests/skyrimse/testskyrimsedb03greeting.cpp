// Task 0343 (Skyrim Dark Brotherhood): a nameless branch-start topic whose INFOs carry a PROMPT (RNAM) is a menu topic, the
// player's own words (Astrid's DBAstridCaughtBranchTopic: "Try not to? But I do realize ..."), not the greeting of a
// conversation. The greeting rule of task 0304 (a nameless start nothing links to, which names its speaker) took it for one and
// Astrid answered her own advice instead of offering the next job (DBAstrid03GiveBranch, a Blocking branch). Written first and run
// red against the old rule. Generated records; no game data.
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

    constexpr FormId kBranchB = kSelf | 0x501;
    constexpr FormId kTopicB = kSelf | 0x330; // the higher id

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

    /// A nameless start nothing links to whose INFO names its speaker, with or without a prompt
    Plugin greetingPlugin(const std::string& prompt, const std::string& secondPrompt)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec first = speaks(info(0x21, "The start line."));
        first.mPrompt = prompt;
        std::vector<InfoSpec> infos{ first };
        if (!secondPrompt.empty())
        {
            InfoSpec second = speaks(info(0x25, "The second start line."));
            second.mPrompt = secondPrompt;
            infos.push_back(second);
        }
        p.mDials.append(topic(kTopicOffer, "StartTopic", "CUST", 0xF0, kBranch, infos));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 2); // Blocking
        return p;
    }

    std::string greetingOf(const Plugin& p)
    {
        Talk talk(p);
        talk.mRuntime->setQuestGreetings(true);
        Turn turn;
        std::string error;
        EXPECT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        return turn.text;
    }

    TEST(SkyrimDb03Greeting, aStartWhoseInfoHasNoPromptIsStillTheGreeting)
    {
        EXPECT_EQ(greetingOf(greetingPlugin("", "")), "The start line.");
    }

    TEST(SkyrimDb03Greeting, aStartWhoseOnlyInfoHasAPromptIsAMenuTopicAndHelloGreets)
    {
        EXPECT_EQ(greetingOf(greetingPlugin("Any advice?", "")), "Yes?");
    }

    TEST(SkyrimDb03Greeting, aStartWhoseEveryInfoHasAPromptIsAMenuTopic)
    {
        EXPECT_EQ(greetingOf(greetingPlugin("Any advice?", "Anything else?")), "Yes?");
    }

    TEST(SkyrimDb03Greeting, aStartWithOneInfoWithoutAPromptKeepsBeingAGreeting)
    {
        // MG02TolfdirStage40Forcegreet is such a mixed topic: only a topic of prompts alone is the player's own words
        EXPECT_EQ(greetingOf(greetingPlugin("", "Anything else?")), "The start line.");
    }
}
