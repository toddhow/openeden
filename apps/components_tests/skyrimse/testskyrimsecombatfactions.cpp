// Task 0197: who fights whom. The pure rules of combat_factions.hpp, and the AI's target choice through the contract
// with a fake data source and writer (relations, followers, guards, assistance, sight, retargeting).
//
// docs/tasks/0197-skyrim-npc-combat-and-factions.md
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_factions.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using Factions::Party;
    using R = ActorRelation;

    // ---- pure rules ------------------------------------------------------------------------------------------------

    Party npc(std::uint8_t aggression = 0)
    {
        Party p;
        p.mAggression = aggression;
        return p;
    }
    Party follower()
    {
        Party p;
        p.mFollower = true;
        return p;
    }
    Party player()
    {
        Party p;
        p.mPlayer = true;
        return p;
    }

    TEST(CombatFactionsPure, thePlayerAndFollowersAreOnTheSameSide)
    {
        EXPECT_TRUE(Factions::playerSide(player()));
        EXPECT_TRUE(Factions::playerSide(follower()));
        EXPECT_FALSE(Factions::playerSide(npc()));
    }

    TEST(CombatFactionsPure, sideRelationIsAllyOnOneSideAndEnemyForTheEnemiesOfThePlayer)
    {
        EXPECT_EQ(Factions::sideRelation(follower(), player(), R::Neutral), R::Ally);
        EXPECT_EQ(Factions::sideRelation(player(), follower(), R::Enemy), R::Ally);
        Party hater = npc(2);
        hater.mRelationToPlayer = R::Enemy;
        EXPECT_EQ(Factions::sideRelation(hater, follower(), R::Neutral), R::Enemy);
        EXPECT_EQ(Factions::sideRelation(follower(), hater, R::Neutral), R::Enemy);
        EXPECT_EQ(Factions::sideRelation(npc(), follower(), R::Neutral), R::Neutral);
        EXPECT_EQ(Factions::sideRelation(npc(), npc(), R::Friend), R::Friend);
    }

    TEST(CombatFactionsPure, theAggressionRuleDecidesWhoStartsAFight)
    {
        const Party none = npc(0), agg = npc(1), very = npc(2), frenzied = npc(3);
        const Party other = npc();
        EXPECT_FALSE(Factions::attacksOnSight(none, other, R::Enemy, false));
        EXPECT_TRUE(Factions::attacksOnSight(agg, other, R::Enemy, false));
        EXPECT_FALSE(Factions::attacksOnSight(agg, other, R::Neutral, false));
        EXPECT_TRUE(Factions::attacksOnSight(very, other, R::Neutral, false));
        EXPECT_FALSE(Factions::attacksOnSight(very, other, R::Ally, false));
        EXPECT_TRUE(Factions::attacksOnSight(frenzied, other, R::Ally, false));
    }

    TEST(CombatFactionsPure, aFollowerAttacksWhatIsHostileToThePlayersSideAndNeverItsOwnSide)
    {
        Party hostile = npc(2);
        hostile.mHostileOnSight = true;
        EXPECT_TRUE(Factions::attacksOnSight(follower(), hostile, R::Enemy, false));
        EXPECT_FALSE(Factions::attacksOnSight(follower(), npc(2), R::Neutral, false));
        EXPECT_TRUE(Factions::attacksOnSight(follower(), npc(), R::Neutral, true));
        EXPECT_FALSE(Factions::attacksOnSight(follower(), player(), R::Ally, true));
        EXPECT_FALSE(Factions::attacksOnSight(follower(), follower(), R::Ally, true));
        EXPECT_FALSE(Factions::attacksOnSight(player(), hostile, R::Enemy, true));
    }

    TEST(CombatFactionsPure, aHelperJoinsByAssistanceOrAsAGuard)
    {
        Party none = npc(), one = npc(), two = npc(), guard = npc();
        one.mAssistance = 1;
        two.mAssistance = 2;
        guard.mGuard = true;
        EXPECT_FALSE(Factions::assists(none, R::Ally, R::Enemy, false));
        EXPECT_TRUE(Factions::assists(one, R::Ally, R::Enemy, false));
        EXPECT_FALSE(Factions::assists(one, R::Friend, R::Enemy, false));
        EXPECT_TRUE(Factions::assists(two, R::Friend, R::Neutral, false));
        EXPECT_FALSE(Factions::assists(two, R::Neutral, R::Enemy, false));
        EXPECT_FALSE(Factions::assists(one, R::Ally, R::Ally, false));
        EXPECT_FALSE(Factions::assists(two, R::Ally, R::Friend, false));
        EXPECT_TRUE(Factions::assists(guard, R::Ally, R::Ally, false));
        EXPECT_TRUE(Factions::assists(guard, R::Friend, R::Neutral, false));
        EXPECT_FALSE(Factions::assists(guard, R::Neutral, R::Enemy, false));
        EXPECT_FALSE(Factions::assists(guard, R::Enemy, R::Neutral, false));
        EXPECT_TRUE(Factions::assists(guard, R::Neutral, R::Enemy, true));
        guard.mRelationToPlayer = R::Enemy;
        EXPECT_FALSE(Factions::assists(guard, R::Neutral, R::Enemy, true));
    }

    TEST(CombatFactionsPure, friendlyFireIsRefusedUnlessTheVictimIsTheTarget)
    {
        EXPECT_FALSE(Factions::mayHit(R::Ally, false));
        EXPECT_FALSE(Factions::mayHit(R::Friend, false));
        EXPECT_TRUE(Factions::mayHit(R::Ally, true));
        EXPECT_TRUE(Factions::mayHit(R::Enemy, false));
        EXPECT_TRUE(Factions::mayHit(R::Neutral, false));
    }

    TEST(CombatFactionsPure, theSightRayRunsFromTheEyeToTheTarget)
    {
        const Factions::SightRay r = Factions::sightRay(0.0, 0.0, 10.0, 0.0, 300.0, 10.0);
        ASSERT_TRUE(r.mValid);
        EXPECT_NEAR(r.mRay.x, 0.0, 1e-9);
        EXPECT_NEAR(r.mRay.z, 10.0 + Factions::kEyeHeight, 1e-9);
        EXPECT_NEAR(r.mRay.heading, 0.f, 1e-6);
        const double dz = Factions::kTargetHeight - Factions::kEyeHeight;
        EXPECT_NEAR(r.mRay.pitch, std::atan2(dz, 300.0), 1e-5);
        EXPECT_NEAR(r.mRay.range, std::hypot(300.0, dz), 1e-3);
        const Factions::SightRay e = Factions::sightRay(0.0, 0.0, 0.0, 300.0, 0.0, 0.0);
        EXPECT_NEAR(e.mRay.heading, 1.5707963f, 1e-5);
    }

    TEST(CombatFactionsPure, aBadSightRayIsInvalid)
    {
        const double nan = std::nan("");
        EXPECT_FALSE(Factions::sightRay(nan, 0, 0, 1, 1, 1).mValid);
        EXPECT_FALSE(Factions::sightRay(0, 0, 0, 1, std::numeric_limits<double>::infinity(), 1).mValid);
    }

    TEST(CombatFactionsPure, aStaticBodyBlocksSightPeopleDoNot)
    {
        EXPECT_TRUE(Factions::sightClear(RayFate::Nothing));
        EXPECT_TRUE(Factions::sightClear(RayFate::Actor));
        EXPECT_TRUE(Factions::sightClear(RayFate::Player));
        EXPECT_FALSE(Factions::sightClear(RayFate::World));
    }

    // ---- the AI through the contract --------------------------------------------------------------------------------

    constexpr WorldId kBaseA = 100;
    constexpr WorldId kBaseB = 200;
    constexpr WorldId kBaseC = 300;
    constexpr WorldId kBaseD = 400;

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
            mSword.mSkillValue = kFirstSkillActorValue;
            for (const WorldId base : { kBaseA, kBaseB, kBaseC, kBaseD })
            {
                FighterProfile npc;
                npc.mHealthMax = 100.f;
                npc.mStaminaMax = 100.f;
                npc.mSkills[0] = 50;
                npc.mUnarmedDamage = 4.f;
                npc.mUnarmedReach = 96.f;
                npc.mBase = static_cast<FormId>(base);
                npc.mAi.mKnown = true;
                npc.mAi.mConfidence = 4;
                mProfiles[base] = npc;
            }
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
        ActorRelation factionRelation(const FighterProfile& a, const FighterProfile& b) override
        {
            const auto it = mRelations.find({ std::min(a.mBase, b.mBase), std::max(a.mBase, b.mBase) });
            return it == mRelations.end() ? ActorRelation::Neutral : it->second;
        }
        void relate(WorldId a, WorldId b, ActorRelation r)
        {
            mRelations[{ static_cast<FormId>(std::min(a, b)), static_cast<FormId>(std::max(a, b)) }] = r;
        }
        FighterProfile& of(WorldId base) { return mProfiles[base]; }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        std::map<std::pair<FormId, FormId>, ActorRelation> mRelations;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest&) override
        {
            mSweeps.push_back(subject);
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
        bool castRay(CombatSubject, const RayRequest& request, RayHit& out) override
        {
            ++mRays;
            out = {};
            if (!mRaysWork)
                return false;
            const double endX = request.x
                + std::sin(static_cast<double>(request.heading)) * std::cos(static_cast<double>(request.pitch))
                    * static_cast<double>(request.range);
            if (mWall && ((request.x < mWallX) != (endX < mWallX)))
                out.fate = RayFate::World;
            return true;
        }

        std::vector<std::pair<CombatSubject, ClipSlot>> mClips;
        std::vector<CombatSubject> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        bool mWall = false;
        double mWallX = 50.0;
        bool mRaysWork = true;
        std::size_t mRays = 0;
    };

    ActorInfo infoOf(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatFactions : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 11, 1 };
        ActorHandle mC{ 12, 1 };
        ActorHandle mD{ 13, 1 };
        std::vector<std::string> mTrace;
        std::vector<SkyrimCombat::AiMoveIntent> mMoves;
        std::vector<WorldId> mFollowers;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setCombatAi(true);
            mCombat.setCombatFactions(true);
            mCombat.setFollowerQuery([this](WorldId reference) {
                return std::find(mFollowers.begin(), mFollowers.end(), reference) != mFollowers.end();
            });
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(infoOf(10, kBaseA, "ActorA"));
            mCombat.onActorAdded(infoOf(11, kBaseB, "ActorB"));
            mCombat.onActorAdded(infoOf(12, kBaseC, "ActorC"));
            mCombat.onActorAdded(infoOf(13, kBaseD, "ActorD"));
            mWriter.mStates[10] = { 0.0, 0.0, 0.0, 0.f, 0.f, 0.f };
            mWriter.mStates[11] = { 0.0, 400.0, 0.0, 0.f, 0.f, 0.f };
            mWriter.mStates[12] = { 300.0, 200.0, 0.0, 0.f, 0.f, 0.f };
            mWriter.mStates[13] = { -3000.0, -3000.0, 0.0, 0.f, 0.f, 0.f }; // out of everybody's sight
            mWriter.mPlayerState = { 6000.0, 6000.0, 0.0, 0.f, 100.f, 100.f }; // far away
        }
        void run(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
            {
                mCombat.combatUpdate(dt, mWriter);
                mCombat.drainAiMoves(mMoves);
            }
        }
        bool engaged(ActorHandle h) const { return mCombat.aiModeOf(h) != AiMode::Calm; }
        bool targets(ActorHandle who, ActorHandle target) const
        {
            return engaged(who) && mCombat.aiTargetOf(who) == CombatSubject::of(target);
        }
    };

    TEST_F(CombatFactions, anAggressiveNpcEngagesAnEnemyNpcNotOnlyThePlayer)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        run(30);
        EXPECT_TRUE(targets(mA, mB));
        EXPECT_FALSE(engaged(mB));
    }

    TEST_F(CombatFactions, withTheFactionsRulesOffTheNpcsLeaveEachOtherAlone)
    {
        mCombat.setCombatFactions(false);
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        run(30);
        EXPECT_FALSE(engaged(mA));
        EXPECT_EQ(mCombat.aiEngagedCount(), 0u);
    }

    TEST_F(CombatFactions, anUnaggressiveNpcStaysCalmAmongEnemies)
    {
        mData.relate(kBaseA, kBaseB, R::Enemy); // aggression 0
        run(20);
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, anAggressiveNpcLeavesANeutralAlone)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        run(20);
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, aVeryAggressiveNpcAttacksTheNearestNeutral)
    {
        mData.of(kBaseA).mAi.mAggression = 2;
        run(30);
        EXPECT_TRUE(engaged(mA));
        EXPECT_NE(mCombat.aiTargetOf(mA), CombatSubject::ofPlayer());
    }

    TEST_F(CombatFactions, aWallBetweenThemKeepsAHostileFromEngagingAndOpeningItDoes)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mWriter.mStates[11] = { 400.0, 0.0, 0.0, 0.f, 0.f, 0.f };
        mWriter.mWall = true;
        run(30);
        EXPECT_FALSE(engaged(mA));
        EXPECT_GT(mWriter.mRays, 0u);
        mWriter.mWall = false;
        run(30);
        EXPECT_TRUE(targets(mA, mB));
    }

    TEST_F(CombatFactions, aRayTheCoreCannotAnswerLeavesTheTargetInSight)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mWriter.mRaysWork = false;
        run(30);
        EXPECT_TRUE(targets(mA, mB));
    }

    TEST_F(CombatFactions, aTargetBeyondSightIsNotEngaged)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mWriter.mStates[11] = { 0.0, 2500.0, 0.0, 0.f, 0.f, 0.f };
        run(30);
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, anAllyOfTheAttackedJoinsTheFight)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mData.of(kBaseC).mAi.mAssistance = 1;
        mData.relate(kBaseC, kBaseB, R::Ally);
        mData.of(kBaseD).mAi.mAssistance = 0; // helps nobody
        mData.relate(kBaseD, kBaseB, R::Ally);
        mWriter.mStates[13] = { 100.0, 300.0, 0.0, 0.f, 0.f, 0.f };
        run(40);
        EXPECT_TRUE(targets(mC, mA));
        EXPECT_FALSE(engaged(mD));
    }

    TEST_F(CombatFactions, anAllyOfBothSidesStaysOut)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mData.of(kBaseC).mAi.mAssistance = 1;
        mData.relate(kBaseC, kBaseB, R::Ally);
        mData.relate(kBaseC, kBaseA, R::Ally);
        run(40);
        EXPECT_TRUE(engaged(mA));
        EXPECT_FALSE(engaged(mC));
    }

    TEST_F(CombatFactions, aGuardEngagesWhoAttacksAnAllyEvenIfTheGuardIsUnaggressive)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mData.of(kBaseC).mAi.mGuard = true;
        mData.relate(kBaseC, kBaseB, R::Ally);
        run(40);
        EXPECT_TRUE(targets(mC, mA));
    }

    TEST_F(CombatFactions, aGuardDoesNotProtectItsEnemy)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        mData.of(kBaseC).mAi.mGuard = true;
        mData.relate(kBaseC, kBaseB, R::Enemy);
        run(40);
        EXPECT_FALSE(engaged(mC));
    }

    TEST_F(CombatFactions, aFollowerFightsAHostileNearThePlayer)
    {
        mFollowers = { 10 };
        mWriter.mPlayerState = { 100.0, -100.0, 0.0, 0.f, 100.f, 100.f }; // beside the follower
        mData.of(kBaseB).mAi.mHostileOnSight = true;
        run(40);
        EXPECT_TRUE(targets(mA, mB));
    }

    TEST_F(CombatFactions, aFollowerLeavesWhatIsNotHostileToThePlayerAlone)
    {
        mFollowers = { 10 };
        mWriter.mPlayerState = { 100.0, -100.0, 0.0, 0.f, 100.f, 100.f };
        run(40); // B and C are neutral
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, aFollowerFarFromThePlayerDoesNotGoLookingForTrouble)
    {
        mFollowers = { 10 };
        mData.of(kBaseB).mAi.mHostileOnSight = true;
        run(40); // the player is 8,000 units away
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, aHostileNpcFightsTheFollowerWhenThePlayerIsNotNear)
    {
        mFollowers = { 11 };
        mData.of(kBaseA).mAi.mAggression = 2;
        mData.of(kBaseA).mAi.mHostileOnSight = true;
        mData.of(kBaseA).mAi.mRelationToPlayer = static_cast<std::uint8_t>(R::Enemy);
        mWriter.mStates[12] = { 4000.0, -4000.0, 0.0, 0.f, 0.f, 0.f }; // a very aggressive one also attacks neutrals
        run(40);
        EXPECT_TRUE(targets(mA, mB));
    }

    TEST_F(CombatFactions, aHostileNpcStillEngagesThePlayerWhenItIsNear)
    {
        mData.of(kBaseA).mAi.mAggression = 2;
        mData.of(kBaseA).mAi.mHostileOnSight = true;
        mData.of(kBaseA).mAi.mRelationToPlayer = static_cast<std::uint8_t>(R::Enemy);
        mWriter.mStates[11] = { 4000.0, 4000.0, 0.0, 0.f, 0.f, 0.f };
        mWriter.mStates[12] = { 4000.0, -4000.0, 0.0, 0.f, 0.f, 0.f };
        mWriter.mPlayerState = { 0.0, 300.0, 0.0, 0.f, 100.f, 100.f };
        run(30);
        EXPECT_TRUE(engaged(mA));
        EXPECT_EQ(mCombat.aiTargetOf(mA), CombatSubject::ofPlayer());
    }

    TEST_F(CombatFactions, anNpcHurtByAnotherNpcFightsBack)
    {
        DamageReport report;
        report.attacker = mA;
        report.victim = mB;
        report.amount = 5.f;
        report.healthAfter = 90.f;
        report.healthMax = 100.f;
        run(2);
        mCombat.aiNoteDamage(report);
        run(20);
        EXPECT_TRUE(targets(mB, mA));
    }

    TEST_F(CombatFactions, whenTheTargetDiesTheFighterLetsGo)
    {
        mData.of(kBaseA).mAi.mAggression = 1;
        mData.relate(kBaseA, kBaseB, R::Enemy);
        run(30);
        ASSERT_TRUE(targets(mA, mB));
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 0.f));
        run(20);
        EXPECT_FALSE(targets(mA, mB));
        EXPECT_EQ(mCombat.aiModeOf(mA), AiMode::Calm);
    }

    TEST_F(CombatFactions, theTestAttackerIsFrenziedAndPicksTheNearestTarget)
    {
        mCombat.setTestAttacker("ActorA");
        mCombat.setTestCrowd(0, 0, 0.f); // the delay of the test rules
        run(30);
        EXPECT_TRUE(targets(mA, mC)); // C is 360 away, B 400
    }

    TEST_F(CombatFactions, nothingEngagesBeforeTheFactionsDelayNotEvenAHostileThePlayerIsNear)
    {
        // A regression of the first build: the delay skipped the new look and fell into the old hostile-on-sight code.
        mCombat.setFactionsDelay(100.f);
        mData.of(kBaseA).mAi.mAggression = 2;
        mData.of(kBaseA).mAi.mHostileOnSight = true;
        mData.of(kBaseA).mAi.mRelationToPlayer = static_cast<std::uint8_t>(R::Enemy);
        mWriter.mPlayerState = { 0.0, 300.0, 0.0, 0.f, 100.f, 100.f };
        run(30);
        EXPECT_FALSE(engaged(mA));
        mCombat.setFactionsDelay(0.f);
        run(10);
        EXPECT_TRUE(engaged(mA));
    }

    TEST_F(CombatFactions, theTestAttackerWaitsForTheDelay)
    {
        mCombat.setTestAttacker("ActorA");
        mCombat.setTestCrowd(0, 0, 100.f);
        run(30);
        EXPECT_FALSE(engaged(mA));
    }

    TEST_F(CombatFactions, twoRunsGiveTheSameTrace)
    {
        auto runOnce = [](std::vector<std::string>& trace) {
            FakeData data;
            data.of(kBaseA).mAi.mAggression = 1;
            data.relate(kBaseA, kBaseB, R::Enemy);
            data.of(kBaseC).mAi.mGuard = true;
            data.relate(kBaseC, kBaseB, R::Ally);
            SkyrimCombat combat;
            combat.setData(&data);
            combat.setTestWeapon("IronSword");
            combat.setCombatAi(true);
            combat.setCombatFactions(true);
            combat.setTrace([&trace](const std::string& line) { trace.push_back(line); });
            FakeWriter writer;
            combat.onActorAdded(infoOf(10, kBaseA, "a"));
            combat.onActorAdded(infoOf(11, kBaseB, "b"));
            combat.onActorAdded(infoOf(12, kBaseC, "c"));
            writer.mStates[10] = { 0.0, 0.0, 0.0, 0.f, 0.f, 0.f };
            writer.mStates[11] = { 0.0, 300.0, 0.0, 0.f, 0.f, 0.f };
            writer.mStates[12] = { 300.0, 200.0, 0.0, 0.f, 0.f, 0.f };
            writer.mPlayerState = { 6000.0, 6000.0, 0.0, 0.f, 100.f, 100.f };
            std::vector<SkyrimCombat::AiMoveIntent> moves;
            for (int i = 0; i < 60; ++i)
            {
                combat.combatUpdate(0.1f, writer);
                combat.drainAiMoves(moves);
            }
        };
        std::vector<std::string> one, two;
        runOnce(one);
        runOnce(two);
        EXPECT_FALSE(one.empty());
        EXPECT_EQ(one, two);
    }
}
