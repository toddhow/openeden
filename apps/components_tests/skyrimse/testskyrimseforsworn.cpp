// Task 0337 (Skyrim side quest "The Forsworn Conspiracy", MS01): the paired kill move a quest script plays
// (`Actor.PlayIdleWithTarget`, Weylin's `SneakKillMove` on Margret) kills its target; any other idle does not. Written first
// and run red against the stub rule. Generated scripts and a fake game; no game data.
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/forsworn.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/survey.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kWeylin = 0x01000A01;
    constexpr FormId kMargret = 0x01000A02;
    constexpr FormId kKillIdle = 0x01000C01;
    constexpr FormId kOtherIdle = 0x01000C02;
    constexpr FormId kBase = 0x01000B00;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

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
        std::string nameOf(FormId form) const override
        {
            const auto it = mNames.find(form);
            return it != mNames.end() ? it->second : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, std::string> mNames;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool isDead(FormId actor) override { return mDead.count(actor) != 0; }
        bool killActor(FormId actor) override
        {
            mKills.push_back(actor);
            return true;
        }
        bool startCombatAt(FormId actor, FormId target) override
        {
            mFights.emplace_back(actor, target);
            return true;
        }
        bool startCombat(FormId actor) override
        {
            mFightsWithPlayer.push_back(actor);
            return true;
        }
        std::vector<std::pair<FormId, FormId>> mFights;
        std::vector<FormId> mFightsWithPlayer;
        std::set<FormId> mDead;
        std::vector<FormId> mKills;
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
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            mData.add(ref);
            ScriptBuilder actor("Actor");
            actor.parent("ObjectReference");
            actor.function("PlayIdleWithTarget", "Bool")
                .native()
                .param("akIdle", "Form")
                .param("akTarget", "ObjectReference");
            actor.function("StartCombat").native().param("akTarget", "ObjectReference");
            mData.add(actor);
            ScriptBuilder s("Probe");
            s.parent("Actor");
            s.autoProperty("Idle", "Form");
            s.autoProperty("Other", "ObjectReference");
            s.var("played", "Bool");
            s.function("Fight")
                .local("n", "None")
                .callMethod("StartCombat", Id("self"), Id("n"), { Id("::Other_var") });
            s.function("Play")
                .local("b", "Bool")
                .callMethod("PlayIdleWithTarget", Id("self"), Id("b"), { Id("::Idle_var"), Id("::Other_var") })
                .assign(Id("played"), Id("b"));
            mData.add(s);
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kWeylin] = "Actor";
            mData.mClasses[kMargret] = "Actor";
            mData.mClasses[kKillIdle] = "Form";
            mData.mClasses[kOtherIdle] = "Form";
            mData.mNames[kKillIdle] = "SneakKillMove";
            mData.mNames[kOtherIdle] = "IdleBowAttack";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            mHost->setPairedKillMoves(true);
            mHost->setActorFights(true);
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
        }
        void attachAndPlay(FormId idle, FormId target)
        {
            vmad::Script script;
            script.mName = "Probe";
            script.mProperties = { objProp("Idle", idle), objProp("Other", target) };
            ASSERT_EQ(mHost->attachReference(kWeylin, kBase, true, { std::move(script) }), 1u);
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
            ASSERT_TRUE(mHost->callFunction(kWeylin, "Probe", "Play"));
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        void attachAndFight(FormId target)
        {
            vmad::Script script;
            script.mName = "Probe";
            script.mProperties = { objProp("Idle", kKillIdle), objProp("Other", target) };
            ASSERT_EQ(mHost->attachReference(kWeylin, kBase, true, { std::move(script) }), 1u);
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
            ASSERT_TRUE(mHost->callFunction(kWeylin, "Probe", "Fight"));
            for (int i = 0; i < 3; ++i)
                mHost->tick(0.1f);
        }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimForswornKillMove, theNamesOfTheDataAreKillMoves)
    {
        EXPECT_TRUE(forsworn::isKillMoveName("SneakKillMove"));
        EXPECT_TRUE(forsworn::isKillMoveName("KillMove2HWB00"));
        EXPECT_TRUE(forsworn::isKillMoveName("pa_1HMKillMoveShortJ"));
        EXPECT_TRUE(forsworn::isKillMoveName("KILLMOVEBACKSTAB00")) << "no case";
        EXPECT_FALSE(forsworn::isKillMoveName("IdleBowAttack"));
        EXPECT_FALSE(forsworn::isKillMoveName("pa_Bash"));
        EXPECT_FALSE(forsworn::isKillMoveName(""));
        EXPECT_EQ(std::string(forsworn::kKey), "forsworn conspiracy");
        EXPECT_EQ(forsworn::kIntroQuest, 0xAF75Fu);
    }

    TEST(SkyrimForswornKillMove, aKillMoveAScriptPlaysKillsTheTargetNotTheActor)
    {
        Rig rig;
        rig.attachAndPlay(kKillIdle, kMargret);
        ASSERT_EQ(rig.mHooks.mKills.size(), 1u);
        EXPECT_EQ(rig.mHooks.mKills[0], kMargret);
    }

    TEST(SkyrimForswornKillMove, withTheOptionOffTheIdleIsTolerantAndKillsNobody)
    {
        Rig rig;
        rig.mHost->setPairedKillMoves(false);
        rig.attachAndPlay(kKillIdle, kMargret);
        EXPECT_TRUE(rig.mHooks.mKills.empty());
    }

    TEST(SkyrimForswornKillMove, anotherIdleKillsNobody)
    {
        Rig rig;
        rig.attachAndPlay(kOtherIdle, kMargret);
        EXPECT_TRUE(rig.mHooks.mKills.empty());
    }

    TEST(SkyrimForswornKillMove, aDeadTargetIsNotKilledAgain)
    {
        Rig rig;
        rig.mHooks.mDead.insert(kMargret);
        rig.attachAndPlay(kKillIdle, kMargret);
        EXPECT_TRUE(rig.mHooks.mKills.empty());
    }

    TEST(SkyrimForswornFight, aFightAScriptStartsAgainstAnotherActorGoesToTheCombatWithBothSides)
    {
        Rig rig;
        rig.attachAndFight(kMargret);
        ASSERT_EQ(rig.mHooks.mFights.size(), 1u);
        EXPECT_EQ(rig.mHooks.mFights[0].first, kWeylin);
        EXPECT_EQ(rig.mHooks.mFights[0].second, kMargret);
        EXPECT_TRUE(rig.mHooks.mFightsWithPlayer.empty());
    }

    TEST(SkyrimForswornFight, withTheOptionOffAFightAgainstAnotherActorIsIgnoredAsBefore)
    {
        Rig rig;
        rig.mHost->setActorFights(false);
        rig.attachAndFight(kMargret);
        EXPECT_TRUE(rig.mHooks.mFights.empty());
        EXPECT_TRUE(rig.mHooks.mFightsWithPlayer.empty());
    }

    // ---- a death in this session is dead for another reference's condition (the force-greet package of Eltrys) ----

    constexpr FormId kNpc = 0x01000200;
    constexpr FormId kRefLive = 0x01000500;
    constexpr FormId kRefDeadAtStart = 0x01000501;
    constexpr FormId kCell = 0x01000100;

    survey::Ctda getDeadOn(FormId reference)
    {
        survey::Ctda c;
        c.mFunction = 46; // GetDead
        c.mRunOn = 2; // another reference
        c.mReference = reference;
        c.mCompare = 0; // ==
        c.mComparison = 1.f;
        return c;
    }

    struct DeadFixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        DeadFixture()
        {
            ActorRecords ar;
            ActorNpc npc;
            npc.mCommon.mId = kNpc;
            ar.mNpcs.push_back(npc);
            mActors.add(ar);
            PlacedRefRecords r;
            PlacedEntry live;
            live.mId = kRefLive;
            live.mBase = kNpc;
            live.mCell = kCell;
            live.mFlags = PlacedFlags::IsActor;
            r.mRefs.push_back(live);
            PlacedEntry dead = live;
            dead.mId = kRefDeadAtStart;
            dead.mFlags = PlacedFlags::IsActor | PlacedFlags::Dead;
            r.mRefs.push_back(dead);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
        }
        bool holds(FormId reference)
        {
            const survey::Ctda c = getDeadOn(reference);
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimForswornDead, aReferenceTheRecordPlacesDeadIsDeadAndALivingOneIsNot)
    {
        DeadFixture f;
        EXPECT_TRUE(f.holds(kRefDeadAtStart));
        EXPECT_FALSE(f.holds(kRefLive));
    }

    TEST(SkyrimForswornDead, aReferenceTheSessionKilledIsDeadForTheConditionToo)
    {
        DeadFixture f;
        std::set<FormId> killed;
        f.mWorld.setDeadLookup([&killed](FormId ref) { return killed.count(ref) != 0; });
        EXPECT_FALSE(f.holds(kRefLive));
        killed.insert(kRefLive);
        EXPECT_TRUE(f.holds(kRefLive));
        EXPECT_TRUE(f.holds(kRefDeadAtStart));
    }

    // ---- a condition run on the reference the actor is LINKED to (run-on 4): Eltrys is linked to Weylin ----

    constexpr FormId kLinkKeyword = 0x01000700;

    survey::Ctda getDeadOnLinked(FormId keyword)
    {
        survey::Ctda c;
        c.mFunction = 46; // GetDead
        c.mRunOn = 4; // the linked reference
        c.mReference = keyword; // the keyword of the link, 0 for the plain link
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    TEST(SkyrimForswornLinkedRef, aConditionRunOnTheLinkedReferenceAsksTheReferenceTheActorIsLinkedTo)
    {
        DeadFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = kNpc;
        speaker.mReference = kRefLive; // the actor whose package this is
        f.mWorld.setSpeaker(speaker);
        f.mWorld.setLinkedRefLookup([](FormId reference, FormId keyword, FormId& out) {
            if (reference != kRefLive || keyword != 0)
                return false;
            out = kRefDeadAtStart; // the plain link goes to a reference that starts dead
            return true;
        });
        const survey::Ctda c = getDeadOnLinked(0);
        conditions::Evaluator e;
        EXPECT_TRUE(f.mWorld.answers(c));
        EXPECT_TRUE(e.evaluate(std::span<const survey::Ctda>(&c, 1), f.mWorld));
    }

    TEST(SkyrimForswornLinkedRef, withNoLinkTheConditionIsNotAnsweredAndFailsClosed)
    {
        DeadFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = kNpc;
        speaker.mReference = kRefLive;
        f.mWorld.setSpeaker(speaker);
        f.mWorld.setLinkedRefLookup([](FormId, FormId, FormId&) { return false; });
        const survey::Ctda c = getDeadOnLinked(0);
        conditions::Evaluator e;
        EXPECT_FALSE(e.evaluate(std::span<const survey::Ctda>(&c, 1), f.mWorld));
    }

    TEST(SkyrimForswornLinkedRef, aKeywordedLinkIsNotThePlainOne)
    {
        DeadFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = kNpc;
        speaker.mReference = kRefLive;
        f.mWorld.setSpeaker(speaker);
        f.mWorld.setLinkedRefLookup([](FormId reference, FormId keyword, FormId& out) {
            if (reference != kRefLive || keyword != kLinkKeyword)
                return false;
            out = kRefDeadAtStart;
            return true;
        });
        conditions::Evaluator e;
        const survey::Ctda plain = getDeadOnLinked(0);
        const survey::Ctda keyed = getDeadOnLinked(kLinkKeyword);
        EXPECT_FALSE(e.evaluate(std::span<const survey::Ctda>(&plain, 1), f.mWorld));
        EXPECT_TRUE(e.evaluate(std::span<const survey::Ctda>(&keyed, 1), f.mWorld));
    }
}
