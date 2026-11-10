#ifndef OPENEDEN_RENDER_REFLECTIONGPU_H
#define OPENEDEN_RENDER_REFLECTIONGPU_H

// OpenEden (task 0028, stage d): how a cell's reflection probes are laid out in the buffer that
// lib/view/reflection.glsl reads (binding 11 of the view set, std430, an array of vec4). Change both together.
//   [0] = (probe count, resolution, levels per chain, enabled)
//   [1] = (strength, bakedAmbient.xyz)
//   [2] = (sky environment flag, 0, 0, 0): 1 when the buffer holds the exterior sky image (rendermanager.cpp
//         updateSkyEnvironment sets it after packing), 0 for an interior's baked probes; the rest kept for later
//   [3 + p] = (position.xyz, 0) of probe p, for p below maxReflectionProbes
//   [reflectionTexelsBase + p * chainTexels + i] = (r, g, b, 0): texel i of probe p's chain (reflectionmath.hpp)

#include <cstddef>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include "reflectionmath.hpp"
#include "reflectionprobes.hpp"

namespace Render::Gi
{
    constexpr std::size_t reflectionHeaderVec4s = 3;
    constexpr std::size_t reflectionTexelsBase = reflectionHeaderVec4s + maxReflectionProbes;
    // The size of the buffer the game allocates: room for the most and largest probes.
    constexpr std::size_t reflectionBufferCapacity
        = reflectionTexelsBase + maxReflectionProbes * reflectionChainTexels(maxReflectionResolution);

    constexpr std::size_t reflectionPackedSize(std::size_t probes, std::uint32_t resolution)
    {
        return reflectionTexelsBase + probes * reflectionChainTexels(resolution);
    }

    // Header, positions and chains of `set`; the result has reflectionPackedSize(count, resolution) entries.
    // Precondition: validate(set) is empty.
    inline std::vector<vsg::vec4> packReflections(const ReflectionSet& set, float strength)
    {
        std::vector<vsg::vec4> out;
        out.reserve(reflectionPackedSize(set.probes.size(), set.resolution));
        out.emplace_back(static_cast<float>(set.probes.size()), static_cast<float>(set.resolution),
            static_cast<float>(reflectionLevels(set.resolution)), 1.f);
        out.emplace_back(strength, set.ambient.x, set.ambient.y, set.ambient.z);
        out.emplace_back(0.f, 0.f, 0.f, 0.f);
        for (std::size_t p = 0; p < maxReflectionProbes; ++p)
        {
            if (p < set.probes.size())
                out.emplace_back(set.probes[p].position.x, set.probes[p].position.y, set.probes[p].position.z, 0.f);
            else
                out.emplace_back(0.f, 0.f, 0.f, 0.f);
        }
        for (const ReflectionProbe& probe : set.probes)
            for (const vsg::vec3& t : buildReflectionChain(probe.texels, set.resolution))
                out.emplace_back(t.x, t.y, t.z, 0.f);
        return out;
    }
}

#endif
