#ifndef OPENEDEN_RENDER_AMBIENTCUBE_H
#define OPENEDEN_RENDER_AMBIENTCUBE_H

// OpenEden (task 0355 stage e): the ambient cube of an interior. Six colours, one per axis direction, light a surface
// by the squares of its normal's components (the ambient cube of the game this was made for: Skyrim SE's XCLL and
// LGTM DALC). The C++ twin of files/shaders/lib/view/ambientcube.glsl and .slang: change all three together.
//
// How it reaches the GPU: it rides at the end of the sky-visibility header buffer (binding VIEW_SKYVIS_SKY_BINDING),
// after the cloud shadow data (see cloudgpu.hpp), so the view descriptor set has no new binding. Eight vec4 from
// `base`:
//   [base + 0]     = (enabled, 0, 0, 0): 0 when the cell has no cube (the shader then returns the flat ambient)
//   [base + 1]     = (baseline.xyz, 0): the flat ambient the cube was made for; what the flat ambient has gained since
//                    (Night Eye) is added to every face, as the baked probes do (giAmbient)
//   [base + 2 + i] = (colour of face i, 0), i = +X, -X, +Y, -Y, +Z, -Z
// All colours are in the view's colour space (linear with HDR). No game types.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include "cloudgpu.hpp"

namespace Render::AmbientCube
{
    using Cube = std::array<vsg::vec3, 6>;

    constexpr std::size_t base = Cloud::headerTotalVec4s;
    constexpr std::size_t vec4s = 8;
    /// The header buffer's size with the cube's entries.
    constexpr std::size_t headerTotalVec4s = base + vec4s;

    inline Cube fromBytes(const std::array<std::array<std::uint8_t, 3>, 6>& bytes)
    {
        Cube out;
        for (std::size_t i = 0; i < 6; ++i)
            out[i] = vsg::vec3(bytes[i][0] / 255.f, bytes[i][1] / 255.f, bytes[i][2] / 255.f);
        return out;
    }

    /// The mean of the six faces: what a surface facing every direction at once sees (the flat colour a uniform sphere
    /// of this cube would have).
    inline vsg::vec3 average(const Cube& cube)
    {
        vsg::vec3 sum(0.f, 0.f, 0.f);
        for (const vsg::vec3& face : cube)
            sum += face;
        return sum * (1.f / 6.f);
    }

    inline float luminance(const vsg::vec3& c)
    {
        return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    }

    /// As MWAnim::brighten on the flat ambient (the interior's minimum brightness), applied to the whole cube by the
    /// luminance of its average: all six faces are scaled together, so their ratios stay. A black cube becomes grey.
    inline Cube brightened(const Cube& cube, float minLuminance)
    {
        const vsg::vec3 mean = average(cube);
        const float lum = luminance(mean);
        if (!(lum < minLuminance))
            return cube;
        Cube out;
        if (mean.x == 0.f && mean.y == 0.f && mean.z == 0.f)
        {
            out.fill(vsg::vec3(minLuminance, minLuminance, minLuminance));
            return out;
        }
        const float factor = minLuminance / lum;
        for (std::size_t i = 0; i < 6; ++i)
            out[i] = cube[i] * factor;
        return out;
    }

    /// The cube's colour for a surface facing `normal` (any length; zero faces up): each axis contributes the face its
    /// component points to, weighed by the component squared.
    inline vsg::vec3 sample(const Cube& cube, const vsg::vec3& normal)
    {
        vsg::vec3 n = normal;
        const float length2 = n.x * n.x + n.y * n.y + n.z * n.z;
        if (length2 > 1e-20f)
            n = n * (1.f / std::sqrt(length2));
        else
            n = vsg::vec3(0.f, 0.f, 1.f);
        const float wx = n.x * n.x, wy = n.y * n.y, wz = n.z * n.z;
        return cube[n.x >= 0.f ? 0 : 1] * wx + cube[n.y >= 0.f ? 2 : 3] * wy + cube[n.z >= 0.f ? 4 : 5] * wz;
    }

    /// What the shader computes: the cube's colour plus what the flat ambient has gained since `baseline`, never below
    /// zero.
    inline vsg::vec3 ambientFor(
        const Cube& cube, const vsg::vec3& normal, const vsg::vec3& flat, const vsg::vec3& baseline)
    {
        const vsg::vec3 c = sample(cube, normal) + (flat - baseline);
        return vsg::vec3(std::max(c.x, 0.f), std::max(c.y, 0.f), std::max(c.z, 0.f));
    }

    /// Writes the cube's entries of `header` (headerTotalVec4s long, from the start of the buffer). A null cube writes
    /// only the "off" flag.
    inline void write(vsg::vec4* header, const Cube* cube, const vsg::vec3& baseline)
    {
        vsg::vec4* at = header + base;
        if (cube == nullptr)
        {
            at[0] = vsg::vec4(0.f, 0.f, 0.f, 0.f);
            return;
        }
        at[0] = vsg::vec4(1.f, 0.f, 0.f, 0.f);
        at[1] = vsg::vec4(baseline.x, baseline.y, baseline.z, 0.f);
        for (std::size_t i = 0; i < 6; ++i)
            at[2 + i] = vsg::vec4((*cube)[i].x, (*cube)[i].y, (*cube)[i].z, 0.f);
    }
}

#endif
