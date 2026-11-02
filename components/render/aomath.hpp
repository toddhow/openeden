#ifndef OPENEDEN_RENDER_AOMATH_H
#define OPENEDEN_RENDER_AOMATH_H

// OpenEden (task 0029, stage a): the maths of ambient occlusion, GTAO class. The pass's shader twin is
// files/shaders/lib/ao/gtao.glsl (stage b); change both together. No game types.
//
// GTAO: Jimenez, Wu, Pesce, Jarabo, "Practical Real-Time Strategies for Accurate Indirect Occlusion" (SIGGRAPH 2016
// course notes). The closed form below is their visibility integral for one slice; this is a reimplementation from the
// paper's formula and no code of theirs.

#include <algorithm>
#include <cmath>

#include <vsg/maths/vec3.h>
#include <vsg/vk/vulkan.h>

namespace Render::Ao
{
    constexpr float pi = 3.14159265358979f;
    constexpr float halfPi = pi * 0.5f;

    // The defaults of `ao radius` (game units, about 1.4 m), `ao slices` and `ao steps` (each side of a slice).
    constexpr float defaultRadius = 100.f;
    constexpr int defaultSlices = 2;
    constexpr int defaultSteps = 6;

    // The pass needs its setting, the G-buffer (depth and normals) and the material model's output (the ambient output
    // is the one after it: Render::GBuffer::extraColorFormats).
    inline bool useAo(bool settingOn, bool gbuffer, bool materialOutput)
    {
        return settingOn && gbuffer && materialOutput;
    }

    // Task 0048: the terrain also writes its ambient part when `ao terrain` is on besides everything `useAo` needs.
    inline bool useTerrainAo(bool settingOn, bool terrainSetting, bool gbuffer, bool materialOutput)
    {
        return useAo(settingOn, gbuffer, materialOutput) && terrainSetting;
    }

    // Task 0048: the CPU twin of the terrain shader's ambient part (files/shaders/terrain/main.frag, `AO_TERRAIN`): the
    // texture colour at the lighting multiply times the ambient light the terrain used times the ambient colour term
    // times the scale it used (diffuse scale times the `_rmaos` occlusion with the material model, 1 without), times
    // the fog's share of the colour. It is the ambient summand of materialTermsScaled, so removing (1 - ao) of it in
    // the tonemap takes out exactly that share of the ambient light. A scale below 0 or not a number counts as 0, a
    // fog share is limited to 0 to 1 (not a number counts as 0).
    inline vsg::vec3 terrainAmbientPart(const vsg::vec3& textureColour, const vsg::vec3& ambientLight,
        const vsg::vec3& ambientColour, float scale, float fogShare)
    {
        const float s = scale > 0.f ? scale : 0.f;
        const float f = fogShare > 0.f ? std::min(fogShare, 1.f) : 0.f;
        const float k = s * f;
        return vsg::vec3(textureColour.x * ambientLight.x * ambientColour.x * k,
            textureColour.y * ambientLight.y * ambientColour.y * k,
            textureColour.z * ambientLight.z * ambientColour.z * k);
    }

    // The occlusion image is half the scene's size in each direction, rounded up, and at least one texel.
    inline VkExtent2D halfExtent(const VkExtent2D& extent)
    {
        return VkExtent2D{ std::max(1u, (extent.width + 1) / 2), std::max(1u, (extent.height + 1) / 2) };
    }

    // The visibility of one slice: `n` is the angle of the slice's projected normal from the view vector, `h1` (at most
    // 0) and `h2` (at least 0) the horizon angles on its two sides from the view vector. The horizons are first limited
    // to the hemisphere around the normal. 1 is fully open, 0 fully covered.
    inline float gtaoSliceVisibility(float h1, float h2, float n)
    {
        h1 = n + std::max(h1 - n, -halfPi);
        h2 = n + std::min(h2 - n, halfPi);
        const float sinN = std::sin(n);
        const float cosN = std::cos(n);
        return 0.25f * (-std::cos(2.f * h1 - n) + cosN + 2.f * h1 * sinN)
            + 0.25f * (-std::cos(2.f * h2 - n) + cosN + 2.f * h2 * sinN);
    }

    // The cosine of the horizon a sample makes. `delta` is the sample's offset from the pixel in a frame whose +Z
    // points at the camera. A sample at or beyond `radius` (and a sample at the pixel itself) counts as no occluder,
    // -1. With `falloff` between 0 and 1 a nearer sample is also pulled towards -1 the farther it is, so occluders fade
    // out at the radius instead of ending in a ring.
    inline float horizonCosine(const vsg::vec3& delta, float radius, float falloff)
    {
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (!(length > 0.f) || length >= radius)
            return -1.f;
        const float weight = std::clamp(1.f - length / radius, 0.f, 1.f);
        const float blend = 1.f + (weight - 1.f) * std::clamp(falloff, 0.f, 1.f);
        const float cosine = delta.z / length;
        return -1.f + (cosine + 1.f) * blend;
    }

    // Strength 1 keeps the value, 0 removes all occlusion, more than 1 darkens it further.
    inline float aoCurve(float visibility, float strength)
    {
        // pow(0, 0) is not defined everywhere, and strength 0 means no occlusion at all.
        if (!(strength > 0.f))
            return 1.f;
        return std::pow(std::clamp(visibility, 0.f, 1.f), strength);
    }

    inline float smoothStep(float low, float high, float x)
    {
        const float t = std::clamp((x - low) / (high - low), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // The weight of a neighbour in the spatial filter. A neighbour whose depth differs by more than 5 percent of the
    // pixel's, or whose normal is more than about 45 degrees away (dot below 0.707), is another surface and gets 0;
    // below 2 percent and above a dot of 0.9 it gets 1.
    inline float bilateralWeight(float depthDifference, float depth, float normalDot)
    {
        const float relative = depth > 0.f ? std::abs(depthDifference) / depth : 1.f;
        return (1.f - smoothStep(0.02f, 0.05f, relative)) * smoothStep(0.707f, 0.9f, normalDot);
    }

    // The tonemap's operation: takes the ambient part of a colour out by (1 - ao). With ao 1 it returns the colour.
    inline vsg::vec3 removeOcclusion(const vsg::vec3& colour, const vsg::vec3& ambient, float ao)
    {
        const float remove = 1.f - ao;
        return vsg::vec3(std::max(colour.x - ambient.x * remove, 0.f), std::max(colour.y - ambient.y * remove, 0.f),
            std::max(colour.z - ambient.z * remove, 0.f));
    }
}

#endif
