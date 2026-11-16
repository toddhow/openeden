// Task 0249 (the player's first-person hands, third slice): the pack half. What the pack tells the core about the
// player's bow and about a spell he holds (SkyrimCombat::playerHands, game_skyrimse/player_hands.hpp, and the trailing
// PlayerBodyState fields of player_body.hpp 0.3), and the bow as a held weapon of the first-person arms. A fake data
// source and a recording writer; every number is derived by hand: the bow "IronBow" draws to bowDrawn at 1.66667 s,
// "Healing" is a concentration spell on the caster (cost 12 a second, the cast goes 0.367 s after the press, the combat
// clock stepping 0.25 s), "WardLesser" is a concentration ward, "Firebolt" fire and forget.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/player_body.hpp>
#include <game_skyrimse/player_hands.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/player_body.hpp>

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

    constexpr FormId kFirebolt = 0x1000, kHealing = 0x1001, kWard = 0x1003;
    constexpr FormId kFireEffect = 0x2000, kHealEffect = 0x2003, kAccumulateEffect = 0x2005, kPeakEffect = 0x2006;
    constexpr FormId kProjectile = 0x3000;
    constexpr float kPi = 3.14159265358979f;

    class HandsData final : public CombatData
    {
    public:
        HandsData()
        {
            mBow.mCommon.mEditorId = "IronBow";
            mBow.mHasData = true;
            mBow.mDamage = 10;
            mBow.mWeight = 5.f;
            mBow.mHasDnam = true;
            mBow.mAnimType = WeaponAnimType::Bow;
            mBow.mSpeed = 1.f;
            mBow.mReach = 1.f;
            mBow.mSkillValue = kFirstSkillActorValue + 2;
            mBow.mMinRange = 500.f;
            mBow.mMaxRange = 2500.f;
            mAmmo.mCommon.mEditorId = "IronArrow";
            mAmmo.mHasData = true;
            mAmmo.mDamage = 8.f;
            mAmmo.mProjectile = 77;
            mArrow.mCommon.mEditorId = "IronArrowProjectile";
            mArrow.mHasData = true;
            mArrow.mType = ProjectileRecord::kTypeArrow;
            mArrow.mSpeed = 6000.f;
            mArrow.mRange = 15000.f;
            mArrow.mModel = "Weapons\\Iron\\IronArrowProjectile.nif";
            mBolt.mCommon.mId = kProjectile;
            mBolt.mHasData = true;
            mBolt.mType = 1;
            mBolt.mSpeed = 2500.f;
            mBolt.mRange = 10000.f;
            mBolt.mModel = "Magic\\FireBoltProjectile.nif";

            addEffect(kFireEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile, 41);
            addEffect(kHealEffect, 0, 24, 0x201000);
            addEffect(kAccumulateEffect, MagicArchetype::AccumulateMagnitude, 63, 0x40001002);
            addEffect(kPeakEffect, MagicArchetype::PeakValueModifier, 39, 0x40001002);
            addSpell(kFirebolt, "Firebolt", 41, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kFireEffect, 25.f, 0 } });
            addSpell(kHealing, "Healing", 12, MagicCast::Concentration, MagicDelivery::Self,
                { { kHealEffect, 10.f, 1 } });
            addSpell(kWard, "WardLesser", 34, MagicCast::Concentration, MagicDelivery::Self,
                { { kAccumulateEffect, 40.f, 0 }, { kPeakEffect, 40.f, 0 } });

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId, FighterProfile&) override { return false; }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            return editorId == "ironbow" ? &mBow : nullptr;
        }
        const AmmoRecord* ammoByEditorId(std::string_view editorId) override
        {
            return editorId == "ironarrow" ? &mAmmo : nullptr;
        }
        const ProjectileRecord* projectileOf(const AmmoRecord& ammo) override
        {
            return ammo.mProjectile == 77 ? &mArrow : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mBolt : nullptr; }
        MagicSettings magicSettings() override { return MagicSettings(); }

        struct E
        {
            FormId mEffect;
            float mMagnitude;
            std::uint32_t mDuration;
        };
        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags,
            FormId projectile = kNullFormId, std::int32_t resist = -1)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            m.mResistValue = resist;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t cast, std::uint32_t delivery,
            const std::vector<E>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = cast;
            s.mDelivery = delivery;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                se.mDuration = e.mDuration;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        WeaponRecord mBow;
        AmmoRecord mAmmo;
        ProjectileRecord mArrow;
        ProjectileRecord mBolt;
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        FighterProfile mPlayer;
    };

    class HandsWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (!mTimesReady)
                return false;
            if (kind == ClipEventKind::BowDrawn && clip == "Bow_DrawLight")
            {
                out = 1.66667f;
                return true;
            }
            if (kind == ClipEventKind::SpellFire && clip == "MRh_SelfRelease")
            {
                out = 0.367f;
                return true;
            }
            if (kind == ClipEventKind::SpellFire && clip == "MRh_Release")
            {
                out = 0.167f;
                return true;
            }
            return false;
        }
        bool launchProjectile(CombatSubject, const ProjectileRequest&) override
        {
            ++mLaunches;
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
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
            if (!subject.player)
                return false;
            out = mPlayerState;
            return true;
        }
        SubjectState mPlayerState;
        int mLaunches = 0;
        bool mTimesReady = true;
    };

    // ---- the pure rules ---------------------------------------------------------------------------------------------

    TEST(SkyrimPlayerHandsClips, everyHoldHasItsFirstPersonLoopAndNoneHasNone)
    {
        EXPECT_TRUE(handsHoldClip(HandsHold::None, false, false).empty());
        EXPECT_TRUE(handsHoldClip(HandsHold::None, true, true).empty());
        EXPECT_EQ(handsHoldClip(HandsHold::Charging, false, false), "MRh_ChargeLoop");
        EXPECT_EQ(handsHoldClip(HandsHold::Charging, true, false), "MRh_SelfChargeLoop");
        EXPECT_EQ(handsHoldClip(HandsHold::Charging, true, true), "MRh_WardCharge");
        EXPECT_EQ(handsHoldClip(HandsHold::Holding, false, false), "MRh_AimedConcentration");
        EXPECT_EQ(handsHoldClip(HandsHold::Holding, true, false), "MRh_SelfConcentration");
        EXPECT_EQ(handsHoldClip(HandsHold::Holding, true, true), "MRh_WardLoop");
    }

    TEST(SkyrimPlayerHandsClock, aHoldCountsFromItsFirstUpdateAndKeepsCountingThroughACharge)
    {
        HandsHoldClock clock;
        EXPECT_EQ(clock.hold(), HandsHold::None);
        EXPECT_FLOAT_EQ(clock.seconds(5.f), 0.f);
        clock.update(HandsHold::Charging, 1.f);
        EXPECT_EQ(clock.hold(), HandsHold::Charging);
        EXPECT_FLOAT_EQ(clock.seconds(1.5f), 0.5f);
        clock.update(HandsHold::Holding, 1.4f); // the charge became the hold: the same press
        EXPECT_EQ(clock.hold(), HandsHold::Holding);
        EXPECT_FLOAT_EQ(clock.seconds(2.f), 1.f);
        clock.update(HandsHold::None, 3.f);
        EXPECT_EQ(clock.hold(), HandsHold::None);
        EXPECT_FLOAT_EQ(clock.seconds(4.f), 0.f);
        clock.update(HandsHold::Charging, 5.f); // a new press starts again
        EXPECT_FLOAT_EQ(clock.seconds(5.5f), 0.5f);
    }

    TEST(SkyrimPlayerHandsClock, aClockThatGoesBackOrIsNotANumberGivesZero)
    {
        HandsHoldClock clock;
        clock.update(HandsHold::Holding, 10.f);
        EXPECT_FLOAT_EQ(clock.seconds(9.f), 0.f);
        EXPECT_FLOAT_EQ(clock.seconds(std::numeric_limits<float>::quiet_NaN()), 0.f);
        EXPECT_FLOAT_EQ(clock.seconds(std::numeric_limits<float>::infinity()), 0.f);
    }

    TEST(SkyrimPlayerHandsState, theBodyStateCarriesTheNewFieldsWithQuietDefaults)
    {
        EXPECT_TRUE(kPlayerBodyVersion == (openeden::sdk::Version{ 0, 3 }));
        const PlayerBodyState state;
        EXPECT_EQ(state.bowDrawToken, 0u);
        EXPECT_EQ(state.bowReleaseToken, 0u);
        EXPECT_FALSE(state.bowDrawing);
        EXPECT_TRUE(state.bowDrawClip.empty());
        EXPECT_TRUE(state.bowReleaseClip.empty());
        EXPECT_EQ(state.castHold, 0);
        EXPECT_FLOAT_EQ(state.castHoldSeconds, 0.f);
        EXPECT_TRUE(state.castHoldClip.empty());
    }

    // ---- the bow ---------------------------------------------------------------------------------------------------

    struct HandsBow : ::testing::Test
    {
        HandsData mData;
        SkyrimCombat mCombat;
        HandsWriter mWriter;

        void SetUp() override
        {
            mCombat.setData(&mData);
            ASSERT_TRUE(mCombat.setTestWeapon("IronBow"));
            ASSERT_TRUE(mCombat.setTestAmmo("IronArrow"));
            SubjectState player;
            player.heading = kPi / 2.f;
            player.pitch = 0.1f;
            player.healthMax = 100.f;
            player.health = 100.f;
            mWriter.mPlayerState = player;
        }
        void update(float seconds = 0.05f) { mCombat.combatUpdate(seconds, mWriter); }
        // Presses, holds `seconds` of game time, releases and updates once more.
        void drawAndRelease(float seconds)
        {
            update();
            mCombat.onPlayerAttackEdge(true);
            update();
            for (float t = 0.f; t < seconds; t += 0.05f)
                update();
            mCombat.onPlayerAttackEdge(false);
            update();
        }
    };

    TEST_F(HandsBow, nothingIsHappeningBeforeAPress)
    {
        update();
        const PlayerHands h = mCombat.playerHands();
        EXPECT_EQ(h.bowDrawToken, 0u);
        EXPECT_EQ(h.bowReleaseToken, 0u);
        EXPECT_FALSE(h.bowDrawing);
        EXPECT_EQ(h.castHold, 0);
    }

    TEST_F(HandsBow, aPressStartsTheDrawWithTheDrawClipAndAReleaseLetsTheArrowGo)
    {
        update();
        mCombat.onPlayerAttackEdge(true);
        update();
        PlayerHands h = mCombat.playerHands();
        EXPECT_EQ(h.bowDrawToken, 1u);
        EXPECT_TRUE(h.bowDrawing);
        EXPECT_EQ(h.bowDrawClip, "Bow_DrawLight");
        EXPECT_EQ(h.bowReleaseToken, 0u);
        for (int i = 0; i < 40; ++i)
            update();
        EXPECT_TRUE(mCombat.playerHands().bowDrawing); // held back for as long as the control is down
        mCombat.onPlayerAttackEdge(false);
        update();
        h = mCombat.playerHands();
        EXPECT_FALSE(h.bowDrawing);
        EXPECT_EQ(h.bowDrawToken, 1u);
        EXPECT_EQ(h.bowReleaseToken, 1u);
        EXPECT_EQ(h.bowReleaseClip, "Bow_Release");
        EXPECT_EQ(mWriter.mLaunches, 1);
    }

    TEST_F(HandsBow, aPullThatIsTooShortIsADrawWithNoRelease)
    {
        drawAndRelease(0.f);
        const PlayerHands h = mCombat.playerHands();
        EXPECT_EQ(h.bowDrawToken, 1u);
        EXPECT_EQ(h.bowReleaseToken, 0u);
        EXPECT_FALSE(h.bowDrawing);
        EXPECT_EQ(mWriter.mLaunches, 0);
    }

    TEST_F(HandsBow, everyDrawAndEveryReleaseCountsOnce)
    {
        drawAndRelease(1.8f);
        drawAndRelease(1.8f);
        const PlayerHands h = mCombat.playerHands();
        EXPECT_EQ(h.bowDrawToken, 2u);
        EXPECT_EQ(h.bowReleaseToken, 2u);
    }

    TEST_F(HandsBow, aPressIgnoredForWantOfTheDrawTimeIsNotADraw)
    {
        mWriter.mTimesReady = false; // the draw clip's timing has not arrived: the press is dropped
        drawAndRelease(0.5f);
        EXPECT_EQ(mCombat.playerHands().bowDrawToken, 0u);
        EXPECT_FALSE(mCombat.playerHands().bowDrawing);
    }

    // ---- a spell he holds ------------------------------------------------------------------------------------------

    struct HandsSpell : ::testing::Test
    {
        HandsData mData;
        SkyrimCombat mCombat;
        HandsWriter mWriter;

        void SetUp() override
        {
            mCombat.setData(&mData);
            SubjectState player;
            player.heading = kPi / 2.f;
            player.healthMax = 100.f;
            player.health = 100.f;
            mWriter.mPlayerState = player;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n)
        {
            for (int i = 0; i < n; ++i)
                update();
        }
    };

    TEST_F(HandsSpell, aHeldSelfSpellChargesUntilItGoesThenHoldsUntilTheControlIsReleased)
    {
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        update(); // the data is read
        EXPECT_EQ(mCombat.playerHands().castHold, 0);
        mCombat.onPlayerAttackEdge(true);
        update(); // the cast begins; the spell goes 0.367 s later
        PlayerHands h = mCombat.playerHands();
        EXPECT_EQ(h.castHold, 1);
        EXPECT_EQ(h.castHoldClip, "MRh_SelfChargeLoop");
        EXPECT_GE(h.castHoldSeconds, 0.f);
        updates(3); // the spell has gone and is held
        h = mCombat.playerHands();
        EXPECT_EQ(h.castHold, 2);
        EXPECT_EQ(h.castHoldClip, "MRh_SelfConcentration");
        const float first = h.castHoldSeconds;
        updates(2);
        EXPECT_NEAR(mCombat.playerHands().castHoldSeconds - first, 0.5f, 1e-3f); // counted from the press
        mCombat.onPlayerAttackEdge(false);
        update();
        h = mCombat.playerHands();
        EXPECT_EQ(h.castHold, 0);
        EXPECT_TRUE(h.castHoldClip.empty());
        EXPECT_FLOAT_EQ(h.castHoldSeconds, 0.f);
    }

    TEST_F(HandsSpell, aWardHasItsOwnLoops)
    {
        ASSERT_TRUE(mCombat.setTestPlayerSpell("WardLesser"));
        update();
        mCombat.onPlayerAttackEdge(true);
        update();
        EXPECT_EQ(mCombat.playerHands().castHold, 1);
        EXPECT_EQ(mCombat.playerHands().castHoldClip, "MRh_WardCharge");
        updates(3);
        EXPECT_EQ(mCombat.playerHands().castHold, 2);
        EXPECT_EQ(mCombat.playerHands().castHoldClip, "MRh_WardLoop");
    }

    TEST_F(HandsSpell, aFireAndForgetSpellIsNeverAHold)
    {
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        update();
        mCombat.onPlayerAttackEdge(true);
        for (int i = 0; i < 6; ++i)
        {
            update();
            EXPECT_EQ(mCombat.playerHands().castHold, 0) << i;
        }
        EXPECT_EQ(mWriter.mLaunches, 1); // a tap fires it
    }

    TEST_F(HandsSpell, aHeldSpellLetGoOfBeforeItGoesLeavesNoHold)
    {
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        update();
        mCombat.onPlayerAttackEdge(true);
        update();
        EXPECT_EQ(mCombat.playerHands().castHold, 1);
        mCombat.onPlayerAttackEdge(false);
        updates(4);
        EXPECT_EQ(mCombat.playerHands().castHold, 0);
    }

    // ---- the bow as a held weapon of the arms -----------------------------------------------------------------------

    constexpr FormId kRace = 0x13746;
    constexpr FormId kSkin = 0x200;
    constexpr FormId kShield = 0x12EB6;
    constexpr FormId kSword = 0x600;
    constexpr FormId kBow = 0x602;
    constexpr FormId kCrossbow = 0x603;

    ArmatureRecord armature(FormId id, std::uint32_t slots, const std::string& thirdPerson, const std::string& first)
    {
        ArmatureRecord a;
        a.mCommon.mId = id;
        a.mRace = kRace;
        a.mSlotMask = slots;
        a.mMaleModel = thirdPerson;
        a.mFemaleModel = "f_" + thirdPerson;
        a.mMaleFirstPerson = first;
        a.mFemaleFirstPerson = "f_" + first;
        return a;
    }

    WeaponLook weapon(FormId id, std::uint8_t type, const std::string& model)
    {
        WeaponLook w;
        w.mCommon.mId = id;
        w.mModel = model;
        w.mHasDnam = true;
        w.mHasData = true;
        w.mAnimType = type;
        w.mDamage = 10;
        return w;
    }

    ActorIndex makeArmsIndex()
    {
        ActorRecords r;
        RaceRecord race;
        race.mCommon.mId = kRace;
        race.mSkeleton[0] = "male/skeleton.nif";
        race.mSkeleton[1] = "female/skeleton_female.nif";
        race.mSkin = kSkin;
        r.mRaces.push_back(race);
        r.mArmatures.push_back(armature(0x500, 0x4, "skin_body.nif", "fp_body.nif"));
        ArmorExtra skin;
        skin.mCommon.mId = kSkin;
        skin.mSlotMask = 0x4;
        skin.mArmatures = { 0x500 };
        r.mArmors.push_back(skin);
        r.mArmatures.push_back(armature(0x523, 0x200, "shield.nif", "fp_shield.nif"));
        ArmorExtra shield;
        shield.mCommon.mId = kShield;
        shield.mSlotMask = 0x200;
        shield.mArmatures = { 0x523 };
        r.mArmors.push_back(shield);
        r.mWeapons.push_back(weapon(kSword, 1, "sword.nif"));
        r.mWeapons.push_back(weapon(kBow, 7, "bow.nif"));
        r.mWeapons.push_back(weapon(kCrossbow, 9, "crossbow.nif"));
        ActorIndex index;
        index.add(r);
        return index;
    }

    ActorAppearance arms(const ActorIndex& index, FormId weaponId, bool heldBow, bool withShield = false)
    {
        PlayerBodyRequest request;
        request.mRace = kRace;
        request.mWeapon = weaponId;
        if (withShield)
            request.mWornArmour = { kShield };
        AppearanceLimits limits;
        limits.mHeldGear = true;
        limits.mHeldBow = heldBow;
        ActorAppearance out;
        std::string error;
        EXPECT_TRUE(makeFirstPersonAppearance(index, request, out, error, limits)) << error;
        return out;
    }

    TEST(SkyrimPlayerHandsGear, aBowIsHeldByTheArmsOnlyWhenTheLimitSaysSo)
    {
        const ActorIndex index = makeArmsIndex();
        const ActorAppearance held = arms(index, kBow, true);
        EXPECT_EQ(held.heldWeaponModel, "bow.nif");
        EXPECT_EQ(held.heldWeaponType, 7);
        const ActorAppearance off = arms(index, kBow, false); // the default: as before this task
        EXPECT_TRUE(off.heldWeaponModel.empty());
        EXPECT_EQ(off.heldWeaponType, 0);
    }

    TEST(SkyrimPlayerHandsGear, aBowTakesBothHandsSoNoShieldIsHeldWithIt)
    {
        const ActorIndex index = makeArmsIndex();
        const ActorAppearance held = arms(index, kBow, true, true);
        EXPECT_EQ(held.heldWeaponModel, "bow.nif");
        EXPECT_TRUE(held.heldShieldModel.empty());
        const ActorAppearance sword = arms(index, kSword, true, true);
        EXPECT_EQ(sword.heldWeaponModel, "sword.nif");
        EXPECT_FALSE(sword.heldShieldModel.empty());
    }

    TEST(SkyrimPlayerHandsGear, aCrossbowAndOtherTypesAreStillNotHeld)
    {
        const ActorIndex index = makeArmsIndex();
        const ActorAppearance crossbow = arms(index, kCrossbow, true);
        EXPECT_TRUE(crossbow.heldWeaponModel.empty());
    }
}
