// Task 0184 (loot, equip and keys): what the player wields and wears reaches the combat rules, and a potion's health
// reaches the engine's health. The fixtures follow testskyrimsecombatmelee.cpp (a fake data source and a recording
// writer); every number is worked by hand in a comment. Written from the task spec before the code and run red against
// stubs first.
//
// Defaults used: damage = WEAP damage x min(1.5, 1 + skill / 200) (the stand-in player's skills are 15: 1.075), after
// armour x (1 - mitigation / 100); stamina = 20 + weight; armour percent = rating x 0.12 + 3 per main piece, where a
// light piece's rating is base x (1 + light armour skill / 200).
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

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

            mSteel = mSword;
            mSteel.mCommon.mEditorId = "SteelSword";
            mSteel.mDamage = 10;
            mSteel.mWeight = 12.f;
            mSteel.mReach = 1.2f;

            mBow = mSword;
            mBow.mCommon.mEditorId = "Bow";
            mBow.mDamage = 30;
            mBow.mAnimType = WeaponAnimType::Bow;

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

        WeaponRecord mSword, mSteel, mBow;
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
                return mHavePlayer;
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
        bool mHavePlayer = true;
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

    ArmourPiece lightPiece(float rating, std::uint32_t slot)
    {
        ArmourPiece p;
        p.mRating = rating;
        p.mSlotMask = slot;
        p.mType = 0; // light
        return p;
    }

    struct CombatEquip : ::testing::Test
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

        // The player swings (a normal attack) and the sweep finds the victim B. Returns the damage report, if any.
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

    TEST_F(CombatEquip, withNothingEquippedThePlayerSwingsTheTestWeaponAsBefore)
    {
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        // 7 x 1.075 = 7.525, the victim's 20 percent off: 6.02
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f);
        const DamageReport* back = npcHitsPlayer(5);
        ASSERT_NE(back, nullptr);
        EXPECT_NEAR(back->amount, 8.75f, 1e-4f); // 7 x 1.25, no armour
        EXPECT_FLOAT_EQ(mCombat.playerMitigationPercent(), 0.f);
    }

    TEST_F(CombatEquip, anEmptyEquipmentChangesNothing)
    {
        mCombat.setPlayerEquipment({});
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f);
    }

    TEST_F(CombatEquip, anEquippedMeleeWeaponReplacesTheTestWeaponForThePlayerOnly)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mSteel;
        mCombat.setPlayerEquipment(eq);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        // 10 x 1.075 = 10.75; 20 percent off: 8.6
        EXPECT_NEAR(d->amount, 8.6f, 1e-3f);
        // the sweep used the equipped weapon's reach: 1.2 x 141 = 169.2
        EXPECT_NEAR(mWriter.mSweeps[0].second.reach, 169.2f, 1e-3f);
        // the stamina of that swing: 20 + 12 = 32
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), view));
        EXPECT_LT(view.mStamina, 100.f - 31.f); // spent 32, a little may have regenerated
        // an NPC still swings the test weapon: 8.75
        const DamageReport* back = npcHitsPlayer(5);
        ASSERT_NE(back, nullptr);
        EXPECT_NEAR(back->amount, 8.75f, 1e-4f);
    }

    TEST_F(CombatEquip, unequippingTheWeaponBringsTheTestWeaponBack)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mSteel;
        mCombat.setPlayerEquipment(eq);
        ASSERT_NE(playerHitsVictim(), nullptr);
        mCombat.setPlayerEquipment({});
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f);
    }

    TEST_F(CombatEquip, aBowIsNotAMeleeWeaponAndIsIgnoredByTheMeleeRule)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mBow;
        mCombat.setPlayerEquipment(eq);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f); // the test sword
    }

    TEST_F(CombatEquip, equippedArmourTakesDamageOffWhatTheNpcDeals)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mPieces = { lightPiece(25.f, ArmourSlot::Body) };
        mCombat.setPlayerEquipment(eq);
        // rating 25 x (1 + 15 / 200) = 26.875; percent = 26.875 x 0.12 + 100 x 0.03 x 1 piece = 6.225
        const DamageReport* back = npcHitsPlayer(5);
        ASSERT_NE(back, nullptr);
        EXPECT_NEAR(mCombat.playerMitigationPercent(), 6.225f, 1e-3f);
        EXPECT_NEAR(back->amount, 8.75f * (1.f - 0.06225f), 1e-3f);
        // the same armour in the damage report's health: 90 - amount
        EXPECT_NEAR(back->healthAfter, 90.f - back->amount, 1e-3f);
    }

    TEST_F(CombatEquip, takingTheArmourOffRestoresTheStandInsNumber)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mPieces = { lightPiece(25.f, ArmourSlot::Body), lightPiece(10.f, ArmourSlot::Head) };
        mCombat.setPlayerEquipment(eq);
        ASSERT_NE(npcHitsPlayer(5), nullptr);
        EXPECT_GT(mCombat.playerMitigationPercent(), 6.225f); // two pieces
        mCombat.setPlayerEquipment({});
        const DamageReport* back = npcHitsPlayer(6);
        ASSERT_NE(back, nullptr);
        EXPECT_NEAR(back->amount, 8.75f, 1e-4f);
        EXPECT_FLOAT_EQ(mCombat.playerMitigationPercent(), 0.f);
    }

    TEST_F(CombatEquip, aQueuedHealIsAppliedThroughTheEnginesHealthAtTheNextUpdate)
    {
        // the engine says 60 of 100
        mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 60.f, 100.f };
        EXPECT_TRUE(mCombat.queuePlayerHeal(25.f));
        EXPECT_TRUE(mWriter.mDamage.empty()); // nothing happens inside the call
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const DamageReport& r = mWriter.mDamage[0];
        EXPECT_TRUE(r.victimIsPlayer);
        EXPECT_TRUE(r.attackerIsPlayer);
        EXPECT_FLOAT_EQ(r.amount, 0.f);
        EXPECT_FLOAT_EQ(r.healthAfter, 85.f);
        EXPECT_FLOAT_EQ(r.healthMax, 100.f);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u); // once
    }

    TEST_F(CombatEquip, aHealNeverGoesPastTheMaximumOrBelowNothingAndBadNumbersAreRefused)
    {
        mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        EXPECT_TRUE(mCombat.queuePlayerHeal(50.f));
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthAfter, 100.f);
        EXPECT_FALSE(mCombat.queuePlayerHeal(-5.f));
        EXPECT_FALSE(mCombat.queuePlayerHeal(0.f));
        EXPECT_FALSE(mCombat.queuePlayerHeal(std::numeric_limits<float>::quiet_NaN()));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
    }

    TEST_F(CombatEquip, withNoCombatDataAHealIsRefused)
    {
        SkyrimCombat bare;
        EXPECT_FALSE(bare.queuePlayerHeal(25.f));
    }

    TEST_F(CombatEquip, aHealWaitsForTheEnginesPlayerHealth)
    {
        mWriter.mHavePlayer = false;
        EXPECT_TRUE(mCombat.queuePlayerHeal(25.f));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(mWriter.mDamage.empty()); // no engine health yet: kept
        mWriter.mHavePlayer = true;
        mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 10.f, 100.f };
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthAfter, 35.f);
    }
}
