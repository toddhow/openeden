// Task 0172 (milestone M-S5, stage S5-4): block, bash, power attack and stagger in the Skyrim pack (game_skyrimse/
// combat.hpp, combat_stats.hpp), tested through the contract with a fake data source and a recording writer. Written
// from the task's spec (docs/tasks/0172-skyrim-combat-s5-4.md) before the code. Every number is worked out by hand in a
// comment. The fixture is the S5-3 one: an "Iron Sword" of damage 7, weight 9, reach 1.0 (141 units), the attacker's
// One-handed skill 50 (x 1.25), the victim's armour 20 percent: a normal blow takes 7 x 1.25 x 0.8 = 7.0 health, a
// power attack 14.0.
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

    // ---- the pure numbers ----------------------------------------------------------------------------------------

    TEST(SkyrimCombatBlockNumbers, theDefaultsAreTheValuesTheDataStores)
    {
        const BlockSettings s;
        EXPECT_FLOAT_EQ(s.mBlockMax, 0.7f);
        EXPECT_FLOAT_EQ(s.mBlockWeaponBase, 0.3f);
        EXPECT_FLOAT_EQ(s.mBlockWeaponScaling, 0.2f);
        EXPECT_FLOAT_EQ(s.mBlockSkillMult, 2.0f);
        EXPECT_FLOAT_EQ(s.mBlockPowerAttackMult, 0.66f);
        EXPECT_FLOAT_EQ(s.mStaminaBlockDmgMult, 0.25f);
        EXPECT_FLOAT_EQ(s.mStaminaBlockBase, 0.f);
        EXPECT_FLOAT_EQ(s.mStaminaBashBase, 35.f);
        EXPECT_FLOAT_EQ(s.mStaggerThreshold, 1.f);
        EXPECT_FLOAT_EQ(s.mPowerAttackStaggerBonus, 1.f);
    }

    TEST(SkyrimCombatBlockNumbers, theBlockFractionGrowsWithTheSkillAndReachesTheMaximumAtOneHundred)
    {
        const BlockSettings s;
        BlockInput in;
        in.mDamage = 10.f;
        in.mBlockerStamina = 100.f;
        // skill 0: 0.3 + 0.2 x 0 = 0.3; blocked 3, taken 7, the stamina paid is 3 x 0.25 = 0.75.
        in.mBlockerSkill = 0.f;
        BlockResult r = computeBlock(in, s);
        EXPECT_TRUE(r.mBlocked);
        EXPECT_FALSE(r.mBroken);
        EXPECT_NEAR(r.mFraction, 0.3f, 1e-6f);
        EXPECT_NEAR(r.mDamageTaken, 7.f, 1e-5f);
        EXPECT_NEAR(r.mStaminaDrain, 0.75f, 1e-5f);
        // skill 15: 0.3 + 0.2 x 15 x 2 / 100 = 0.36.
        in.mBlockerSkill = 15.f;
        r = computeBlock(in, s);
        EXPECT_NEAR(r.mFraction, 0.36f, 1e-6f);
        EXPECT_NEAR(r.mDamageTaken, 6.4f, 1e-5f);
        // skill 100: 0.3 + 0.2 x 2 = 0.7 = fBlockMax; above 100 the skill is clamped.
        in.mBlockerSkill = 100.f;
        EXPECT_NEAR(computeBlock(in, s).mFraction, 0.7f, 1e-6f);
        in.mBlockerSkill = 250.f;
        EXPECT_NEAR(computeBlock(in, s).mFraction, 0.7f, 1e-6f);
        // A raised maximum below the formula caps it: fBlockMax 0.5 at skill 100.
        BlockSettings capped = s;
        capped.mBlockMax = 0.5f;
        in.mBlockerSkill = 100.f;
        EXPECT_NEAR(computeBlock(in, capped).mFraction, 0.5f, 1e-6f);
    }

    TEST(SkyrimCombatBlockNumbers, aPowerAttackIsBlockedLess)
    {
        const BlockSettings s;
        BlockInput in;
        in.mDamage = 10.f;
        in.mBlockerStamina = 100.f;
        in.mPowerAttack = true;
        in.mBlockerSkill = 0.f;
        // 0.3 x fBlockPowerAttackMult 0.66 = 0.198; blocked 1.98, taken 8.02, stamina 0.495.
        const BlockResult r = computeBlock(in, s);
        EXPECT_NEAR(r.mFraction, 0.198f, 1e-6f);
        EXPECT_NEAR(r.mDamageTaken, 8.02f, 1e-5f);
        EXPECT_NEAR(r.mStaminaDrain, 0.495f, 1e-5f);
    }

    TEST(SkyrimCombatBlockNumbers, aBlockerWithTooLittleStaminaHasItBrokenAndTakesTheWholeBlow)
    {
        const BlockSettings s;
        BlockInput in;
        in.mDamage = 10.f;
        in.mBlockerSkill = 0.f;
        in.mBlockerStamina = 0.5f; // the block would cost 0.75
        const BlockResult r = computeBlock(in, s);
        EXPECT_FALSE(r.mBlocked);
        EXPECT_TRUE(r.mBroken);
        EXPECT_FLOAT_EQ(r.mFraction, 0.f);
        EXPECT_FLOAT_EQ(r.mDamageTaken, 10.f);
        EXPECT_FLOAT_EQ(r.mStaminaDrain, 0.5f); // all it had
        // Exactly enough holds.
        in.mBlockerStamina = 0.75f;
        EXPECT_TRUE(computeBlock(in, s).mBlocked);
    }

    TEST(SkyrimCombatBlockNumbers, theStaminaBaseIsPaidEvenForAFeebleBlow)
    {
        BlockSettings s;
        s.mStaminaBlockBase = 2.f;
        BlockInput in;
        in.mDamage = 0.f;
        in.mBlockerStamina = 10.f;
        const BlockResult r = computeBlock(in, s);
        EXPECT_TRUE(r.mBlocked);
        EXPECT_FLOAT_EQ(r.mStaminaDrain, 2.f);
        in.mBlockerStamina = 1.f;
        EXPECT_TRUE(computeBlock(in, s).mBroken);
    }

    TEST(SkyrimCombatBlockNumbers, nonFiniteAndNegativeInputsCountAsZero)
    {
        const BlockSettings s;
        BlockInput in;
        in.mDamage = std::numeric_limits<float>::quiet_NaN();
        in.mBlockerSkill = -5.f;
        in.mBlockerStamina = std::numeric_limits<float>::infinity();
        const BlockResult r = computeBlock(in, s);
        EXPECT_TRUE(std::isfinite(r.mDamageTaken));
        EXPECT_TRUE(std::isfinite(r.mStaminaDrain));
        EXPECT_TRUE(std::isfinite(r.mFraction));
        EXPECT_FLOAT_EQ(r.mDamageTaken, 0.f);
        in.mDamage = -4.f;
        in.mBlockerStamina = 5.f;
        EXPECT_FLOAT_EQ(computeBlock(in, s).mDamageTaken, 0.f);
    }

    TEST(SkyrimCombatBlockNumbers, aPowerAttackStaggersWhateverTheWeaponAndANormalBlowOnlyWithAHeavyOne)
    {
        const BlockSettings s;
        EXPECT_FLOAT_EQ(staggerMagnitude(0.f, true, s), 1.f);
        EXPECT_FLOAT_EQ(staggerMagnitude(0.5f, true, s), 1.5f);
        EXPECT_FLOAT_EQ(staggerMagnitude(0.5f, false, s), 0.5f);
        EXPECT_TRUE(causesStagger(0.f, true, s));
        EXPECT_FALSE(causesStagger(0.f, false, s));
        EXPECT_FALSE(causesStagger(0.99f, false, s));
        EXPECT_TRUE(causesStagger(1.0f, false, s));
        EXPECT_TRUE(causesStagger(1.2f, false, s));
        EXPECT_FALSE(causesStagger(std::numeric_limits<float>::quiet_NaN(), false, s));
    }

    TEST(SkyrimCombatBlockNumbers, aBashCostsTheBashBase)
    {
        BlockSettings s;
        EXPECT_FLOAT_EQ(bashStaminaCost(s), 35.f);
        s.mStaminaBashBase = 12.f;
        EXPECT_FLOAT_EQ(bashStaminaCost(s), 12.f);
        s.mStaminaBashBase = -3.f;
        EXPECT_FLOAT_EQ(bashStaminaCost(s), 0.f);
    }

    TEST(SkyrimCombatBlockNumbers, theTableGivesTheGmstsItHasAndTheDefaultsForTheRest)
    {
        SettingsTable table;
        BlockSettings s = table.blockSettings();
        EXPECT_FLOAT_EQ(s.mBlockMax, 0.7f);
        table.set("fBlockMax", 0.5f);
        table.set("fStaminaBashBase", 20.f);
        table.set("fBlockPowerAttackMult", 0.5f);
        table.set("fStaminaBlockDmgMult", 0.1f);
        table.set("fBlockWeaponBase", 0.4f);
        table.set("fBlockWeaponScaling", 0.1f);
        table.set("fBlockSkillMult", 3.f);
        table.set("fStaminaBlockBase", 1.f);
        s = table.blockSettings();
        EXPECT_FLOAT_EQ(s.mBlockMax, 0.5f);
        EXPECT_FLOAT_EQ(s.mStaminaBashBase, 20.f);
        EXPECT_FLOAT_EQ(s.mBlockPowerAttackMult, 0.5f);
        EXPECT_FLOAT_EQ(s.mStaminaBlockDmgMult, 0.1f);
        EXPECT_FLOAT_EQ(s.mBlockWeaponBase, 0.4f);
        EXPECT_FLOAT_EQ(s.mBlockWeaponScaling, 0.1f);
        EXPECT_FLOAT_EQ(s.mBlockSkillMult, 3.f);
        EXPECT_FLOAT_EQ(s.mStaminaBlockBase, 1.f);
        // The two numbers that are ours are never read from a record.
        EXPECT_FLOAT_EQ(s.mStaggerThreshold, 1.f);
        EXPECT_FLOAT_EQ(s.mPowerAttackStaggerBonus, 1.f);
    }

    // ---- the rules through the contract ----------------------------------------------------------------------------

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
        BlockSettings blockSettings() override { return mBlock; }

        WeaponRecord mSword;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        CombatSettings mSettings;
        BlockSettings mBlock;
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
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
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
        std::size_t clipsOf(ClipSlot slot) const
        {
            std::size_t n = 0;
            for (const Clip& c : mClips)
                if (c.mSlot == slot)
                    ++n;
            return n;
        }
        const Clip* lastClip(ClipSlot slot) const
        {
            for (auto it = mClips.rbegin(); it != mClips.rend(); ++it)
                if (it->mSlot == slot)
                    return &*it;
            return nullptr;
        }

        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, std::uint32_t session, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, session };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token, ClipSlot slot)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    SweepHit hitOn(ActorHandle actor)
    {
        SweepHit h;
        h.target = SweepTarget::Actor;
        h.actor = actor;
        h.distance = 20.f;
        return h;
    }

    struct CombatBlock : ::testing::Test
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

        void swing(
            CombatSubject attacker, std::uint32_t token, std::vector<SweepHit> hits, ClipSlot slot = ClipSlot::Attack)
        {
            mCombat.onClipEvent(hitEvent(attacker, token, slot));
            mCombat.combatUpdate(0.016f, mWriter);
            mCombat.onSweepResult(attacker, token, hits.data(), hits.size());
            mCombat.combatUpdate(0.016f, mWriter);
        }
        SkyrimCombat::FighterView view(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v;
        }
    };

    // -- block ---------------------------------------------------------------------------------------------------

    TEST_F(CombatBlock, anActorNamedByTheBlockSettingBlocksAndOthersDoNot)
    {
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim")); // seen again: now a blocker
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_TRUE(view(mB).mBlocking);
        EXPECT_FALSE(view(mB).mStaggered);
        // The attacker is not named; it is ready once it takes part in a blow.
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        EXPECT_FALSE(view(mA).mBlocking);
    }

    TEST_F(CombatBlock, aDeadBlockerDoesNotBlock)
    {
        mData.mProfiles[kVictimBase].mHealthMax = 1.f;
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5, { hitOn(mB) }); // 4.9 health through the block kills it
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_FALSE(view(mB).mBlocking);
    }

    TEST_F(CombatBlock, aBlockedBlowTakesTheFormulaDamageAndPlaysTheBlockHitClip)
    {
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        // 7.0 after armour; the victim's Block skill is 0: fraction 0.3, blocked 2.1, taken 4.9; health 50 - 4.9.
        EXPECT_NEAR(mWriter.mDamage[0].amount, 4.9f, 1e-4f);
        EXPECT_NEAR(mWriter.mDamage[0].healthAfter, 45.1f, 1e-4f);
        // The blocker paid 2.1 x 0.25 = 0.525 stamina: 80 - 0.525.
        EXPECT_NEAR(view(mB).mStamina, 79.475f, 1e-4f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_TRUE(mWriter.mClips[0].mSubject == CombatSubject::of(mB));
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::BlockHit);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_BlockHit");
        EXPECT_FALSE(view(mB).mStaggered);
    }

    TEST_F(CombatBlock, aHigherBlockSkillBlocksMoreAndCapsAtTheMaximum)
    {
        mData.mProfiles[kVictimBase].mSkills[3] = 100; // slot 3 is Block
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        // fraction 0.7: blocked 4.9, taken 2.1.
        EXPECT_NEAR(mWriter.mDamage[0].amount, 2.1f, 1e-4f);
    }

    TEST_F(CombatBlock, aBlockedPowerAttackIsBlockedLessAndStillStaggersNobody)
    {
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        // 14.0 after armour; fraction 0.3 x 0.66 = 0.198; blocked 2.772; taken 11.228.
        EXPECT_NEAR(mWriter.mDamage[0].amount, 11.228f, 1e-3f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::BlockHit); // the block held: no stagger
        EXPECT_FALSE(view(mB).mStaggered);
    }

    TEST_F(CombatBlock, aBlockerThatCannotPayHasItBrokenTakesTheWholeBlowAndStaggers)
    {
        mData.mProfiles[kVictimBase].mStaminaMax = 0.3f; // a block would cost 0.525
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.0f, 1e-4f);
        EXPECT_FLOAT_EQ(view(mB).mStamina, 0.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Stagger);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_StaggerBackMedium");
        EXPECT_TRUE(view(mB).mStaggered);
    }

    TEST_F(CombatBlock, aBlockerThatIsNotNamedTakesTheFullBlowAsBefore)
    {
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.0f, 1e-4f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil);
        EXPECT_FALSE(view(mB).mBlocking);
    }

    TEST_F(CombatBlock, aSwingingBlockerDoesNotBlockAndABlockerBlocksAgainOnceItsSwingEnds)
    {
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        ASSERT_TRUE(mCombat.setTestHostile("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        // Put the victim in reach of the player so that it swings.
        mWriter.mStates[20] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_EQ(mWriter.clipsOf(ClipSlot::Attack), 1u);
        const FakeWriter::Clip swingClip = *mWriter.lastClip(ClipSlot::Attack);
        EXPECT_FALSE(view(mB).mBlocking); // mid-swing
        // Its own blow lands on the attacker while it is swinging: the attacker is not blocking (not named), and the
        // swinging victim takes a blow from the attacker at full strength.
        swing(CombatSubject::of(mA), 9, { hitOn(mB) });
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.0f, 1e-4f); // not blocked
        // The recoil replaced the swing, so the victim is free again and blocks.
        mCombat.onClipFinished(CombatSubject::of(mB), swingClip.mToken, ClipEnd::Interrupted);
        EXPECT_TRUE(view(mB).mBlocking);
    }

    // -- stagger -------------------------------------------------------------------------------------------------

    TEST_F(CombatBlock, aPowerAttackStaggersItsTargetWithTheStaggerClip)
    {
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_TRUE(mWriter.mClips[0].mSubject == CombatSubject::of(mB));
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Stagger);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_StaggerBackMedium");
        EXPECT_TRUE(view(mB).mStaggered);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 14.0f, 1e-4f); // the S5-3 power attack numbers hold
    }

    TEST_F(CombatBlock, aNormalBlowRecoilsUnlessTheWeaponIsHeavyEnough)
    {
        swing(CombatSubject::of(mA), 5, { hitOn(mB) });
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil);
        // A weapon with stagger 0.5 still recoils; one with 1.2 staggers.
        mData.mSword.mStagger = 0.5f;
        swing(CombatSubject::of(mA), 6, { hitOn(mB) });
        EXPECT_EQ(mWriter.mClips.back().mSlot, ClipSlot::Recoil);
        mData.mSword.mStagger = 1.2f;
        swing(CombatSubject::of(mA), 7, { hitOn(mB) });
        EXPECT_EQ(mWriter.mClips.back().mSlot, ClipSlot::Stagger);
    }

    TEST_F(CombatBlock, theStaggerThresholdIsTunable)
    {
        mData.mBlock.mStaggerThreshold = 5.f;
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Recoil); // 1.0 < 5
    }

    TEST_F(CombatBlock, aStaggeredFighterIsFreeAgainWhenTheClipEnds)
    {
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack);
        ASSERT_TRUE(view(mB).mStaggered);
        const std::uint32_t token = mWriter.mClips[0].mToken;
        // Another clip's end (a token that is not the stagger's) changes nothing.
        mCombat.onClipFinished(CombatSubject::of(mB), token + 100, ClipEnd::Completed);
        EXPECT_TRUE(view(mB).mStaggered);
        mCombat.onClipFinished(CombatSubject::of(mB), token, ClipEnd::Completed);
        EXPECT_FALSE(view(mB).mStaggered);
    }

    TEST_F(CombatBlock, aStaggeredHostileDoesNotAttackUntilItsStaggerEnds)
    {
        ASSERT_TRUE(mCombat.setTestHostile("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        mWriter.mStates[20] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter); // it swings (attack 1)
        ASSERT_EQ(mWriter.clipsOf(ClipSlot::Attack), 1u);
        mWriter.mClips.clear();
        swing(CombatSubject::of(mA), 6, { hitOn(mB) }, ClipSlot::PowerAttack); // staggers it
        ASSERT_EQ(mWriter.clipsOf(ClipSlot::Stagger), 1u);
        const std::uint32_t token = mWriter.lastClip(ClipSlot::Stagger)->mToken;
        mCombat.combatUpdate(5.f, mWriter); // well past the attack cooldown, still staggered
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Attack), 0u);
        mCombat.onClipFinished(CombatSubject::of(mB), token, ClipEnd::Completed);
        mCombat.combatUpdate(0.1f, mWriter);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Attack), 1u);
    }

    // -- bash ----------------------------------------------------------------------------------------------------

    TEST_F(CombatBlock, aBashCostsTheBashStaminaDealsNoDamageAndStaggers)
    {
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 8, ClipSlot::Bash));
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        EXPECT_EQ(mWriter.mSweeps[0].second.token, 8u);
        EXPECT_FLOAT_EQ(view(mA).mStamina, 100.f - 35.f); // fStaminaBashBase
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::of(mA), 8, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].amount, 0.f);
        EXPECT_FLOAT_EQ(mWriter.mDamage[0].healthAfter, 50.f);
        EXPECT_FLOAT_EQ(view(mB).mHealth, 50.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Stagger);
        EXPECT_TRUE(view(mB).mStaggered);
    }

    TEST_F(CombatBlock, aBashBreaksThroughABlockWithoutSpendingTheBlockersStamina)
    {
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mCombat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 8, ClipSlot::Bash));
        mCombat.combatUpdate(0.016f, mWriter);
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::of(mA), 8, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::BlockHit), 0u);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Stagger), 1u);
        EXPECT_FLOAT_EQ(view(mB).mStamina, 80.f);
    }

    TEST_F(CombatBlock, aBashRespectsTheSettingForItsCost)
    {
        mData.mBlock.mStaminaBashBase = 10.f;
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 8, ClipSlot::Bash));
        mCombat.combatUpdate(0.016f, mWriter);
        EXPECT_FLOAT_EQ(view(mA).mStamina, 90.f);
    }

    // -- the NPC's choice ----------------------------------------------------------------------------------------

    TEST_F(CombatBlock, aHostileActorsEveryNthAttackIsAPowerAttackAndEveryMthABash)
    {
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mCombat.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
        mCombat.setTestPowerEvery(2);
        mCombat.setTestBashEvery(3);
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f }; // in reach of the player at the origin
        std::vector<ClipSlot> slots;
        for (int i = 0; i < 6; ++i)
        {
            const std::size_t before = mWriter.mClips.size();
            mCombat.combatUpdate(2.6f, mWriter); // past the cooldown
            ASSERT_EQ(mWriter.mClips.size(), before + 1) << "attack " << (i + 1);
            slots.push_back(mWriter.mClips.back().mSlot);
            mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips.back().mToken, ClipEnd::Completed);
        }
        // 1 normal, 2 power, 3 bash, 4 power, 5 normal, 6 bash (bash wins on a shared number).
        const std::vector<ClipSlot> expected = { ClipSlot::Attack, ClipSlot::PowerAttack, ClipSlot::Bash,
            ClipSlot::PowerAttack, ClipSlot::Attack, ClipSlot::Bash };
        EXPECT_EQ(slots, expected);
        EXPECT_EQ(mWriter.mClips[1].mName, "1HM_AttackPower");
        EXPECT_EQ(mWriter.mClips[2].mName, "1HM_BlockBash");
    }

    TEST_F(CombatBlock, withTheEveryNSettingsOffEveryAttackIsTheNormalOne)
    {
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mCombat.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
        mCombat.setTestPowerEvery(0);
        mCombat.setTestBashEvery(-4);
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        for (int i = 0; i < 5; ++i)
        {
            mCombat.combatUpdate(2.6f, mWriter);
            ASSERT_EQ(mWriter.mClips.size(), static_cast<std::size_t>(i + 1));
            EXPECT_EQ(mWriter.mClips.back().mSlot, ClipSlot::Attack);
            mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips.back().mToken, ClipEnd::Completed);
        }
    }

    TEST_F(CombatBlock, theChoiceIsRepeatableTwoRunsGiveTheSameTrace)
    {
        auto run = [](std::vector<std::string>& trace) {
            FakeData data;
            SkyrimCombat combat;
            FakeWriter writer;
            combat.setData(&data);
            combat.setTestWeapon("IronSword");
            combat.setTrace([&trace](const std::string& line) { trace.push_back(line); });
            combat.setTestHostile("attacker");
            combat.setTestPowerEvery(2);
            combat.setTestBashEvery(3);
            combat.setTestBlock("victim");
            combat.onActorAdded(info(10, 1, kAttackerBase, "Attacker"));
            combat.onActorAdded(info(20, 1, kVictimBase, "Victim"));
            writer.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
            writer.mStates[20] = { 100.0, 160.0, 5.0, 0.f, 0.f, 0.f };
            writer.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
            for (std::uint32_t i = 1; i <= 8; ++i)
            {
                combat.combatUpdate(2.6f, writer);
                if (!writer.mClips.empty())
                {
                    const FakeWriter::Clip last = writer.mClips.back();
                    ClipEvent e;
                    e.subject = last.mSubject;
                    e.slot = last.mSlot;
                    e.token = last.mToken;
                    e.kind = ClipEventKind::Hit;
                    combat.onClipEvent(e);
                    combat.combatUpdate(0.016f, writer);
                    const SweepHit hit = hitOn({ 20, 1 });
                    combat.onSweepResult(last.mSubject, last.mToken, &hit, 1);
                    combat.combatUpdate(0.016f, writer);
                    combat.onClipFinished(last.mSubject, last.mToken, ClipEnd::Completed);
                    writer.mClips.clear();
                }
            }
        };
        std::vector<std::string> first;
        std::vector<std::string> second;
        run(first);
        run(second);
        ASSERT_FALSE(first.empty());
        EXPECT_EQ(first, second);
    }

    // -- the player's key ----------------------------------------------------------------------------------------

    TEST_F(CombatBlock, aPlayerPowerAttackKeyStartsTheSwingOfThePowerClipAndTheHitIsAPowerHit)
    {
        mCombat.onPlayerAttack(true);
        mCombat.combatUpdate(0.016f, mWriter);
        // The swing runs on the hidden clock; the hit comes at the sidecar's HitFrame time (0.367 s).
        mCombat.combatUpdate(0.4f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        EXPECT_TRUE(mWriter.mSweeps[0].first.player);
        // The sweep finds the victim: 14.0 for a power attack (the player's skill 15 gives 7 x 1.075 x 2 x 0.8).
        const SweepHit hit = hitOn(mB);
        mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f * 1.075f * 2.f * 0.8f, 1e-3f);
        EXPECT_EQ(mWriter.clipsOf(ClipSlot::Stagger), 1u); // and it staggers
    }
}
