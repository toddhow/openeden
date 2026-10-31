#ifndef MISC_MATHUTIL_H
#define MISC_MATHUTIL_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

#include <cmath>
#include <type_traits>

namespace Misc
{
    /// Normalizes given angle to the range [-PI, PI]. E.g. PI*3/2 -> -PI/2.
    inline double normalizeAngle(double angle)
    {
        double fullTurns = angle / (2 * VsgMath::PI) + 0.5;
        return (fullTurns - floor(fullTurns) - 0.5) * (2 * VsgMath::PI);
    }

    /// Rotates given 2d vector counterclockwise. Angle is in radians.
    inline vsg::vec2 rotateVec2f(vsg::vec2 vec, float angle)
    {
        float s = std::sin(angle);
        float c = std::cos(angle);
        return vsg::vec2(vec.x * c + vec.y * -s, vec.x * s + vec.y * c);
    }

    inline vsg::vec3 toEulerAnglesXZ(vsg::vec3 forward)
    {
        float x = -std::asin(forward.z);
        float z = std::atan2(forward.x, forward.y);
        return vsg::vec3(x, 0, z);
    }

    inline vsg::vec3 toEulerAnglesXZ(const vsg::dquat& quat)
    {
        vsg::vec3 forward = vsg::quat(quat) * vsg::vec3(0, 1, 0);
        VsgMath::normalizeInPlace(forward);
        return toEulerAnglesXZ(forward);
    }

    inline vsg::vec3 toEulerAnglesXZ(const vsg::mat4& m)
    {
        vsg::vec3 forward(m[1][0], m[1][1], m[1][2]);
        VsgMath::normalizeInPlace(forward);
        return toEulerAnglesXZ(forward);
    }

    inline vsg::vec3 toEulerAnglesZYX(vsg::vec3 forward, vsg::vec3 up)
    {
        float y = -std::asin(up.x);
        float x = std::atan2(up.y, up.z);
        vsg::vec3 forwardZ = vsg::quat(VsgMath::makeRotateFromAxis(x, vsg::vec3(1, 0, 0)) * VsgMath::makeRotateFromAxis(y, vsg::vec3(0, 1, 0))) * forward;
        float z = std::atan2(forwardZ.x, forwardZ.y);
        return vsg::vec3(x, y, z);
    }

    inline vsg::vec3 toEulerAnglesZYX(const vsg::dquat& quat)
    {
        vsg::vec3 forward = vsg::quat(quat) * vsg::vec3(0, 1, 0);
        VsgMath::normalizeInPlace(forward);
        vsg::vec3 up = vsg::quat(quat) * vsg::vec3(0, 0, 1);
        VsgMath::normalizeInPlace(up);
        return toEulerAnglesZYX(forward, up);
    }

    inline vsg::vec3 toEulerAnglesZYX(const vsg::mat4& m)
    {
        vsg::vec3 forward(m[1][0], m[1][1], m[1][2]);
        vsg::vec3 up(m[2][0], m[2][1], m[2][2]);
        VsgMath::normalizeInPlace(forward);
        VsgMath::normalizeInPlace(up);
        return toEulerAnglesZYX(forward, up);
    }

    template <class T>
    bool isPowerOfTwo(T x)
    {
        static_assert(std::is_integral_v<T>);
        return ((x > 0) && ((x & (x - 1)) == 0));
    }

    inline int nextPowerOfTwo(int v)
    {
        if (isPowerOfTwo(v))
            return v;
        int depth = 0;
        while (v)
        {
            v >>= 1;
            depth++;
        }
        return 1 << depth;
    }
}

#endif
