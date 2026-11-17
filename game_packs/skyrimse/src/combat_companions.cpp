// Task 0264 (companions): the combat's side of what the game's own quest scripts ask: Actor.StartCombat, StopCombat,
// ResetHealthAndLimbs and GetCombatState. See combat.hpp.
#include "game_skyrimse/combat.hpp"

#include <algorithm>

namespace game_skyrimse
{
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatSubject;

    bool SkyrimCombat::scriptStartCombat(ActorHandle handle)
    {
        if (!mAiOn)
            return false;
        Fighter* f = find(CombatSubject::of(handle));
        if (f == nullptr || f->mDead || !ready(*f))
            return false;
        if (aiFind(handle) != nullptr)
            return true; // already fighting
        return aiBegin(*f, "provoked", CombatSubject::ofPlayer());
    }

    bool SkyrimCombat::scriptStopCombat(ActorHandle handle)
    {
        for (std::size_t i = 0; i < mAiActors.size(); ++i)
        {
            if (mAiActors[i].mHandle == handle)
            {
                aiRelease(i, "script");
                // a script that stops a fight does not want the actor to start it again by itself at once
                mAiCooldown[handle.reference] = mClock + kAiGiveUpCooldown;
                return true;
            }
        }
        return false;
    }

    bool SkyrimCombat::scriptResetHealth(ActorHandle handle)
    {
        Fighter* f = find(CombatSubject::of(handle));
        if (f == nullptr || f->mDead || !ready(*f))
            return false;
        f->mHealth = f->mProfile.mHealthMax;
        f->mBleedout = false;
        return true;
    }

    bool SkyrimCombat::scriptInCombat(ActorHandle handle) const
    {
        return std::any_of(mAiActors.begin(), mAiActors.end(), [&](const AiActor& a) { return a.mHandle == handle; });
    }
}
