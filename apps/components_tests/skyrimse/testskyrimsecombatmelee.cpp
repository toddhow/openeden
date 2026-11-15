// Task 0171 (milestone M-S5, stage S5-3): the Skyrim pack's melee rules (game_skyrimse/combat.hpp), tested through the
// contract with a fake data source and a recording writer. Every number is worked out by hand in a comment from the
// formulas of task 0168's stats (combat_stats.hpp) with the default GMSTs: damage = WEAP damage x min(1.5, 1 + skill /
// 200) (x 2 for a power attack), after armour x (1 - mitigation / 100); stamina = 20 + weight x 1 (x 2 for a power
// attack, fPowerAttackStaminaPenalty). The weapon is an "Iron Sword" of damage 7, weight 9, reach 1.0 (reach 141 units).
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue; // One-handed: skill slot 0

            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mSkills[0] = 50; // multiplier 1 + 50 / 200 = 1.25
            attacker.mUnarmedDamage = 4.f;
            attacker.mUnarmedReach = 96.f;
            mProfiles[kAttackerBase] = attacker;

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15); // multiplier 1.075
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
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return mSettings; }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            if (mRefusePlay)
                return false;
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            if (mRefuseSweep)
                return false;
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
        bool faceHeading(CombatSubject subject, float heading) override
        {
            mFacings.push_back({ subject, heading });
            return true;
        }
        // Task 0171 stage S5-3b: the player's swing timing, as the sidecar of 1HM_AttackRight gives it (defaultmale.txt:
        // HitFrame 0.367, 1.287 s long). Ready by default so that the older tests run as before.
        bool clipTiming(std::string_view clip, bool, ClipTiming& out) override
        {
            ++mTimingAsks;
            mTimingClips.emplace_back(clip);
            out = {};
            if (!mTimingReady)
                return false;
            out = mTiming;
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return mHavePlayer;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }

        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::vector<std::pair<CombatSubject, float>> mFacings;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        bool mHavePlayer = true;
        bool mRefusePlay = false;
        bool mRefuseSweep = false;
        bool mTimingReady = true;
        ClipTiming mTiming{ true, 0.367f, 1.287f };
        int mTimingAsks = 0;
        std::vector<std::string> mTimingClips;
    };

    ActorInfo info(WorldId reference, std::uint32_t session, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, session };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token, ClipSlot slot = ClipSlot::Attack)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    SweepHit hitOn(ActorHandle actor, float distance = 20.f)
    {
        SweepHit h;
        h.target = SweepTarget::Actor;
        h.actor = actor;
        h.distance = distance;
        return h;
    }

    struct CombatMelee : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        // A hit event, one frame to turn it into a sweep request, the result, one frame to resolve it.
        void swing(CombatSubject attacker, std::uint32_t token, std::vector<SweepHit> hits,
            ClipSlot slot = ClipSlot::Attack)
        {
            mCombat.onClipEvent(hitEvent(attacker, token, slot));
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.onSweepResult(attacker, token, hits.data(), hits.size());
            mCombat.combatUpdate(0.016f, mWriter);
        }
    };

    TEST_F(CombatMelee, aHitFrameAsksForASweepWithTheWeaponsReachAndSpendsStamina)
    {
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 5));
        EXPECT_TRUE(mWriter.mSweeps.empty()); // nothing is requested from inside an event
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        const SweepRequest& r = mWriter.mSweeps[0].second;
        EXPECT_TRUE(mWriter.mSweeps[0].first == CombatSubject::of(mA));
        EXPECT_EQ(r.token, 5u);
        EXPECT_FLOAT_EQ(r.reach, 141.f); // reach 1.0 x fCombatDistance 141
        EXPECT_FLOAT_EQ(r.halfAngle, kStrikeHalfAngle);
        EXPECT_DOUBLE_EQ(r.x, 100.0);
        EXPECT_DOUBLE_EQ(r.y, 200.0);
        EXPECT_FLOAT_EQ(r.heading, 0.25f);
        EXPECT_GT(r.heightAbove, 0.f);
        // Stamina 100 - (20 + 9 x 1) = 71.
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mStamina, 71.f);
        EXPECT_FLOAT_EQ(view.mStaminaMax, 100.f);
    }

    TEST_F(CombatMelee, theDamageUsesTheSkillAndTheTargetsArmourAndTheHealthFollows)
    {
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        // 7 x 1.25 = 8.75 before armour; 20 percent off: 8.75 x 0.8 = 7.0; health 50 - 7 = 43.
        EXPECT_FALSE(d.attackerIsPlayer);
        EXPECT_FALSE(d.victimIsPlayer);
        EXPECT_TRUE(d.attacker == mA);
        EXPECT_TRUE(d.victim == mB);
        EXPECT_NEAR(d.amount, 7.0f, 1e-4f);
        EXPECT_NEAR(d.healthAfter, 43.0f, 1e-4f);
        EXPECT_FLOAT_EQ(d.healthMax, 50.f);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), view));
        EXPECT_NEAR(view.mHealth, 43.0f, 1e-4f);
        EXPECT_FALSE(view.mDead);
        // A hit reaction clip on the victim, not on the attacker.
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_TRUE(mWriter.mClips[0].mSubject == CombatSubject::of(mB));
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_RecoilRight");
        EXPECT_EQ(mCombat.hitsResolved(), 1u);
        // The trace line carries both numbers.
        bool found = false;
        for (const std::string& line : mTrace)
            if (line.find("before armour 8.750 after armour 7.000") != std::string::npos
                && line.find("health 50.000 -> 43.000") != std::string::npos)
                found = true;
        EXPECT_TRUE(found);
    }

    TEST_F(CombatMelee, aPowerAttackDoublesTheDamageAndTheStaminaCost)
    {
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        // 8.75 x (1 + fPowerAttackDefaultBonus 1.0) = 17.5; x 0.8 = 14; health 50 - 14 = 36.
        EXPECT_NEAR(mWriter.mDamage[0].amount, 14.0f, 1e-4f);
        EXPECT_NEAR(mWriter.mDamage[0].healthAfter, 36.0f, 1e-4f);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mStamina, 100.f - 2.f * 29.f); // fPowerAttackStaminaPenalty 2.0 x (20 + 9)
    }

    TEST_F(CombatMelee, staminaNeverGoesBelowZero)
    {
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 10 + i, ClipSlot::PowerAttack));
            mCombat.combatUpdate(0.f, mWriter);
        }
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mStamina, 0.f); // 100 - 58 - 58 clamps, then stays
    }

    TEST_F(CombatMelee, healthReachingZeroReportsTheDeathOnceAndTheDeadTakeNoMoreHits)
    {
        mData.mProfiles[kVictimBase].mHealthMax = 5.f;
        mCombat.onActorRemoved(mB);
        mCombat.onActorAdded(info(20, 2, kVictimBase, "Victim"));
        mB = { 20, 2 };
        mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthAfter, 0.f); // 5 - 7.0 clamps at 0
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_TRUE(mWriter.mDeaths[0] == mB);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), view));
        EXPECT_TRUE(view.mDead);
        // No hit reaction for the dead: the clip asked for is the stagger-back that lays it down, as a Death-slot clip
        // (the core holds its last pose).
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Death);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_StaggerBackMedium");
        // A second hit on the body: no damage, no second death.
        swing(CombatSubject::of(mA), 6, { hitOn(mB) });
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths.size(), 1u);
    }

    TEST_F(CombatMelee, aSweepThatFindsOnlyTheWorldOrNobodyIsAMiss)
    {
        SweepHit wall;
        wall.target = SweepTarget::World;
        swing(CombatSubject::of(mA), 5, { wall });
        swing(CombatSubject::of(mA), 6, {});
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_EQ(mCombat.hitsResolved(), 0u);
    }

    TEST_F(CombatMelee, aSwingHurtsOnlyTheNearestValidTargetAndSkipsItselfAndTheWorld)
    {
        mCombat.onActorAdded(info(30, 1, kVictimBase, "Other"));
        SweepHit wall;
        wall.target = SweepTarget::World;
        // The attacker itself first (a core bug would do that), then a wall, then B and then another actor.
        swing(CombatSubject::of(mA), 5, { hitOn(mA, 0.f), wall, hitOn(mB, 10.f), hitOn(ActorHandle{ 30, 1 }, 30.f) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(mWriter.mDamage[0].victim == mB);
    }

    TEST_F(CombatMelee, aResultWithNoMatchingRequestIsRefusedAndCounted)
    {
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::of(mA), 99, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.refusedSweepResults(), 1u);
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatMelee, aSweepTheCoreRefusesIsNotWaitedFor)
    {
        mWriter.mRefuseSweep = true;
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 5));
        mCombat.combatUpdate(0.016f, mWriter);
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::of(mA), 5, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.refusedSweepResults(), 1u);
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatMelee, anActorWithNoRecordsNeverFightsAndNeverHasHealthToLose)
    {
        mCombat.onActorAdded(info(40, 1, 999, "Nobody"));
        mWriter.mStates[40] = { 0.0, 0.0, 0.0, 0.f, 0.f, 0.f };
        mCombat.onClipEvent(hitEvent(CombatSubject::of(ActorHandle{ 40, 1 }), 3));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mWriter.mSweeps.empty());
        swing(CombatSubject::of(mA), 5, { hitOn(ActorHandle{ 40, 1 }) });
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatMelee, anNpcHittingThePlayerReadsTheEnginesHealthAndReportsTheNewOne)
    {
        // The engine says the player has 90 of 100. No armour: 8.75, 90 - 8.75 = 81.25. No reaction clip for the player.
        swing(CombatSubject::of(mA), 5, { SweepHit{ SweepTarget::Player, {}, 0, 0, 0, 30.f } });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_TRUE(d.victimIsPlayer);
        EXPECT_NEAR(d.amount, 8.75f, 1e-4f);
        EXPECT_NEAR(d.healthAfter, 81.25f, 1e-4f);
        EXPECT_FLOAT_EQ(d.healthMax, 100.f);
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_TRUE(mWriter.mDeaths.empty());
    }

    TEST_F(CombatMelee, whenTheEngineHasNoHealthForThePlayerThePacksStandInIsUsed)
    {
        mWriter.mPlayerState.healthMax = 0.f;
        mWriter.mPlayerState.health = 0.f;
        swing(CombatSubject::of(mA), 5, { SweepHit{ SweepTarget::Player, {}, 0, 0, 0, 30.f } });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].healthAfter, 100.f - 8.75f, 1e-4f);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthMax, 100.f);
    }

    TEST_F(CombatMelee, thePlayerSwingsOnTheTimerOfTheSidecarAndHitsAfterTheHitFrameTime)
    {
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter); // clock 0.1: the swing starts
        EXPECT_TRUE(mWriter.mSweeps.empty());
        mCombat.combatUpdate(0.2f, mWriter); // elapsed 0.2 < 0.367
        EXPECT_TRUE(mWriter.mSweeps.empty());
        mCombat.combatUpdate(0.2f, mWriter); // elapsed 0.4 >= 0.367
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        EXPECT_TRUE(mWriter.mSweeps[0].first == CombatSubject::ofPlayer());
        EXPECT_FLOAT_EQ(mWriter.mSweeps[0].second.heading, 1.f);
        mCombat.combatUpdate(0.2f, mWriter);
        EXPECT_EQ(mWriter.mSweeps.size(), 1u); // one hit a swing
        // Result: the player (skill 15: 7 x 1.075 = 7.525) hits the victim (20 percent): 6.02; health 50 - 6.02.
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
        mCombat.combatUpdate(0.f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(mWriter.mDamage[0].attackerIsPlayer);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 6.02f, 1e-3f);
        EXPECT_NEAR(mWriter.mDamage[0].healthAfter, 43.98f, 1e-3f);
        // The stamina of the player: 100 - 29.
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), view));
        EXPECT_FLOAT_EQ(view.mStamina, 71.f);
        // A second attack while the swing runs (1.3 s long) is ignored; after it ends a new one starts.
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.2f, mWriter); // elapsed 1.0: still swinging, the pending request is dropped
        mCombat.combatUpdate(0.6f, mWriter); // elapsed 1.6 >= 1.3: ended
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter); // a new swing begins (counted)
        EXPECT_EQ(mCombat.swingsRequested(), 2u);
    }

    TEST_F(CombatMelee, theAutomaticPlayerSwingTimerStartsAtOneSecondAndRepeatsByItsPeriod)
    {
        mCombat.setTestPlayerSwing(4.f);
        mCombat.combatUpdate(0.9f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
        mCombat.combatUpdate(0.2f, mWriter); // clock 1.1 >= 1
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
        mCombat.combatUpdate(3.8f, mWriter); // clock 4.9 < 5.0
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
        mCombat.combatUpdate(0.3f, mWriter); // clock 5.2 >= 5.1 (the next swing is 1.1 + 4)
        EXPECT_EQ(mCombat.swingsRequested(), 2u);
        mCombat.setTestPlayerSwing(-1.f);
        EXPECT_NO_THROW(mCombat.combatUpdate(100.f, mWriter));
        EXPECT_EQ(mCombat.swingsRequested(), 2u);
    }

    TEST_F(CombatMelee, aHostileActorInReachFacesThePlayerAndAttacksThenWaitsForItsCooldown)
    {
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mCombat.onActorAdded(info(10, 1, kAttackerBase, "Attacker")); // the placement is seen again: now a hostile
        EXPECT_EQ(mCombat.hostiles(), 1u);
        // Player at (0, 0); the attacker at (100, 200): 223 away, reach 141 + 22 - 4 = 159: too far.
        mCombat.combatUpdate(0.1f, mWriter);
        EXPECT_TRUE(mWriter.mClips.empty());
        // Now at (100, 100): 100 away: in reach. It turns to face (-100, -100): atan2(dx, dy) = -3 pi / 4.
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Attack);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_AttackRight");
        ASSERT_EQ(mWriter.mFacings.size(), 1u);
        EXPECT_NEAR(mWriter.mFacings[0].second, -2.35619449f, 1e-5f);
        // Still swinging, then cooling down: no second clip for 2.5 s even after the clip finishes.
        mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips[0].mToken, ClipEnd::Completed);
        mCombat.combatUpdate(2.0f, mWriter);
        EXPECT_EQ(mWriter.mClips.size(), 1u);
        mCombat.combatUpdate(0.6f, mWriter);
        EXPECT_EQ(mWriter.mClips.size(), 2u);
    }

    TEST_F(CombatMelee, healthAndStaminaComeBackAtTheRacesRateAndSlowerInCombat)
    {
        mData.mProfiles[kAttackerBase].mStaminaRegenPercent = 10.f; // percent of the maximum a second
        mData.mProfiles[kVictimBase].mHealthRegenPercent = 5.f;
        swing(CombatSubject::of(mA), 5, { hitOn(mB) }); // two updates of 0.016 s; stamina 71, victim health 43
        SkyrimCombat::FighterView attacker;
        SkyrimCombat::FighterView victim;
        // The first update spends the stamina (the fighter is not ready before it, so it has not regenerated), the second
        // regenerates 100 x 10 / 100 x 0.016 x fCombatStaminaRegenRateMult 0.35 = 0.056 in combat.
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), attacker));
        EXPECT_NEAR(attacker.mStamina, 71.056f, 1e-3f);
        // One more second in combat: +3.5 stamina; the health mult in combat is fCombatHealthRegenRateMult 0: no health.
        mCombat.combatUpdate(1.f, mWriter);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), attacker));
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), victim));
        EXPECT_NEAR(attacker.mStamina, 74.556f, 1e-3f);
        EXPECT_NEAR(victim.mHealth, 43.f, 1e-4f);
        // Eleven seconds on it is out of combat: full rate (50 x 5 / 100 = 2.5 a second) and the maximum is the cap.
        mCombat.combatUpdate(11.f, mWriter);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mB), victim));
        EXPECT_FLOAT_EQ(victim.mHealth, 50.f);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), attacker));
        EXPECT_FLOAT_EQ(attacker.mStamina, 100.f);
    }

    TEST_F(CombatMelee, anUnarmedSwingUsesTheRacesDamageAndReachAndItsOwnStaminaCost)
    {
        mCombat.setTestWeapon("");
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 5));
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mSweeps[0].second.reach, 96.f); // the race's unarmed reach
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), view));
        EXPECT_FLOAT_EQ(view.mStamina, 100.f - 22.f); // fActionPointsAttackUnarmed
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::of(mA), 5, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 3.2f, 1e-4f); // 4 x 0.8
    }

    TEST_F(CombatMelee, withNoDataNothingIsRequestedAndEventsAreOnlyCounted)
    {
        SkyrimCombat bare;
        bare.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
        bare.onClipEvent(hitEvent(CombatSubject::of(mA), 1));
        bare.onPlayerAttack(false);
        for (int i = 0; i < 20; ++i)
            bare.combatUpdate(0.5f, mWriter);
        EXPECT_TRUE(mWriter.mSweeps.empty());
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_EQ(bare.refusedEvents(), 1u);
    }

    TEST_F(CombatMelee, aRemovedActorIsForgotten)
    {
        mCombat.onActorRemoved(mB);
        SkyrimCombat::FighterView view;
        EXPECT_FALSE(mCombat.fighterView(CombatSubject::of(mB), view));
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    // ---- stage S5-3b: the player's swing timing from the sidecar ----------------------------------------------------

    TEST_F(CombatMelee, thePlayerCannotSwingUntilTheTimingIsReadyAndThenUsesTheSidecarsNumbers)
    {
        mWriter.mTimingReady = false;
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
        EXPECT_TRUE(mWriter.mSweeps.empty());
        EXPECT_GE(mWriter.mTimingAsks, 1);
        ASSERT_FALSE(mWriter.mTimingClips.empty());
        EXPECT_EQ(mWriter.mTimingClips[0], "1HM_AttackRight"); // the clip of the attack slot
        // The sidecar arrives: the swing starts at the next press and hits at 0.367 s.
        mWriter.mTimingReady = true;
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter); // the swing starts
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
        mCombat.combatUpdate(0.3f, mWriter); // elapsed 0.3 < 0.367
        EXPECT_TRUE(mWriter.mSweeps.empty());
        mCombat.combatUpdate(0.1f, mWriter); // elapsed 0.4
        EXPECT_EQ(mWriter.mSweeps.size(), 1u);
    }

    TEST_F(CombatMelee, theSwingFollowsWhateverTheSidecarSaysNotAConstant)
    {
        mWriter.mTiming = { true, 0.9f, 2.0f };
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter); // starts
        mCombat.combatUpdate(0.5f, mWriter); // elapsed 0.5
        mCombat.combatUpdate(0.3f, mWriter); // elapsed 0.8: past the old 0.367, before 0.9
        EXPECT_TRUE(mWriter.mSweeps.empty());
        mCombat.combatUpdate(0.2f, mWriter); // elapsed 1.0 >= 0.9
        EXPECT_EQ(mWriter.mSweeps.size(), 1u);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.6f, mWriter); // elapsed 1.6 < 2.0: still swinging, press dropped
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
        mCombat.combatUpdate(0.5f, mWriter); // elapsed 2.1: ended
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 2u);
    }

    TEST_F(CombatMelee, aClipWithNoHitFrameIsNoSwing)
    {
        mWriter.mTiming = { false, 0.f, 1.5f };
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
        EXPECT_TRUE(mWriter.mSweeps.empty());
    }

    TEST_F(CombatMelee, theAutomaticTestSwingWaitsForTheTimingToo)
    {
        mWriter.mTimingReady = false;
        mCombat.setTestPlayerSwing(4.f);
        mCombat.combatUpdate(1.5f, mWriter);
        mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
        mWriter.mTimingReady = true;
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
    }

    TEST_F(CombatMelee, aTimingThatNeverArrivesIsReportedOnceAfterFiveSeconds)
    {
        mWriter.mTimingReady = false;
        mCombat.setTestPlayerSwing(1.f);
        for (int i = 0; i < 20; ++i)
            mCombat.combatUpdate(0.5f, mWriter); // ten seconds
        EXPECT_EQ(mCombat.playerTimingTimeouts(), 1u);
        EXPECT_EQ(mCombat.swingsRequested(), 0u);
        int lines = 0;
        for (const std::string& line : mTrace)
            if (line.find("player swing timing never arrived") != std::string::npos)
                ++lines;
        EXPECT_EQ(lines, 1);
    }

    TEST_F(CombatMelee, thePowerAttackAsksForTheTimingOfThePowerClip)
    {
        mCombat.onPlayerAttack(true);
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_FALSE(mWriter.mTimingClips.empty());
        // Both attack clips were asked for at the first update (the read starts early), and the press asks for the power one.
        EXPECT_EQ(mWriter.mTimingClips.front(), "1HM_AttackRight");
        EXPECT_EQ(mWriter.mTimingClips.back(), "1HM_AttackPower");
    }

    TEST_F(CombatMelee, theReadOfThePlayersAttackTimingStartsAtTheFirstUpdateAndOnlyOnce)
    {
        EXPECT_EQ(mWriter.mTimingAsks, 0);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.mTimingAsks, 2); // the one-handed attack and power attack clips
        mCombat.combatUpdate(0.016f, mWriter);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.mTimingAsks, 2); // nothing asks again until a swing wants the numbers
    }
}
