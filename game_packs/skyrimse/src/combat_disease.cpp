// Task 0240: diseases, the combat half. What a disease takes from the player (diseases.hpp builds a
// PlayerValueModifiers) is applied to his profile here: stamina and magicka maxima and regeneration rates move by the
// amounts, a weapon skill's damage is scaled by 1 + percent / 100. The profile gets the realised amounts (never below
// 0), and they come off again when the modifiers change. progression sync rewrites the maxima, so it forgets what was
// applied and asks again (combat_skills.cpp). Only the player uses it. Main thread.
#include "game_skyrimse/combat.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace game_skyrimse
{
    namespace
    {
        // Realises `wanted` on a profile value: the value without what was applied before, plus the wanted amount, never
        // below `floor`. Returns the amount really added.
        float realise(float& value, float applied, float wanted, float floor)
        {
            const float base = value - applied;
            const float delta = std::isfinite(wanted) ? std::max(wanted, floor - base) : 0.f;
            value = base + delta;
            return delta;
        }
    }

    void SkyrimCombat::setPlayerValueModifiers(const PlayerValueModifiers& modifiers)
    {
        if (modifiers == mValueMods)
            return;
        mValueMods = modifiers;
        mValueModsDirty = true;
        applyPlayerValueModifiers();
    }

    std::vector<FormId> SkyrimCombat::playerSpells() const
    {
        for (const Fighter& f : mFighters)
            if (f.mSubject.player)
                return f.mHasProfile ? f.mProfile.mSpells : std::vector<FormId>();
        return {};
    }

    void SkyrimCombat::notifyPlayerHit(const Fighter& attacker, const Fighter& victim)
    {
        if (!mPlayerHitHook || !victim.mSubject.player || attacker.mSubject.player)
            return;
        mPlayerHitHook(attacker.mProfile.mAttackSpells, attacker.mSubject.actor.reference);
    }

    void SkyrimCombat::applyPlayerValueModifiers()
    {
        if (!mValueModsDirty)
            return;
        Fighter* player = find(openeden::sdk::game::CombatSubject::ofPlayer());
        if (player == nullptr || mData == nullptr || !ready(*player) || !player->mHasProfile)
            return; // asked again when the profile is made
        FighterProfile& profile = player->mProfile;
        const PlayerValueModifiers& wanted = mValueMods;
        PlayerValueModifiers& applied = mValueModsApplied;
        applied.mStaminaMax = realise(profile.mStaminaMax, applied.mStaminaMax, wanted.mStaminaMax, 0.f);
        applied.mMagickaMax = realise(profile.mMagickaMax, applied.mMagickaMax, wanted.mMagickaMax, 0.f);
        applied.mStaminaRegen
            = realise(profile.mStaminaRegenPercent, applied.mStaminaRegen, wanted.mStaminaRegen, 0.f);
        applied.mMagickaRegen
            = realise(profile.mMagickaRegenPercent, applied.mMagickaRegen, wanted.mMagickaRegen, 0.f);
        player->mStamina = std::min(player->mStamina, profile.mStaminaMax);
        player->mMagicka = std::min(player->mMagicka, profile.mMagickaMax);
        mValueModsDirty = false;
        if (mTrace)
            trace("COMBAT player value modifiers: stamina max " + std::to_string(profile.mStaminaMax) + " ("
                + std::to_string(applied.mStaminaMax) + "), magicka max " + std::to_string(profile.mMagickaMax) + " ("
                + std::to_string(applied.mMagickaMax) + "), stamina regen " + std::to_string(profile.mStaminaRegenPercent)
                + ", magicka regen " + std::to_string(profile.mMagickaRegenPercent));
    }

    void SkyrimCombat::applySkillPercent(const Fighter& attacker, const WeaponRecord* weapon, MeleeHit& hit) const
    {
        if (!attacker.mSubject.player || weapon == nullptr)
            return;
        const std::size_t slot = weapon->skillSlot();
        if (slot >= mValueMods.mSkillPercent.size())
            return;
        const float percent = mValueMods.mSkillPercent[slot];
        if (percent == 0.f || !std::isfinite(percent))
            return;
        const float scale = std::max(0.f, 1.f + percent / 100.f);
        hit.mDamageBefore *= scale;
        hit.mDamageAfter *= scale;
        if (mTrace)
            trace("COMBAT skill modifier " + std::to_string(percent)
                + " percent on the weapon's skill: damage x " + std::to_string(scale));
    }
}
