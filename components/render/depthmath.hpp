#ifndef OPENEDEN_RENDER_DEPTHMATH_H
#define OPENEDEN_RENDER_DEPTHMATH_H

#include <cmath>
#include <limits>

#include <vsg/maths/mat4.h>

namespace Render::DepthMath
{
    /*
     * The reversed depth buffer's numbers (task 0066, stage a). The renderer stores depth as VSG's perspective matrix
     * writes it (vsg/maths/transform.h, `perspective`): 1 at the near plane, 0 at the far plane, in a D32_SFLOAT buffer
     * with the compare op GREATER and the clear value 0 (Render::nearDepth and farDepth, depth.hpp). These functions
     * give the maths the tests and the cost notes use; the shaders repeat `viewZFromDepth` in
     * files/shaders/lib/view/depthbuffer.glsl (`depthBufferValueToViewZ`): change both together.
     *
     * Everything is plain functions on doubles, no allocation, any thread.
     */

    /// The stored depth of a point `distance` in front of the camera: n (f - d) / ((f - n) d). 1 at d = n, 0 at d = f.
    /// Returns 0 for a projection that cannot exist (near not positive, far not beyond near) or a distance not
    /// positive.
    inline double ndcDepth(double nearDistance, double farDistance, double distance)
    {
        if (!(nearDistance > 0.0) || !(farDistance > nearDistance) || !(distance > 0.0))
            return 0.0;
        return nearDistance * (farDistance - distance) / ((farDistance - nearDistance) * distance);
    }

    /// The inverse of ndcDepth: n f / (depth (f - n) + n). Returns 0 for a projection that cannot exist.
    inline double distanceFromNdcDepth(double nearDistance, double farDistance, double depth)
    {
        if (!(nearDistance > 0.0) || !(farDistance > nearDistance))
            return 0.0;
        return nearDistance * farDistance / (depth * (farDistance - nearDistance) + nearDistance);
    }

    /// The smallest change in distance at `distance` that changes the stored float depth: the distance difference for
    /// one float step of the depth value there. 0 for an impossible projection or a distance outside (0, far].
    inline double depthResolution(double nearDistance, double farDistance, double distance)
    {
        if (!(nearDistance > 0.0) || !(farDistance > nearDistance) || !(distance > 0.0) || distance > farDistance)
            return 0.0;
        const float depth = static_cast<float>(ndcDepth(nearDistance, farDistance, distance));
        const float next = std::nextafter(depth, std::numeric_limits<float>::infinity());
        return std::fabs(distanceFromNdcDepth(nearDistance, farDistance, next)
            - distanceFromNdcDepth(nearDistance, farDistance, depth));
    }

    /// The same for the conventional mapping, 0 at the near plane and 1 at the far plane, f (d - n) / (d (f - n)), for
    /// the comparison that shows why the buffer is reversed.
    inline double forwardDepthResolution(double nearDistance, double farDistance, double distance)
    {
        if (!(nearDistance > 0.0) || !(farDistance > nearDistance) || !(distance > 0.0) || distance > farDistance)
            return 0.0;
        const double range = farDistance - nearDistance;
        const float depth = static_cast<float>(farDistance * (distance - nearDistance) / (distance * range));
        const float next = std::nextafter(depth, std::numeric_limits<float>::infinity());
        const auto distanceOf
            = [&](double value) { return farDistance * nearDistance / (farDistance - value * range); };
        return std::fabs(distanceOf(next) - distanceOf(depth));
    }

    /// The view-space z (negative in front of the camera) from a stored depth and the float inverse projection, as
    /// depthBufferValueToViewZ computes it in the shaders (GLSL indexes [column][row], so does vsg::mat4).
    inline float viewZFromDepth(const vsg::mat4& invProjection, float depth)
    {
        return invProjection[3][2] / (invProjection[2][3] * depth + invProjection[3][3]);
    }
}

#endif
