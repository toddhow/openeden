#ifndef CSV_RENDER_INSTANCEMATH_H
#define CSV_RENDER_INSTANCEMATH_H

#include <algorithm>
#include <cmath>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>

#include <components/misc/vsgmath.hpp>

/// The sums of InstanceMode, with no window, so that openmw-cs-tests can check them. vsg::dquat's a * b means a, then
/// b, as osg::Quat's did: its formula is OSG's (vsg/maths/quat.h).
namespace CSVRender::InstanceMath
{
    inline vsg::vec3 quatToEuler(const vsg::dquat& rot)
    {
        float x, y, z;
        const float test = static_cast<float>(2 * (rot.w * rot.y + rot.x * rot.z));

        if (std::abs(test) >= 1.f)
        {
            x = static_cast<float>(std::atan2(rot.x, rot.w));
            y = static_cast<float>((test > 0) ? Misc::VsgMath::PI_2 : -Misc::VsgMath::PI_2);
            z = 0;
        }
        else
        {
            x = static_cast<float>(
                std::atan2(2 * (rot.w * rot.x - rot.y * rot.z), 1 - 2 * (rot.x * rot.x + rot.y * rot.y)));
            y = std::asin(std::clamp(test, -1.0f, 1.0f));
            z = static_cast<float>(
                std::atan2(2 * (rot.w * rot.z - rot.x * rot.y), 1 - 2 * (rot.y * rot.y + rot.z * rot.z)));
        }

        return vsg::vec3(-x, -y, -z);
    }

    inline vsg::dquat eulerToQuat(const vsg::vec3& euler)
    {
        const vsg::dquat xr = Misc::VsgMath::makeRotateFromAxis(-euler.x, vsg::dvec3(1, 0, 0));
        const vsg::dquat yr = Misc::VsgMath::makeRotateFromAxis(-euler.y, vsg::dvec3(0, 1, 0));
        const vsg::dquat zr = Misc::VsgMath::makeRotateFromAxis(-euler.z, vsg::dvec3(0, 0, 1));
        return zr * yr * xr;
    }

    inline float roundToMult(float val, double mult)
    {
        if (mult == 0)
            return val;
        return static_cast<float>(std::round(val / mult) * mult);
    }

    inline vsg::vec3 snapRelativeToTarget(const vsg::vec3& initialPosition, const vsg::vec3& targetPosition,
        const vsg::vec3& targetRotation, const vsg::vec3& translation, double snap)
    {
        // osg::Quat(x, X_AXIS, y, Y_AXIS, z, Z_AXIS): x, then y, then z.
        const vsg::dquat rotation = Misc::VsgMath::makeRotateFromAxis(targetRotation.x, vsg::dvec3(1, 0, 0))
            * Misc::VsgMath::makeRotateFromAxis(targetRotation.y, vsg::dvec3(0, 1, 0))
            * Misc::VsgMath::makeRotateFromAxis(targetRotation.z, vsg::dvec3(0, 0, 1));
        const vsg::dquat inverse(-rotation.x, -rotation.y, -rotation.z, rotation.w);

        // Break object world coords into snap target space
        const vsg::dvec3 local = rotation * (vsg::dvec3(initialPosition) - vsg::dvec3(targetPosition));

        const vsg::dvec3 snapped(roundToMult(static_cast<float>(local.x + translation.x), snap),
            roundToMult(static_cast<float>(local.y + translation.y), snap),
            roundToMult(static_cast<float>(local.z + translation.z), snap));

        // rebuild object's world coordinates (note: inverse operations from local construction)
        return vsg::vec3(inverse * snapped + vsg::dvec3(targetPosition));
    }

    /// One of a group: turned with the rest around their common centre.
    inline void rotateAroundCentre(
        vsg::vec3& position, vsg::vec3& euler, const vsg::vec3& centre, const vsg::dquat& rotation)
    {
        position = vsg::vec3(rotation * vsg::dvec3(position - centre)) + centre;
        euler = quatToEuler(eulerToQuat(euler) * rotation);
    }

    /// Where a point is in the picture: x and y from -1 to 1, y going down as VSG's projections have it, and z its
    /// depth, which mousePlaneCoords takes back.
    inline vsg::vec3 projectionCoords(const vsg::dmat4& projection, const vsg::dmat4& view, const vsg::vec3& position)
    {
        return vsg::vec3(projection * (view * vsg::dvec3(position)));
    }

    /// The world's point at a place in the picture, where (0, 0) is its top left and (1, 1) its bottom right, at a
    /// depth from projectionCoords.
    inline vsg::vec3 mousePlaneCoords(
        const vsg::dmat4& projection, const vsg::dmat4& view, double x, double y, double depth)
    {
        return vsg::vec3(
            vsg::inverse(view) * (vsg::inverse(projection) * vsg::dvec3(x * 2.0 - 1.0, y * 2.0 - 1.0, depth)));
    }

    /// In pixels of a window of this size, with y counted from the bottom, as OSG's window matrix did and as drag()
    /// expects.
    inline vsg::vec3 windowCoords(
        const vsg::dmat4& projection, const vsg::dmat4& view, const vsg::vec3& position, int width, int height)
    {
        const vsg::vec3 ndc = projectionCoords(projection, view, position);
        return vsg::vec3(
            (ndc.x + 1.f) * 0.5f * static_cast<float>(width), (1.f - ndc.y) * 0.5f * static_cast<float>(height), ndc.z);
    }
}

#endif
