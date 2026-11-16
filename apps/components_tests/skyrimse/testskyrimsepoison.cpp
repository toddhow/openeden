// Task 0229 (poisons): a poison on the player's melee weapon is applied to the target of the next landed hit through
// the magic runtime of task 0186, then spent. The fixtures follow testskyrimsecombatequip.cpp (a fake data source and a
// recording writer). Numbers by hand: the test sword hits the victim for 7 x 1.075 x (1 - 20 percent) = 6.02 (the
// stand-in player's skills are 15); the poison is "AlchDamageHealth" (hostile, detrimental, archetype ValueModifier, on
// health), magnitude 5 for 4 seconds: -5 a second, 20 in all; the victim has 50 health: 50 - 6.02 = 43.98, then 23.98.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/poison.hpp>

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
    constexpr FormId kDamageEffect = 0x2000, kWeakEffect = 0x2001, kPoisonItem = 0x5000;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mDamage.mCommon.mId = kDamageEffect;
            mDamage.mCommon.mEditorId = "AlchDamageHealth";
            mDamage.mHasData = true;
            mDamage.mArchetype = MagicArchetype::ValueModifier;
            mDamage.mPrimaryValue = 24;
            mDamage.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;
            mWeak.mCommon.mId = kWeakEffect;
            mWeak.mCommon.mEditorId = "AlchWeaknessFire";
            mWeak.mHasData = true;
            mWeak.mArchetype = MagicArchetype::ValueModifier;
            mWeak.mPrimaryValue = 41; // a resistance value the pack does not track: a status only
            mWeak.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;

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
            attacker.mSkills[0] = 50;
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
        const MagicEffectRecord* effectById(FormId id) override
        {
            return id == kDamageEffect ? &mDamage : id == kWeakEffect ? &mWeak : nullptr;
        }

        WeaponRecord mSword;
        MagicEffectRecord mDamage, mWeak;
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
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
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

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Attack;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    struct CombatPoison : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }

        // The player swings (a normal attack) and the sweep finds the victim B. Returns the first damage report.
        const DamageReport* playerHitsVictim()
        {
            mWriter.mSweeps.clear();
            mWriter.mDamage.clear();
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            if (mWriter.mSweeps.empty())
                return nullptr;
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
            mCombat.combatUpdate(0.f, mWriter);
            // let the swing end so that the next one can start
            for (int i = 0; i < 10; ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }

        // An NPC hits the player.
        const DamageReport* npcHitsPlayer(std::uint32_t token)
        {
            mWriter.mDamage.clear();
            mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), token));
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit hit;
            hit.target = SweepTarget::Player;
            hit.distance = 30.f;
            mCombat.onSweepResult(CombatSubject::of(mA), token, &hit, 1);
            mCombat.combatUpdate(0.016f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }
    };

    SpellRecord damagePoison()
    {
        crafting::WeaponPoison p;
        p.mPoison = kPoisonItem;
        p.mName = "Test Poison";
        crafting::EffectEntry e;
        e.mEffect = kDamageEffect;
        e.mMagnitude = 5.f;
        e.mDuration = 4;
        p.mEffects.push_back(e);
        return crafting::poisonSpell(p);
    }

    float victimHealth(SkyrimCombat& combat, ActorHandle victim)
    {
        SkyrimCombat::FighterView view;
        EXPECT_TRUE(combat.fighterView(CombatSubject::of(victim), view));
        return view.mHealth;
    }

    // The sum of the damage reports after the first (the hit itself): what the poison did.
    float poisonTotal(const std::vector<DamageReport>& reports)
    {
        float sum = 0.f;
        for (std::size_t i = 1; i < reports.size(); ++i)
            sum += reports[i].amount;
        return sum;
    }

    TEST_F(CombatPoison, aPoisonedWeaponHurtsTheTargetOfTheNextHitOverTime)
    {
        mCombat.setPlayerPoison(damagePoison(), 1);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 1);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f); // the blow itself is unchanged
        for (int i = 0; i < 10; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_NEAR(poisonTotal(mWriter.mDamage), 20.f, 1e-2f); // 5 a second for 4 seconds
        EXPECT_NEAR(victimHealth(mCombat, mB), 50.f - 6.02f - 20.f, 1e-2f);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 0); // the one charge went with the hit
    }

    TEST_F(CombatPoison, theNextHitAfterTheChargeIsSpentCarriesNoPoison)
    {
        mCombat.setPlayerPoison(damagePoison(), 1);
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 12; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        const float before = victimHealth(mCombat, mB);
        mWriter.mDamage.clear();
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 12; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u); // the hit only: no poison report
        EXPECT_NEAR(victimHealth(mCombat, mB), before - 6.02f, 1e-2f);
    }

    TEST_F(CombatPoison, twoChargesPoisonTwoHits)
    {
        mCombat.setPlayerPoison(damagePoison(), 2);
        ASSERT_NE(playerHitsVictim(), nullptr);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 1);
        mWriter.mDamage.clear();
        ASSERT_NE(playerHitsVictim(), nullptr);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 0);
    }

    TEST_F(CombatPoison, theSpentHookIsToldTheChargesLeft)
    {
        std::vector<int> told;
        mCombat.setPoisonSpentHook([&](int remaining) { told.push_back(remaining); });
        mCombat.setPlayerPoison(damagePoison(), 2);
        ASSERT_NE(playerHitsVictim(), nullptr);
        ASSERT_NE(playerHitsVictim(), nullptr);
        ASSERT_EQ(told.size(), 2u);
        EXPECT_EQ(told[0], 1);
        EXPECT_EQ(told[1], 0);
    }

    TEST_F(CombatPoison, noChargesOrAClearedPoisonAppliesNothing)
    {
        mCombat.setPlayerPoison(damagePoison(), 0);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 0);
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 12; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
        mCombat.setPlayerPoison(damagePoison(), 1);
        mCombat.setPlayerPoison(damagePoison(), 0); // cleared again
        mWriter.mDamage.clear();
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 12; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
    }

    TEST_F(CombatPoison, aSwingThatMissesKeepsTheCharge)
    {
        mCombat.setPlayerPoison(damagePoison(), 1);
        mWriter.mSweeps.clear();
        mCombat.onPlayerAttack(false);
        for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
            mCombat.combatUpdate(0.2f, mWriter);
        ASSERT_FALSE(mWriter.mSweeps.empty());
        mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, nullptr, 0);
        mCombat.combatUpdate(0.f, mWriter);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 1);
    }

    TEST_F(CombatPoison, anNpcsBlowNeverUsesThePlayersPoison)
    {
        mCombat.setPlayerPoison(damagePoison(), 1);
        const DamageReport* back = npcHitsPlayer(5);
        ASSERT_NE(back, nullptr);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 1);
        for (int i = 0; i < 10; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
    }

    TEST_F(CombatPoison, aKillingBlowSpendsTheChargeButLeavesNoEffectOnTheDead)
    {
        mData.mProfiles[kVictimBase].mHealthMax = 5.f; // the 6.02 blow kills
        mCombat.onActorRemoved(mB);
        mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
        mCombat.setPlayerPoison(damagePoison(), 1);
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 12; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u); // no report after the death
        EXPECT_EQ(mCombat.playerPoisonCharges(), 0);
    }

    TEST_F(CombatPoison, aStatusEffectOnAValueThePackDoesNotTrackIsKeptButHarmsNothing)
    {
        crafting::WeaponPoison p;
        p.mPoison = kPoisonItem;
        p.mName = "Weakness";
        crafting::EffectEntry e;
        e.mEffect = kWeakEffect;
        e.mMagnitude = 25.f;
        e.mDuration = 30;
        p.mEffects.push_back(e);
        mCombat.setPlayerPoison(crafting::poisonSpell(p), 1);
        ASSERT_NE(playerHitsVictim(), nullptr);
        for (int i = 0; i < 4; ++i)
            mCombat.combatUpdate(0.5f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_EQ(mCombat.playerPoisonCharges(), 0);
    }

    // ---- the pure parts
    // -------------------------------------------------------------------------------------------------------

    crafting::WeaponPoison coat(FormId poison, std::int32_t charges)
    {
        crafting::WeaponPoison p;
        p.mPoison = poison;
        p.mCharges = charges;
        crafting::EffectEntry e;
        e.mEffect = 0x20;
        e.mMagnitude = 3.f;
        e.mDuration = 5;
        p.mEffects.push_back(e);
        return p;
    }

    TEST(PoisonCoatingsRule, aCoatingHoldsItsChargesPerWeapon)
    {
        crafting::PoisonCoatings coats;
        EXPECT_EQ(coats.apply(0xA, coat(0x10, 2)), crafting::CoatStatus::Ok);
        EXPECT_EQ(coats.apply(0xB, coat(0x11, 1)), crafting::CoatStatus::Ok); // another weapon
        ASSERT_NE(coats.find(0xA), nullptr);
        EXPECT_EQ(coats.find(0xA)->mPoison, 0x10u);
        EXPECT_EQ(coats.find(0xB)->mPoison, 0x11u);
        EXPECT_EQ(coats.size(), 2u);
    }

    TEST(PoisonCoatingsRule, aSecondPoisonOnTheSameWeaponIsRefusedAndKeepsTheFirst)
    {
        crafting::PoisonCoatings coats;
        ASSERT_EQ(coats.apply(0xA, coat(0x10, 1)), crafting::CoatStatus::Ok);
        EXPECT_EQ(coats.apply(0xA, coat(0x11, 1)), crafting::CoatStatus::AlreadyPoisoned);
        EXPECT_EQ(coats.find(0xA)->mPoison, 0x10u);
    }

    TEST(PoisonCoatingsRule, badCoatingsAreRefused)
    {
        crafting::PoisonCoatings coats;
        crafting::WeaponPoison none = coat(0x10, 1);
        none.mEffects.clear();
        EXPECT_EQ(coats.apply(0xA, none), crafting::CoatStatus::BadArgument);
        EXPECT_EQ(coats.apply(0xA, coat(0x10, 0)), crafting::CoatStatus::NoCharges);
        EXPECT_EQ(coats.apply(0, coat(0x10, 1)), crafting::CoatStatus::BadArgument); // no weapon
        EXPECT_EQ(coats.size(), 0u);
    }

    TEST(PoisonCoatingsRule, spendingCountsDownAndEndsTheCoating)
    {
        crafting::PoisonCoatings coats;
        ASSERT_EQ(coats.apply(0xA, coat(0x10, 2)), crafting::CoatStatus::Ok);
        EXPECT_EQ(coats.spend(0xA), 1);
        EXPECT_EQ(coats.spend(0xA), 0);
        EXPECT_EQ(coats.find(0xA), nullptr);
        EXPECT_EQ(coats.spend(0xA), -1);
        EXPECT_EQ(coats.spend(0xF), -1);
    }

    TEST(PoisonCoatingsRule, theTableIsBounded)
    {
        crafting::PoisonCoatings coats;
        for (std::uint32_t i = 1; i <= crafting::PoisonCoatings::kMax; ++i)
            ASSERT_EQ(coats.apply(i, coat(0x10, 1)), crafting::CoatStatus::Ok);
        EXPECT_EQ(coats.apply(0x9999, coat(0x10, 1)), crafting::CoatStatus::BadArgument);
    }

    TEST(PoisonSpellRule, theSpellIsATouchSpellOfTheCoatingsEffects)
    {
        crafting::WeaponPoison p = coat(0x10, 1);
        p.mName = "Poison of Damage Health";
        p.mEffects[0].mArea = 7;
        crafting::EffectEntry second;
        second.mEffect = 0x21;
        second.mMagnitude = 1.f;
        p.mEffects.push_back(second);
        const SpellRecord s = crafting::poisonSpell(p);
        EXPECT_EQ(s.mCommon.mId, 0x10u);
        EXPECT_EQ(s.mCommon.mEditorId, "Poison of Damage Health");
        EXPECT_TRUE(s.mHasData);
        EXPECT_EQ(s.mCastType, MagicCast::FireAndForget);
        EXPECT_EQ(s.mDelivery, MagicDelivery::Touch);
        ASSERT_EQ(s.mEffects.size(), 2u);
        EXPECT_EQ(s.mEffects[0].mEffect, 0x20u);
        EXPECT_FLOAT_EQ(s.mEffects[0].mMagnitude, 3.f);
        EXPECT_EQ(s.mEffects[0].mArea, 7u);
        EXPECT_EQ(s.mEffects[0].mDuration, 5u);
        EXPECT_EQ(s.mEffects[1].mEffect, 0x21u);
        EXPECT_EQ(s.mEffects[0].mConditions, 0u);
    }
}
