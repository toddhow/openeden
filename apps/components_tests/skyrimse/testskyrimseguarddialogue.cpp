// Task 0258 (docs/tasks/0258-skyrim-guard-dialogue.md): the guards use the game's own arrest dialogue. Four groups,
// written from the task spec before the code and run red against stubs (see the stage log of the task):
//  * SkyrimGuardDialogueRuntime: the forced greeting (subtype PFGT), the link-to choices, the crime conditions;
//  * SkyrimGuardDialogueOffer:   how the crime system chooses between the conversation and the window;
//  * SkyrimGuardDialogueService: the bindings and the script-driven choices of the crime service;
//  * SkyrimGuardDialogueNatives: Actor.GetCrimeFaction, Faction.PlayerPayCrimeGold, SendPlayerToJail,
//                                Actor.SetPlayerResistingArrest and Faction.GetCrimeGold in the Papyrus host.
// Generated plugins and scripts, no game data.
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/crime_service.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorSpeed;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::SubjectState;
    using Turn = openeden::sdk::game::DialogueTurn;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kNpcGuard = kSelf | 0x200;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicForce = kSelf | 0x310;
    constexpr FormId kTopicPay = kSelf | 0x311;
    constexpr FormId kTopicJail = kSelf | 0x312;
    constexpr FormId kTopicResist = kSelf | 0x313;
    constexpr FormId kTopicNever = kSelf | 0x314;
    constexpr FormId kTopicPlead = kSelf | 0x315;
    constexpr FormId kTopicRumors = kSelf | 0x316;
    constexpr FormId kClassGuard = kSelf | 0x900;
    constexpr FormId kClassCitizen = kSelf | 0x901;
    constexpr FormId kFactionGuard = kSelf | 0x902;

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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    // compare codes: 0 ==, 2 >, 4 <
    constexpr std::uint16_t kGetAlarmed = 61;
    constexpr std::uint16_t kIsGuard = 125;
    constexpr std::uint16_t kGetCrimeGold = 459;
    constexpr std::uint16_t kCanPayCrimeGold = 497;

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::uint16_t mFlags = 0;
        Bytes mConditions;
        std::vector<std::string> mTexts{ "Hello." };
        std::string mPrompt;
        std::vector<std::uint32_t> mLinks;
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        for (const std::uint32_t link : s.mLinks)
            b.append(sub4("TCLT", Bytes().u32(link)));
        std::uint8_t n = 1;
        for (const std::string& t : s.mTexts)
        {
            Bytes trdt;
            trdt.u32(0).u32(50).u32(0).u8(n++).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
            b.append(sub4("TRDT", trdt));
            b.append(sub4("NAM1", Bytes().zstring(t)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        if (!s.mPrompt.empty())
            b.append(sub4("RNAM", Bytes().zstring(s.mPrompt)));
        b.append(s.mConditions);
        return record4("INFO", 0u, s.mId, b);
    }

    /// A topic with a FULL name (the player's choice text) and a subtype, and its infos.
    Bytes topic(std::uint32_t id, const std::string& editor, const std::string& full, const std::string& subtype,
        const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(editor));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mTexts = { text };
        return s;
    }

    Bytes guardNpc(FormId cls, bool inGuardFaction)
    {
        Bytes b;
        b.append(edid("Guard"));
        b.append(sub4("FULL", Bytes().zstring("Whiterun Guard")));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        b.append(sub4("ACBS", acbs));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(cls)));
        if (inGuardFaction)
            b.append(sub4("SNAM", Bytes().u32(kFactionGuard).u8(0).u8(0).u8(0).u8(0)));
        return record4("NPC_", 0, kNpcGuard, b);
    }

    /// A CLAS record: DATA is 36 bytes, the last holds the flags (bit 0: Guard, VERIFIED on the five masters).
    Bytes classRecord(FormId id, const std::string& editor, std::uint8_t flags)
    {
        Bytes data;
        data.u32(0xFFFC0000u).u8(0).u8(0);
        for (int i = 0; i < 18; ++i)
            data.u8(1);
        data.f32(0.1f).u32(0).u8(1).u8(1).u8(1).u8(flags);
        Bytes b;
        b.append(edid(editor));
        b.append(sub4("DATA", data));
        return record4("CLAS", 0, id, b);
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        return record4("RACE", 0, kRaceNord, b);
    }

    /// The arrest conversation in the shape of the game's own: a forced greeting (PFGT) whose first INFO has no
    /// response (like 0x10f519), a second that needs GetAlarmed and a bounty and links to the choices; the choices
    /// are topics with a FULL name whose INFOs carry their own conditions.
    Bytes arrestPlugin(bool saidOnce = false, bool guardClass = true, bool inGuardFaction = false)
    {
        Bytes dials;
        InfoSpec noText = info(0x20, "");
        noText.mTexts.clear();
        noText.mConditions = ctda(kGetAlarmed, 0, 1.0f);
        InfoSpec halt = info(0x21, "Halt! You have committed crimes against Skyrim.");
        halt.mConditions = ctda(kGetAlarmed, 0, 1.0f);
        halt.mConditions.append(ctda(kGetCrimeGold, 2, 0.0f));
        halt.mConditions.append(ctda(kIsGuard, 0, 1.0f));
        halt.mLinks = { kTopicPlead, kTopicPay, kTopicJail, kTopicResist, kTopicNever };
        if (saidOnce)
            halt.mFlags = InfoFlags::SayOnce;
        dials.append(topic(kTopicForce, "DGCrimeForcegreetTopic", "What's the problem?", "PFGT", { noText, halt }));
        InfoSpec pay = info(0x22, "Smart move. Now come along.");
        pay.mConditions = ctda(kCanPayCrimeGold, 0, 1.0f);
        dials.append(topic(
            kTopicPay, "DGCrimePayFine", "You caught me. I'll pay off my bounty. (<CrimeGold> gold)", "CUST", { pay }));
        InfoSpec jail = info(0x23, "Come along, then.");
        jail.mPrompt = "I submit. Take me to jail.";
        dials.append(topic(kTopicJail, "DGCrimeGoToJail", "ignored full name", "CUST", { jail }));
        dials.append(topic(kTopicResist, "DGCrimeResistArrest", "I'd rather die than go to prison!", "CUST",
            { info(0x24, "Then pay with your blood.") }));
        InfoSpec never = info(0x25, "Go, then.");
        never.mConditions = ctda(kGetCrimeGold, 4, 5.0f); // only for a small bounty
        dials.append(topic(kTopicNever, "DGCrimeNeverMind", "I don't have time for this.", "CUST", { never }));
        InfoSpec plead = info(0x26, "A likely story.");
        plead.mLinks = { kTopicJail };
        dials.append(topic(kTopicPlead, "DGCrimePlead", "Please, I was framed!", "CUST", { plead }));
        dials.append(topic(kTopicHello, "Hello", "", "HELO", { info(0x27, "Hello there.") }));
        dials.append(topic(kTopicRumors, "Rumors", "Rumors", "CUST", { info(0x28, "Dragons, they say.") }));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, dials));
        bytes.append(group4(
            Content::fourCC("NPC_"), kGrupTop, guardNpc(guardClass ? kClassGuard : kClassCitizen, inGuardFaction)));
        Bytes factions;
        {
            Bytes f;
            f.append(edid("IsGuardFaction"));
            factions.append(record4("FACT", 0, kFactionGuard, f));
        }
        bytes.append(group4(Content::fourCC("FACT"), kGrupTop, factions));
        Bytes classes = classRecord(kClassGuard, "GuardImperial", 1);
        classes.append(classRecord(kClassCitizen, "Citizen", 0));
        bytes.append(group4(Content::fourCC("CLAS"), kGrupTop, classes));
        bytes.append(group4(Content::fourCC("RACE"), kGrupTop, raceRecord()));
        return bytes;
    }

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(const Bytes& bytes)
        {
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
        }

        static DialogueSpeaker speaker()
        {
            DialogueSpeaker s;
            s.mBase = kNpcGuard;
            s.mReference = kNpcGuard | 0x1000;
            return s;
        }

        /// A source that says: the player owes `gold`, can pay when `canPay`, the guard is alarmed when `alarmed`.
        void source(float gold, bool canPay, bool alarmed)
        {
            CrimeConditionSource s;
            s.mCrimeGold = [gold](FormId, FormId) { return gold; };
            s.mCanPay = [canPay](FormId, FormId) { return canPay; };
            s.mAlarmed = [alarmed](FormId) { return alarmed; };
            mRuntime->setCrimeSource(std::move(s));
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
        ADD_FAILURE() << "choice not offered: " << name;
        return 0;
    }

    // ---- the runtime ------------------------------------------------------------------------------------------------

    TEST(SkyrimGuardDialogueRuntime, withNoCrimeSourceNoForcedGreetingPasses)
    {
        Talk talk(arrestPlugin());
        EXPECT_FALSE(talk.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        Turn turn;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimGuardDialogueRuntime, anAlarmedGuardWithABountyGivesTheForcedGreetingAndSkipsTheInfoWithNoText)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        EXPECT_TRUE(talk.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        EXPECT_EQ(turn.text, "Halt! You have committed crimes against Skyrim.");
        EXPECT_FALSE(turn.endsConversation);
        EXPECT_TRUE(talk.mRuntime->open());
    }

    TEST(SkyrimGuardDialogueRuntime, aGuardThatIsNotAlarmedOrNotOwedNothingGivesNoForcedGreeting)
    {
        Talk calm(arrestPlugin());
        calm.source(100.f, true, false);
        EXPECT_FALSE(calm.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        Talk clean(arrestPlugin());
        clean.source(0.f, true, true);
        EXPECT_FALSE(clean.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
    }

    TEST(SkyrimGuardDialogueRuntime, onlyAGuardClassPassesTheIsGuardCondition)
    {
        Talk citizen(arrestPlugin(false, false));
        citizen.source(100.f, true, true);
        EXPECT_FALSE(citizen.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        Talk guard(arrestPlugin(false, true));
        guard.source(100.f, true, true);
        EXPECT_TRUE(guard.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
    }

    TEST(SkyrimGuardDialogueRuntime, aMemberOfTheGamesGuardFactionPassesIsGuardWhateverItsClass)
    {
        // the street guards' class is the civil war soldier's, which has no Guard flag; they are in IsGuardFaction
        Talk member(arrestPlugin(false, false, true));
        member.source(100.f, true, true);
        EXPECT_TRUE(member.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
    }

    TEST(SkyrimGuardDialogueRuntime, theClassRecordReadsItsGuardFlag)
    {
        Talk talk(arrestPlugin());
        const ClassRecord* guard = talk.mActors.classRecord(kClassGuard);
        const ClassRecord* citizen = talk.mActors.classRecord(kClassCitizen);
        ASSERT_NE(guard, nullptr);
        ASSERT_NE(citizen, nullptr);
        EXPECT_TRUE(guard->mGuard);
        EXPECT_FALSE(citizen->mGuard);
    }

    TEST(SkyrimGuardDialogueRuntime, theChoicesAreTheLinkedTopicsWhoseInfosPassShownByPromptElseByName)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        // Never Mind needs a bounty under 5 and does not pass; the text tag <CrimeGold> reads the source; the jail
        // INFO has a prompt that wins over its topic's name. The order is the link order of the greeting.
        EXPECT_EQ(names(turn),
            (std::vector<std::string>{ "Please, I was framed!", "You caught me. I'll pay off my bounty. (100 gold)",
                "I submit. Take me to jail.", "I'd rather die than go to prison!" }));
    }

    TEST(SkyrimGuardDialogueRuntime, thePayChoiceIsLeftOutWhenThePlayerCannotPay)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, false, true);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        EXPECT_EQ(names(turn),
            (std::vector<std::string>{
                "Please, I was framed!", "I submit. Take me to jail.", "I'd rather die than go to prison!" }));
    }

    TEST(SkyrimGuardDialogueRuntime, aChosenLinkGivesItsResponseAndEndsTheConversationWhenItLinksNowhere)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "I submit. Take me to jail."), reply, error)) << error;
        EXPECT_EQ(reply.text, "Come along, then.");
        EXPECT_TRUE(reply.endsConversation);
        EXPECT_TRUE(reply.topics.empty());
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimGuardDialogueRuntime, aLinkedResponseWithLinksOfItsOwnOffersThoseAndTheConversationGoesOn)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Please, I was framed!"), reply, error)) << error;
        EXPECT_EQ(reply.text, "A likely story.");
        EXPECT_FALSE(reply.endsConversation);
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "I submit. Take me to jail." }));
        EXPECT_TRUE(talk.mRuntime->open());
    }

    TEST(SkyrimGuardDialogueRuntime, aTopicThatWasNotOfferedCannotBeChosen)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        EXPECT_FALSE(talk.mRuntime->choose(kTopicNever, reply, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimGuardDialogueRuntime, askingWhetherTheGreetingPassesMarksNothingSaid)
    {
        Talk talk(arrestPlugin(true));
        talk.source(100.f, true, true);
        EXPECT_TRUE(talk.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        EXPECT_TRUE(talk.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT"));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        talk.mRuntime->end();
        EXPECT_FALSE(talk.mRuntime->forcedGreetingAvailable(Talk::speaker(), "PFGT")) << "the say-once line was said";
    }

    TEST(SkyrimGuardDialogueRuntime, theSourceIsAskedAboutTheSpeakersBase)
    {
        Talk talk(arrestPlugin());
        std::set<FormId> alarmedAsked;
        CrimeConditionSource s;
        s.mCrimeGold = [](FormId, FormId faction) { return faction == 0 ? 80.f : 0.f; };
        s.mCanPay = [](FormId, FormId) { return true; };
        s.mAlarmed = [&alarmedAsked](FormId base) {
            alarmedAsked.insert(base);
            return true;
        };
        talk.mRuntime->setCrimeSource(std::move(s));
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        EXPECT_EQ(alarmedAsked, (std::set<FormId>{ kNpcGuard }));
    }

    TEST(SkyrimGuardDialogueRuntime, anOrdinaryConversationAfterAForcedOneIsOrdinaryAgain)
    {
        Talk talk(arrestPlugin());
        talk.source(100.f, true, true);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForced(Talk::speaker(), "PFGT", turn, error)) << error;
        talk.mRuntime->end();
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(turn.text, "Hello there.");
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Rumors"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Dragons, they say.");
        EXPECT_FALSE(reply.endsConversation) << "an ordinary response goes back to the topic list";
        EXPECT_FALSE(reply.topics.empty());
    }

    // ---- the crime system's choice of offer
    // -----------------------------------------------------------------------------

    constexpr FormId kHold = kSelf | 0x100;

    class FakeWorld final : public crime::CrimeWorld
    {
    public:
        void actors(std::vector<crime::CrimeActor>& out) override
        {
            for (const auto& [ref, a] : mActors)
                out.push_back(a);
        }
        bool actor(ActorHandle handle, crime::CrimeActor& out) override
        {
            const auto it = mActors.find(static_cast<std::uint32_t>(handle.reference));
            if (it == mActors.end())
                return false;
            out = it->second;
            return true;
        }
        const CrimeData* crimeData(FormId) override { return &mData; }
        std::string factionName(FormId) override { return "Whiterun"; }
        std::int32_t itemValue(FormId) override { return 100; }
        std::int32_t playerGold() override { return mGold; }
        bool takeGold(std::int32_t amount) override
        {
            if (amount > mGold)
                return false;
            mGold -= amount;
            return true;
        }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}

        std::map<std::uint32_t, crime::CrimeActor> mActors;
        CrimeData mData;
        std::int32_t mGold = 500;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = 100.0; // every actor stands next to the player
            return true;
        }
    };

    struct SkyrimGuardDialogueOfferTest : ::testing::Test
    {
        crime::Ledger mLedger;
        FakeWorld mWorld;
        FakeWriter mWriter;
        std::unique_ptr<crime::CrimeSystem> mSystem;
        int mProbes = 0;

        void SetUp() override
        {
            mWorld.mData.mPresent = true;
            mWorld.mData.mMurder = 1000;
            mWorld.mData.mAssault = 40;
            crime::CrimeActor guard;
            guard.mHandle = { 10, 1 };
            guard.mBase = kNpcGuard;
            guard.mGuard = true;
            guard.mCrimeFaction = kHold;
            guard.mName = "guard";
            mWorld.mActors[10] = guard;
            mSystem = std::make_unique<crime::CrimeSystem>(mLedger, mWorld);
            mLedger.addBounty(kHold, 100);
        }
        void probe(bool answer)
        {
            mSystem->setDialogueProbe([this, answer](const crime::CrimeActor& guard) {
                ++mProbes;
                EXPECT_EQ(guard.mBase, kNpcGuard);
                EXPECT_EQ(guard.mCrimeFaction, kHold);
                return answer;
            });
        }
        void toOffer()
        {
            for (int i = 0; i < 6 && mSystem->state() != crime::State::Offer; ++i)
                mSystem->update(0.5f, mWriter);
            ASSERT_EQ(mSystem->state(), crime::State::Offer);
        }
    };

    TEST_F(SkyrimGuardDialogueOfferTest, withNoProbeTheOfferIsTheWindow)
    {
        toOffer();
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
        EXPECT_EQ(mSystem->guardReference(), 10u);
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aProbeThatSaysYesMakesTheOfferTheGuardsConversation)
    {
        probe(true);
        toOffer();
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Dialogue);
        EXPECT_EQ(mProbes, 1) << "asked once when the offer starts";
        EXPECT_TRUE(mSystem->wantsDialogueWith(10));
        EXPECT_FALSE(mSystem->wantsDialogueWith(11)) << "another actor's activation is no arrest";
        EXPECT_EQ(mSystem->guardBase(), kNpcGuard);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aProbeThatSaysNoLeavesTheWindow)
    {
        probe(false);
        toOffer();
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
        EXPECT_EQ(mProbes, 1);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, onceTheConversationBeganItIsNotWantedAgainButTheModeStays)
    {
        probe(true);
        toOffer();
        mSystem->dialogueBegan(10);
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Dialogue);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aChoiceMadeInTheConversationEndsTheOfferAndTheModeGoesBackToWindow)
    {
        probe(true);
        toOffer();
        mSystem->dialogueBegan(10);
        EXPECT_EQ(mSystem->choose(crime::Choice::Pay), crime::ChoiceStatus::Ok);
        mSystem->dialogueEnded(10); // the box closes after the choice: nothing changes
        EXPECT_EQ(mSystem->state(), crime::State::None);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aConversationClosedWithNoChoiceFallsBackToTheWindowAtTheNextUpdate)
    {
        probe(true);
        toOffer();
        const std::string before = mSystem->message();
        const std::uint32_t revision = mSystem->revision();
        mSystem->dialogueBegan(10);
        mSystem->dialogueEnded(10);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Dialogue) << "the fragment of the response has not run yet";
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mSystem->state(), crime::State::Offer);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
        EXPECT_NE(mSystem->message(), before);
        EXPECT_GT(mSystem->revision(), revision);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aChoiceMadeBeforeTheNextUpdateKeepsTheWindowAway)
    {
        probe(true);
        toOffer();
        mSystem->dialogueBegan(10);
        mSystem->dialogueEnded(10);
        ASSERT_EQ(mSystem->choose(crime::Choice::Jail), crime::ChoiceStatus::Ok); // the fragment, in the first frame
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mSystem->state(), crime::State::Jailed);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, theWindowIsNotRaisedTwiceForOneClosedConversation)
    {
        probe(true);
        toOffer();
        mSystem->dialogueBegan(10);
        mSystem->dialogueEnded(10);
        mSystem->update(0.5f, mWriter);
        const std::uint32_t revision = mSystem->revision();
        mSystem->update(0.5f, mWriter);
        EXPECT_EQ(mSystem->revision(), revision);
    }

    TEST_F(SkyrimGuardDialogueOfferTest, aConversationThatCouldNotBeginFallsBackToTheWindow)
    {
        probe(true);
        toOffer();
        mSystem->dialogueFailed(10);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
    }

    TEST_F(SkyrimGuardDialogueOfferTest, anEventAboutAnotherActorChangesNothing)
    {
        probe(true);
        toOffer();
        mSystem->dialogueFailed(11);
        mSystem->dialogueBegan(11);
        mSystem->dialogueEnded(11);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Dialogue);
        EXPECT_TRUE(mSystem->wantsDialogueWith(10));
    }

    TEST_F(SkyrimGuardDialogueOfferTest, theModeIsWindowOutsideAnOfferAndEachOfferAsksTheProbeAgain)
    {
        probe(true);
        toOffer();
        ASSERT_EQ(mSystem->choose(crime::Choice::Resist), crime::ChoiceStatus::Ok);
        EXPECT_EQ(mSystem->offerMode(), crime::OfferMode::Window);
        EXPECT_FALSE(mSystem->wantsDialogueWith(10));
    }

    TEST(SkyrimGuardDialogueOffer, theModesHaveStableNames)
    {
        EXPECT_EQ(crime::offerModeName(crime::OfferMode::Window), "window");
        EXPECT_EQ(crime::offerModeName(crime::OfferMode::Dialogue), "dialogue");
    }

    // ---- the service
    // -------------------------------------------------------------------------------------------------

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name)
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            return it->second.handler->call({});
        }
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    struct SkyrimGuardDialogueServiceTest : ::testing::Test
    {
        CrimeData mData;
        std::vector<crime::CrimeActor> mActors;
        std::int32_t mGold = 200;
        bool mDialogueAvailable = true;
        std::unique_ptr<crime::CrimeService> mService;
        StubBindings mBindings;
        FakeWriter mWriter;

        void SetUp() override
        {
            mData.mPresent = true;
            mData.mMurder = 1000;
            mData.mAssault = 40;
            crime::CrimeActor guard;
            guard.mHandle = { 10, 1 };
            guard.mBase = kNpcGuard;
            guard.mGuard = true;
            guard.mCrimeFaction = kHold;
            guard.mName = "guard";
            mActors.push_back(guard);
            crime::ServiceHooks hooks;
            hooks.mActors = [this](std::vector<crime::CrimeActor>& out) {
                out.insert(out.end(), mActors.begin(), mActors.end());
            };
            hooks.mActor = [this](ActorHandle h, crime::CrimeActor& out) {
                for (const crime::CrimeActor& a : mActors)
                    if (a.mHandle == h)
                    {
                        out = a;
                        return true;
                    }
                return false;
            };
            hooks.mCrimeData = [this](FormId f) { return f == kHold ? &mData : nullptr; };
            hooks.mFactionName = [](FormId) { return std::string("Whiterun"); };
            hooks.mGold = [this] { return mGold; };
            hooks.mTakeGold = [this](std::int32_t n) {
                if (n > mGold)
                    return false;
                mGold -= n;
                return true;
            };
            hooks.mJail = [](FormId, crime::JailDestination& out) {
                out.mValid = true;
                out.mCell = "WhiterunJail";
                return true;
            };
            hooks.mDialogueAvailable = [this](const crime::CrimeActor&) { return mDialogueAvailable; };
            mService = std::make_unique<crime::CrimeService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
            mService->ledger().addBounty(kHold, 100);
        }
        void toOffer()
        {
            for (int i = 0; i < 6 && mService->system().state() != crime::State::Offer; ++i)
                mService->system().update(0.5f, mWriter);
            ASSERT_EQ(mService->system().state(), crime::State::Offer);
        }
        std::string text(const std::string& name) { return std::string(mBindings.call(name).value.text); }
    };

    TEST_F(SkyrimGuardDialogueServiceTest, theNamespaceGainedTheOfferModeAndTheGuardBaseAsAMinorVersion)
    {
        EXPECT_EQ(mBindings.mVersion.major, 1u);
        EXPECT_GE(mBindings.mVersion.minor, 1u);
        EXPECT_NE(mBindings.mFunctions.find("offer_mode"), mBindings.mFunctions.end());
        EXPECT_NE(mBindings.mFunctions.find("guard_base"), mBindings.mFunctions.end());
    }

    TEST_F(SkyrimGuardDialogueServiceTest, theOfferModeAndTheGuardBaseAreReadable)
    {
        EXPECT_EQ(text("offer_mode"), "window");
        EXPECT_EQ(text("guard_base"), "");
        toOffer();
        EXPECT_EQ(text("offer_mode"), "dialogue");
        EXPECT_EQ(text("guard_base"), std::to_string(kNpcGuard));
    }

    TEST_F(SkyrimGuardDialogueServiceTest, whenTheHookSaysNoTheOfferIsTheWindow)
    {
        mDialogueAvailable = false;
        toOffer();
        EXPECT_EQ(text("offer_mode"), "window");
    }

    TEST_F(SkyrimGuardDialogueServiceTest, theConditionSourceAnswersForTheOfferingGuardOnly)
    {
        const CrimeConditionSource source = mService->conditionSource();
        ASSERT_TRUE(source.mCrimeGold);
        ASSERT_TRUE(source.mCanPay);
        ASSERT_TRUE(source.mAlarmed);
        EXPECT_FALSE(source.mAlarmed(kNpcGuard)) << "no offer yet";
        toOffer();
        EXPECT_TRUE(source.mAlarmed(kNpcGuard));
        EXPECT_FALSE(source.mAlarmed(kNpcGuard + 1));
        EXPECT_EQ(source.mCrimeGold(kNpcGuard, 0), 100.f);
        EXPECT_EQ(source.mCrimeGold(kNpcGuard, kHold), 100.f);
        EXPECT_EQ(source.mCrimeGold(kNpcGuard, kHold + 1), 0.f);
        EXPECT_TRUE(source.mCanPay(kNpcGuard, 0));
        mGold = 50;
        EXPECT_FALSE(source.mCanPay(kNpcGuard, 0));
    }

    TEST_F(SkyrimGuardDialogueServiceTest, theGuardsFactionIsTheOneAGuardWithNoOfferYetOwes)
    {
        // before any offer the gold the speaker's crime faction is owed is still the player's bounty there
        const CrimeConditionSource source = mService->conditionSource();
        EXPECT_EQ(source.mCrimeGold(kNpcGuard, 0), 100.f);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, aScriptThatPaysTheFineTakesTheGoldAndClearsTheBounty)
    {
        toOffer();
        EXPECT_TRUE(mService->payByScript(kHold, true));
        EXPECT_EQ(mGold, 100);
        EXPECT_EQ(mService->ledger().bounty(kHold), 0);
        EXPECT_EQ(mService->system().state(), crime::State::None);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, aScriptThatPaysWithTooLittleGoldOrForAnotherFactionOrWithNoOfferDoesNothing)
    {
        EXPECT_FALSE(mService->payByScript(kHold, true)) << "no offer";
        toOffer();
        EXPECT_FALSE(mService->payByScript(kHold + 1, true)) << "another faction";
        mGold = 10;
        EXPECT_FALSE(mService->payByScript(kHold, true)) << "not enough gold";
        EXPECT_EQ(mService->ledger().bounty(kHold), 100);
        EXPECT_EQ(mService->system().state(), crime::State::Offer);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, aScriptThatSendsThePlayerToJailJailsHim)
    {
        toOffer();
        EXPECT_TRUE(mService->jailByScript(kHold));
        EXPECT_EQ(mService->system().state(), crime::State::Jailed);
        EXPECT_EQ(mService->ledger().bounty(kHold), 0);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, aScriptThatResistsMakesTheGuardsFight)
    {
        toOffer();
        EXPECT_TRUE(mService->resistByScript());
        EXPECT_EQ(mService->system().state(), crime::State::Resisted);
        EXPECT_EQ(mService->ledger().bounty(kHold), 100);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, theCrimeFactionOfALoadedActorIsFoundByReferenceAndByBase)
    {
        EXPECT_EQ(mService->crimeFactionOfReference(10), kHold);
        EXPECT_EQ(mService->crimeFactionOfReference(99), kNullFormId);
        EXPECT_EQ(mService->crimeFactionOfBase(kNpcGuard), kHold);
        EXPECT_EQ(mService->crimeFactionOfBase(kNpcGuard + 5), kNullFormId);
    }

    TEST_F(SkyrimGuardDialogueServiceTest, theScriptActionsNeedAnOffer)
    {
        EXPECT_FALSE(mService->jailByScript(kHold));
        EXPECT_FALSE(mService->resistByScript());
        EXPECT_EQ(mService->crimeGoldOf(kHold), 100);
        EXPECT_EQ(mService->crimeGoldOf(kHold + 1), 0);
    }

    // ---- the Papyrus natives ----------------------------------------------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kFaction = 0x01000C00;
    constexpr FormId kBase = 0x01000B00;

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        void add(const PexAsm::ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        FormId crimeFactionOf(FormId actor) override
        {
            mLog.push_back("crimefaction " + std::to_string(actor));
            return mHasFaction ? kFaction : 0;
        }
        bool payCrimeGold(FormId faction, bool removeStolen, bool goToJail) override
        {
            mLog.push_back("pay " + std::to_string(faction) + " " + (removeStolen ? "stolen" : "keep") + " "
                + (goToJail ? "jail" : "nojail"));
            return true;
        }
        bool sendToJail(FormId faction, bool removeInventory, bool realJail) override
        {
            mLog.push_back("jail " + std::to_string(faction) + " " + (removeInventory ? "strip" : "keep") + " "
                + (realJail ? "real" : "soft"));
            return true;
        }
        bool resistArrest(FormId actor) override
        {
            mLog.push_back("resist " + std::to_string(actor));
            return true;
        }
        std::int32_t crimeGold(FormId faction) override
        {
            mLog.push_back("gold " + std::to_string(faction));
            return 321;
        }
        bool mHasFaction = true;
        std::vector<std::string> mLog;
    };

    struct Rig
    {
        Rig()
        {
            using namespace PexAsm;
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("GetCrimeFaction", "Faction").native();
            actor.function("SetPlayerResistingArrest").native();
            mData.add(actor);
            ScriptBuilder faction("Faction");
            faction.parent("Form");
            faction.function("PlayerPayCrimeGold")
                .native()
                .param("abRemoveStolenItems", "Bool")
                .param("abGoToJail", "Bool");
            faction.function("SendPlayerToJail")
                .native()
                .param("abRemoveInventory", "Bool")
                .param("abRealJail", "Bool");
            faction.function("GetCrimeGold", "Int").native();
            mData.add(faction);
            ScriptBuilder probe("Probe");
            probe.parent("Actor");
            probe.var("gold", "Int");
            probe.var("factionId", "Int");
            probe.function("Pay")
                .local("f", "Faction")
                .local("n", "None")
                .callMethod("GetCrimeFaction", Id("self"), Id("f"))
                .callMethod("PlayerPayCrimeGold", Id("f"), Id("n"), { Bool(true), Bool(true) });
            probe.function("Jail")
                .local("f", "Faction")
                .local("n", "None")
                .callMethod("GetCrimeFaction", Id("self"), Id("f"))
                .callMethod("SendPlayerToJail", Id("f"), Id("n"), { Bool(false), Bool(true) });
            probe.function("Resist").local("n", "None").callMethod("SetPlayerResistingArrest", Id("self"), Id("n"));
            probe.function("Gold")
                .local("f", "Faction")
                .local("v", "Int")
                .callMethod("GetCrimeFaction", Id("self"), Id("f"))
                .callMethod("GetCrimeGold", Id("f"), Id("v"))
                .assign(Id("gold"), Id("v"))
                .callMethod("GetFormID", Id("f"), Id("v"))
                .assign(Id("factionId"), Id("v"));
            mData.add(probe);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActor] = "Actor";
            mData.mClasses[kFaction] = "Faction";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            vmad::Script script;
            script.mName = "Probe";
            EXPECT_EQ(mHost->attachReference(kActor, kBase, true, { std::move(script) }), 1u);
            tick();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kActor, "Probe", function);
            tick();
            return ok;
        }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimGuardDialogueNatives, payingTheFineCallsThePackWithTheActorsCrimeFactionAndTheFlags)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Pay"));
        ASSERT_EQ(rig.mHooks.mLog.size(), 2u);
        EXPECT_EQ(rig.mHooks.mLog[0], "crimefaction " + std::to_string(kActor));
        EXPECT_EQ(rig.mHooks.mLog[1], "pay " + std::to_string(kFaction) + " stolen jail");
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimGuardDialogueNatives, goingToJailCallsThePackWithTheFlags)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Jail"));
        ASSERT_EQ(rig.mHooks.mLog.size(), 2u);
        EXPECT_EQ(rig.mHooks.mLog[1], "jail " + std::to_string(kFaction) + " keep real");
    }

    TEST(SkyrimGuardDialogueNatives, resistingArrestCallsThePackWithTheActor)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Resist"));
        ASSERT_EQ(rig.mHooks.mLog.size(), 1u);
        EXPECT_EQ(rig.mHooks.mLog[0], "resist " + std::to_string(kActor));
    }

    TEST(SkyrimGuardDialogueNatives, theCrimeGoldOfAFactionIsTheBountyThePackSays)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Gold"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "gold"), 321.0);
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "factionId"), static_cast<double>(kFaction));
    }

    TEST(SkyrimGuardDialogueNatives, anActorWithNoCrimeFactionMakesTheFactionNativesHarmless)
    {
        Rig rig;
        rig.mHooks.mHasFaction = false;
        ASSERT_TRUE(rig.call("Pay"));
        EXPECT_EQ(rig.mHooks.mLog.size(), 1u) << "only the crime faction was asked";
    }

    TEST(SkyrimGuardDialogueNatives, withoutHooksTheNativesAreTolerant)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        EXPECT_TRUE(rig.call("Pay"));
        EXPECT_TRUE(rig.call("Jail"));
        EXPECT_TRUE(rig.call("Resist"));
        EXPECT_TRUE(rig.call("Gold"));
    }
}
