#ifndef OPENEDEN_RENDER_REFLECTIONMATH_H
#define OPENEDEN_RENDER_REFLECTIONMATH_H

// OpenEden (task 0028, stage d): the maths of reflection probes. A reflection probe is a small picture of what a room
// looks like from one point, stored as an octahedral map: the whole sphere of directions unfolded into one square.
// Rougher surfaces read a blurrier copy, so the map is stored with its blurrier copies (a "chain": the full map, then
// half size, quarter size and so on down to 4 by 4 texels). The GLSL twin is files/shaders/lib/view/reflection.glsl;
// change both together. No game types.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace Render::Gi
{
    // A room gets at most this many reflection probes (the GPU buffer's capacity and the parser's limit).
    constexpr std::size_t maxReflectionProbes = 16;
    constexpr std::uint32_t defaultReflectionResolution = 32;
    constexpr std::uint32_t minReflectionResolution = 8;
    constexpr std::uint32_t maxReflectionResolution = 64;
    // The smallest copy in a chain.
    constexpr std::uint32_t minReflectionLevelSize = 4;

    // Octahedral mapping (Cigolle, Donow, Evangelakos, Mara, McGuire, Meyer, "A Survey of Efficient Representations
    // for Independent Unit Vectors", JCGT 2014). The sign of zero counts as positive.
    inline float octSign(float v)
    {
        return v >= 0.f ? 1.f : -1.f;
    }

    // `dir` need not be unit length (a zero vector maps like +Z). The result is in [0, 1] on both axes.
    inline vsg::vec2 octEncode(const vsg::vec3& dir)
    {
        const float sum = std::abs(dir.x) + std::abs(dir.y) + std::abs(dir.z);
        if (!(sum > 0.f))
            return vsg::vec2(0.5f, 0.5f);
        float x = dir.x / sum;
        float y = dir.y / sum;
        if (dir.z < 0.f)
        {
            const float ox = (1.f - std::abs(y)) * octSign(x);
            const float oy = (1.f - std::abs(x)) * octSign(y);
            x = ox;
            y = oy;
        }
        return vsg::vec2(x * 0.5f + 0.5f, y * 0.5f + 0.5f);
    }

    // The unit direction of a point of the map.
    inline vsg::vec3 octDecode(const vsg::vec2& uv)
    {
        const float fx = uv.x * 2.f - 1.f;
        const float fy = uv.y * 2.f - 1.f;
        float x = fx;
        float y = fy;
        const float z = 1.f - std::abs(fx) - std::abs(fy);
        if (z < 0.f)
        {
            x = (1.f - std::abs(fy)) * octSign(fx);
            y = (1.f - std::abs(fx)) * octSign(fy);
        }
        const float length = std::sqrt(x * x + y * y + z * z);
        return vsg::vec3(x / length, y / length, z / length);
    }

    inline bool validReflectionResolution(std::uint32_t resolution)
    {
        return resolution == 8 || resolution == 16 || resolution == 32 || resolution == 64;
    }

    // Copies in a chain of a map `resolution` texels wide: 32 gives 32, 16, 8 and 4, so 4 copies.
    constexpr std::uint32_t reflectionLevels(std::uint32_t resolution)
    {
        std::uint32_t levels = 1;
        while ((resolution >> levels) >= minReflectionLevelSize)
            ++levels;
        return levels;
    }

    // The number of texels before copy `level` in a chain (the copies are stored one after the other).
    constexpr std::size_t reflectionLevelOffset(std::uint32_t resolution, std::uint32_t level)
    {
        std::size_t offset = 0;
        for (std::uint32_t l = 0; l < level; ++l)
        {
            const std::size_t size = resolution >> l;
            offset += size * size;
        }
        return offset;
    }

    constexpr std::size_t reflectionChainTexels(std::uint32_t resolution)
    {
        return reflectionLevelOffset(resolution, reflectionLevels(resolution));
    }

    // The direction texel (x, y) of a map stands for (the texel's centre).
    inline vsg::vec3 reflectionTexelDirection(std::uint32_t resolution, std::uint32_t x, std::uint32_t y)
    {
        const float r = static_cast<float>(resolution);
        return octDecode(vsg::vec2((static_cast<float>(x) + 0.5f) / r, (static_cast<float>(y) + 0.5f) / r));
    }

    // The full chain from the base map (resolution * resolution texels, row by row). Each smaller copy is the average
    // of 2 by 2 texels of the one before: a box blur, a stand-in for a proper filter by roughness. Precondition: the
    // resolution is valid and base has resolution * resolution texels.
    inline std::vector<vsg::vec3> buildReflectionChain(const std::vector<vsg::vec3>& base, std::uint32_t resolution)
    {
        std::vector<vsg::vec3> chain;
        chain.reserve(reflectionChainTexels(resolution));
        chain.insert(chain.end(), base.begin(), base.end());
        const std::uint32_t levels = reflectionLevels(resolution);
        for (std::uint32_t l = 1; l < levels; ++l)
        {
            const std::uint32_t size = resolution >> l;
            const std::uint32_t parent = size * 2;
            const std::size_t parentOffset = reflectionLevelOffset(resolution, l - 1);
            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const auto at = [&](std::uint32_t px, std::uint32_t py) {
                        return chain[parentOffset + static_cast<std::size_t>(py) * parent + px];
                    };
                    const vsg::vec3 sum
                        = at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1);
                    chain.push_back(sum * 0.25f);
                }
        }
        return chain;
    }

    // The copy to read for a surface of this perceptual roughness: 0 is the sharp map, 1 the smallest copy.
    inline float reflectionLod(float roughness, std::uint32_t levels)
    {
        return std::clamp(roughness, 0.f, 1.f) * static_cast<float>(levels - 1);
    }

    // Bilinear read of one copy at `uv`; reads outside the square are clamped to its edge. Known limit: this does not
    // mirror across the octahedron's fold, so a faint seam shows along the square's edges (the GLSL twin does the same).
    inline vsg::vec3 sampleReflectionLevel(const vsg::vec3* level, std::uint32_t size, const vsg::vec2& uv)
    {
        const float maxIndex = static_cast<float>(size - 1);
        const float fx = std::clamp(uv.x * static_cast<float>(size) - 0.5f, 0.f, maxIndex);
        const float fy = std::clamp(uv.y * static_cast<float>(size) - 0.5f, 0.f, maxIndex);
        const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
        const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
        const std::uint32_t x1 = std::min(x0 + 1, size - 1);
        const std::uint32_t y1 = std::min(y0 + 1, size - 1);
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);
        const vsg::vec3 top = level[y0 * size + x0] * (1.f - tx) + level[y0 * size + x1] * tx;
        const vsg::vec3 bottom = level[y1 * size + x0] * (1.f - tx) + level[y1 * size + x1] * tx;
        return top * (1.f - ty) + bottom * ty;
    }

    // The light a surface of this roughness sees in direction `dir`: two neighbouring copies of the chain, each read
    // bilinearly, then blended.
    inline vsg::vec3 sampleReflectionChain(
        const vsg::vec3* chain, std::uint32_t resolution, const vsg::vec3& dir, float roughness)
    {
        const std::uint32_t levels = reflectionLevels(resolution);
        const float lod = reflectionLod(roughness, levels);
        const std::uint32_t l0 = std::min(static_cast<std::uint32_t>(lod), levels - 1);
        const std::uint32_t l1 = std::min(l0 + 1, levels - 1);
        const float t = lod - static_cast<float>(l0);
        const vsg::vec2 uv = octEncode(dir);
        const vsg::vec3 a = sampleReflectionLevel(chain + reflectionLevelOffset(resolution, l0), resolution >> l0, uv);
        if (l1 == l0 || t <= 0.f)
            return a;
        const vsg::vec3 b = sampleReflectionLevel(chain + reflectionLevelOffset(resolution, l1), resolution >> l1, uv);
        return a * (1.f - t) + b * t;
    }

    // The split-sum environment BRDF in the analytic form of Karis, "Physically Based Shading on Mobile" (2014): a
    // surface of specular colour f0 reflects an environment by f0 * x + y. Both are clamped to be at least zero (the
    // fit dips slightly below at the rough end).
    inline vsg::vec2 envBrdfScaleBias(float roughness, float nDotV)
    {
        const float r0 = roughness * -1.f + 1.f;
        const float r1 = roughness * -0.0275f + 0.0425f;
        const float r2 = roughness * -0.572f + 1.04f;
        const float r3 = roughness * 0.022f + -0.04f;
        const float a004 = std::min(r0 * r0, std::exp2(-9.28f * nDotV)) * r0 + r1;
        const float x = -1.04f * a004 + r2;
        const float y = 1.04f * a004 + r3;
        return vsg::vec2(std::max(x, 0.f), std::max(y, 0.f));
    }

    // Which reflection probes a point reads, and how much of each. Two probes, weighted by how much nearer they are
    // than the third nearest, so that a probe leaving the pair has weight zero (no pop). One probe: weight 1. Two:
    // weights in proportion to the other one's distance. No probes: count 0.
    struct ReflectionBlend
    {
        std::uint32_t count = 0;
        std::array<std::uint32_t, 2> index{};
        std::array<float, 2> weight{};
    };

    inline ReflectionBlend reflectionBlend(const vsg::vec3* positions, std::uint32_t count, const vsg::vec3& point)
    {
        ReflectionBlend result;
        if (count == 0)
            return result;
        // The three nearest by distance, ties to the lower index.
        std::array<float, 3> distance{ 1e30f, 1e30f, 1e30f };
        std::array<std::uint32_t, 3> nearest{ 0, 0, 0 };
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const vsg::vec3 d = positions[i] - point;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            int slot = 3;
            while (slot > 0 && dist < distance[slot - 1])
                --slot;
            if (slot >= 3)
                continue;
            for (int s = 2; s > slot; --s)
            {
                distance[s] = distance[s - 1];
                nearest[s] = nearest[s - 1];
            }
            distance[slot] = dist;
            nearest[slot] = i;
        }
        result.index[0] = nearest[0];
        if (count == 1)
        {
            result.count = 1;
            result.weight[0] = 1.f;
            return result;
        }
        result.count = 2;
        result.index[1] = nearest[1];
        float w0, w1;
        if (count == 2)
        {
            w0 = distance[1];
            w1 = distance[0];
        }
        else
        {
            w0 = std::max(0.f, 1.f - distance[0] / distance[2]);
            w1 = std::max(0.f, 1.f - distance[1] / distance[2]);
        }
        const float total = w0 + w1;
        if (!(total > 1e-6f))
        {
            w0 = 1.f;
            w1 = 0.f;
        }
        else
        {
            w0 /= total;
            w1 /= total;
        }
        result.weight = { w0, w1 };
        return result;
    }
}

#endif
