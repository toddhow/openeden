// Task 0264 (docs/tasks/0264-skyrim-companions.md): the start of the Companions questline. Groups:
//  * SkyrimCompanionsForceGreet:  the pure force-greet bookkeeping (request, open, close, timeout, the binding);
//  * SkyrimCompanionsPackage:     PDTO of a ForceGreet package;
//  * SkyrimCompanionsDialogue:    a conversation that opens with the first INFO of a package's topic;
//  * SkyrimCompanionsNatives:     Actor.StartCombat / StopCombat / ResetHealthAndLimbs / GetCombatState / GetAV and
//                                 OnEnterBleedout in the Papyrus host;
//  * SkyrimCompanionsCombat:      the combat's script entry points and the essential actor's bleedout.
// Generated plugins, scripts and fighters; no game data. Written for the task; see its stage log for the red evidence.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/forcegreet.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cstdint>
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
    using openeden::sdk::game::ActorInfo;
    using openeden::sdk::game::ClipEnd;
    using openeden::sdk::game::ClipRequest;
    using openeden::sdk::game::ClipTiming;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::DamageReport;
    using openeden::sdk::game::FurniturePose;
    using openeden::sdk::game::GoalResult;
    using openeden::sdk::game::SubjectState;
    using openeden::sdk::game::SweepHit;
    using openeden::sdk::game::SweepRequest;
    using openeden::sdk::game::SweepTarget;
    using openeden::sdk::game::WorldId;
    using Turn = openeden::sdk::game::DialogueTurn;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    // ---- ForceGreet
    // ---------------------------------------------------------------------------------------------------

    constexpr FormId kActorRef = 0x0001A68F;
    constexpr FormId kActorBase = 0x0001A68E;
    constexpr FormId kTopic = 0x000A3E50;

    TEST(SkyrimCompanionsForceGreet, aRequestWaitsForItsConversationAndNamesTheTopicAndTheBase)
    {
        ForceGreet fg;
        EXPECT_EQ(fg.pendingBase(), "");
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        EXPECT_EQ(fg.stateOf(kActorRef), ForceGreet::State::Waiting);
        EXPECT_EQ(fg.topicFor(kActorRef), kTopic);
        EXPECT_EQ(fg.pendingBase(), std::to_string(kActorBase));
        EXPECT_EQ(fg.serial(), 1u);
        EXPECT_FALSE(fg.finished(kActorRef));
    }

    TEST(SkyrimCompanionsForceGreet, anOpenConversationIsNoLongerPendingAndItsEndFinishesThePackage)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        fg.began(kActorRef);
        EXPECT_EQ(fg.stateOf(kActorRef), ForceGreet::State::Open);
        EXPECT_EQ(fg.pendingBase(), "");
        EXPECT_EQ(fg.topicFor(kActorRef), kNullFormId) << "an open conversation does not ask again";
        EXPECT_FALSE(fg.finished(kActorRef));
        fg.ended(kActorRef);
        EXPECT_TRUE(fg.finished(kActorRef));
        EXPECT_EQ(fg.stateOf(kActorRef), ForceGreet::State::Done) << "a finished request is forgotten";
    }

    TEST(SkyrimCompanionsForceGreet, aConversationThatCouldNotOpenFailsTheRequestAtOnce)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        fg.ended(kActorRef); // the begin was refused: ended without began
        EXPECT_TRUE(fg.finished(kActorRef));
    }

    TEST(SkyrimCompanionsForceGreet, aRequestThatNeverOpensTimesOutAfterAFixedNumberOfChecks)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        std::uint32_t checks = 0;
        while (!fg.finished(kActorRef) && checks < 100000)
            ++checks;
        EXPECT_EQ(checks, ForceGreet::kTimeoutChecks);
        EXPECT_EQ(fg.timeouts(), 1u);
    }

    TEST(SkyrimCompanionsForceGreet, anOpenConversationNeverTimesOut)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        fg.began(kActorRef);
        for (std::uint32_t i = 0; i < ForceGreet::kTimeoutChecks * 3; ++i)
            ASSERT_FALSE(fg.finished(kActorRef));
        EXPECT_EQ(fg.timeouts(), 0u);
    }

    TEST(SkyrimCompanionsForceGreet, aSecondRequestOfTheSameActorOrANullOneIsRefusedAndCancelForgetsOne)
    {
        ForceGreet fg;
        EXPECT_FALSE(fg.request(0, kActorBase, kTopic));
        EXPECT_FALSE(fg.request(kActorRef, kActorBase, kNullFormId));
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        EXPECT_FALSE(fg.request(kActorRef, kActorBase, kTopic));
        fg.cancel(kActorRef);
        EXPECT_EQ(fg.pendingBase(), "");
        EXPECT_TRUE(fg.finished(kActorRef)) << "no request means nothing to wait for";
        EXPECT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        EXPECT_EQ(fg.serial(), 2u);
    }

    TEST(SkyrimCompanionsForceGreet, theTableIsBounded)
    {
        ForceGreet fg;
        for (FormId i = 1; i <= ForceGreet::kMaxRequests; ++i)
            ASSERT_TRUE(fg.request(i, i + 1000, kTopic));
        EXPECT_FALSE(fg.request(0x7000, 1, kTopic));
    }

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& ns) override
        {
            mNamespace = ns.name;
            mVersion = ns.version;
            for (const auto& f : ns.functions)
                mFunctions[std::string(f.name)] = f;
            openeden::sdk::DeclareResult r;
            r.ok = true;
            return r;
        }
        std::string mNamespace;
        openeden::sdk::Version mVersion{};
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    TEST(SkyrimCompanionsForceGreet, theBindingsNamespaceAnswersThePendingBaseAndTheSerial)
    {
        ForceGreet fg;
        StubBindings stub;
        ASSERT_TRUE(fg.declare(stub).ok);
        EXPECT_EQ(stub.mNamespace, "skyrimse.forcegreet");
        ASSERT_EQ(stub.mFunctions.count("pending_base"), 1u);
        ASSERT_EQ(stub.mFunctions.count("pending_serial"), 1u);
        const auto base = [&] { return std::string(stub.mFunctions["pending_base"].handler->call({}).value.text); };
        const auto serial = [&] { return stub.mFunctions["pending_serial"].handler->call({}).value.integer; };
        EXPECT_EQ(base(), "");
        EXPECT_EQ(serial(), 0);
        ASSERT_TRUE(fg.request(kActorRef, kActorBase, kTopic));
        EXPECT_EQ(base(), std::to_string(kActorBase));
        EXPECT_EQ(serial(), 1);
        for (const auto& [name, f] : stub.mFunctions)
            EXPECT_EQ(f.access, openeden::sdk::BindingAccess::Read) << name << " must be Read only";
    }

    // ---- PDTO of a package
    // --------------------------------------------------------------------------------------------

    TEST(SkyrimCompanionsPackage, theFirstTopicOfAPackagesDataIsItsForceGreetTopic)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("ForceGreetPlayer")));
        pack.append(sub4("PKDT", Bytes().u32(0).u8(18).u8(0).u8(0x02).u8(0x61).u16(0).u16(0xFE)));
        pack.append(sub4("PDTO", Bytes().u32(0).u32(0x01000000 | 0x3E50)));
        pack.append(sub4("PDTO", Bytes().u32(0).u32(0))); // a later empty one (the event sections) changes nothing
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("PACK"), 0, record4("PACK", 0u, 0x01000010u, pack)));
        PackageRecords records;
        Content::Error error;
        ASSERT_TRUE(parsePackageRecordsBytes(bytes.str(), "test.esp", order(), PackageLimits(), records, error))
            << error.mMessage;
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, 0x01000000u | 0x3E50u);
        EXPECT_EQ(records.mOddSubrecords, 0u);
    }

    TEST(SkyrimCompanionsPackage, aPackageWithNoPackageDataHasNoTopic)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("Plain")));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("PACK"), 0, record4("PACK", 0u, 0x01000011u, pack)));
        PackageRecords records;
        Content::Error error;
        ASSERT_TRUE(parsePackageRecordsBytes(bytes.str(), "test.esp", order(), PackageLimits(), records, error));
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, kNullFormId);
    }

    TEST(SkyrimCompanionsPackage, aPackageDataOfTheWrongSizeIsOddAndKeepsNoTopic)
    {
        Bytes pack;
        pack.append(sub4("EDID", Bytes().zstring("Odd")));
        pack.append(sub4("PDTO", Bytes().u32(0)));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("PACK"), 0, record4("PACK", 0u, 0x01000012u, pack)));
        PackageRecords records;
        Content::Error error;
        ASSERT_TRUE(parsePackageRecordsBytes(bytes.str(), "test.esp", order(), PackageLimits(), records, error));
        ASSERT_EQ(records.mPackages.size(), 1u);
        EXPECT_EQ(records.mPackages[0].mForceGreetTopic, kNullFormId);
        EXPECT_EQ(records.mOddSubrecords, 1u);
    }

    // ---- a conversation opened by a topic
    // ------------------------------------------------------------------------------

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kTopicBack = kSelf | 0x310; // the forced greeting's topic
    constexpr FormId kTopicLearn = kSelf | 0x311; // a choice
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicOther = kSelf | 0x320;

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        Bytes mConditions;
        std::string mText = "Hello.";
        std::string mPrompt;
        std::vector<std::uint32_t> mLinks;
    };

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        for (const std::uint32_t link : s.mLinks)
            b.append(sub4("TCLT", Bytes().u32(link)));
        Bytes trdt;
        trdt.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", trdt));
        b.append(sub4("NAM1", Bytes().zstring(s.mText)));
        b.append(sub4("NAM2", Bytes().u8(0)));
        if (!s.mPrompt.empty())
            b.append(sub4("RNAM", Bytes().zstring(s.mPrompt)));
        b.append(s.mConditions);
        return record4("INFO", 0u, s.mId, b);
    }

    Bytes topic(std::uint32_t id, const std::string& editor, const std::string& full, const std::string& subtype,
        const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring(editor)));
        if (!full.empty())
            b.append(sub4("FULL", Bytes().zstring(full)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, 7, children));
        return record;
    }

    Bytes talkPlugin()
    {
        Bytes dials;
        // the forced greeting's topic: a first INFO that needs a failing condition, a second that passes and links
        InfoSpec blocked;
        blocked.mId = kSelf | 0x30;
        blocked.mText = "Never said.";
        blocked.mConditions = ctda(59, 0, 99.0f, kQuest); // GetStageDone(quest, 99) == 1... never true
        InfoSpec back;
        back.mId = kSelf | 0x31;
        back.mText = "Of course. How are you in a battle?";
        back.mLinks = { kTopicLearn };
        dials.append(topic(kTopicBack, "BackToThePlayer", "", "CUST", { blocked, back }));
        InfoSpec learn;
        learn.mId = kSelf | 0x32;
        learn.mText = "Vilkas will get started on that.";
        learn.mPrompt = "I have much to learn.";
        dials.append(topic(kTopicLearn, "TeachMe", "I have much to learn.", "CUST", { learn }));
        InfoSpec hello;
        hello.mId = kSelf | 0x33;
        hello.mText = "Hello there.";
        dials.append(topic(kTopicHello, "Hello", "", "HELO", { hello }));
        InfoSpec other;
        other.mId = kSelf | 0x34;
        other.mText = "Dragons.";
        dials.append(topic(kTopicOther, "Rumors", "Rumors", "CUST", { other }));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), 0, dials));
        Bytes npc;
        npc.append(sub4("EDID", Bytes().zstring("Kodlak")));
        npc.append(sub4("FULL", Bytes().zstring("Kodlak Whitemane")));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        npc.append(sub4("ACBS", acbs));
        npc.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        bytes.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, kNpc, npc)));
        Bytes race;
        race.append(sub4("EDID", Bytes().zstring("NordRace")));
        bytes.append(group4(Content::fourCC("RACE"), 0, record4("RACE", 0, kRaceNord, race)));
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

        Talk()
        {
            const Bytes bytes = talkPlugin();
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

    TEST(SkyrimCompanionsDialogue, theConversationOpensWithTheFirstInfoOfTheTopicThatPassesAndOffersItsLinks)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicBack, turn, error)) << error;
        EXPECT_EQ(turn.text, "Of course. How are you in a battle?");
        ASSERT_EQ(turn.topics.size(), 1u);
        EXPECT_EQ(turn.topics[0].text, "I have much to learn.");
        EXPECT_FALSE(turn.endsConversation);
        EXPECT_EQ(turn.speakerName, "Kodlak Whitemane");
    }

    TEST(SkyrimCompanionsDialogue, choosingTheLinkGivesItsResponseAndEndsTheConversation)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicBack, turn, error));
        Turn next;
        ASSERT_TRUE(talk.mRuntime->choose(turn.topics[0].id, next, error)) << error;
        EXPECT_EQ(next.text, "Vilkas will get started on that.");
        EXPECT_TRUE(next.endsConversation);
    }

    TEST(SkyrimCompanionsDialogue, aTopicWhoseInfosAllFailOpensNothingAndSaysWhy)
    {
        Talk talk;
        Turn turn;
        std::string error;
        EXPECT_FALSE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicOther + 0x1000, turn, error));
        EXPECT_FALSE(error.empty());
        // and the ordinary conversation is unharmed by the failed one
        Turn ordinary;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), ordinary, error)) << error;
        EXPECT_EQ(ordinary.text, "Hello there.");
    }

    TEST(SkyrimCompanionsDialogue, anOrdinaryConversationAfterAForcedOneListsTheTopLevelTopicsAgain)
    {
        Talk talk;
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->beginForcedTopic(Talk::speaker(), kTopicBack, turn, error));
        talk.mRuntime->end();
        Turn ordinary;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), ordinary, error)) << error;
        bool rumors = false;
        for (const auto& t : ordinary.topics)
            rumors = rumors || t.text == "Rumors";
        EXPECT_TRUE(rumors);
    }

    // ---- the Papyrus natives
    // --------------------------------------------------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kPlayerRef = 0x14;

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
        bool isScene(FormId form) const override { return mScenes.count(form) != 0; }
        FormId questOfScene(FormId scene) const override
        {
            const auto it = mSceneQuests.find(scene);
            return it != mSceneQuests.end() ? it->second : 0;
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
        std::set<FormId> mScenes;
        std::map<FormId, FormId> mSceneQuests;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool startCombat(FormId actor) override
        {
            mLog.push_back("start " + std::to_string(actor));
            return true;
        }
        bool stopCombat(FormId actor) override
        {
            mLog.push_back("stop " + std::to_string(actor));
            return true;
        }
        bool resetHealth(FormId actor) override
        {
            mLog.push_back("reset " + std::to_string(actor));
            return true;
        }
        bool inCombat(FormId actor) override
        {
            mLog.push_back("state " + std::to_string(actor));
            return mFighting;
        }
        bool mFighting = true;
        std::vector<std::string> mLog;
    };

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

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
            actor.function("StartCombat").native().param("akTarget", "ObjectReference");
            actor.function("StopCombat").native();
            actor.function("ResetHealthAndLimbs").native();
            actor.function("EvaluatePackage").native();
            actor.function("GetCombatState", "Int").native();
            actor.function("GetAV", "Float").native().param("asValueName", "String");
            mData.add(actor);
            ScriptBuilder questClass("Quest");
            questClass.parent("Form");
            mData.add(questClass);
            ScriptBuilder scene("Scene");
            scene.parent("Form");
            scene.function("GetOwningQuest", "Quest").native();
            mData.add(scene);
            ScriptBuilder probe("Probe");
            probe.parent("Actor");
            probe.autoProperty("Target", "ObjectReference");
            probe.var("state", "Int");
            probe.var("bleeds", "Int");
            probe.var("av", "Float");
            probe.function("Fight")
                .local("n", "None")
                .callMethod("StartCombat", Id("self"), Id("n"), { Id("::Target_var") });
            probe.function("FightNobody").local("n", "None").callMethod("StartCombat", Id("self"), Id("n"), { Nil() });
            probe.function("Stop").local("n", "None").callMethod("StopCombat", Id("self"), Id("n"));
            probe.function("Reset").local("n", "None").callMethod("ResetHealthAndLimbs", Id("self"), Id("n"));
            probe.function("Eval").local("n", "None").callMethod("EvaluatePackage", Id("self"), Id("n"));
            probe.function("State")
                .local("v", "Int")
                .callMethod("GetCombatState", Id("self"), Id("v"))
                .assign(Id("state"), Id("v"));
            probe.function("Av")
                .local("v", "Float")
                .callMethod("GetAV", Id("self"), Id("v"), { Str("confidence") })
                .assign(Id("av"), Id("v"));
            probe.function("OnEnterBleedout").iadd(Id("bleeds"), Id("bleeds"), Int(1));
            mData.add(probe);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActor] = "Actor";
            mData.mClasses[kPlayerRef] = "Actor";
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
            script.mProperties = { objProp("Target", kPlayerRef) };
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

    TEST(SkyrimCompanionsNatives, startingCombatAgainstThePlayerAsksThePackToFightHim)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Fight"));
        ASSERT_EQ(rig.mHooks.mLog.size(), 1u);
        EXPECT_EQ(rig.mHooks.mLog[0], "start " + std::to_string(kActor));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimCompanionsNatives, startingCombatAgainstNobodyOrAnotherActorAsksNothing)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("FightNobody"));
        EXPECT_TRUE(rig.mHooks.mLog.empty());
    }

    TEST(SkyrimCompanionsNatives, stoppingCombatAndResettingHealthReachThePack)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Stop"));
        ASSERT_TRUE(rig.call("Reset"));
        ASSERT_EQ(rig.mHooks.mLog.size(), 2u);
        EXPECT_EQ(rig.mHooks.mLog[0], "stop " + std::to_string(kActor));
        EXPECT_EQ(rig.mHooks.mLog[1], "reset " + std::to_string(kActor));
    }

    TEST(SkyrimCompanionsNatives, theCombatStateIsOneWhileThePackSaysTheActorFightsAndZeroAfter)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("State"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "state"), 1.0);
        rig.mHooks.mFighting = false;
        ASSERT_TRUE(rig.call("State"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "state"), 0.0);
    }

    TEST(SkyrimCompanionsNatives, evaluatePackageIsAQuietNoOp)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Eval"));
        EXPECT_TRUE(rig.mHooks.mLog.empty());
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimCompanionsNatives, getAVReadsTheSameValueAsGetActorValue)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Av"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "av"), 0.0) << "no value is known: zero, as GetActorValue";
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimCompanionsNatives, withoutHooksTheCombatNativesAreTolerant)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        EXPECT_TRUE(rig.call("Fight"));
        EXPECT_TRUE(rig.call("Stop"));
        EXPECT_TRUE(rig.call("Reset"));
        EXPECT_TRUE(rig.call("State"));
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "state"), 0.0);
    }

    TEST(SkyrimCompanionsNatives, aBleedoutEventReachesTheScriptsOfTheActor)
    {
        Rig rig;
        rig.mHost->onActorBleedout(kActor);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "bleeds"), 1.0);
        rig.mHost->onActorBleedout(kActor + 1); // another actor: nothing
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kActor, "Probe", "bleeds"), 1.0);
    }

    // ---- an engaged sitter stands up
    // ------------------------------------------------------------------------------------

    class BrainSource final : public PackageSource, public FurnitureSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            const auto it = mLists.find(base);
            if (it == mLists.end())
                return false;
            out = it->second;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Eat; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            if (reference != 0x500)
                return false;
            x = 1000;
            y = 1000;
            z = 0;
            return true;
        }
        void slotsNear(FormId, double, double, double, double, FurniturePose pose, double, double, double,
            std::vector<FurnitureSlot>& out) override
        {
            out.clear();
            for (const FurnitureSlot& slot : mSlots)
                if (slot.mPose == pose)
                    out.push_back(slot);
        }
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::vector<FurnitureSlot> mSlots;
    };

    class BrainWriter final : public openeden::sdk::game::ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const openeden::sdk::game::ActorGoal&) override { return true; }
        bool cancelGoal(ActorHandle) override { return true; }
        bool setHeading(ActorHandle, float) override { return true; }
        bool useFurniture(ActorHandle, const openeden::sdk::game::FurnitureUse&) override
        {
            ++mUses;
            return true;
        }
        bool leaveFurniture(ActorHandle, double, double, double) override
        {
            ++mLeaves;
            return true;
        }
        bool placeFurniture(ActorHandle, double, double, double, float) override { return true; }
        openeden::sdk::game::ActorClock clock() const override { return { 12.f, 0, true }; }
        int mUses = 0;
        int mLeaves = 0;
    };

    struct SeatedRig
    {
        BrainSource mSource;
        PackageRecord mPackage;
        BrainWriter mWriter;
        std::unique_ptr<PackageBrain> mBrain;
        ActorHandle mHandle{ 0x10, 1 };
        SeatedRig()
        {
            mPackage.mCommon.mId = 3;
            mPackage.mHasPkdt = true;
            mPackage.mType = 18;
            mPackage.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 600 });
            mSource.mLists[0x00013BA7] = { &mPackage };
            FurnitureSlot slot;
            slot.mKey = 77;
            slot.mPose = FurniturePose::Sit;
            slot.mX = 1000;
            slot.mY = 1000;
            slot.mEntryX = 1000;
            slot.mEntryY = 936;
            slot.mClip = "chair_idlebasevar1";
            mSource.mSlots.push_back(slot);
            mBrain = std::make_unique<PackageBrain>(mSource);
            mBrain->setFurnitureSource(&mSource);
            ActorInfo info;
            info.handle = mHandle;
            info.base = 0x00013BA7;
            info.editorId = "Sitter";
            info.x = 1000;
            info.y = 800;
            mBrain->onActorAdded(info);
            for (int i = 0; i < 3; ++i)
                mBrain->update(0.1f, mWriter);
            mBrain->onGoalFinished(mHandle, openeden::sdk::game::GoalResult::Arrived);
            for (int i = 0; i < 5; ++i)
                mBrain->update(0.1f, mWriter);
        }
        void run(int n)
        {
            for (int i = 0; i < n; ++i)
                mBrain->update(0.1f, mWriter);
        }
    };

    TEST(SkyrimCompanionsStand, anEngagedSitterGetsUpOnceTheBrainIsTold)
    {
        SeatedRig rig;
        ASSERT_TRUE(rig.mBrain->view(0x10)->mSeated);
        rig.mBrain->forceStand(0x10);
        rig.mBrain->onConversation(0x10, true); // what the combat AI's Suspend does
        rig.run(10);
        EXPECT_EQ(rig.mWriter.mLeaves, 1);
        EXPECT_FALSE(rig.mBrain->view(0x10)->mSeated);
    }

    TEST(SkyrimCompanionsStand, withoutTheForceASitterStaysSeatedAsInAConversation)
    {
        SeatedRig rig;
        rig.mBrain->onConversation(0x10, true);
        rig.run(30);
        EXPECT_EQ(rig.mWriter.mLeaves, 0);
        EXPECT_TRUE(rig.mBrain->view(0x10)->mSeated);
    }

    TEST(SkyrimCompanionsStand, theForceDoesNothingToAnActorThatIsNotSeatedOrNotKnown)
    {
        SeatedRig rig;
        rig.mBrain->forceStand(0x999);
        rig.run(3);
        EXPECT_EQ(rig.mWriter.mLeaves, 0);
        EXPECT_TRUE(rig.mBrain->view(0x10)->mSeated);
    }

    // ---- the combat
    // ------------------------------------------------------------------------------------------------------

    constexpr WorldId kFighterBase = 100;

    class CombatFakeData final : public CombatData
    {
    public:
        CombatFakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 100.f;
            npc.mSkills[0] = 50;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 4;
            mProfiles[kFighterBase] = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }
        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class CombatFakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
            return true;
        }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CompanionsCombat : ::testing::Test
    {
        CombatFakeData mData;
        SkyrimCombat mCombat;
        CombatFakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        std::vector<ActorHandle> mBleedouts;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.setBleedoutSink([this](ActorHandle h) { mBleedouts.push_back(h); });
            mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
            mWriter.mStates[10] = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 100.0, 100.0, 5.0, 0.f, 90.f, 100.f };
        }
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mCombat.combatUpdate(dt, mWriter);
                mCombat.drainAiMoves(mMoves);
            }
        }
        void playerHits()
        {
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6; ++i)
                run(1);
            ASSERT_FALSE(mWriter.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mA;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps.back().second.token, &hit, 1);
            run(1);
        }
    };

    TEST_F(CompanionsCombat, aScriptStartMakesTheActorFightThePlayerAndAScriptStopEndsIt)
    {
        run(3);
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_FALSE(mCombat.scriptInCombat(mA));
        ASSERT_TRUE(mCombat.scriptStartCombat(mA));
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        EXPECT_TRUE(mCombat.scriptInCombat(mA));
        EXPECT_TRUE(mCombat.scriptStartCombat(mA)) << "already fighting: still true";
        EXPECT_EQ(mCombat.aiEngaged(), 1u);
        ASSERT_TRUE(mCombat.scriptStopCombat(mA));
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
        EXPECT_FALSE(mCombat.scriptInCombat(mA));
        EXPECT_FALSE(mCombat.scriptStopCombat(mA)) << "nothing left to stop";
    }

    TEST_F(CompanionsCombat, aStoppedFightDoesNotStartAgainAtOnce)
    {
        mData.mProfiles[kFighterBase].mAi.mHostileOnSight = true;
        mData.mProfiles[kFighterBase].mAi.mAggression = 2;
        mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
        run(20);
        ASSERT_EQ(mCombat.aiEngaged(), 1u);
        ASSERT_TRUE(mCombat.scriptStopCombat(mA));
        run(20); // two seconds
        EXPECT_EQ(mCombat.aiEngaged(), 0u);
    }

    TEST_F(CompanionsCombat, theScriptEntryPointsAreInertWithTheAiOffOrForAnUnknownActor)
    {
        EXPECT_FALSE(mCombat.scriptStartCombat(ActorHandle{ 99, 1 }));
        EXPECT_FALSE(mCombat.scriptResetHealth(ActorHandle{ 99, 1 }));
        EXPECT_FALSE(mCombat.scriptStopCombat(ActorHandle{ 99, 1 }));
        mCombat.setCombatAi(false);
        EXPECT_FALSE(mCombat.scriptStartCombat(mA));
    }

    TEST_F(CompanionsCombat, anEssentialActorThatABlowWouldKillCollapsesInsteadAndTellsTheSinkOnce)
    {
        mCombat.setEssentialBleedout(true);
        mData.mProfiles[kFighterBase].mHealthMax = 3.f;
        mData.mProfiles[kFighterBase].mEssential = true;
        mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
        run(5);
        playerHits();
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_TRUE(mWriter.mDeaths.empty()) << "an essential actor does not die";
        EXPECT_FLOAT_EQ(mWriter.mDamage.back().healthAfter, 1.f);
        ASSERT_EQ(mBleedouts.size(), 1u);
        EXPECT_EQ(mBleedouts[0], mA);
        playerHits();
        EXPECT_TRUE(mWriter.mDeaths.empty());
        EXPECT_EQ(mBleedouts.size(), 1u) << "the collapse is told once";
    }

    TEST_F(CompanionsCombat, withTheSettingOffAnEssentialActorDiesAsBefore)
    {
        mData.mProfiles[kFighterBase].mHealthMax = 3.f;
        mData.mProfiles[kFighterBase].mEssential = true;
        mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
        run(5);
        playerHits();
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_TRUE(mBleedouts.empty());
    }

    TEST_F(CompanionsCombat, anOrdinaryActorStillDiesWithTheSettingOn)
    {
        mCombat.setEssentialBleedout(true);
        mData.mProfiles[kFighterBase].mHealthMax = 3.f;
        mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
        run(5);
        playerHits();
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_TRUE(mBleedouts.empty());
    }

    TEST_F(CompanionsCombat, resettingHealthHealsAnActorAndEndsItsBleedout)
    {
        mCombat.setEssentialBleedout(true);
        mData.mProfiles[kFighterBase].mHealthMax = 3.f;
        mData.mProfiles[kFighterBase].mEssential = true;
        mCombat.onActorAdded(infoOf(10, kFighterBase, "Vilkas"));
        run(5);
        playerHits();
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mHealth, 1.f);
        ASSERT_TRUE(mCombat.scriptResetHealth(mA));
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mHealth, 3.f);
        EXPECT_FALSE(view.mDead);
        run(30); // the player's swing recovers
        mWriter.mSweeps.clear();
        playerHits(); // it can collapse again, and says so again
        EXPECT_EQ(mBleedouts.size(), 2u);
    }

    // ---- scenes: their quest and their fragment scripts across a save
    // ----------------------------------------------------

    constexpr FormId kScene = 0x01000D01;

    TEST(SkyrimCompanionsScene, aSceneFragmentScriptReachesTheQuestTheSceneBelongsTo)
    {
        using namespace PexAsm;
        Rig rig;
        rig.mData.mScenes.insert(kScene);
        rig.mData.mSceneQuests[kScene] = kQuestA;
        rig.mData.mClasses[kScene] = "";
        // a scene script is the scene's own: GetOwningQuest() is the quest, whose id the script reads back
        ScriptBuilder fragment("SF_Test");
        fragment.parent("Scene");
        fragment.var("quest", "Int");
        fragment.function("Fragment_1")
            .local("q", "Quest")
            .local("id", "Int")
            .callMethod("GetOwningQuest", Id("self"), Id("q"))
            .callMethod("GetFormID", Id("q"), Id("id"))
            .assign(Id("quest"), Id("id"));
        rig.mData.add(fragment);
        vmad::Vmad v;
        v.mHasSceneFragments = true;
        vmad::Script script;
        script.mName = "SF_Test";
        v.mScripts.push_back(script);
        ASSERT_EQ(rig.mHost->runSceneFragment(kScene, v, "SF_Test", "Fragment_1"), 1u);
        rig.tick();
        EXPECT_EQ(
            rig.mHost->numberVariable(kScene, "SF_Test", "quest"), std::optional<double>(static_cast<double>(kQuestA)));
        EXPECT_TRUE(rig.mErrors.empty());
        if (::testing::Test::HasFailure())
            for (const std::string& line : rig.mLogs)
                std::cerr << "  log: " << line << std::endl;
    }

    TEST(SkyrimCompanionsScene, aSaveMadeAfterASceneFragmentRanIsRestoredNotRefused)
    {
        using namespace PexAsm;
        Rig rig;
        rig.mData.mScenes.insert(kScene);
        ScriptBuilder fragment("SF_Test2");
        fragment.parent("Form");
        fragment.var("ran", "Int");
        fragment.function("Fragment_1").iadd(Id("ran"), Id("ran"), Int(1));
        rig.mData.add(fragment);
        vmad::Vmad v;
        v.mHasSceneFragments = true;
        vmad::Script script;
        script.mName = "SF_Test2";
        v.mScripts.push_back(script);
        ASSERT_EQ(rig.mHost->runSceneFragment(kScene, v, "SF_Test2", "Fragment_1"), 1u);
        rig.tick();
        const openeden::script::SaveBlob blob = rig.mHost->serialiseState();
        ASSERT_FALSE(blob.bytes.empty());
        EXPECT_TRUE(rig.mHost->restoreState(blob)) << "a scene is something the game still has";
    }
}
