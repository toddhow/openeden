#ifndef OPENMW_MWPHYSICS_NEUTRALSIGHT_H
#define OPENMW_MWPHYSICS_NEUTRALSIGHT_H

// Task 0171 (combat stage S5-3b): is the straight line between a swing and its target blocked by the world? One ray
// through the Jolt system against the static layers the caller names (walls, floors, closed doors); the bodies in
// `ignore` (the neutral actors' own capsules, which live on the WORLD layer too) never block. Main thread; allocation
// free (the ignore list is a span and the filter holds it by pointer).

#include <span>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>

#include <vsg/maths/vec3.h>

namespace JPH
{
    class PhysicsSystem;
}

namespace MWPhysics::NeutralSight
{
    /// The segment is shortened by this much at its far end, so a target standing against a wall is not blocked by it.
    inline constexpr float kEndTrim = 2.f;

    /// True when a body of `layerMask` (Layers bits) that is not in `ignore` is hit between `from` and `to`. A segment
    /// that is not finite or shorter than kEndTrim is never blocked.
    [[nodiscard]] bool blocked(const JPH::PhysicsSystem& system, const vsg::vec3& from, const vsg::vec3& to,
        int layerMask, std::span<const JPH::BodyID> ignore);

    /// Task 0178 (a projectile's flight): the first body of `layerMask` that is not in `ignore` along the whole segment
    /// from `from` to `to`, as the fraction of the segment in [0, 1] (not shortened by kEndTrim). False for no hit, a
    /// segment that is not finite or shorter than 1e-4.
    [[nodiscard]] bool firstHit(const JPH::PhysicsSystem& system, const vsg::vec3& from, const vsg::vec3& to,
        int layerMask, std::span<const JPH::BodyID> ignore, float& fraction);
}

#endif
