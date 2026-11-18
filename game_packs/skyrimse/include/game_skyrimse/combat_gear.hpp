#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_GEAR_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_GEAR_H

// Real weapons and shields in combat (task 0214; `[extension.skyrimse] combat real gear`, off by default).
//
// Until now every fighter swung the one `combat test weapon` and played the one-handed sword clips. With real gear an
// NPC fights with the best melee weapon and the shield of its inventory and outfit, the clip set follows the weapon's
// animation type (WEAP DNAM +0) and a shield changes how a block looks and sounds and how hard it is.
//
// No engine type. Everything here is pure; the selection rules take plain candidate lists so that they are tested
// without records.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/combat_stats.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace game_skyrimse
{
    /// The clip table's "weapon animation type" column. 1 to 4 (sword, dagger, axe, mace) share the one-handed
    /// clips (1HM_*: the archive has no dagger, axe or mace set), 5 is the two-handed sword (2HM_*), 6 the two-handed
    /// axe and hammer (2HW_*). 10 is OURS: the clips of a raised shield (SHD_*), used for the block and the bash of
    /// a fighter that carries one.
    inline constexpr std::uint8_t kClipClassOneHanded = 1;
    inline constexpr std::uint8_t kClipClassTwoHandedSword = 5;
    inline constexpr std::uint8_t kClipClassTwoHandedAxe = 6;
    inline constexpr std::uint8_t kClipClassShield = 10;

    /// The pack setting that switches all of it on.
    inline constexpr std::string_view kCombatRealGearKey = "combat real gear";

    /// Block bonus of a shield, per point of the shield's armour rating (OURS, tunable; Skyrim's own numbers are not
    /// in the data: no fBlockShield GMST exists in the five masters). The bonus is added to the block fraction before
    /// the fBlockMax cap.
    inline constexpr float kShieldBlockPerRating = 0.01f;

    /// The table column of a melee weapon's animation type; 1 for a type with no clips of its own (unarmed, bow...).
    [[nodiscard]] std::uint8_t meleeClipClass(std::uint8_t weaponAnimType);
    [[nodiscard]] bool isTwoHanded(std::uint8_t weaponAnimType);
    /// A weapon a fighter can swing as its main weapon: has data, is melee and is not hand to hand.
    [[nodiscard]] bool isFightingWeapon(const WeaponRecord& weapon);

    /// The best of the candidates: the highest base damage, then the lowest form id. Null when none qualifies.
    /// (Deterministic: no clock, no random number; the order of the list does not matter.)
    [[nodiscard]] const WeaponRecord* chooseMeleeWeapon(const std::vector<const WeaponRecord*>& candidates);

    /// Task 0295: the staff an NPC carries: an anim type 8 weapon with data and an enchantment, the lowest form id
    /// among them (deterministic). Null when none.
    [[nodiscard]] const WeaponRecord* chooseStaff(const std::vector<const WeaponRecord*>& candidates);

    /// An armour item a fighter could wear: its piece and, for a shield, its block material (ARMO BAMT).
    struct ArmourCandidate
    {
        ArmourPiece mPiece;
        FormId mBlockMaterial = kNullFormId;
    };
    [[nodiscard]] bool isShield(const ArmourPiece& piece);
    /// The shield with the highest rating (then the lowest form id); null when there is none in the list.
    [[nodiscard]] const ArmourCandidate* chooseShield(const std::vector<ArmourCandidate>& candidates);
    /// Armour worth wearing besides `worn`: pieces (not shields) taken by descending rating, then form id, while
    /// their slots do not overlap what is already covered (the slots of `worn` included).
    [[nodiscard]] std::vector<ArmourPiece> chooseExtraArmour(
        const std::vector<ArmourCandidate>& candidates, const std::vector<ArmourPiece>& worn);

    /// What one fighter fights with. `mKnown` is false until it was looked up.
    struct FighterGear
    {
        bool mKnown = false;
        const WeaponRecord* mWeapon = nullptr; ///< valid as long as the data; null: unarmed
        const WeaponRecord* mStaff = nullptr; ///< task 0295: an enchanted staff it carries (cast with, not swung)
        bool mHasShield = false;
        ArmourPiece mShield;
        FormId mShieldBlockMaterial = kNullFormId;
        std::vector<ArmourPiece> mExtraPieces; ///< armour from the inventory worn in slots the outfit leaves free
    };

    /// The gear of an NPC from its resolved inventory and default outfit (leveled lists resolved with `level` and
    /// `variation`, as the armour is). A shield goes with a one-handed weapon or no weapon, never with a two-handed
    /// one. False for an NPC that cannot be resolved.
    [[nodiscard]] bool resolveFighterGear(const ActorIndex& actors, const CombatIndex& combat, FormId npc,
        std::uint32_t variation, std::uint16_t level, const std::vector<ArmourPiece>& worn, FighterGear& out);
}

#endif
