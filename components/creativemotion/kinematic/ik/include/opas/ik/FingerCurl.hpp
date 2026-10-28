#pragma once

// Finger curl to contact (A5c, docs/tasks/0009-a5-body-surfaces-fingers.md):
// one curl parameter t in [0, 1] per finger; joint i turns by t * its limit
// about its hinge axis, and the solver finds the smallest t at which the
// fingertip touches a capsule. Works on world positions, like TwoBoneIk.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

#include <span>
#include <vector>

namespace opas::ik {

struct FingerJoint {
    math::Vec3 position;     // world position of the joint
    math::Vec3 hingeAxis;    // world-space axis it bends about (any length > 0)
    double maxCurlRad = 0.0; // its turn at t = 1, in [0, pi]
};

// A capsule: the segment from `start` to `end` widened by `radius`.
struct CapsuleShape {
    math::Vec3 start;
    math::Vec3 end;
    double radius = 0.0; // > 0
};

struct FingerCurlResult {
    bool valid = false;   // false for fewer than 2 joints, bad axes, limits or capsule
    bool touched = false; // the tip reaches the surface at some t in [0, 1]
    double t = 1.0;       // the curl used (1 when not touched: closed to the limits)
    // World-space delta rotation of each joint's bone (apply on top of its
    // posed world rotation), in joint order; each includes the turns of the
    // joints before it.
    std::vector<math::Quat> jointDeltas;
    math::Vec3 tip; // solved fingertip position
};

// The fingertip is the last joint plus the last segment (previous joint to
// last joint) once more along the same direction, since skeletons have no
// tip bone. If the tip already touches at t = 0, t = 0. Otherwise the curl
// range is scanned in equal steps for the first touch (at least `scanSteps`,
// and enough that the tip moves at most half the target radius per step, up
// to 4096) and then bisected to `tolerance` in t. A grazing touch shorter
// than one step can still be missed. Deterministic; never throws.
FingerCurlResult solveFingerCurl(std::span<const FingerJoint> joints, const CapsuleShape& target,
                                 int scanSteps = 32, double tolerance = 1e-6);

} // namespace opas::ik
