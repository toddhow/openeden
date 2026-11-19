// Task 0347 (docs/tasks/0347-skyrim-thieves-guild-4.md): TG01 "Taking Care of Business", the debts. Groups:
//  * SkyrimTg01bScriptFight: a script's Actor.StartCombat that comes before the combat's records are read must not leave
//                            the actor without fighting numbers for good.
// Generated fighters; no game data.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/story.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"
#include <string>

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorInfo;
    using openeden::sdk::game::ClipRequest;
    using openeden::sdk::game::ClipTiming;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::DamageReport;
    using openeden::sdk::game::SubjectState;
    using openeden::sdk::game::SweepHit;
    using openeden::sdk::game::SweepTarget;
    using openeden::sdk::game::SweepRequest;
    using openeden::sdk::game::WorldId;

    constexpr WorldId kBase = 100;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            FighterProfile npc;
            npc.mHealthMax = 100.f;
            npc.mStaminaMax = 100.f;
            npc.mUnarmedDamage = 4.f;
            npc.mUnarmedReach = 96.f;
            npc.mAi.mKnown = true;
            npc.mAi.mConfidence = 2;
            mProfile = npc;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
        }
        bool combatReady() override { return mReadyNow; }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            if (!mReadyNow || base != kBase)
                return false; // a real data object answers nothing until its records are read
            out = mProfile;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view id) override { return id == "ironsword" ? &mSword : nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        WeaponRecord mSword;
        bool mReadyNow = false;
        FighterProfile mProfile;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
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
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
                out = { 100.0, 100.0, 5.0, 0.f, 90.f, 100.f };
            else
                out = { 100.0, 0.0, 5.0, 0.f, 0.f, 0.f };
            return true;
        }
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    TEST(SkyrimTg01bScriptFight, aScriptStartBeforeTheRecordsAreReadDoesNotSpoilTheActorForGood)
    {
        FakeData data;
        SkyrimCombat combat;
        FakeWriter writer;
        combat.setData(&data);
        combat.setCombatAi(true);
        const ActorHandle keerava{ 10, 1 };
        combat.onActorAdded(infoOf(10, kBase, "Keerava"));
        // the first ask comes from a script before the records are read: refused, as it must be
        EXPECT_FALSE(combat.scriptStartCombat(keerava));
        EXPECT_EQ(combat.aiEngaged(), 0u);
        // the records arrive; the same script asks again (the brawl of a later minute)
        data.mReadyNow = true;
        combat.combatUpdate(0.1f, writer);
        EXPECT_TRUE(combat.scriptStartCombat(keerava)) << "the first refusal must not have fixed her as having no numbers";
        EXPECT_EQ(combat.aiEngaged(), 1u);
        EXPECT_TRUE(combat.scriptInCombat(keerava));
    }

    TEST(SkyrimTg01bBrawl, aScriptsConfidenceReachesTheFightersNumbersBeforeOrAfterTheyAreRead)
    {
        FakeData data;
        data.mReadyNow = true;
        SkyrimCombat combat;
        FakeWriter writer;
        combat.setData(&data);
        combat.setCombatAi(true);
        const ActorHandle early{ 10, 1 };
        const ActorHandle late{ 11, 1 };
        combat.onActorAdded(infoOf(10, kBase, "Keerava"));
        combat.onActorAdded(infoOf(11, kBase, "Bersi"));
        EXPECT_EQ(combat.aiConfidenceOf(early), -1) << "not read yet";
        combat.setConfidence(late, 9); // a script sets it before the actor is read: kept, and cut to 4
        combat.combatUpdate(0.1f, writer);
        ASSERT_TRUE(combat.scriptStartCombat(early));
        ASSERT_TRUE(combat.scriptStartCombat(late));
        EXPECT_EQ(combat.aiConfidenceOf(early), 2) << "the record's own number until a script says otherwise";
        EXPECT_EQ(combat.aiConfidenceOf(late), 4);
        combat.setConfidence(early, 4); // DGIntimidateQuest: OpponentREF.SetAv("Confidence", 4) after the read
        EXPECT_EQ(combat.aiConfidenceOf(early), 4);
    }

    TEST(SkyrimTg01bBrawl, aBleedingActorIsToldAndAScriptThatRestoresItsHealthStandsItUp)
    {
        FakeData data;
        data.mReadyNow = true;
        data.mProfile.mHealthMax = 3.f;
        data.mProfile.mEssential = true;
        SkyrimCombat combat;
        FakeWriter writer;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        combat.setEssentialBleedout(true);
        const ActorHandle keerava{ 10, 1 };
        combat.onActorAdded(infoOf(10, kBase, "Keerava"));
        for (int i = 0; i < 5; ++i)
            combat.combatUpdate(0.1f, writer);
        combat.onPlayerAttack(false);
        for (int i = 0; i < 6; ++i)
            combat.combatUpdate(0.1f, writer);
        ASSERT_FALSE(writer.mSweeps.empty());
        SweepHit hit;
        hit.target = SweepTarget::Actor;
        hit.actor = keerava;
        hit.distance = 20.f;
        combat.onSweepResult(CombatSubject::ofPlayer(), writer.mSweeps.back().second.token, &hit, 1);
        combat.combatUpdate(0.1f, writer);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(combat.fighterView(CombatSubject::of(keerava), view));
        EXPECT_TRUE(view.mBleedout);
        EXPECT_FLOAT_EQ(view.mHealth, 1.f);
        // a script's health write of 1 or less leaves it down; more stands it up
        EXPECT_TRUE(combat.setFighterHealth(CombatSubject::of(keerava), 1.f));
        ASSERT_TRUE(combat.fighterView(CombatSubject::of(keerava), view));
        EXPECT_TRUE(view.mBleedout);
        EXPECT_TRUE(combat.setFighterHealth(CombatSubject::of(keerava), 10.f));
        ASSERT_TRUE(combat.fighterView(CombatSubject::of(keerava), view));
        EXPECT_FALSE(view.mBleedout);
        EXPECT_FALSE(view.mDead);
    }

    TEST(SkyrimTg01bBrawl, anActorThatCollapsesIntoBleedoutLeavesTheFightAndCannotBeStartedAgainUntilItStandsUp)
    {
        FakeData data;
        data.mReadyNow = true;
        data.mProfile.mHealthMax = 3.f;
        data.mProfile.mEssential = true;
        SkyrimCombat combat;
        FakeWriter writer;
        combat.setData(&data);
        combat.setTestWeapon("IronSword");
        combat.setCombatAi(true);
        combat.setEssentialBleedout(true);
        const ActorHandle bersi{ 10, 1 };
        combat.onActorAdded(infoOf(10, kBase, "Bersi"));
        for (int i = 0; i < 5; ++i)
            combat.combatUpdate(0.1f, writer);
        ASSERT_TRUE(combat.scriptStartCombat(bersi));
        ASSERT_EQ(combat.aiEngaged(), 1u);
        combat.onPlayerAttack(false);
        for (int i = 0; i < 6; ++i)
            combat.combatUpdate(0.1f, writer);
        ASSERT_FALSE(writer.mSweeps.empty());
        SweepHit hit;
        hit.target = SweepTarget::Actor;
        hit.actor = bersi;
        hit.distance = 20.f;
        combat.onSweepResult(CombatSubject::ofPlayer(), writer.mSweeps.back().second.token, &hit, 1);
        combat.combatUpdate(0.1f, writer);
        combat.combatUpdate(0.1f, writer);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(combat.fighterView(CombatSubject::of(bersi), view));
        ASSERT_TRUE(view.mBleedout);
        EXPECT_EQ(combat.aiEngaged(), 0u) << "a bleeding actor does not go on fighting (the forced greeting waits for it)";
        EXPECT_FALSE(combat.scriptStartCombat(bersi)) << "nor does it start again while it is down";
        EXPECT_TRUE(combat.setFighterHealth(CombatSubject::of(bersi), 10.f));
        EXPECT_TRUE(combat.scriptStartCombat(bersi)) << "standing up again, it can fight";
    }

    TEST(SkyrimTg01bScriptFight, aScriptStartAgainstAnActorTheDataNeverKnowsIsStillRefused)
    {
        FakeData data;
        data.mReadyNow = true;
        SkyrimCombat combat;
        combat.setData(&data);
        combat.setCombatAi(true);
        combat.onActorAdded(infoOf(11, 999, "Nobody"));
        EXPECT_FALSE(combat.scriptStartCombat(ActorHandle{ 11, 1 }));
        EXPECT_EQ(combat.aiEngaged(), 0u);
    }

    // ---- a quest the story manager starts again begins clean ---------------------------------------------------
    constexpr FormId kBrawlQuest = 0x01000900;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class NoScripts final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string&, std::vector<std::uint8_t>&) override { return false; }
        std::string formClass(FormId form) const override { return form == kBrawlQuest ? "Quest" : std::string(); }
    };

    struct QuestRig
    {
        QuestRig()
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Brawl")));
            quest.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kBrawlQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        NoScripts mData;
        QuestIndex mIndex;
        QuestState mState;
        std::unique_ptr<PapyrusHost> mHost;
    };

    TEST(SkyrimTg01bBrawl, aQuestTheStoryManagerStartsAgainForgetsItsEarlierStages)
    {
        QuestRig rig;
        rig.mHost->setStoryEventSink([](const StoryEvent&) { return std::vector<FormId>{ kBrawlQuest }; });
        std::vector<FormId> reset;
        rig.mHost->setQuestReset([&](FormId quest) { reset.push_back(quest); }); // the package brain's copy is told too
        StoryEvent event;
        event.mType = "SCPT";
        EXPECT_EQ(rig.mHost->raiseStoryEvent(event), 1u); // the first brawl
        rig.mState.setStage(rig.mIndex, kBrawlQuest, 15);
        EXPECT_TRUE(rig.mState.stageDone(kBrawlQuest, 15));
        rig.mHost->stopQuestForTest(kBrawlQuest); // its last stage stops it
        EXPECT_EQ(rig.mHost->raiseStoryEvent(event), 1u); // the second brawl, with another opponent
        EXPECT_TRUE(rig.mState.running(kBrawlQuest));
        EXPECT_FALSE(rig.mState.stageDone(kBrawlQuest, 15)) << "stage 15 must be settable again";
        EXPECT_FLOAT_EQ(rig.mState.stage(kBrawlQuest), 0.f);
        ASSERT_EQ(reset.size(), 2u);
        EXPECT_EQ(reset[1], kBrawlQuest);
    }

    TEST(SkyrimTg01bBrawl, aQuestThatIsStillRunningIsNotResetByAnotherEvent)
    {
        QuestRig rig;
        rig.mHost->setStoryEventSink([](const StoryEvent&) { return std::vector<FormId>{ kBrawlQuest }; });
        StoryEvent event;
        event.mType = "SCPT";
        EXPECT_EQ(rig.mHost->raiseStoryEvent(event), 1u);
        rig.mState.setStage(rig.mIndex, kBrawlQuest, 15);
        EXPECT_EQ(rig.mHost->raiseStoryEvent(event), 0u) << "running: not started again";
        EXPECT_TRUE(rig.mState.stageDone(kBrawlQuest, 15));
    }
}
