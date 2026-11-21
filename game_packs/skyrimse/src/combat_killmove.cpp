#include "game_skyrimse/combat.hpp"

#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
        constexpr std::int32_t kHealthValue = 24; // actor value 24: health
    }

    void SkyrimCombat::killMoveFinish(ActorHandle victim)
    {
        if (mKillMoveActive && mKillMoveVictim == victim)
            mKillMoveFinishPending = true;
    }

    void SkyrimCombat::tickKillMove(CombatRequestWriter& writer)
    {
        if (!mKillMoveFinishPending)
            return;
        mKillMoveFinishPending = false;
        const ActorHandle handle = mKillMoveVictim;
        mKillMoveActive = false;
        mKillMoveVictim = {};
        Fighter* victim = find(CombatSubject::of(handle));
        if (victim == nullptr || victim->mDead || !ready(*victim))
            return;
        victim->mStatus.mParalyzedUntil = -1.f;
        // The move's clip ends with the victim lying: he dies now, through the ordinary death (the ragdoll starts from
        // the pose the clip holds).
        (void)changeVital(
            writer, nullptr, *victim, kHealthValue, -(victim->mProfile.mHealthMax + victim->mHealth + 1.f), nullptr);
        if (mTrace)
            trace("COMBAT kill move ends: death of actor " + std::to_string(victim->mSubject.actor.reference));
    }
}
