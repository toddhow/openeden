#pragma once

// Swing-twist decomposition (Milestone C6, task 0021).
// Decomposes an arbitrary rotation quaternion into a swing rotation about an axis
// perpendicular to twistAxis and a twist rotation purely about twistAxis.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

namespace opas::anim
{

    struct SwingTwist
    {
        math::Quat swing;
        math::Quat twist;
        double twistAngleRad = 0.0;
    };

    // Decomposes unit quaternion q into q = swing * twist about unit twistAxis.
    // twist is a rotation purely about twistAxis by twistAngleRad in [-pi, pi].
    // swing is a rotation about an axis perpendicular to twistAxis.
    // If twist is degenerate (swing is 180 degrees), returns identity twist and twistAngleRad = 0.
    // Double cover safe (q and -q produce identical decompositions).
    SwingTwist decomposeSwingTwist(math::Quat q, math::Vec3 twistAxis);

} // namespace opas::anim
