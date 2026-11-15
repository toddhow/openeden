#ifndef OPENMW_MWPHYSICS_NEUTRALPLAYERATTACK_H
#define OPENMW_MWPHYSICS_NEUTRALPLAYERATTACK_H

// Task 0172 (combat stage S5-4): turns the frames of "the attack action is down" into the one event the pack's
// CombatRules::onPlayerAttack wants. A tap (released before kPowerHoldSeconds) is an attack, told on the release; a hold
// that reaches kPowerHoldSeconds is a power attack, told at that moment, and its release tells nothing more. While
// `enabled` is false (menus, disabled controls) nothing is told and the tracker forgets a hold in progress.
//
// Pure: no engine type, no allocation. Main thread.

namespace MWPhysics
{
    class NeutralPlayerAttackInput
    {
    public:
        enum class Event
        {
            None,
            Attack,
            PowerAttack,
        };

        /// How long the action must be held for a power attack (RECALLED-VERIFY: Skyrim's hold time is not read from
        /// any record here; tunable).
        static constexpr float kPowerHoldSeconds = 0.5f;

        /// One frame. `held`: the action is down now; `dt`: the frame's seconds (non-finite or negative counts as 0).
        Event update(bool held, bool enabled, float dt);

        [[nodiscard]] bool holding() const { return mHolding; }

    private:
        bool mHolding = false;
        bool mIgnoreUntilRelease = false;
        bool mFired = false;
        float mHeld = 0.f;
    };
}

#endif
