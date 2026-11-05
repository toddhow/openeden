#ifndef OPENEDEN_RENDER_POINTSHADOWMATH_H
#define OPENEDEN_RENDER_POINTSHADOWMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

#include "depthmath.hpp"

namespace Render::PointShadow
{
    /*
     * The maths of the point lights' shadow maps (task 0077). One light has six square depth faces in world axes; the
     * receiver's shader repeats these functions in files/shaders/lib/light/pointshadow.slang and .glsl (change them
     * together; apps/components_tests/render/testpointshadowshader.cpp compares the tables).
     *
     * Everything is plain functions on doubles, no allocation, any thread.
     */

    constexpr int facesPerLight = 6;
    constexpr int maxSlots = 8;
    constexpr int maxTaps = 16;
    constexpr double nearPlane = 4.0; // game units
    constexpr double farMargin = 1.1; // far plane = farMargin * the light's reach
    constexpr double defaultGuardTexels = 2.0;
    // Bytes of the shadow maps above which a warning is logged at start. A round figure chosen with toddhow's
    // decision 0012 in mind (8 slots at 2048 texels is 768 MiB); about a fifth of a 12 GiB card, a quarter of 8 GiB
    // headroom for textures. Not measured against any card: it is a prompt to look, not a limit.
    constexpr std::uint64_t memoryWarningBytes = 256ull * 1024ull * 1024ull;
    constexpr int depthBytesPerTexel = 4; // D32_SFLOAT

    /// The memory of the shadow maps: slots * 6 faces * resolution squared * 4 bytes (View::PointShadow's one array).
    inline std::uint64_t memoryBytes(int slots, int resolution)
    {
        if (slots <= 0 || resolution <= 0)
            return 0;
        const auto texels = static_cast<std::uint64_t>(resolution) * static_cast<std::uint64_t>(resolution);
        return static_cast<std::uint64_t>(slots) * facesPerLight * texels * depthBytesPerTexel;
    }

    /// The start-up warning, or an empty string when the maps take at most memoryWarningBytes.
    inline std::string memoryWarningText(int slots, int resolution)
    {
        const std::uint64_t bytes = memoryBytes(slots, resolution);
        if (bytes <= memoryWarningBytes)
            return {};
        return "Point light shadows: " + std::to_string(slots) + " slots at " + std::to_string(resolution)
            + " texels take " + std::to_string(bytes / (1024ull * 1024ull))
            + " MiB of video memory. To use less, lower [Shadows] point light shadow count or point light shadow "
              "resolution (each halving of the resolution divides the memory by four).";
    }

    struct FaceAxes
    {
        vsg::dvec3 right;
        vsg::dvec3 up;
        vsg::dvec3 forward;
    };

    /// Faces 0..5 = +X, -X, +Y, -Y, +Z, -Z (forward); right = cross(forward, up), what vsg::LookAt uses.
    inline FaceAxes faceAxes(int face)
    {
        struct Row
        {
            vsg::dvec3 forward;
            vsg::dvec3 up;
        };
        static const Row rows[facesPerLight] = {
            { { 1, 0, 0 }, { 0, 0, 1 } },
            { { -1, 0, 0 }, { 0, 0, 1 } },
            { { 0, 1, 0 }, { 0, 0, 1 } },
            { { 0, -1, 0 }, { 0, 0, 1 } },
            { { 0, 0, 1 }, { 0, -1, 0 } },
            { { 0, 0, -1 }, { 0, 1, 0 } },
        };
        const Row& row = rows[face >= 0 && face < facesPerLight ? face : 0];
        return { vsg::cross(row.forward, row.up), row.up, row.forward };
    }

    /// The face whose forward axis has the largest component of |direction| (x before y before z on ties); the sign
    /// picks + or -. The zero vector gives 0.
    inline int faceOf(const vsg::dvec3& direction)
    {
        const double ax = std::fabs(direction.x);
        const double ay = std::fabs(direction.y);
        const double az = std::fabs(direction.z);
        if (ax >= ay && ax >= az)
            return direction.x < 0.0 ? 1 : 0;
        if (ay >= az)
            return direction.y < 0.0 ? 3 : 2;
        return direction.z < 0.0 ? 5 : 4;
    }

    /// The tangent of the half field of view of a face with a guard band of `guardTexels` on each side.
    inline double tanHalfFov(double guardTexels, double resolution)
    {
        if (!(guardTexels > 0.0) || !(resolution > 4.0 * guardTexels))
            return 1.0;
        return 1.0 + 2.0 * guardTexels / (resolution - 2.0 * guardTexels);
    }

    struct FaceCoord
    {
        int face = 0;
        double u = 0.5;
        double v = 0.5;
        double distance = 0.0;
    };

    /// `direction` in world axes from the light to the point.
    inline FaceCoord project(const vsg::dvec3& direction, double tanHalf)
    {
        if (direction.x == 0.0 && direction.y == 0.0 && direction.z == 0.0)
            return {};
        FaceCoord result;
        result.face = faceOf(direction);
        const FaceAxes axes = faceAxes(result.face);
        result.distance = vsg::dot(direction, axes.forward);
        const double scale = 0.5 / (result.distance * tanHalf);
        result.u = 0.5 + scale * vsg::dot(direction, axes.right);
        result.v = 0.5 + scale * vsg::dot(direction, axes.up);
        return result;
    }

    /// The stored depth of a point `distance` along a face's axis: the reversed depth buffer's n (f - d) / ((f - n) d).
    inline double depthValue(double far, double distance)
    {
        return DepthMath::ndcDepth(nearPlane, far, distance);
    }

    /// The world size of one texel of a face at `distance`.
    inline double texelWorldSize(double distance, double tanHalf, double resolution)
    {
        return 2.0 * distance * tanHalf / resolution;
    }

    struct Bias
    {
        double normalOffsetTexels = 1.0;
        double depthBiasTexels = 1.0;
        double scale = 1.5; // multiplies both
    };

    /// The receiver moved along its surface normal by a fraction of a texel's world size.
    inline vsg::dvec3 offsetReceiver(
        const vsg::dvec3& toReceiver, const vsg::dvec3& unitNormal, double tanHalf, double resolution, const Bias& bias)
    {
        const double distance = project(toReceiver, tanHalf).distance;
        const double length = bias.scale * bias.normalOffsetTexels * texelWorldSize(distance, tanHalf, resolution);
        return toReceiver + unitNormal * length;
    }

    /// The depth the receiver is compared with: its distance reduced by a bias in texels, never below the near plane.
    inline double compareDepth(double far, double distance, double tanHalf, double resolution, const Bias& bias)
    {
        const double biased
            = distance - bias.scale * bias.depthBiasTexels * texelWorldSize(distance, tanHalf, resolution);
        return depthValue(far, std::max(biased, nearPlane));
    }

    /// Two unit vectors orthogonal to each other and to the normal, cross(t1, t2) = normal. Frisvad's construction,
    /// which is continuous everywhere but at the -Z pole; for a normal that points down (n.z < -0.95) it is done for
    /// the normal turned half a turn about the X axis and the answer turned back, so the basis stays exact at the pole.
    inline void frisvadBasis(const vsg::dvec3& n, vsg::dvec3& t1, vsg::dvec3& t2)
    {
        const double a = 1.0 / (1.0 + n.z);
        const double b = -n.x * n.y * a;
        t1 = vsg::dvec3(1.0 - n.x * n.x * a, b, -n.x);
        t2 = vsg::dvec3(b, 1.0 - n.y * n.y * a, -n.y);
    }

    inline void tangentBasis(const vsg::dvec3& n, vsg::dvec3& t1, vsg::dvec3& t2)
    {
        if (n.z < -0.95)
        {
            frisvadBasis(vsg::dvec3(n.x, -n.y, -n.z), t1, t2);
            t1 = vsg::dvec3(t1.x, -t1.y, -t1.z);
            t2 = vsg::dvec3(t2.x, -t2.y, -t2.z);
            return;
        }
        frisvadBasis(n, t1, t2);
    }

    /// The sun's 16-entry Poisson disk (files/shaders/lib/view/shadow.slang, `poissonDisk`).
    inline vsg::dvec2 pcfTap(int index)
    {
        static const std::array<vsg::dvec2, maxTaps> taps
            = { vsg::dvec2(-0.94201624, -0.39906216), vsg::dvec2(0.94558609, -0.76890725),
                  vsg::dvec2(-0.094184101, -0.92938870), vsg::dvec2(0.34495938, 0.29387760),
                  vsg::dvec2(-0.91588581, 0.45771432), vsg::dvec2(-0.81544232, -0.87912464),
                  vsg::dvec2(-0.38277543, 0.27676845), vsg::dvec2(0.97484398, 0.75648379),
                  vsg::dvec2(0.44323325, -0.97511554), vsg::dvec2(0.53742981, -0.47373420),
                  vsg::dvec2(-0.26496911, -0.41893023), vsg::dvec2(0.79197514, 0.19090188),
                  vsg::dvec2(-0.24188840, 0.99706507), vsg::dvec2(-0.81409955, 0.91437590),
                  vsg::dvec2(0.19984126, 0.78641367), vsg::dvec2(0.14383161, -0.14100790) };
        return taps[static_cast<std::size_t>(std::clamp(index, 0, maxTaps - 1))];
    }

    /// The view matrix of a face: vsg::LookAt(eye = the light, centre = eye + forward, up).
    inline vsg::dmat4 faceViewMatrix(int face, const vsg::dvec3& lightPosition)
    {
        const FaceAxes axes = faceAxes(face);
        return vsg::lookAt(lightPosition, lightPosition + axes.forward, axes.up);
    }

    /// The projection of a face: clip.x = x / tanHalf, clip.y = y / tanHalf, w = -z_view and a reversed finite depth,
    /// clip.z / w = depthValue(far, distance).
    inline vsg::dmat4 faceProjectionMatrix(double tanHalf, double far)
    {
        vsg::dmat4 m; // column major: m[column][row]
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
                m[c][r] = 0.0;
        }
        m[0][0] = 1.0 / tanHalf;
        m[1][1] = 1.0 / tanHalf;
        m[2][2] = nearPlane / (far - nearPlane);
        m[3][2] = nearPlane * far / (far - nearPlane);
        m[2][3] = -1.0;
        return m;
    }

    /// The CPU reference of the receiver: the lit fraction (0 shadowed .. 1 lit), the mean over `taps` of "compareDepth
    /// is greater than stored(face, u, v)" (compare op GREATER, reversed depth). `stored` is any callable
    /// (int face, double u, double v) -> double.
    template <class Sampler>
    double visibility(const vsg::dvec3& toReceiver, const vsg::dvec3& unitNormal, double far, double tanHalf,
        double resolution, const Bias& bias, int taps, double pcfRadiusTexels, Sampler&& stored)
    {
        const int count = std::clamp(taps, 1, maxTaps);
        const double d0 = project(toReceiver, tanHalf).distance;
        const vsg::dvec3 p = offsetReceiver(toReceiver, unitNormal, tanHalf, resolution, bias);
        const double radius = pcfRadiusTexels * texelWorldSize(d0, tanHalf, resolution);
        vsg::dvec3 t1, t2;
        tangentBasis(unitNormal, t1, t2);
        int lit = 0;
        for (int i = 0; i < count; ++i)
        {
            const vsg::dvec2 o = pcfTap(i);
            const vsg::dvec3 q = p + t1 * (o.x * radius) + t2 * (o.y * radius);
            const FaceCoord c = project(q, tanHalf);
            // Beyond the face's far plane nothing was drawn (a light whose reach is larger than its range): lit.
            if (c.distance >= far
                || compareDepth(far, c.distance, tanHalf, resolution, bias) > stored(c.face, c.u, c.v))
                ++lit;
        }
        return static_cast<double>(lit) / count;
    }
}

#endif
