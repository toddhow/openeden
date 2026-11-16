// Task 0171, review fixes: the Skyrim pack's combat rules (game_skyrimse/combat.hpp) must not block on data that is
// still being read, must not leak entries of its sweep table, must not ask the engine for a timing it already has, and
// a hit must never do negative damage or more than the blow. Written first and shown red against the code before the
// fixes (the header carried only the accessors).
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;

    class Data final : public CombatData
    {
    public:
        Data()
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
            attacker.mStaminaMax = 100000.f; // the tests swing many times
            attacker.mSkills[0] = 50;
            attacker.mUnarmedDamage = 4.f;
            attacker.mUnarmedReach = 96.f;
            mProfiles[kAttackerBase] = attacker;
            FighterProfile victim;
            victim.mHealthMax = 100000.f;
            victim.mStaminaMax = 80.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            ++mFighterCalls;
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override
        {
            ++mPlayerCalls;
            return mPlayer;
        }
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override
        {
            ++mSettingsCalls;
            return mSettings;
        }
        bool combatReady() override
        {
            ++mReadyAsks;
            return mReady;
        }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
        bool mReady = true;
        int mReadyAsks = 0;
        int mSettingsCalls = 0;
        int mFighterCalls = 0;
        int mPlayerCalls = 0;
    };

    class Writer final : public CombatRequestWriter
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
            ++mTimingAsks;
            out = {};
            if (!mTimingReady)
                return false;
            out = ClipTiming{ true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
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
        bool mTimingReady = true;
        int mTimingAsks = 0;
    };

    ActorInfo info(WorldId reference, std::uint32_t session, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, session };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Attack;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    struct CombatReview : ::testing::Test
    {
        Data mData;
        SkyrimCombat mCombat;
        Writer mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
        }
    };

    // ---- finding 1: nothing blocks, nothing happens until the data is ready ----------------------------------------

    TEST_F(CombatReview, nothingIsAskedOfTheDataAndNothingHappensWhileItIsNotReady)
    {
        mData.mReady = false;
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 1));
        mCombat.onPlayerAttack(false);
        for (int i = 0; i < 5; ++i)
            mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mData.mSettingsCalls, 0); // settings() is what read every plugin on the main thread
        EXPECT_EQ(mData.mFighterCalls, 0);
        EXPECT_EQ(mData.mPlayerCalls, 0);
        EXPECT_TRUE(mWriter.mSweeps.empty()); // no hit
        EXPECT_EQ(mCombat.swingsRequested(), 0u); // no swing
        EXPECT_EQ(mCombat.updatesWaitingForData(), 5u);
        EXPECT_GE(mData.mReadyAsks, 5);
    }

    TEST_F(CombatReview, aHitThatCameWhileTheDataWasNotReadyIsNotCarriedOver)
    {
        mData.mReady = false;
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 1));
        mCombat.combatUpdate(0.016f, mWriter);
        mData.mReady = true;
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mWriter.mSweeps.empty());
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 2));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.mSweeps.size(), 1u); // combat works as soon as the data is there
        EXPECT_GE(mData.mSettingsCalls, 1);
    }

    // ---- finding 2: the table of sweeps waiting for their result does not leak -------------------------------------

    TEST_F(CombatReview, sweepsWhoseResultNeverComesAreForgottenAndCombatKeepsWorking)
    {
        for (std::uint32_t i = 0; i < 60; ++i)
        {
            mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 100 + i));
            mCombat.combatUpdate(0.016f, mWriter); // the sweep is asked for; the engine never answers
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.combatUpdate(0.016f, mWriter);
        }
        EXPECT_EQ(mWriter.mSweeps.size(), 60u); // before the fix: stopped after 32
        EXPECT_LE(mCombat.pendingSweeps(), 1u);
    }

    TEST_F(CombatReview, anActorThatGoesTakesItsWaitingSweepsWithIt)
    {
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 7));
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mCombat.pendingSweeps(), 1u);
        mCombat.onActorRemoved(mA);
        EXPECT_EQ(mCombat.pendingSweeps(), 0u);
        const std::size_t refused = mCombat.refusedSweepResults();
        mCombat.onSweepResult(CombatSubject::of(mA), 7, nullptr, 0); // late: nothing is waiting for it
        EXPECT_EQ(mCombat.refusedSweepResults(), refused + 1);
    }

    TEST_F(CombatReview, aFullSweepTableIsReportedOnceAndNothingIsSpent)
    {
        std::vector<std::string> warnings;
        mCombat.setWarning([&](const std::string& line) { warnings.push_back(line); });
        // Fill the table in one update: kMaxConcurrentAttacks hits (the most remembered between updates).
        for (std::uint32_t i = 0; i < kMaxConcurrentAttacks; ++i)
            mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 300 + i));
        mCombat.combatUpdate(0.f, mWriter);
        ASSERT_EQ(mCombat.pendingSweeps(), kMaxConcurrentAttacks);
        SkyrimCombat::FighterView before;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), before));
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 400));
        mCombat.combatUpdate(0.f, mWriter);
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 401));
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_EQ(mWriter.mSweeps.size(), kMaxConcurrentAttacks); // no sweep was asked for that nobody could remember
        EXPECT_EQ(warnings.size(), 1u); // once
        SkyrimCombat::FighterView after;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::of(mA), after));
        EXPECT_FLOAT_EQ(after.mStamina, before.mStamina);
    }

    // ---- finding 5: a timing that is known is not asked for again -------------------------------------------------

    TEST_F(CombatReview, theEngineIsNotAskedForTheSwingTimingAgainOnceItIsKnown)
    {
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter); // the read starts, the swing begins
        const int asks = mWriter.mTimingAsks;
        for (int i = 0; i < 200; ++i)
            mCombat.combatUpdate(0.016f, mWriter); // the swing ends (1.287 s)
        mCombat.onPlayerAttack(false);
        for (int i = 0; i < 200; ++i)
            mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mCombat.swingsRequested(), 2u);
        EXPECT_EQ(mWriter.mTimingAsks, asks);
    }

    TEST_F(CombatReview, aTimingThatIsNotReadyIsStillAskedForUntilItIs)
    {
        mWriter.mTimingReady = false;
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter);
        const int asks = mWriter.mTimingAsks;
        EXPECT_GE(asks, 3);
        mWriter.mTimingReady = true;
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_GT(mWriter.mTimingAsks, asks);
        EXPECT_EQ(mCombat.swingsRequested(), 1u);
    }

    // ---- finding 10: the damage of a blow ---------------------------------------------------------------------------

    TEST(CombatReviewStats, theDamageAfterArmourIsBetweenZeroAndTheBlow)
    {
        WeaponRecord sword;
        sword.mHasData = true;
        sword.mDamage = 10;
        sword.mWeight = 1.f;
        sword.mHasDnam = true;
        sword.mAnimType = WeaponAnimType::OneHandSword;
        sword.mReach = 1.f;
        CombatSettings settings;
        MeleeInput input;
        input.mWeapon = &sword;
        input.mTargetMitigationPercent = -50.f; // a negative mitigation in the data
        MeleeHit hit = computeMeleeHit(input, settings);
        EXPECT_GT(hit.mDamageBefore, 0.f);
        EXPECT_FLOAT_EQ(hit.mDamageAfter, hit.mDamageBefore); // before the fix: 1.5 x the blow
        input.mTargetMitigationPercent = 100.f;
        settings.mMaxArmorRating = 500.f; // a GMST that lets the rating pass 100
        input.mTargetMitigationPercent = 250.f;
        hit = computeMeleeHit(input, settings);
        EXPECT_GE(hit.mDamageAfter, 0.f); // before the fix: negative damage heals
        EXPECT_FLOAT_EQ(hit.mDamageAfter, 0.f);
    }
}
