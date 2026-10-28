#pragma once

// 3D vector in double precision. Units are whatever the caller stores
// (usually meters). Right-handed, Z up.

#include <cmath>

namespace opas::math {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vec3 operator-(Vec3 v)
{
    return {-v.x, -v.y, -v.z};
}

constexpr Vec3 operator*(Vec3 v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}

constexpr Vec3 operator*(double s, Vec3 v)
{
    return v * s;
}

constexpr Vec3 operator/(Vec3 v, double s)
{
    return {v.x / s, v.y / s, v.z / s};
}

constexpr double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr double lengthSquared(Vec3 v)
{
    return dot(v, v);
}

inline double length(Vec3 v)
{
    return std::sqrt(lengthSquared(v));
}

inline double distance(Vec3 a, Vec3 b)
{
    return length(a - b);
}

// Unit vector in the same direction. The zero vector stays zero (there is no
// direction to keep), so callers that need a direction must check for it.
inline Vec3 normalized(Vec3 v)
{
    const double len = length(v);
    return len > 0.0 ? v / len : Vec3{};
}

} // namespace opas::math
