// Task 0200: the combat's side of the player's progression. What a fight reports to the skill hook (a weapon hit, a
// block, a hit taken in armour, a spell) and what a progression changes in the player's fighter (skills, vitals).
// Fixtures follow testskyrimsecombatequip.cpp; the numbers are worked by hand in the comments. Written AFTER the code
// (not run red against stubs: a mutation check instead, see the task log).
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/progression.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
            attacker.mSkills[0] = 50;
            mProfiles[kAttackerBase] = attacker;
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMitigationPercent = 20.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
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

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Attack;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    progression::SkillTable makeTable()
    {
        progression::SkillTable table;
        for (std::size_t s = 0; s < progression::kSkillCount; ++s)
        {
            progression::SkillRecord r;
            r.mCommon.mId = 0x44C + static_cast<FormId>(s);
            r.mCommon.mEditorId = std::string(progression::skillEditorId(s));
            r.mHasSkillData = true;
            r.mUseMult = 1.f;
            r.mImproveMult = 1.f;
            table.add(r);
        }
        return table;
    }

    struct CombatSkills : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mB{ 20, 1 };
        std::vector<std::pair<std::size_t, float>> mUses;

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

        void hook()
        {
            mCombat.setSkillUseHook([this](std::size_t slot, float amount) { mUses.emplace_back(slot, amount); });
        }

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
            for (int i = 0; i < 10; ++i)
                mCombat.combatUpdate(0.2f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }

        const DamageReport* npcHitsPlayer(std::uint32_t token)
        {
            mWriter.mDamage.clear();
            mCombat.onClipEvent(hitEvent(CombatSubject::of(ActorHandle{ 10, 1 }), token));
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit hit;
            hit.target = SweepTarget::Player;
            hit.distance = 30.f;
            mCombat.onSweepResult(CombatSubject::of(ActorHandle{ 10, 1 }), token, &hit, 1);
            mCombat.combatUpdate(0.016f, mWriter);
            return mWriter.mDamage.empty() ? nullptr : &mWriter.mDamage[0];
        }
    };

    TEST_F(CombatSkills, withNoHookAndNoProgressionTheFightIsAsBefore)
    {
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f); // 7 x 1.075 x 0.8
        EXPECT_TRUE(mUses.empty());
    }

    TEST_F(CombatSkills, aWeaponHitByThePlayerReportsTheWeaponSkillAndTheDamageDealt)
    {
        hook();
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        ASSERT_EQ(mUses.size(), 1u);
        EXPECT_EQ(mUses[0].first, progression::Slot::OneHanded);
        EXPECT_NEAR(mUses[0].second, 6.02f, 1e-3f);
        EXPECT_NEAR(d->amount, 6.02f, 1e-3f); // the hook changes nothing
    }

    TEST_F(CombatSkills, aHitTakenWithNoArmourTrainsNothing)
    {
        hook();
        ASSERT_NE(npcHitsPlayer(5), nullptr);
        EXPECT_TRUE(mUses.empty());
    }

    TEST_F(CombatSkills, aHitTakenInLightOrHeavyArmourTrainsThatSkillWithTheDamageTaken)
    {
        hook();
        SkyrimCombat::PlayerEquipment light;
        ArmourPiece piece;
        piece.mRating = 25.f;
        piece.mSlotMask = ArmourSlot::Body;
        piece.mType = 0;
        light.mPieces = { piece };
        mCombat.setPlayerEquipment(light);
        const DamageReport* d = npcHitsPlayer(5);
        ASSERT_NE(d, nullptr);
        ASSERT_EQ(mUses.size(), 1u);
        EXPECT_EQ(mUses[0].first, progression::Slot::LightArmor);
        EXPECT_NEAR(mUses[0].second, d->amount, 1e-4f);
        mUses.clear();
        piece.mType = 1; // heavy
        SkyrimCombat::PlayerEquipment heavy;
        heavy.mPieces = { piece };
        mCombat.setPlayerEquipment(heavy);
        d = npcHitsPlayer(6);
        ASSERT_NE(d, nullptr);
        ASSERT_EQ(mUses.size(), 1u);
        EXPECT_EQ(mUses[0].first, progression::Slot::HeavyArmor);
    }

    TEST_F(CombatSkills, aProgressionGivesThePlayerItsOwnSkillsInTheFight)
    {
        progression::SkillTable table = makeTable();
        progression::PlayerProgression p(table, nullptr);
        p.setStart(1, 50); // One-Handed 50: 7 x (1 + 50 / 200) = 8.75, 20 percent off: 7.0
        mCombat.setPlayerProgression(&p);
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 7.0f, 1e-3f);
    }

    TEST_F(CombatSkills, aProgressionChangeIsReadAgainAndALevelUpRaisesTheMaximums)
    {
        progression::SkillTable table = makeTable();
        progression::PlayerProgression p(table, nullptr);
        mCombat.setPlayerProgression(&p);
        mCombat.combatUpdate(0.016f, mWriter);
        SkyrimCombat::FighterView view;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), view));
        EXPECT_FLOAT_EQ(view.mStaminaMax, 100.f);
        (void)p.useSkill(0, 1.0e6f);
        ASSERT_TRUE(p.chooseLevelUp(progression::Vital::Stamina));
        ASSERT_TRUE(p.chooseLevelUp(progression::Vital::Health));
        mWriter.mDamage.clear();
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), view));
        EXPECT_FLOAT_EQ(view.mStaminaMax, 110.f);
        EXPECT_FLOAT_EQ(view.mMagickaMax, 100.f);
        // the health gain reaches the engine's health: a report that takes none off, +10 on the engine's 90
        ASSERT_FALSE(mWriter.mDamage.empty());
        const DamageReport& r = mWriter.mDamage.back();
        EXPECT_TRUE(r.victimIsPlayer);
        EXPECT_FLOAT_EQ(r.amount, 0.f);
        EXPECT_FLOAT_EQ(r.healthMax, 110.f);
        EXPECT_FLOAT_EQ(r.healthAfter, 100.f);
        // skills now 100 for One-Handed: the next blow uses it: 7 x 1.5 = 10.5 x 0.8
        const DamageReport* d = playerHitsVictim();
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->amount, 7.f * std::min(1.5f, 1.f + static_cast<float>(p.skill(0)) / 200.f) * 0.8f, 1e-3f);
    }
}
