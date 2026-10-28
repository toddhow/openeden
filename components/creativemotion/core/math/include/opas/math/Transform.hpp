#pragma once

// Rigid transform: rotate, then translate. Used for bone transforms
// (local = relative to the parent bone, world = relative to the model root).

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

namespace opas::math {

struct Transform {
    Vec3 translation;
    Quat rotation;

    friend bool operator==(const Transform&, const Transform&) = default;
};

constexpr Vec3 transformPoint(const Transform& t, Vec3 p)
{
    return rotate(t.rotation, p) + t.translation;
}

// parent * child: the child's transform expressed in the parent's space,
// e.g. world = parentWorld * childLocal.
inline Transform operator*(const Transform& parent, const Transform& child)
{
    return {transformPoint(parent, child.translation),
            normalized(parent.rotation * child.rotation)};
}

inline Transform inverse(const Transform& t)
{
    const Quat inv = conjugate(t.rotation);
    return {rotate(inv, -t.translation), inv};
}

} // namespace opas::math
