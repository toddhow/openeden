// Task 0210: the combat's side of waiting and sleeping: a queued rest gives every living fighter its health, magicka
// and stamina back by its own regeneration, the player's health through a report that takes none off, and the combat
// rules say whether the player may rest. Fixtures follow testskyrimsecombatskills.cpp.
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
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
            mSword.mSkillValue = kFirstSkillActorValue;
            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mMagickaMax = 50.f;
            attacker.mSkills[0] = 50;
            attacker.mHealthRegenPercent = 1.f;
            attacker.mStaminaRegenPercent = 5.f;
            attacker.mMagickaRegenPercent = 3.f;
            mProfiles[kAttackerBase] = attacker;
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mHealthRegenPercent = 1.f;
            victim.mStaminaRegenPercent = 5.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mHealthRegenPercent = 1.f;
            mPlayer.mStaminaRegenPercent = 5.f;
            mPlayer.mMagickaRegenPercent = 3.f;
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
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct CombatRest : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mB{ 20, 1 };

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 40.f, 100.f };
            mCombat.combatUpdate(0.016f, mWriter); // the fighters become ready
        }

        SkyrimCombat::FighterView view(CombatSubject who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(who, v));
            return v;
        }

        /// The player power-attacks and hits the victim (it costs stamina and puts both in combat).
        void playerFights()
        {
            mWriter.mSweeps.clear();
            mCombat.onPlayerAttack(true);
            for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            ASSERT_FALSE(mWriter.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
            for (int i = 0; i < 4; ++i)
                mCombat.combatUpdate(0.1f, mWriter);
        }
    };

    TEST_F(CombatRest, aRestWithNothingQueuedDoesNothing)
    {
        mWriter.mDamage.clear();
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.restsApplied(), 0u);
    }

    TEST_F(CombatRest, aQueuedRestRestoresAHurtActorByItsOwnRegenerationRate)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 10.f));
        mCombat.requestRest(20.f); // 1 percent of 50 a second for 20 s = 10 points
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_NEAR(view(CombatSubject::of(mB)).mHealth, 20.f, 1e-3f);
        EXPECT_EQ(mCombat.restsApplied(), 1u);
    }

    TEST_F(CombatRest, aLongRestFillsEverythingToTheMaximumAndNoFurther)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 1.f));
        mCombat.requestRest(5000.f);
        mCombat.combatUpdate(0.f, mWriter);
        const auto v = view(CombatSubject::of(mB));
        EXPECT_FLOAT_EQ(v.mHealth, v.mHealthMax);
        EXPECT_FLOAT_EQ(v.mStamina, v.mStaminaMax);
    }

    TEST_F(CombatRest, theRestIsAppliedOnceNotOnEveryUpdate)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 10.f));
        mCombat.requestRest(20.f);
        mCombat.combatUpdate(0.f, mWriter);
        const float after = view(CombatSubject::of(mB)).mHealth;
        mCombat.combatUpdate(0.f, mWriter);
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_FLOAT_EQ(view(CombatSubject::of(mB)).mHealth, after);
        EXPECT_EQ(mCombat.restsApplied(), 1u);
    }

    TEST_F(CombatRest, thePlayersHealthGoesToTheEngineThroughAReportThatTakesNoneOff)
    {
        mWriter.mDamage.clear();
        mCombat.requestRest(30.f); // 1 percent of 100 a second for 30 s = 30 points on the engine's 40
        mCombat.combatUpdate(0.f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& d = mWriter.mDamage[0];
        EXPECT_TRUE(d.victimIsPlayer);
        EXPECT_FLOAT_EQ(d.amount, 0.f);
        EXPECT_NEAR(d.healthAfter, 70.f, 1e-3f);
        EXPECT_FLOAT_EQ(d.healthMax, 100.f);
        EXPECT_NEAR(view(CombatSubject::ofPlayer()).mHealth, 70.f, 1e-3f);
    }

    TEST_F(CombatRest, thePlayersHealthIsNeverRaisedPastItsMaximum)
    {
        mWriter.mDamage.clear();
        mCombat.requestRest(9000.f);
        mCombat.combatUpdate(0.f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthAfter, 100.f);
    }

    TEST_F(CombatRest, aPlayerAlreadyAtFullHealthSendsNoReport)
    {
        mWriter.mPlayerState.health = 100.f;
        mWriter.mDamage.clear();
        mCombat.requestRest(100.f);
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatRest, spentStaminaComesBackWithTheRest)
    {
        playerFights();
        const auto spent = view(CombatSubject::ofPlayer());
        ASSERT_LT(spent.mStamina, spent.mStaminaMax);
        mCombat.requestRest(3600.f);
        mCombat.combatUpdate(0.f, mWriter);
        const auto rested = view(CombatSubject::ofPlayer());
        EXPECT_FLOAT_EQ(rested.mStamina, rested.mStaminaMax);
        EXPECT_FLOAT_EQ(rested.mMagicka, rested.mMagickaMax);
    }

    TEST_F(CombatRest, aRequestThatIsNotANumberOrNotPositiveQueuesNothing)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 10.f));
        mCombat.requestRest(std::numeric_limits<float>::quiet_NaN());
        mCombat.requestRest(std::numeric_limits<float>::infinity());
        mCombat.requestRest(-5.f);
        mCombat.requestRest(0.f);
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_FLOAT_EQ(view(CombatSubject::of(mB)).mHealth, 10.f);
        EXPECT_EQ(mCombat.restsApplied(), 0u);
    }

    TEST_F(CombatRest, queuedSecondsAddUpButNeverPastTheCap)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 1.f));
        mCombat.requestRest(5.f);
        mCombat.requestRest(5.f); // 10 s in all: 5 points
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_NEAR(view(CombatSubject::of(mB)).mHealth, 6.f, 1e-3f);
        mCombat.requestRest(SkyrimCombat::kMaxRestSeconds);
        mCombat.requestRest(SkyrimCombat::kMaxRestSeconds);
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_EQ(mCombat.restsApplied(), 2u);
    }

    TEST_F(CombatRest, theDeadAreNotRestored)
    {
        ASSERT_TRUE(mCombat.setFighterHealth(CombatSubject::of(mB), 0.f));
        mCombat.combatUpdate(0.016f, mWriter);
        SkyrimCombat::FighterView before;
        const bool known = mCombat.fighterView(CombatSubject::of(mB), before);
        mCombat.requestRest(1000.f);
        mCombat.combatUpdate(0.f, mWriter);
        SkyrimCombat::FighterView after;
        ASSERT_EQ(mCombat.fighterView(CombatSubject::of(mB), after), known);
        EXPECT_FLOAT_EQ(after.mHealth, before.mHealth);
    }

    TEST_F(CombatRest, aBlowPutsThePlayerInCombatUntilTheLingerTimeHasPassed)
    {
        EXPECT_FALSE(mCombat.playerInCombat());
        playerFights();
        EXPECT_TRUE(mCombat.playerInCombat());
        for (int i = 0; i < 400; ++i)
            mCombat.combatUpdate(0.5f, mWriter); // 200 s
        EXPECT_FALSE(mCombat.playerInCombat());
    }

    TEST_F(CombatRest, withNoHostileNobodyIsNear)
    {
        EXPECT_FALSE(mCombat.enemiesNear());
    }

    TEST_F(CombatRest, aNamedTestHostileThatIsAliveIsNear)
    {
        ASSERT_TRUE(mCombat.setTestHostile("Attacker"));
        mCombat.onActorAdded(info(11, kAttackerBase, "Attacker"));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mCombat.enemiesNear());
    }

    // ---- task 0282: Actor.IsInCombat of a script (the word walls refuse to teach during a fight)

    TEST_F(CombatRest, aScriptSeesThePlayerOutOfCombatWhenNothingHappened)
    {
        EXPECT_FALSE(mCombat.scriptInCombatOf(CombatSubject::ofPlayer()));
        EXPECT_FALSE(mCombat.scriptInCombatOf(CombatSubject::of(mB)));
    }

    TEST_F(CombatRest, aScriptSeesThePlayerInCombatAfterABlowAndOutOfItOnceTheLingerTimeHasPassed)
    {
        playerFights();
        EXPECT_TRUE(mCombat.scriptInCombatOf(CombatSubject::ofPlayer()));
        for (int i = 0; i < 400; ++i)
            mCombat.combatUpdate(0.5f, mWriter); // 200 s
        EXPECT_FALSE(mCombat.scriptInCombatOf(CombatSubject::ofPlayer()));
    }

    TEST_F(CombatRest, aScriptSeesThePlayerInCombatWhileALivingHostileIsEngagedWithHim)
    {
        ASSERT_TRUE(mCombat.setTestHostile("Attacker"));
        mCombat.onActorAdded(info(11, kAttackerBase, "Attacker"));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mCombat.scriptInCombatOf(CombatSubject::ofPlayer()));
    }
}
