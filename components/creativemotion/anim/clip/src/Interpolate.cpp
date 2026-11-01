#include "opas/anim/Interpolate.hpp"

#include <cmath>

namespace opas::anim
{
    namespace
    {

        // Rotations closer than this (|dot| above it) interpolate linearly, where
        // slerp's sin(theta) would lose precision.
        constexpr double kLinearAbove = 0.9995;

    } // namespace

    math::Quat slerp(math::Quat a, math::Quat b, double t)
    {
        double d = math::dot(a, b);
        if (d < 0.0)
        {
            b = math::Quat{ -b.x, -b.y, -b.z, -b.w };
            d = -d;
        }
        double wa = 1.0 - t;
        double wb = t;
        if (d < kLinearAbove)
        {
            const double theta = std::acos(d);
            const double s = std::sin(theta);
            wa = std::sin((1.0 - t) * theta) / s;
            wb = std::sin(t * theta) / s;
        }
        return math::normalized(
            math::Quat{ wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w });
    }

} // namespace opas::anim
