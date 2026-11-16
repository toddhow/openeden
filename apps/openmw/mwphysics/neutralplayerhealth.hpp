#ifndef OPENMW_MWPHYSICS_NEUTRALPLAYERHEALTH_H
#define OPENMW_MWPHYSICS_NEUTRALPLAYERHEALTH_H

// Task 0206 (wiring): what the engine's player health takes when the pack reports a damage or heal (task 0171) or a
// raised maximum (task 0200: a level up). Pure; the stat itself is written in worldimp.cpp (setNeutralPlayerHooks).

#include <cmath>

namespace MWPhysics
{
    /// A maximum closer than this to the stat's own is the same one (the pack's float arithmetic): not rewritten.
    inline constexpr float kMaximumTolerance = 0.001f;

    struct PlayerHealthWrite
    {
        float mBase = 0.f; ///< the stat's base value (its modified value is the base plus the modifier, never below 0)
        float mCurrent = 0.f;
    };

    /// `base`, `modifier` and `current` are the engine stat now; `healthAfter` and `healthMax` the pack's report. A
    /// report with a maximum above 0 makes the stat's modified value that maximum (the base follows, the modifier is
    /// kept), a report with none leaves the maximum alone. The current value is the report's, kept inside [0, maximum];
    /// one that is not a number leaves it as it was.
    [[nodiscard]] inline PlayerHealthWrite neutralPlayerHealthWrite(
        float base, float modifier, float current, float healthAfter, float healthMax)
    {
        const float modified = base + modifier > 0.f ? base + modifier : 0.f;
        float maximum = modified;
        PlayerHealthWrite out;
        out.mBase = base;
        if (std::isfinite(healthMax) && healthMax > 0.f && std::fabs(healthMax - modified) > kMaximumTolerance)
        {
            out.mBase = healthMax - modifier > 0.f ? healthMax - modifier : 0.f;
            maximum = out.mBase + modifier > 0.f ? out.mBase + modifier : 0.f;
        }
        const float wanted = std::isfinite(healthAfter) ? healthAfter : current;
        out.mCurrent = wanted < 0.f ? 0.f : wanted > maximum ? maximum : wanted;
        return out;
    }
}

#endif
