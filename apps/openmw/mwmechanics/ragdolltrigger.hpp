#ifndef OPENEDEN_MWMECHANICS_RAGDOLLTRIGGER_H
#define OPENEDEN_MWMECHANICS_RAGDOLLTRIGGER_H

namespace MWMechanics
{
    /// What CharacterController::updateRagdoll knows when it decides whether a dead actor becomes a ragdoll (task
    /// 0106).
    struct RagdollTriggerInput
    {
        bool mSettingOn = false; // [Physics] creativemotion ragdolls
        bool mDead = false;
        bool mSawAliveHere = false; // this controller saw the actor alive: a corpse loaded dead lies in the clip's end
        bool mAlreadyTried = false; // one try per controller
        bool mIsNpc = false; // creatures have no humanoid skeleton
        bool mIsPlayer = false; // the player's death reloads the game
        bool mHasBodyAndBones = false; // an animation object to drive
        bool mScriptedAnimation = false; // a script is playing an animation: not interrupted by a death
    };

    /// True when the actor's ragdoll should be built now. Pure.
    inline bool shouldStartActorRagdoll(const RagdollTriggerInput& in)
    {
        return in.mSettingOn && in.mDead && in.mSawAliveHere && !in.mAlreadyTried && in.mIsNpc && !in.mIsPlayer
            && in.mHasBodyAndBones && !in.mScriptedAnimation;
    }
}

#endif
