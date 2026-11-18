// Task 0300 (crossbows, real bows, dual wielding, dual casting): the pure rules of combat_ranged.hpp, written from
// the spec (docs/tasks/0300-skyrim-crossbows-dual-wield.md) before the code. Numbers are worked out by hand.
#include <game_skyrimse/combat_ranged.hpp>
#include <game_skyrimse/combat_style.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;

    WeaponRecord weapon(FormId id, std::uint8_t type, std::uint16_t damage, std::string editorId = {})
    {
        WeaponRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = editorId.empty() ? "W" + std::to_string(id) : std::move(editorId);
        w.mHasData = true;
        w.mHasDnam = true;
        w.mAnimType = type;
        w.mDamage = damage;
        return w;
    }

    AmmoRecord ammo(FormId id, bool arrow, float damage, FormId projectile = 0x77)
    {
        AmmoRecord a;
        a.mCommon.mId = id;
        a.mHasData = true;
        a.mProjectile = projectile;
        a.mFlags = arrow ? kAmmoNonBoltFlag : 0;
        a.mDamage = damage;
        return a;
    }

    TEST(SkyrimRangedRules, onlyTypesSevenAndNineAreRanged)
    {
        for (std::uint8_t type = 0; type < 12; ++type)
            EXPECT_EQ(isRangedAnimType(type), type == 7 || type == 9) << int(type);
    }

    TEST(SkyrimRangedRules, aBoltIsAnAmmunitionWithTheNonBoltFlagClear)
    {
        EXPECT_TRUE(isBolt(ammo(1, false, 5)));
        EXPECT_FALSE(isBolt(ammo(2, true, 5)));
    }

    TEST(SkyrimRangedRules, aCrossbowTakesBoltsAndABowTakesArrows)
    {
        const WeaponRecord bow = weapon(1, 7, 10);
        const WeaponRecord crossbow = weapon(2, 9, 19);
        const AmmoRecord arrow = ammo(3, true, 8);
        const AmmoRecord bolt = ammo(4, false, 12);
        EXPECT_TRUE(ammoFitsWeapon(bow, arrow));
        EXPECT_FALSE(ammoFitsWeapon(bow, bolt));
        EXPECT_TRUE(ammoFitsWeapon(crossbow, bolt));
        EXPECT_FALSE(ammoFitsWeapon(crossbow, arrow));
    }

    TEST(SkyrimRangedRules, aSwordTakesNoAmmunitionAndAnAmmunitionWithNoDataOrProjectileFitsNothing)
    {
        EXPECT_FALSE(ammoFitsWeapon(weapon(1, 1, 7), ammo(3, true, 8)));
        AmmoRecord noData = ammo(3, true, 8);
        noData.mHasData = false;
        EXPECT_FALSE(ammoFitsWeapon(weapon(1, 7, 10), noData));
        EXPECT_FALSE(ammoFitsWeapon(weapon(1, 7, 10), ammo(3, true, 8, 0)));
    }

    TEST(SkyrimRangedRules, theBestRangedWeaponIsTheHighestDamageThenTheLowestId)
    {
        const WeaponRecord sword = weapon(1, 1, 99);
        const WeaponRecord bowA = weapon(5, 7, 12);
        const WeaponRecord bowB = weapon(4, 7, 12);
        const WeaponRecord crossbow = weapon(9, 9, 11);
        EXPECT_EQ(chooseRangedWeapon({ &sword, &bowA, &crossbow, &bowB }), &bowB); // damage 12 twice: lower id 4
        EXPECT_EQ(chooseRangedWeapon({ &crossbow, &sword }), &crossbow);
        EXPECT_EQ(chooseRangedWeapon({ &sword }), nullptr);
        EXPECT_EQ(chooseRangedWeapon({}), nullptr);
        EXPECT_EQ(chooseRangedWeapon({ nullptr, &bowA }), &bowA);
    }

    TEST(SkyrimRangedRules, aRangedWeaponWithNoDataIsNotChosen)
    {
        WeaponRecord bare = weapon(1, 7, 50);
        bare.mHasDnam = false;
        EXPECT_EQ(chooseRangedWeapon({ &bare }), nullptr);
    }

    TEST(SkyrimRangedRules, theBestAmmunitionFitsTheWeaponThenHasTheHighestDamageThenTheLowestId)
    {
        const WeaponRecord crossbow = weapon(2, 9, 19);
        const AmmoRecord arrow = ammo(1, true, 99);
        const AmmoRecord boltLow = ammo(5, false, 8);
        const AmmoRecord boltHigh = ammo(7, false, 12);
        const AmmoRecord boltTwin = ammo(6, false, 12);
        EXPECT_EQ(chooseAmmo(crossbow, { &arrow, &boltLow, &boltHigh, &boltTwin }), &boltTwin);
        EXPECT_EQ(chooseAmmo(crossbow, { &arrow }), nullptr);
        EXPECT_EQ(chooseAmmo(weapon(3, 7, 10), { &arrow, &boltHigh }), &arrow);
        EXPECT_EQ(chooseAmmo(weapon(3, 1, 10), { &arrow, &boltHigh }), nullptr);
    }

    TEST(SkyrimRangedRules, theCrossbowClipsAreTheDlcFolderOnesAndTheDwarvenSetFollowsTheEditorId)
    {
        const CrossbowClips plain = crossbowClipsFor(weapon(1, 9, 19, "DLC1CrossBow"));
        EXPECT_EQ(plain.mAim, "dlc01/crossbow_aim");
        EXPECT_EQ(plain.mRelease, "dlc01/crossbow_release");
        EXPECT_EQ(plain.mReload, "dlc01/crossbow_reload");
        const CrossbowClips dwarven = crossbowClipsFor(weapon(2, 9, 22, "DLC1DwarvenCrossBow"));
        EXPECT_EQ(dwarven.mAim, "dlc01/crossbow_aimdwarven");
        EXPECT_EQ(dwarven.mRelease, "dlc01/crossbow_dwarvenrelease");
        EXPECT_EQ(dwarven.mReload, "dlc01/crossbow_reloaddwarven");
        EXPECT_EQ(
            crossbowClipsFor(weapon(3, 9, 22, "DLC1EnchDwarvenCrossbowFire05")).mAim, "dlc01/crossbow_aimdwarven");
    }

    TEST(SkyrimDualWield, theOffHandIsTheBestOneHandedWeaponLeftAfterTheMainOne)
    {
        const WeaponRecord a = weapon(1, 1, 20);
        const WeaponRecord b = weapon(2, 3, 15);
        const WeaponRecord c = weapon(3, 2, 15);
        const WeaponRecord greatsword = weapon(4, 5, 40);
        const WeaponRecord bow = weapon(5, 7, 30);
        EXPECT_EQ(chooseOffHandWeapon({ &a, &b, &c, &greatsword, &bow }, &a), &b); // 15 twice: lower id 2
    }

    TEST(SkyrimDualWield, oneInstanceOfTheMainWeaponIsTakenOutSoTwoOfAKindDualWield)
    {
        const WeaponRecord a = weapon(1, 1, 20);
        EXPECT_EQ(chooseOffHandWeapon({ &a }, &a), nullptr);
        EXPECT_EQ(chooseOffHandWeapon({ &a, &a }, &a), &a);
    }

    TEST(SkyrimDualWield, aTwoHandedOrRangedMainWeaponHasNoOffHand)
    {
        const WeaponRecord a = weapon(1, 1, 20);
        const WeaponRecord greatsword = weapon(4, 5, 40);
        const WeaponRecord bow = weapon(5, 7, 30);
        EXPECT_EQ(chooseOffHandWeapon({ &greatsword, &a }, &greatsword), nullptr);
        EXPECT_EQ(chooseOffHandWeapon({ &bow, &a }, &bow), nullptr);
        EXPECT_EQ(chooseOffHandWeapon({ &a }, nullptr), nullptr);
    }

    TEST(SkyrimDualWield, aTwoHandedWeaponIsNeverTheOffHand)
    {
        const WeaponRecord a = weapon(1, 1, 20);
        const WeaponRecord greatsword = weapon(4, 5, 40);
        EXPECT_EQ(chooseOffHandWeapon({ &a, &greatsword }, &a), nullptr);
    }

    TEST(SkyrimDualWield, theCombatStyleBitAndTwoOneHandedWeaponsDecide)
    {
        const WeaponRecord a = weapon(1, 1, 20);
        const WeaponRecord b = weapon(2, 1, 20);
        const WeaponRecord greatsword = weapon(4, 5, 40);
        EXPECT_TRUE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding, &a, &b));
        EXPECT_TRUE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding | CombatStyleFlag::Dueling, &a, &b));
        EXPECT_FALSE(dualWieldAllowed(CombatStyleFlag::Dueling | CombatStyleFlag::Flanking, &a, &b));
        EXPECT_FALSE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding, &a, nullptr));
        EXPECT_FALSE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding, nullptr, &b));
        EXPECT_FALSE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding, &greatsword, &b));
        EXPECT_FALSE(dualWieldAllowed(CombatStyleFlag::AllowDualWielding, &a, &greatsword));
    }

    TEST(SkyrimDualWield, theHandsAlternateStartingWithTheRight)
    {
        EXPECT_FALSE(dualWieldUsesLeft(0));
        EXPECT_TRUE(dualWieldUsesLeft(1));
        EXPECT_FALSE(dualWieldUsesLeft(2));
        EXPECT_TRUE(dualWieldUsesLeft(3));
        EXPECT_TRUE(dualWieldUsesLeft(4000001));
    }

    TEST(SkyrimDualCast, theDefaultsAreTheGmstValuesOfTheFiveMasters)
    {
        const DualCastSettings defaults;
        EXPECT_FLOAT_EQ(defaults.mEffectiveness, 2.2f);
        EXPECT_FLOAT_EQ(defaults.mCostMult, 2.8f);
        EXPECT_FLOAT_EQ(defaults.mTimeMult, 1.f);
    }

    TEST(SkyrimDualCast, aSettingFromTheTableReplacesTheDefault)
    {
        SettingsTable table;
        table.set("fMagicDualCastingEffectivenessBase", 3.f);
        table.set("fMagicDualCastingCostMult", 4.f);
        const DualCastSettings s = dualCastSettingsFrom(table);
        EXPECT_FLOAT_EQ(s.mEffectiveness, 3.f);
        EXPECT_FLOAT_EQ(s.mCostMult, 4.f);
        EXPECT_FLOAT_EQ(s.mTimeMult, 1.f); // not in the table: the default stays
    }

    TEST(SkyrimDualCast, aDualCastScalesTheMagnitudeAndTheCostAndASingleCastDoesNot)
    {
        const DualCastSettings s;
        EXPECT_FLOAT_EQ(dualCastMagnitude(25.f, true, s), 55.f); // 25 x 2.2
        EXPECT_FLOAT_EQ(dualCastMagnitude(25.f, false, s), 25.f);
        EXPECT_FLOAT_EQ(dualCastCost(41.f, true, s), 114.8f); // 41 x 2.8
        EXPECT_FLOAT_EQ(dualCastCost(41.f, false, s), 41.f);
    }

    TEST(SkyrimDualCast, aNonFiniteNumberIsLeftAlone)
    {
        const DualCastSettings s;
        EXPECT_TRUE(std::isinf(dualCastMagnitude(std::numeric_limits<float>::infinity(), true, s)));
        EXPECT_TRUE(std::isnan(dualCastCost(std::nanf(""), true, s)));
    }
}
