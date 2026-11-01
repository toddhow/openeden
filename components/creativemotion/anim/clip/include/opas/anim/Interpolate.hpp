#pragma once

// Rotation interpolation shared by clip sampling and pose blending (task 0020,
// stages C3a and C3c), so every layer blends rotations the same way.

#include "opas/math/Quat.hpp"

namespace opas::anim
{

    // Spherical interpolation from a (t = 0) to b (t = 1) along the shorter path
    // (q and -q are the same rotation), normalized (anim/CLAUDE.md). Rotations
    // closer than |dot| 0.9995 interpolate linearly, then normalize.
    math::Quat slerp(math::Quat a, math::Quat b, double t);

} // namespace opas::anim
