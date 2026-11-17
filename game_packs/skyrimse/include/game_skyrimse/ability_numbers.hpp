#ifndef OPENEDEN_GAME_SKYRIMSE_ABILITY_NUMBERS_H
#define OPENEDEN_GAME_SKYRIMSE_ABILITY_NUMBERS_H

// Task 0241: the numbers a held spell gives the player. A constant ability (a standing stone's sign, a race's ability)
// and the Fortify effects of a timed shrine blessing change a few actor values for as long as they are held:
//   24 / 25 / 26   health, magicka and stamina MAXIMUM (a Fortify effect),
//   32             carry weight,
//   40 to 45       the resistances (poison, fire, shock, frost, magic, disease); a negative sum is a weakness,
//   27 to 29       the three regeneration RATES (added to the rate), 155 to 157 the three "Mult" values (percent of
//   it), 83             the chance to absorb a hostile spell (kept as a number only: no absorption rule is built).
// VERIFIED on the five masters (tools/skyrimse_stones_survey/stones.py): the Atronach stone is Peak 83 +50,
// ValueModifier 156 -50 (Detrimental flag) and Peak 25 +50; the Lady stone Peak 155 +25 and 157 +25; the Steed Peak 32
// +100; the Lord ValueModifier 39 +50 and 44 +25; the Apprentice Peak 156 +100 and a second ability ValueModifier 44
// 100 (Detrimental); the nine blessings are Peak 24, 25, 26 or 156 with a duration of 28,800 s (8 hours).
//
// Which effects count (OURS): every effect of an ABILITY spell (cast type constant) whose magic effect is a
// ValueModifier
// or Peak; of any other spell only a Peak effect with a duration and no Hostile flag (a Fortify blessing; a known spell
// such as Healing is an instant ValueModifier and counts for nothing). The sign is the effect's own (signedMagnitude).
// How a resistance and a weakness combine with the hit, and what Fortify does to the current value, is RECALLED-VERIFY.
// Pure: nothing here allocates after the result is returned, throws or reads a clock.

#include <game_skyrimse/perks.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace game_skyrimse
{
    struct AbilityNumbers
    {
        float mVital[3] = { 0.f, 0.f, 0.f }; ///< Fortify health, magicka, stamina (maximum)
        float mCarry = 0.f; ///< actor value 32
        float mResist[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f }; ///< 40 to 45, signed percent (negative: weakness)
        float mAbsorb = 0.f; ///< actor value 83
        float mRegenRate[3] = { 0.f, 0.f, 0.f }; ///< 27 to 29: added to the rate (percent of the maximum a second)
        float mRegenMult[3] = { 0.f, 0.f, 0.f }; ///< 155 to 157: percent of the rate
        /// Task 0248: actor values 6 to 23 (points added to a skill), 96 to 113 (a percent skill modifier) and 86 (the
        /// shout timer). Kept apart from any(): the older tests pin that a blessing of such a value is "not one of
        /// these numbers".
        std::array<float, 18> mSkillPoints{};
        std::array<float, 18> mSkillPower{};
        float mShoutTimer = 0.f;
        [[nodiscard]] bool anySkillOrShout() const;
        [[nodiscard]] bool any() const;
        [[nodiscard]] bool hasRegen() const;
        [[nodiscard]] bool operator==(const AbilityNumbers& other) const;
    };

    /// The most one number may be (a bad record cannot make a number run away): every sum is clamped to +-this.
    inline constexpr float kMaxAbilityNumber = 10000.f;

    /// The sum over `spells` (unknown ids and records without data are skipped; a number that is not finite counts as
    /// 0).
    [[nodiscard]] AbilityNumbers sumAbilityNumbers(const PerkLibrary& library, std::span<const FormId> spells);

    /// The resistance in percent, 0 to kMaxResistPercent, against an effect whose MGEF resist value is `resistValue`
    /// (-1 for none): the element's own net resistance and, except for poison and disease, the magic resistance, as two
    /// independent shares (1 - (1 - a)(1 - b)). A net negative sum is a weakness, not a resistance, and gives 0 here.
    [[nodiscard]] float abilityResistPercent(const AbilityNumbers& numbers, std::int32_t resistValue);

    /// The weakness in percent (>= 0, at most kMaxWeaknessPercent): the negative nets of the element and of magic.
    [[nodiscard]] float abilityWeaknessPercent(const AbilityNumbers& numbers, std::int32_t resistValue);
}

#endif
