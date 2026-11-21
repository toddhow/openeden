// Crossbows, real bows, dual wielding and dual casting (task 0300): see combat_ranged.hpp.
#include <game_skyrimse/combat_ranged.hpp>

#include <game_skyrimse/combat_style.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        bool containsNoCase(std::string_view text, std::string_view word)
        {
            const auto it = std::search(text.begin(), text.end(), word.begin(), word.end(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
            return it != text.end();
        }

        bool isOneHandedMelee(const WeaponRecord& weapon)
        {
            return weapon.mHasData && weapon.mHasDnam && weapon.mAnimType >= WeaponAnimType::OneHandSword
                && weapon.mAnimType <= WeaponAnimType::OneHandMace;
        }

        bool better(const WeaponRecord& a, const WeaponRecord& b)
        {
            if (a.mDamage != b.mDamage)
                return a.mDamage > b.mDamage;
            return a.mCommon.mId < b.mCommon.mId;
        }
    }

    bool isRangedAnimType(std::uint8_t animType)
    {
        return animType == WeaponAnimType::Bow || animType == WeaponAnimType::Crossbow;
    }

    bool isBolt(const AmmoRecord& ammo)
    {
        return (ammo.mFlags & kAmmoNonBoltFlag) == 0;
    }

    bool ammoFitsWeapon(const WeaponRecord& weapon, const AmmoRecord& ammo)
    {
        if (!ammo.mHasData || ammo.mProjectile == 0 || !isRangedAnimType(weapon.mAnimType))
            return false;
        return weapon.mAnimType == WeaponAnimType::Crossbow ? isBolt(ammo) : !isBolt(ammo);
    }

    const WeaponRecord* chooseRangedWeapon(const std::vector<const WeaponRecord*>& candidates)
    {
        const WeaponRecord* best = nullptr;
        for (const WeaponRecord* candidate : candidates)
        {
            if (candidate == nullptr || !candidate->mHasData || !candidate->mHasDnam
                || !isRangedAnimType(candidate->mAnimType))
                continue;
            if (best == nullptr || better(*candidate, *best))
                best = candidate;
        }
        return best;
    }

    const AmmoRecord* chooseAmmo(const WeaponRecord& weapon, const std::vector<const AmmoRecord*>& ammo)
    {
        const AmmoRecord* best = nullptr;
        for (const AmmoRecord* candidate : ammo)
        {
            if (candidate == nullptr || !ammoFitsWeapon(weapon, *candidate))
                continue;
            if (best == nullptr || candidate->mDamage > best->mDamage
                || (candidate->mDamage == best->mDamage && candidate->mCommon.mId < best->mCommon.mId))
                best = candidate;
        }
        return best;
    }

    CrossbowClips crossbowClipsFor(const WeaponRecord& weapon)
    {
        if (containsNoCase(weapon.mCommon.mEditorId, "dwarven"))
            return { "dlc01/crossbow_aimdwarven", "dlc01/crossbow_dwarvenrelease", "dlc01/crossbow_reloaddwarven" };
        return { "dlc01/crossbow_aim", "dlc01/crossbow_release", "dlc01/crossbow_reload" };
    }

    const WeaponRecord* chooseOffHandWeapon(
        const std::vector<const WeaponRecord*>& candidates, const WeaponRecord* main)
    {
        if (main == nullptr || !isOneHandedMelee(*main))
            return nullptr;
        const WeaponRecord* best = nullptr;
        bool skippedMain = false;
        for (const WeaponRecord* candidate : candidates)
        {
            if (candidate == nullptr || !isOneHandedMelee(*candidate))
                continue;
            if (candidate == main && !skippedMain)
            {
                skippedMain = true; // one instance of the main weapon is in the right hand
                continue;
            }
            if (best == nullptr || better(*candidate, *best))
                best = candidate;
        }
        return best;
    }

    bool dualWieldAllowed(std::uint32_t combatStyleFlags, const WeaponRecord* main, const WeaponRecord* offHand)
    {
        return (combatStyleFlags & CombatStyleFlag::AllowDualWielding) != 0 && main != nullptr && offHand != nullptr
            && isOneHandedMelee(*main) && isOneHandedMelee(*offHand);
    }

    bool dualWieldUsesLeft(std::uint32_t index)
    {
        return (index % 2) == 1;
    }

    DualCastSettings dualCastSettingsFrom(const SettingsTable& settings)
    {
        DualCastSettings out;
        out.mEffectiveness = settings.get("fMagicDualCastingEffectivenessBase", out.mEffectiveness);
        out.mCostMult = settings.get("fMagicDualCastingCostMult", out.mCostMult);
        out.mTimeMult = settings.get("fMagicDualCastingTimeMult", out.mTimeMult);
        return out;
    }

    float dualCastMagnitude(float magnitude, bool dual, const DualCastSettings& settings)
    {
        return dual && std::isfinite(magnitude) ? magnitude * settings.mEffectiveness : magnitude;
    }

    float dualCastCost(float cost, bool dual, const DualCastSettings& settings)
    {
        return dual && std::isfinite(cost) ? cost * settings.mCostMult : cost;
    }
}
