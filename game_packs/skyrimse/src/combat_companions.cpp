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
        {
            if (mTrace) // task 0347: why a script's StartCombat did nothing
                trace(std::string("COMBAT script start refused for ") + std::to_string(handle.reference) + ": "
                    + (f == nullptr ? "no fighter" : f->mDead ? "dead" : "records not ready"));
            return false;
        }
        if (aiFind(handle) != nullptr)
            return true; // already fighting
        return aiBegin(*f, "provoked", CombatSubject::ofPlayer());
    }

    bool SkyrimCombat::scriptStartCombat(ActorHandle handle, ActorHandle target)
    {
        if (!mAiOn || !handle.valid() || !target.valid() || handle == target)
            return false;
        Fighter* f = find(CombatSubject::of(handle));
        Fighter* t = find(CombatSubject::of(target));
        if (f == nullptr || t == nullptr || f->mDead || t->mDead || !ready(*f) || !ready(*t))
            return false;
        bool ok = true;
        if (aiFind(handle) == nullptr)
            ok = aiBegin(*f, "provoked", CombatSubject::of(target));
        if (aiFind(target) == nullptr)
            ok = aiBegin(*t, "provoked", CombatSubject::of(handle)) && ok;
        return ok;
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
