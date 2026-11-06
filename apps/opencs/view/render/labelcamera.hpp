#ifndef OPENCS_VIEW_LABELCAMERA_H
#define OPENCS_VIEW_LABELCAMERA_H

#include <cmath>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>

#include <apps/opencs/model/module/labelstroke.hpp>

namespace CSVRender
{
    /// The camera of a 3D view as the label layout needs it (task 0091 stage d): eye and axes from the view matrix
    /// (VSG's lookAt: +X right, +Y up on screen, -Z forward in eye space), the size of a pixel from the projection's
    /// focal term (a perspective projection has 1 / tan(fov / 2) in element (1, 1), its sign flipped for Vulkan).
    inline CSMModule::LabelCamera makeLabelCamera(
        const vsg::dmat4& view, const vsg::dmat4& projection, double viewportHeightPixels)
    {
        const vsg::dmat4 eyeToWorld = vsg::inverse(view);
        const vsg::dvec3 eye = eyeToWorld * vsg::dvec3(0.0, 0.0, 0.0);
        const auto axis = [&](double x, double y, double z) {
            const vsg::dvec3 d = eyeToWorld * vsg::dvec3(x, y, z) - eye;
            return vsg::normalize(d);
        };
        const vsg::dvec3 right = axis(1.0, 0.0, 0.0);
        const vsg::dvec3 up = axis(0.0, 1.0, 0.0);
        const vsg::dvec3 forward = axis(0.0, 0.0, -1.0);

        CSMModule::LabelCamera camera;
        for (int i = 0; i < 3; ++i)
        {
            camera.mEye[i] = eye[i];
            camera.mRight[i] = right[i];
            camera.mUp[i] = up[i];
            camera.mForward[i] = forward[i];
        }
        const double focal = std::abs(projection(1, 1));
        camera.mPixelSlope = (focal > 0.0 && viewportHeightPixels > 0.0) ? 2.0 / (focal * viewportHeightPixels) : 0.0;
        return camera;
    }
}

#endif
