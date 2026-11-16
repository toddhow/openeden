// Task 0229: poisons on the player's weapon, the combat half. A poison the player put on his melee weapon (the pack
// sets it with setPlayerPoison when the weapon is wielded) is applied to the target of each landed, unblocked blow
// through the magic runtime of task 0186 (applySpellEffects): its effects are hostile value modifiers, so they tick
// per second on the target's health, magicka or stamina, are resisted by the target's resistance and held by a ward.
// A blow spends a charge whether or not it kills; a dead target gets no effect. Only the player uses it.
#include "game_skyrimse/combat.hpp"

#include <utility>

namespace game_skyrimse
{
    void SkyrimCombat::setPlayerPoison(SpellRecord spell, int charges)
    {
        if (charges <= 0 || spell.mEffects.empty())
        {
            mPoisonSpell = SpellRecord();
            mPoisonCharges = 0;
            return;
        }
        mPoisonSpell = std::move(spell);
        mPoisonCharges = charges;
    }

    void SkyrimCombat::applyPoisonOnHit(openeden::sdk::game::CombatRequestWriter& writer, Fighter& attacker,
        Fighter& victim, const openeden::sdk::game::SweepHit& hit)
    {
        if (!attacker.mSubject.player || mPoisonCharges <= 0)
            return;
        --mPoisonCharges;
        const int remaining = mPoisonCharges;
        if (mTrace)
            trace("COMBAT poison " + mPoisonSpell.mCommon.mEditorId + " on the blow, " + std::to_string(remaining)
                + " charges left");
        if (mPoisonSpent)
            mPoisonSpent(remaining);
        if (victim.mDead || !(victim.mHealth > 0.f))
            return;
        applySpellEffects(writer, attacker, victim, mPoisonSpell, &hit);
    }
}
