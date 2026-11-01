#ifndef CSV_RENDER_CAMERAPOSE_H
#define CSV_RENDER_CAMERAPOSE_H

#include <cmath>
#include <limits>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>

/// What the camera controllers do to a view matrix, keeping what the OSG editor's osg::Matrixd calls gave. No Qt and no
/// scene, so that openmw-cs-tests can check it. Eye space is the same in OSG and VSG (x right, y up, looking along -z):
/// VSG's Y flip and depth range are in the projection, so nothing here knows of them.
namespace CSVRender::CameraPose
{
    struct LookAt
    {
        vsg::dvec3 mEye;
        vsg::dvec3 mCenter;
        vsg::dvec3 mUp;
    };

    inline vsg::dmat4 viewFrom(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        return vsg::lookAt(eye, center, up);
    }

    /// A direction in eye space, in the world: the inverse of the view's rotation, which is its transpose.
    inline vsg::dvec3 eyeToWorldDirection(const vsg::dmat4& view, const vsg::dvec3& direction)
    {
        return vsg::dvec3(view[0][0] * direction.x + view[0][1] * direction.y + view[0][2] * direction.z,
            view[1][0] * direction.x + view[1][1] * direction.y + view[1][2] * direction.z,
            view[2][0] * direction.x + view[2][1] * direction.y + view[2][2] * direction.z);
    }

    /// As osg::Matrixd::getLookAt: \p distance is how far along the view direction the centre goes.
    inline LookAt lookAtFrom(const vsg::dmat4& view, double distance = 1.0)
    {
        const vsg::dvec3 translation(view[3][0], view[3][1], view[3][2]);
        const vsg::dvec3 eye = -eyeToWorldDirection(view, translation);
        const vsg::dvec3 forward = vsg::normalize(eyeToWorldDirection(view, vsg::dvec3(0, 0, -1)));
        return { eye, eye + forward * distance, eyeToWorldDirection(view, vsg::dvec3(0, 1, 0)) };
    }

    /// `view *= osg::Matrixd::rotate(angle, axis)`: applied after the view.
    inline vsg::dmat4 rotatedInEye(const vsg::dmat4& view, double angle, const vsg::dvec3& axis)
    {
        return vsg::rotate(angle, axis) * view;
    }

    /// `view *= osg::Matrixd::translate(offset)`
    inline vsg::dmat4 translatedInEye(const vsg::dmat4& view, const vsg::dvec3& offset)
    {
        return vsg::translate(offset) * view;
    }

    /// `osg::Quat(angle, axis) * vector`. A zero axis is no rotation, as osg::Quat::makeRotate.
    inline vsg::dvec3 rotated(const vsg::dvec3& vector, double angle, const vsg::dvec3& axis)
    {
        const double length = vsg::length(axis);
        if (length < std::numeric_limits<double>::epsilon())
            return vector;
        const vsg::dvec3 unit = axis / length;
        const double cosine = std::cos(angle);
        return vector * cosine + vsg::cross(unit, vector) * std::sin(angle)
            + unit * (vsg::dot(unit, vector) * (1.0 - cosine));
    }

    /// Is this the same view, but for rounding? Normalizing an unmoved view each tick must ask for no frame.
    inline bool sameView(const vsg::dmat4& a, const vsg::dmat4& b)
    {
        // Rotation entries are unit sized, translations are in world units of up to some 1e6.
        constexpr double rotationTolerance = 1e-9;
        constexpr double translationTolerance = 1e-5;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (std::abs(a[column][row] - b[column][row])
                    > (column == 3 ? translationTolerance : rotationTolerance))
                    return false;
        return true;
    }

    /// Can a view from eye to centre be held upright by this up? vsg::lookAt's normalize of a zero cross product gives
    /// NaN, which then stays in the view, where OSG's normalize left a zero alone.
    inline bool canStabilize(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        const vsg::dvec3 forward = center - eye;
        constexpr double minimumSine2 = 1e-16;
        return vsg::length2(vsg::cross(forward, up)) > minimumSine2 * vsg::length2(forward) * vsg::length2(up);
    }

    /// As osg::Matrixd::orthoNormal: scales what would be its columns, the rows here, to unit length, and keeps the
    /// translation as it is. It corrects scale drift, not skew.
    inline vsg::dmat4 orthoNormal(const vsg::dmat4& view)
    {
        vsg::dmat4 result = view;
        for (int row = 0; row < 3; ++row)
        {
            const double length
                = std::sqrt(view[0][row] * view[0][row] + view[1][row] * view[1][row] + view[2][row] * view[2][row]);
            if (length == 0.0)
                continue;
            for (int column = 0; column < 3; ++column)
                result[column][row] = view[column][row] / length;
        }
        return result;
    }
}

#endif
