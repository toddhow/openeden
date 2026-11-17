// Task 0271 (docs/tasks/0271-skyrim-thieves-guild.md): what the Thieves Guild questline needed of the engine, with
// generated plugins and fakes, no game data:
//  * SkyrimThievesPackage:   a ForceGreet package names its topic (PDTO) through its template;
//  * SkyrimThievesAlias:     a quest alias keeps the packages it gives (ALPC), in order;
//  * SkyrimThievesBrain:     a chosen ForceGreet package asks for its conversation, again every few evaluations;
//  * SkyrimThievesDialogue:  the forced greeting of one named topic and its link-to choices;
//  * SkyrimThievesForceGreet: the brain's asks joining task 0264's ForceGreet (`requestFromBrain`);
//  * SkyrimThievesConditions: IsPlayerMovingIntoNewSpace and the conditions that run on the player's place.
// Written after the code (see the stage log of the task): the evidence is the mutation checks listed there.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/forcegreet.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <map>
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
    using namespace openeden::sdk::game;
    using Turn = openeden::sdk::game::DialogueTurn;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kTopicIntro = kSelf | 0x300;
    constexpr FormId kTopicOther = kSelf | 0x301;
    constexpr FormId kTopicChoice = kSelf | 0x302;
    constexpr FormId kTopicChain = kSelf | 0x303; // no name: the speaker goes on
    constexpr FormId kTopicTail = kSelf | 0x304;
    constexpr FormId kPackTemplateGreet = kSelf | 0x910;
    constexpr FormId kPackTemplateTravel = kSelf | 0x911;
    constexpr FormId kPackGreet = kSelf | 0x912;
    constexpr FormId kPackNotGreet = kSelf | 0x913;
    constexpr FormId kPackZero = kSelf | 0x914;

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

    // ---- packages
    // ----------------------------------------------------------------------------------------------------

    Bytes packRecord(
        FormId id, const std::string& editor, FormId templateId, const std::string& procedure, FormId topic)
    {
        Bytes b;
        b.append(edid(editor));
        b.append(sub4("PKDT", Bytes().u32(0).u8(18).u8(0).u8(2).u8(0xFF).u16(0xEF).u16(0)));
        b.append(sub4("PSDT", Bytes().u8(0xFF).u8(0xFF).u8(0).u8(0xFF).u8(0xFF).u8(0).u8(0).u8(0).u32(0)));
        b.append(sub4("PKCU", Bytes().u32(1).u32(templateId).u32(10)));
        if (!procedure.empty())
        {
            b.append(sub4("PRCB", Bytes().u32(1).u32(0)));
            b.append(sub4("PNAM", Bytes().zstring(procedure)));
            b.append(sub4("FNAM", Bytes().u32(0)));
        }
        // the topic input, then the empty ones of OnBegin and OnEnd
        b.append(sub4("PDTO", Bytes().u32(0).u32(topic)));
        b.append(sub4("PDTO", Bytes().u32(0).u32(0)));
        b.append(sub4("CITC", Bytes().u32(0)));
        return record4("PACK", 0, id, b);
    }

    PackageIndex packageIndex()
    {
        Bytes packs;
        packs.append(packRecord(kPackTemplateGreet, "TemplateGreet", 0, "ForceGreet", 0));
        packs.append(packRecord(kPackTemplateTravel, "TemplateTravel", 0, "Travel", 0));
        packs.append(packRecord(kPackGreet, "BrynjolfGreet", kPackTemplateGreet, "", kTopicIntro));
        packs.append(packRecord(kPackNotGreet, "BrynjolfTravel", kPackTemplateTravel, "", kTopicIntro));
        packs.append(packRecord(kPackZero, "NoTopic", kPackTemplateGreet, "", 0));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("PACK"), kGrupTop, packs));
        PackageRecords records;
        Content::Error error;
        EXPECT_TRUE(parsePackageRecordsBytes(bytes.str(), "test.esp", order(), PackageLimits(), records, error))
            << error.mMessage;
        auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        PackageIndex index;
        index.add(std::move(records), FormIdResolver(plugin->header(), "test.esp", order()));
        return index;
    }

    TEST(SkyrimThievesPackage, aForceGreetPackageNamesItsTopicThroughItsTemplate)
    {
        const PackageIndex index = packageIndex();
        const PackageRecord* greet = index.package(kPackGreet);
        ASSERT_NE(greet, nullptr);
        EXPECT_EQ(greet->mForceGreetTopic, kTopicIntro);
        FormId topic = kNullFormId;
        EXPECT_TRUE(index.forceGreetTopic(*greet, topic));
        EXPECT_EQ(topic, kTopicIntro);
    }

    TEST(SkyrimThievesPackage, aPackageThatIsNotForceGreetNamesNoTopicWhateverItsInputs)
    {
        const PackageIndex index = packageIndex();
        const PackageRecord* travel = index.package(kPackNotGreet);
        ASSERT_NE(travel, nullptr);
        EXPECT_EQ(travel->mForceGreetTopic, kTopicIntro); // the input is there ...
        FormId topic = kNullFormId;
        EXPECT_FALSE(index.forceGreetTopic(*travel, topic)); // ... but the procedure is Travel
        EXPECT_EQ(topic, kNullFormId);
    }

    TEST(SkyrimThievesPackage, aForceGreetPackageWithAnEmptyTopicInputGreetsNothing)
    {
        const PackageIndex index = packageIndex();
        const PackageRecord* none = index.package(kPackZero);
        ASSERT_NE(none, nullptr);
        EXPECT_EQ(none->mForceGreetTopic, kNullFormId);
        FormId topic = kNullFormId;
        EXPECT_FALSE(index.forceGreetTopic(*none, topic));
    }

    TEST(SkyrimThievesPackage, theTemplateItselfHasNoTopicToSay)
    {
        const PackageIndex index = packageIndex();
        FormId topic = kNullFormId;
        EXPECT_FALSE(index.forceGreetTopic(*index.package(kPackTemplateGreet), topic));
    }

    // ---- quest aliases
    // -----------------------------------------------------------------------------------------------

    TEST(SkyrimThievesAlias, anAliasKeepsThePackagesItGivesInTheirOrderAndNothingElseDoes)
    {
        Bytes alias;
        alias.append(sub4("ALST", Bytes().u32(5)));
        alias.append(sub4("ALID", Bytes().zstring("Brynjolf")));
        alias.append(sub4("FNAM", Bytes().u32(0)));
        alias.append(sub4("ALUA", Bytes().u32(kNpc)));
        alias.append(sub4("ALPC", Bytes().u32(kPackGreet)));
        alias.append(sub4("ALPC", Bytes().u32(kPackNotGreet)));
        alias.append(sub4("VTCK", Bytes().u32(0)));
        alias.append(sub4("ALED", Bytes()));
        Bytes other;
        other.append(sub4("ALST", Bytes().u32(6)));
        other.append(sub4("ALID", Bytes().zstring("Other")));
        other.append(sub4("FNAM", Bytes().u32(0)));
        other.append(sub4("ALUA", Bytes().u32(kNpc)));
        other.append(sub4("VTCK", Bytes().u32(0)));
        other.append(sub4("ALED", Bytes()));
        Bytes q;
        q.append(edid("TestQuest"));
        q.append(sub4("DNAM", Bytes().u16(1).u8(30).u8(0).u32(6)));
        q.append(sub4("NEXT", Bytes()));
        q.append(sub4("ANAM", Bytes().u32(7)));
        q.append(alias);
        q.append(other);
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("QUST"), kGrupTop, record4("QUST", 0, kQuest, q)));
        QuestRecords records;
        Content::Error error;
        ASSERT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), records, error))
            << error.mMessage;
        ASSERT_EQ(records.mQuests.size(), 1u);
        const QuestRecord& quest = records.mQuests.front();
        ASSERT_EQ(quest.mAliases.size(), 2u);
        EXPECT_EQ(quest.mAliases[0].mPackages, (std::vector<FormId>{ kPackGreet, kPackNotGreet }));
        EXPECT_TRUE(quest.mAliases[1].mPackages.empty());
    }

    // ---- the package brain
    // ---------------------------------------------------------------------------------------------

    class FakeSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord& p, FormId, FormId) override
        {
            return mFailing.count(p.mCommon.mId) == 0;
        }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        bool forceGreetTopic(const PackageRecord& p, FormId& out) const override
        {
            const auto it = mGreets.find(p.mCommon.mId);
            if (it == mGreets.end())
                return false;
            out = it->second;
            return true;
        }
        std::vector<const PackageRecord*> mList;
        std::set<FormId> mFailing;
        std::map<FormId, FormId> mGreets;
    };

    class FakeWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal&) override { return true; }
        bool cancelGoal(ActorHandle) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
    };

    struct BrainRig
    {
        std::vector<PackageRecord> mPackages;
        FakeSource mSource;
        FakeWriter mWriter;
        std::unique_ptr<PackageBrain> mBrain;
        struct Ask
        {
            WorldId mReference;
            FormId mBase;
            FormId mTopic;
        };
        std::vector<Ask> mAsks;

        BrainRig()
        {
            mPackages.reserve(4);
            for (const FormId id : { kPackGreet, kPackNotGreet })
            {
                PackageRecord p;
                p.mCommon.mId = id;
                p.mHasPkdt = true;
                mPackages.push_back(p);
            }
            mSource.mGreets[kPackGreet] = kTopicIntro;
            mBrain = std::make_unique<PackageBrain>(mSource);
            mBrain->setForceGreetHook([this](WorldId reference, FormId base, FormId topic) {
                mAsks.push_back({ reference, base, topic });
            });
            ActorInfo info;
            info.handle = { 77, 1 };
            info.base = kNpc;
            info.editorId = "Brynjolf";
            mBrain->onActorAdded(info);
        }

        void run(int evaluations)
        {
            for (int i = 0; i < evaluations; ++i)
                mBrain->update(1.1f, mWriter); // one evaluation each
        }
    };

    TEST(SkyrimThievesBrain, aChosenForceGreetPackageAsksForItsConversationAtOnce)
    {
        BrainRig rig;
        rig.mSource.mList = { &rig.mPackages[0], &rig.mPackages[1] };
        rig.run(3);
        ASSERT_FALSE(rig.mAsks.empty());
        EXPECT_EQ(rig.mAsks.front().mReference, 77u);
        EXPECT_EQ(rig.mAsks.front().mBase, kNpc);
        EXPECT_EQ(rig.mAsks.front().mTopic, kTopicIntro);
    }

    TEST(SkyrimThievesBrain, theRequestIsRepeatedEveryFewEvaluationsNotEveryOne)
    {
        BrainRig rig;
        rig.mSource.mList = { &rig.mPackages[0] };
        rig.run(2);
        EXPECT_EQ(rig.mAsks.size(), 1u);
        rig.run(kGreetAskEvaluations + 2);
        EXPECT_GE(rig.mAsks.size(), 2u);
        EXPECT_LE(rig.mAsks.size(), 3u);
    }

    TEST(SkyrimThievesBrain, aPackageThatIsNotForceGreetAsksForNothing)
    {
        BrainRig rig;
        rig.mSource.mList = { &rig.mPackages[1] };
        rig.run(12);
        EXPECT_TRUE(rig.mAsks.empty());
    }

    TEST(SkyrimThievesBrain, whenTheForceGreetPackageNoLongerPassesTheAskingStops)
    {
        BrainRig rig;
        rig.mSource.mList = { &rig.mPackages[0], &rig.mPackages[1] };
        rig.run(2);
        ASSERT_EQ(rig.mAsks.size(), 1u);
        rig.mSource.mFailing.insert(kPackGreet); // the greeting was said: its conditions fail now
        rig.run(20);
        EXPECT_EQ(rig.mAsks.size(), 1u);
    }

    TEST(SkyrimThievesBrain, withNoHookTheBrainRunsAsBefore)
    {
        BrainRig rig;
        rig.mBrain->setForceGreetHook({});
        rig.mSource.mList = { &rig.mPackages[0] };
        rig.run(8);
        EXPECT_TRUE(rig.mAsks.empty());
    }

    // ---- the conversation
    // ----------------------------------------------------------------------------------------------

    Bytes infoRecord(std::uint32_t id, const std::string& text, const std::vector<std::uint32_t>& links,
        const Bytes& conditions = Bytes())
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        for (const std::uint32_t link : links)
            b.append(sub4("TCLT", Bytes().u32(link)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        b.append(sub4("NAM1", Bytes().zstring(text)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        b.append(conditions);
        return record4("INFO", 0u, id, b);
    }

    Bytes topic(std::uint32_t id, const std::string& editor, const std::string& full, const std::string& subtype,
        const std::vector<Bytes>& infos, float priority)
    {
        Bytes children;
        for (const Bytes& i : infos)
            children.append(i);
        Bytes b;
        b.append(edid(editor));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(priority)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    Bytes failingConditions()
    {
        Bytes b;
        b.u8(0).u8(0).u8(0).u8(0).f32(1.0f); // GetDead == 1: never for a living speaker
        b.u16(46).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
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

        explicit Talk(bool introPasses = true)
        {
            Bytes dials;
            // a greeting subtype topic of higher priority: the forced topic must not pick it up
            dials.append(
                topic(kTopicOther, "Other", "", "HELO", { infoRecord(kSelf | 0x20, "Wrong greeting.", {}) }, 9.f));
            std::vector<Bytes> intro;
            intro.push_back(infoRecord(kSelf | 0x21, "A word, friend.", { kTopicChoice, kTopicChain },
                introPasses ? Bytes() : failingConditions()));
            dials.append(topic(kTopicIntro, "Intro", "", "CUST", intro, 1.f));
            dials.append(topic(kTopicChoice, "Choice", "I am sorry, what?", "CUST",
                { infoRecord(kSelf | 0x22, "You heard me.", { kTopicChain }) }, 1.f));
            // a topic with no name: a link to it is no choice, the speaker goes on with it (task 0271)
            dials.append(topic(kTopicChain, "Chain", "", "CUST",
                { infoRecord(kSelf | 0x23, "And here is your cut.", { kTopicTail }) }, 1.f));
            dials.append(
                topic(kTopicTail, "Tail", "Thanks.", "CUST", { infoRecord(kSelf | 0x24, "Anytime.", {}) }, 1.f));
            Bytes npc;
            npc.append(edid("Brynjolf"));
            Bytes acbs;
            acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
            npc.append(sub4("ACBS", acbs));
            npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
            Bytes race;
            race.append(edid("NordRace"));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, dials));
            bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, record4("NPC_", 0, kNpc, npc)));
            bytes.append(group4(Content::fourCC("RACE"), kGrupTop, record4("RACE", 0, kRaceNord, race)));
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
            s.mBase = kNpc;
            s.mReference = kNpc | 0x1000;
            return s;
        }
    };

    TEST(SkyrimThievesDialogue, theForcedGreetingIsAnInfoOfTheNamedTopicAndOfNoOther)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicIntro, turn, error)) << error;
        EXPECT_EQ(turn.text, "A word, friend.");
        EXPECT_TRUE(talk.mRuntime->open());
    }

    TEST(SkyrimThievesDialogue, theChoicesAreTheNamedTopicsItsInfoLinksToAndAnUnnamedLinkIsNotOneOfThem)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicIntro, turn, error)) << error;
        ASSERT_EQ(turn.topics.size(), 1u); // the unnamed link to the chain topic is no button
        EXPECT_EQ(turn.topics.front().text, "I am sorry, what?");
        EXPECT_EQ(turn.text, "A word, friend."); // and, with a named choice offered, it is not followed either
    }

    TEST(SkyrimThievesDialogue, aLinkWithNoNameIsFollowedByTheSpeakerWhenNothingElseIsOffered)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicIntro, turn, error)) << error;
        Turn reply;
        ASSERT_TRUE(talk.mRuntime->choose(turn.topics.front().id, reply, error)) << error;
        EXPECT_EQ(reply.text, "You heard me. And here is your cut.");
        ASSERT_EQ(reply.topics.size(), 1u);
        EXPECT_EQ(reply.topics.front().text, "Thanks.");
        Turn last;
        ASSERT_TRUE(talk.mRuntime->choose(reply.topics.front().id, last, error)) << error;
        EXPECT_EQ(last.text, "Anytime.");
    }

    TEST(SkyrimThievesDialogue, whenNoInfoOfTheTopicPassesThereIsNoForcedConversation)
    {
        Talk talk(false);
        Turn turn;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicIntro, turn, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(talk.mRuntime->open());
    }

    TEST(SkyrimThievesDialogue, aTopicThatDoesNotExistGivesNoConversation)
    {
        Talk talk;
        Turn turn;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kSelf | 0x3FF, turn, error));
    }

    // ---- the brain's asks in task 0264's ForceGreet
    // ----------------------------------------------------------------------

    TEST(SkyrimThievesForceGreet, theBrainsAskMakesOneRequestWithItsBaseAndTopicAndSamesAreAsksNotRequests)
    {
        ForceGreet fg;
        EXPECT_TRUE(fg.requestFromBrain(kNpc | 0x1000, kNpc, kTopicIntro));
        EXPECT_EQ(fg.pendingBase(), std::to_string(kNpc));
        EXPECT_EQ(fg.topicFor(kNpc | 0x1000), kTopicIntro);
        const std::uint32_t serial = fg.serial();
        EXPECT_FALSE(fg.requestFromBrain(kNpc | 0x1000, kNpc, kTopicIntro));
        EXPECT_EQ(fg.serial(), serial) << "the same actor's ask is no new request";
        EXPECT_TRUE(fg.fallsBackToPlain(kNpc | 0x1000));
    }

    TEST(SkyrimThievesForceGreet, aSceneRequestIsNotABrainOneAndAConversationsEndFreesABrainRequest)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(0x10, 0x20, kTopicIntro));
        EXPECT_FALSE(fg.fallsBackToPlain(0x10));
        ASSERT_TRUE(fg.requestFromBrain(0x11, 0x21, kTopicIntro));
        fg.began(0x11);
        EXPECT_EQ(fg.stateOf(0x11), ForceGreet::State::Open);
        fg.ended(0x11);
        EXPECT_EQ(fg.stateOf(0x11), ForceGreet::State::Done) << "forgotten: the brain may ask again";
        EXPECT_TRUE(fg.requestFromBrain(0x11, 0x21, kTopicIntro));
        fg.ended(0x10);
        EXPECT_EQ(fg.stateOf(0x10), ForceGreet::State::Failed) << "a scene's request still waits for finished()";
    }

    TEST(SkyrimThievesForceGreet, aBrainRequestNobodyOpensIsDroppedAfterAFewAsksSoAnotherCanBeServed)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.requestFromBrain(0x11, 0x21, kTopicIntro));
        for (std::uint32_t i = 0; i < ForceGreet::kBrainMaxAsks; ++i)
            EXPECT_FALSE(fg.requestFromBrain(0x11, 0x21, kTopicIntro));
        EXPECT_EQ(fg.stateOf(0x11), ForceGreet::State::Waiting);
        EXPECT_FALSE(fg.requestFromBrain(0x11, 0x21, kTopicIntro));
        EXPECT_EQ(fg.pendingBase(), "") << "dropped";
        EXPECT_EQ(fg.timeouts(), 1u);
        EXPECT_TRUE(fg.requestFromBrain(0x11, 0x21, kTopicIntro)) << "and the next ask is a new request";
    }

    // ---- conditions
    // -----------------------------------------------------------------------------------------------------

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t runOn = 0, std::uint8_t compare = 0,
        float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mCompare = compare;
        c.mComparison = value;
        return c;
    }

    struct CondFixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        CondFixture()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            mWorld.setSpeaker(s);
        }

        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
        static survey::Ctda onPlayer(survey::Ctda c)
        {
            c.mRunOn = 2;
            c.mReference = 0x14;
            return c;
        }
    };

    TEST(SkyrimThievesConditions, theMovementIntoANewSpaceIsAnsweredOnlyWhereThePlayersPlaceIsToldAndThenAlwaysZero)
    {
        CondFixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(358, 0, 0, 0, 0.f))); // a world that is not told: closed, as before
        f.mWorld.setPlayerPlace(true, false, kNullFormId, kSelf | 0x700);
        EXPECT_TRUE(f.mWorld.answers(cond(358, 0, 0, 0, 0.f)));
        EXPECT_TRUE(f.holds(cond(358, 0, 0, 0, 0.f)));
        EXPECT_FALSE(f.holds(cond(358, 0, 0, 0, 1.f)));
    }

    TEST(SkyrimThievesConditions, withNoPlayerPlaceToldTheConditionsOnThePlayerStayUnanswered)
    {
        CondFixture f;
        EXPECT_FALSE(f.mWorld.answers(CondFixture::onPlayer(cond(310, kSelf | 0x700))));
        EXPECT_FALSE(f.mWorld.answers(CondFixture::onPlayer(cond(300, 0))));
    }

    TEST(SkyrimThievesConditions, anExteriorPlaceAnswersTheWorldspaceAndNotTheCellOrTheInterior)
    {
        CondFixture f;
        f.mWorld.setPlayerPlace(true, false, kNullFormId, kSelf | 0x700);
        EXPECT_TRUE(f.holds(CondFixture::onPlayer(cond(310, kSelf | 0x700, 0, 0, 1.f))));
        EXPECT_FALSE(f.holds(CondFixture::onPlayer(cond(310, kSelf | 0x701, 0, 0, 1.f))));
        EXPECT_TRUE(f.holds(CondFixture::onPlayer(cond(300, 0, 0, 0, 0.f)))); // not in an interior
        EXPECT_FALSE(f.holds(CondFixture::onPlayer(cond(67, kSelf | 0x800, 0, 0, 1.f))));
    }

    TEST(SkyrimThievesConditions, anInteriorPlaceAnswersTheCellAndTheInteriorAndIsNoWorldspace)
    {
        CondFixture f;
        f.mWorld.setPlayerPlace(true, true, kSelf | 0x800, kNullFormId);
        EXPECT_TRUE(f.holds(CondFixture::onPlayer(cond(67, kSelf | 0x800, 0, 0, 1.f))));
        EXPECT_FALSE(f.holds(CondFixture::onPlayer(cond(67, kSelf | 0x801, 0, 0, 1.f))));
        EXPECT_TRUE(f.holds(CondFixture::onPlayer(cond(300, 0, 0, 0, 1.f))));
        EXPECT_FALSE(f.holds(CondFixture::onPlayer(cond(310, kSelf | 0x700, 0, 0, 1.f))));
    }

    TEST(SkyrimThievesConditions, thePlaceOfThePlayerDoesNotChangeWhatTheSpeakerAnswers)
    {
        CondFixture f;
        f.mWorld.setPlayerPlace(true, true, kSelf | 0x800, kNullFormId);
        // the speaker's own interior is unknown: its condition still fails closed
        EXPECT_FALSE(f.mWorld.answers(cond(300, 0, 0)));
    }
}
