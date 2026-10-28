#pragma once

// Analytic two-bone IK (e.g. shoulder-elbow-wrist, hip-knee-ankle) with a
// hinge limit on the middle joint. Works on world positions; the root stays
// fixed and both bone lengths are preserved.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

namespace opas::ik {

// Current world positions of the three joints.
struct TwoBoneChain {
    math::Vec3 root;
    math::Vec3 mid;
    math::Vec3 end;
};

// Bend of the middle joint: 0 = straight (bones in line), pi = fully folded.
// Must satisfy 0 <= minBendRad <= maxBendRad <= pi.
struct HingeLimits {
    double minBendRad = 0.0;
    double maxBendRad = 3.14159265358979323846;
};

struct TwoBoneResult {
    bool valid = false;   // false for zero-length bones or bad limits; positions then unchanged
    bool reached = false; // target was within the reach the limits allow
    math::Vec3 mid;       // solved world positions
    math::Vec3 end;
    double bendRad = 0.0; // solved bend of the middle joint
    // World-space rotations that take the original upper bone direction
    // (mid - root) and lower bone direction (end - mid) to the solved ones.
    math::Quat rootRotation;
    math::Quat midRotation;
};

// Places `end` at `target` if the limits allow it, otherwise at the nearest
// reachable distance along the ray root -> target. The bend plane contains
// root, target and the pole direction; the mid joint goes on the pole side.
// If the pole is (nearly) parallel to root -> target, the plane of the current
// chain is used, and if that is degenerate too, a fixed perpendicular.
// If target == root, the current root -> end direction is used.
// Non-finite input (or input so large that lengths overflow) returns
// valid == false with positions unchanged.
TwoBoneResult solveTwoBone(const TwoBoneChain& chain, math::Vec3 target, math::Vec3 poleDirection,
                           const HingeLimits& limits);

} // namespace opas::ik
