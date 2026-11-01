#ifndef OPENEDEN_RENDER_PROBESAMPLERMATH_H
#define OPENEDEN_RENDER_PROBESAMPLERMATH_H

// OpenEden (task 0028): reading light probes. Header-only. files/shaders/lib/view/gi.glsl repeats these formulas;
// change both together.

#include <algorithm>
#include <array>
#include <cmath>

#include <vsg/maths/vec3.h>

#include "probes.hpp"

namespace Render::Gi
{
    // The real spherical harmonics basis (bands 0 to 2) of the unit direction; a zero vector gives the basis of +Z.
    inline std::array<float, 9> shBasis(const vsg::vec3& dir)
    {
        float x = dir.x, y = dir.y, z = dir.z;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length > 1e-20f)
        {
            x /= length;
            y /= length;
            z /= length;
        }
        else
        {
            x = 0.f;
            y = 0.f;
            z = 1.f;
        }
        return { 0.282095f, 0.488603f * y, 0.488603f * z, 0.488603f * x, 1.092548f * x * y, 1.092548f * y * z,
            0.315392f * (3.f * z * z - 1.f), 1.092548f * x * z, 0.546274f * (x * x - y * y) };
    }

    // Adds `radiance` arriving from `dir`, weighted (the weights of a whole sphere of samples sum to 4 * pi).
    inline void shAdd(Sh9& sh, const vsg::vec3& dir, const vsg::vec3& radiance, float weight)
    {
        const auto basis = shBasis(dir);
        for (int i = 0; i < shCoefficients; ++i)
            sh.c[i] += radiance * (basis[i] * weight);
    }

    // The cosine-convolved irradiance for `normal`, divided by pi: for a uniform radiance L it is L. Each channel >= 0.
    inline vsg::vec3 shAmbient(const Sh9& sh, const vsg::vec3& normal)
    {
        // The convolution factors pi, 2 pi / 3 and pi / 4 divided by pi.
        constexpr std::array<float, 9> band
            = { 1.f, 2.f / 3.f, 2.f / 3.f, 2.f / 3.f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
        const auto basis = shBasis(normal);
        vsg::vec3 sum(0.f, 0.f, 0.f);
        for (int i = 0; i < shCoefficients; ++i)
            sum += sh.c[i] * (basis[i] * band[i]);
        return vsg::vec3(std::max(sum.x, 0.f), std::max(sum.y, 0.f), std::max(sum.z, 0.f));
    }

    inline Sh9 shLerp(const Sh9& a, const Sh9& b, float t)
    {
        Sh9 r;
        for (int i = 0; i < shCoefficients; ++i)
            r.c[i] = a.c[i] * (1.f - t) + b.c[i] * t;
        return r;
    }

    // Trilinear in the lattice after the normal bias; positions outside the lattice take its nearest probes.
    inline vsg::vec3 sampleAmbient(const Volume& v, const vsg::vec3& worldPos, const vsg::vec3& worldNormal)
    {
        vsg::vec3 n = worldNormal;
        const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        vsg::vec3 p = worldPos;
        if (nl > 1e-20f)
        {
            n = n * (1.f / nl);
            p += n * (normalBiasFraction * v.spacing);
        }
        std::array<std::size_t, 3> i0{}, i1{};
        std::array<float, 3> t{};
        const float coords[3] = { (p.x - v.origin.x) / v.spacing, (p.y - v.origin.y) / v.spacing,
            (p.z - v.origin.z) / v.spacing };
        for (int a = 0; a < 3; ++a)
        {
            const float top = static_cast<float>(v.size[a] - 1);
            const float g = std::clamp(coords[a], 0.f, top);
            const float f = std::floor(g);
            i0[a] = static_cast<std::size_t>(f);
            i1[a] = std::min<std::size_t>(i0[a] + 1, v.size[a] - 1);
            t[a] = g - f;
        }
        const auto at = [&](std::size_t x, std::size_t y, std::size_t z) {
            return shAmbient(v.probes[x + v.size[0] * (y + v.size[1] * z)], n);
        };
        const auto mix = [](const vsg::vec3& a, const vsg::vec3& b, float f) { return a * (1.f - f) + b * f; };
        const vsg::vec3 x00 = mix(at(i0[0], i0[1], i0[2]), at(i1[0], i0[1], i0[2]), t[0]);
        const vsg::vec3 x10 = mix(at(i0[0], i1[1], i0[2]), at(i1[0], i1[1], i0[2]), t[0]);
        const vsg::vec3 x01 = mix(at(i0[0], i0[1], i1[2]), at(i1[0], i0[1], i1[2]), t[0]);
        const vsg::vec3 x11 = mix(at(i0[0], i1[1], i1[2]), at(i1[0], i1[1], i1[2]), t[0]);
        return mix(mix(x00, x10, t[1]), mix(x01, x11, t[1]), t[2]);
    }

    // 1 inside the lattice box, 0 at one spacing outside it (the largest distance along any axis), linear between.
    inline float volumeWeight(const Volume& v, const vsg::vec3& worldPos)
    {
        const float p[3] = { worldPos.x, worldPos.y, worldPos.z };
        const float o[3] = { v.origin.x, v.origin.y, v.origin.z };
        float outside = 0.f;
        for (int a = 0; a < 3; ++a)
        {
            const float lo = o[a];
            const float hi = o[a] + v.spacing * static_cast<float>(v.size[a] - 1);
            outside = std::max({ outside, lo - p[a], p[a] - hi });
        }
        return std::clamp(1.f - outside / v.spacing, 0.f, 1.f);
    }

    // The ambient light at a point: the baked value (scaled by `strength`) plus what has been added to the flat
    // ambient since the bake (night eye, a changed cell), faded into the flat ambient outside the volume.
    inline vsg::vec3 giAmbient(const Volume* v, const vsg::vec3& worldPos, const vsg::vec3& worldNormal,
        const vsg::vec3& flatAmbient, const vsg::vec3& bakedAmbient, float strength)
    {
        if (v == nullptr)
            return flatAmbient;
        const float w = volumeWeight(*v, worldPos);
        const vsg::vec3 offset = flatAmbient - bakedAmbient;
        const vsg::vec3 baked = sampleAmbient(*v, worldPos, worldNormal);
        const vsg::vec3 gi(std::max(baked.x * strength + offset.x, 0.f), std::max(baked.y * strength + offset.y, 0.f),
            std::max(baked.z * strength + offset.z, 0.f));
        return flatAmbient * (1.f - w) + gi * w;
    }
}

#endif
