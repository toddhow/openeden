#ifndef OPENEDEN_RENDER_LIGHTFADE_H
#define OPENEDEN_RENDER_LIGHTFADE_H

// Task 0355 stage j: the two distance rules a game pack's interior states for itself (Skyrim SE: CELL XCLL light fade
// begin / end and clip distance). Pure; no engine type.

#include <cmath>

namespace Render::LightFade
{
    /// The share of a light's colour that is left at `distance` from the eye: 1 up to `begin`, falling in a line to 0
    /// at `end`. No fade (1) when `end` is not a positive number, or when any number is not finite. An `end` that is
    /// not past `begin` is a cut at `begin` (the lighting templates of the masters write 11,000 and 11,000 five times).
    /// A negative `begin` counts as 0.
    [[nodiscard]] inline float factor(float distance, float begin, float end)
    {
        if (!std::isfinite(distance) || !std::isfinite(begin) || !std::isfinite(end) || !(end > 0.f))
            return 1.f;
        if (begin < 0.f)
            begin = 0.f;
        if (distance < begin)
            return 1.f;
        if (!(end > begin))
            return 0.f;
        if (distance >= end)
            return 0.f;
        return (end - distance) / (end - begin);
    }

    /// The far plane with a cell's clip distance applied: the clip distance when it is a positive number below the far
    /// plane, else the far plane.
    [[nodiscard]] inline float effectiveFarPlane(float farPlane, float clipDistance)
    {
        if (std::isfinite(clipDistance) && clipDistance > 0.f && clipDistance < farPlane)
            return clipDistance;
        return farPlane;
    }
}

#endif
