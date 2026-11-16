#include "game_skyrimse/combat.hpp"

#include "game_skyrimse/sneak.hpp"

#include <cstdio>
#include <string>

// The combat side of a sneak attack (task 0227): which multiplier the player's blow carries. combat.cpp only calls it.

namespace game_skyrimse
{
    namespace
    {
        std::string number(float v)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(v));
            return buffer;
        }
    }

    float SkyrimCombat::sneakAttackFor(Fighter& attacker, Fighter& victim, const WeaponRecord* swung, bool ranged)
    {
        if (!mSneakMultiplier || !attacker.mSubject.player || victim.mSubject.player)
            return 1.f;
        const std::uint8_t animType = ranged ? WeaponAnimType::Bow
                                             : (swung != nullptr ? swung->mAnimType : WeaponAnimType::HandToHand);
        float multiplier = mSneakMultiplier(victim.mSubject.actor.reference, animType, ranged);
        if (!(multiplier > 1.f))
            return 1.f;
        const float base = multiplier;
        if (perksMatter(attacker) && mData != nullptr && mData->perkLibrary() != nullptr)
        {
            if (!mPerkEngine)
                mPerkEngine = std::make_unique<PerkEngine>(*mData->perkLibrary());
            const PerkParticipant owner = actorParticipant(attacker, false);
            const PerkParticipant target = actorParticipant(victim, false);
            const PerkParticipant weapon = weaponParticipant(swung);
            std::size_t applied = 0;
            multiplier = sneak::applySneakAttackPerks(*mPerkEngine, attacker.mProfile.mPerks, owner,
                swung != nullptr ? &weapon : nullptr, target, multiplier, &applied);
            mPerkEffectsApplied += applied;
        }
        if (mTrace)
            trace("COMBAT sneak attack x" + number(multiplier) + " (before perks " + number(base) + ")");
        return multiplier;
    }
}
