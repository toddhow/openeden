#ifndef OPENEDEN_GAME_SKYRIMSE_VALUE_MODIFIERS_H
#define OPENEDEN_GAME_SKYRIMSE_VALUE_MODIFIERS_H

// Task 0240 (diseases): what a long-lasting effect on the player changes in the combat rules' numbers, as plain values.
// The diseases service sums the effects of the player's diseases into one of these and hands it to
// SkyrimCombat::setPlayerValueModifiers; combat applies it to the player's profile (combat_disease.cpp) and takes it off
// again when it changes. Everything is a signed amount (a penalty is negative), 0 for "no change". No dependency.

#include <array>
#include <cstddef>

namespace game_skyrimse
{
    struct PlayerValueModifiers
    {
        float mStaminaMax = 0.f; ///< actor value 26 (Peak): added to the maximum
        float mMagickaMax = 0.f; ///< actor value 25
        /// Actor values 29 and 28 (the stamina and magicka regeneration rates), in the units of the profile's regeneration
        /// fields: percent of the maximum a second (RACE DATA +92, +88). RECALLED-VERIFY: that the rate values and the
        /// race fields are the same quantity.
        float mStaminaRegen = 0.f;
        float mMagickaRegen = 0.f;
        /// Actor values 96 + slot (the "skill modifier" values: Fortify One-Handed is one): a percent of the weapon
        /// damage of the skill with that slot (VERIFIED: the enchantment EnchFortifyOneHandedConstantSelf, magnitude 13,
        /// names actor value 96). Only the slots of the weapon skills change damage; the others are carried and not used.
        std::array<float, 18> mSkillPercent{};
        /// Task 0248: actor value 24 (Sanguinare Vampiris: the health maximum) and 32 (Black-Heart Blight: carry weight).
        float mHealthMax = 0.f;
        float mCarry = 0.f;

        [[nodiscard]] bool isZero() const
        {
            if (mHealthMax != 0.f || mCarry != 0.f)
                return false;
            if (mStaminaMax != 0.f || mMagickaMax != 0.f || mStaminaRegen != 0.f || mMagickaRegen != 0.f)
                return false;
            for (const float v : mSkillPercent)
                if (v != 0.f)
                    return false;
            return true;
        }
        [[nodiscard]] bool operator==(const PlayerValueModifiers& other) const
        {
            return mStaminaMax == other.mStaminaMax && mMagickaMax == other.mMagickaMax
                && mStaminaRegen == other.mStaminaRegen && mMagickaRegen == other.mMagickaRegen
                && mSkillPercent == other.mSkillPercent && mHealthMax == other.mHealthMax && mCarry == other.mCarry;
        }
    };
}

#endif
