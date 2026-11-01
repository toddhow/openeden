#ifndef OPENEDEN_RENDER_PROBEGPU_H
#define OPENEDEN_RENDER_PROBEGPU_H

// OpenEden (task 0028): how a probe volume is laid out in the buffer that lib/view/gi.glsl reads (std430, an array
// of vec4). Change both together.
//   [0] = (origin.xyz, spacing)
//   [1] = (size.x, size.y, size.z, enabled): the sizes as float numbers; enabled is 1 when a volume is loaded
//   [2] = (normalBiasFraction, strength, 0, 0)
//   [3] = (bakedAmbient.xyz, 0)
//   probe i, coefficient k: [4 + 9 * i + k] = (r, g, b, 0)

#include <cstddef>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include "probes.hpp"

namespace Render::Gi
{
    constexpr std::size_t headerVec4s = 4;
    constexpr std::size_t vec4sPerProbe = shCoefficients;

    // The number of vec4 a volume of `probes` probes takes.
    constexpr std::size_t packedSize(std::size_t probes)
    {
        return headerVec4s + vec4sPerProbe * probes;
    }

    // Header and probes of `volume`; the result has packedSize(volume.probes.size()) entries.
    inline std::vector<vsg::vec4> packVolume(const Volume& volume, const vsg::vec3& bakedAmbient, float strength)
    {
        std::vector<vsg::vec4> out;
        out.reserve(packedSize(volume.probes.size()));
        out.emplace_back(volume.origin.x, volume.origin.y, volume.origin.z, volume.spacing);
        out.emplace_back(static_cast<float>(volume.size[0]), static_cast<float>(volume.size[1]),
            static_cast<float>(volume.size[2]), 1.f);
        out.emplace_back(normalBiasFraction, strength, 0.f, 0.f);
        out.emplace_back(bakedAmbient.x, bakedAmbient.y, bakedAmbient.z, 0.f);
        for (const Sh9& sh : volume.probes)
            for (const vsg::vec3& c : sh.c)
                out.emplace_back(c.x, c.y, c.z, 0.f);
        return out;
    }
}

#endif
