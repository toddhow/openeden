#pragma once

// Unit quaternion rotations in double precision.
//
// Rules from CLAUDE.md: normalize after composing, and pick the shorter path
// (q and -q are the same rotation) before blending or measuring an error.

#include "opas/math/Vec3.hpp"

#include <algorithm>
#include <cmath>

namespace opas::math {

// Default-constructed is the identity rotation.
struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;

    friend bool operator==(const Quat&, const Quat&) = default;
};

constexpr double dot(Quat a, Quat b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline double length(Quat q)
{
    return std::sqrt(dot(q, q));
}

// Unit quaternion; a zero quaternion becomes the identity.
inline Quat normalized(Quat q)
{
    const double len = length(q);
    if (!(len > 0.0)) {
        return Quat{};
    }
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

constexpr Quat conjugate(Quat q)
{
    return {-q.x, -q.y, -q.z, q.w};
}

// Hamilton product: rotating by (a * b) applies b first, then a.
constexpr Quat operator*(Quat a, Quat b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Rotation of angleRad about a unit axis (right-hand rule).
inline Quat fromAxisAngle(Vec3 unitAxis, double angleRad)
{
    const double half = 0.5 * angleRad;
    const double s = std::sin(half);
    return {unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(half)};
}

// Rotates v by the unit quaternion q.
constexpr Vec3 rotate(Quat q, Vec3 v)
{
    // v' = v + 2w(u x v) + 2(u x (u x v)), with u the vector part of q.
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = 2.0 * cross(u, v);
    return v + q.w * t + cross(u, t);
}

// Smallest angle (in [0, pi]) of the rotation taking a to b. q and -q give 0.
// Uses atan2 of the relative rotation's vector and scalar parts rather than
// acos of a dot product: acos is inaccurate near 0 (about 1e-8 rad), atan2
// stays accurate for tiny angles. |w| picks the shorter path.
inline double angleBetween(Quat a, Quat b)
{
    const Quat r = conjugate(normalized(a)) * normalized(b);
    const double vectorPart = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    return 2.0 * std::atan2(vectorPart, std::abs(r.w));
}

// Shortest-arc rotation taking direction `from` onto direction `to` (neither
// needs to be unit length). Opposite directions rotate by pi about an axis
// perpendicular to `from`, chosen deterministically. A zero input gives the
// identity.
inline Quat fromTo(Vec3 from, Vec3 to)
{
    const Vec3 a = normalized(from);
    const Vec3 b = normalized(to);
    if (lengthSquared(a) == 0.0 || lengthSquared(b) == 0.0) {
        return Quat{};
    }
    const double d = dot(a, b);
    if (d < -1.0 + 1e-12) {
        // Opposite: any perpendicular axis works; use the world axis least
        // aligned with `a` so the cross product is well conditioned.
        const Vec3 helper = std::abs(a.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
        return fromAxisAngle(normalized(cross(a, helper)), std::acos(-1.0));
    }
    const Vec3 c = cross(a, b);
    return normalized(Quat{c.x, c.y, c.z, 1.0 + d});
}

} // namespace opas::math
