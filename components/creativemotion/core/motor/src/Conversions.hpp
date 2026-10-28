#pragma once

// Internal: between core/math (double) and the physics interface (float, to
// match the single-precision Jolt build). Only the ragdoll builder and the
// motor solver cross this boundary.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/physics/Types.hpp"

namespace opas::motor {

inline physics::Vec3 toPhysics(math::Vec3 v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// Normalized in float so the backend's unit-length check always passes.
inline physics::Quat toPhysics(math::Quat q)
{
    const math::Quat n = math::normalized(q);
    return {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z),
            static_cast<float>(n.w)};
}

inline math::Vec3 fromPhysics(physics::Vec3 v)
{
    return {v.x, v.y, v.z};
}

inline math::Quat fromPhysics(physics::Quat q)
{
    return math::normalized(math::Quat{q.x, q.y, q.z, q.w});
}

} // namespace opas::motor
