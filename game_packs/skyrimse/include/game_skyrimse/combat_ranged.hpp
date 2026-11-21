#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_RANGED_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_RANGED_H

// Crossbows, real bows, dual wielding and dual casting (task 0300; milestone M-S5).
//
// The rules here are pure: they take plain candidate lists and settings, no engine type and no clock, so they are
// tested without records. `combat.cpp` and `combat_ranged_run.cpp` call them.
//
//  - Ranged gear: a bow (WEAP animation type 7) shoots arrows, a crossbow (type 9) shoots bolts. An AMMO's DNAM flag
//    0x04 is "non-bolt" (an arrow): a bolt is an ammunition with that flag clear (VERIFIED by the data: every AMMO
//    named *Bolt* has it clear, every one named *Arrow* has it set; see the gated test).
//  - Dual wielding: an actor whose combat style allows it (CSTY DATA bit 4) and that carries a second one-handed
//    melee weapon swings the right one, then the left one (1HM_AttackLeft), each with its own weapon's numbers.
//  - Dual casting: both hands cast one spell; the magnitude and the cost follow the GMSTs
//  fMagicDualCastingEffectiveness
//    Base (2.2), fMagicDualCastingCostMult (2.8) and fMagicDualCastingTimeMult (1.0), read from the data.

#include <game_skyrimse/combat_stats.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// AMMO DNAM flags bit 2 (value 4): set on an arrow ("non-bolt"), clear on a bolt.
    inline constexpr std::uint32_t kAmmoNonBoltFlag = 4;

    [[nodiscard]] bool isRangedAnimType(std::uint8_t animType); ///< 7 (bow) or 9 (crossbow)
    [[nodiscard]] bool isBolt(const AmmoRecord& ammo);
    /// A crossbow takes bolts and a bow takes arrows; false for any other weapon, or an ammunition with no data or no
    /// projectile.
    [[nodiscard]] bool ammoFitsWeapon(const WeaponRecord& weapon, const AmmoRecord& ammo);

    /// The best ranged weapon of the candidates: a bow or a crossbow with data, the highest base damage, then the
    /// lowest form id. Null when none qualifies.
    [[nodiscard]] const WeaponRecord* chooseRangedWeapon(const std::vector<const WeaponRecord*>& candidates);
    /// The best ammunition that fits `weapon`: the highest damage, then the lowest form id. Null when none fits.
    [[nodiscard]] const AmmoRecord* chooseAmmo(const WeaponRecord& weapon, const std::vector<const AmmoRecord*>& ammo);

    /// The crossbow's clips: aim (the draw), release and reload, under `animations/dlc01/` of the character
    /// skeleton. Two sets exist in the archive: the plain one and the Dwarven one (`crossbow_aimdwarven`,
    /// `crossbow_dwarvenrelease`, `crossbow_reloaddwarven`); the Dwarven set is for a weapon whose editor id says
    /// "dwarven" (RECALLED-VERIFY: the two sets and which weapon takes which are read from the file names only).
    struct CrossbowClips
    {
        std::string_view mAim;
        std::string_view mRelease;
        std::string_view mReload;
    };
    [[nodiscard]] CrossbowClips crossbowClipsFor(const WeaponRecord& weapon);

    /// The second weapon of a dual wielder: after one instance of `main` is taken out of the list, the best
    /// one-handed melee weapon (types 1 to 4: sword, dagger, axe, mace) with data, by damage then form id. Null when
    /// `main` is not itself one-handed or nothing is left.
    [[nodiscard]] const WeaponRecord* chooseOffHandWeapon(
        const std::vector<const WeaponRecord*>& candidates, const WeaponRecord* main);
    /// Whether an actor dual wields: its combat style flags allow it (CSTY DATA bit 4) and both weapons are one-handed
    /// melee weapons.
    [[nodiscard]] bool dualWieldAllowed(
        std::uint32_t combatStyleFlags, const WeaponRecord* main, const WeaponRecord* offHand);
    /// Which hand swings the `index`th blow (counted from 0) of a dual wielder: right, left, right, left...
    [[nodiscard]] bool dualWieldUsesLeft(std::uint32_t index);

    /// The dual cast numbers from the GMSTs; the defaults are the five masters' values.
    struct DualCastSettings
    {
        float mEffectiveness = 2.2f; ///< fMagicDualCastingEffectivenessBase: the magnitude multiplier
        float mCostMult = 2.8f; ///< fMagicDualCastingCostMult
        float mTimeMult = 1.f; ///< fMagicDualCastingTimeMult: the charge time multiplier
    };
    [[nodiscard]] DualCastSettings dualCastSettingsFrom(const SettingsTable& settings);
    /// The magnitude of an effect cast with both hands, and the magicka that costs; a single cast is unchanged.
    [[nodiscard]] float dualCastMagnitude(float magnitude, bool dual, const DualCastSettings& settings);
    [[nodiscard]] float dualCastCost(float cost, bool dual, const DualCastSettings& settings);
}

#endif
