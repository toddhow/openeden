#ifndef OPENEDEN_RENDER_SKYENVMATH_H
#define OPENEDEN_RENDER_SKYENVMATH_H

// OpenEden (task 0036, stage d): the sky environment image. A small octahedral map (the format of a reflection probe,
// reflectionmath.hpp) of the colour of the sky in every direction, built from the atmosphere model's sky and horizon
// colours with no bake at all. Exterior surfaces reflect it through the reflection probe buffer, so wet stone and
// metal armour pick up the sky outdoors. No game types.

#include <algorithm>
#include <cmath>

#include <vsg/maths/vec3.h>

#include "reflectionprobes.hpp"
#include "skyvismath.hpp"

namespace Render::Gi
{
    // The map's width in texels (a sky is smooth).
    constexpr std::uint32_t skyEnvironmentResolution = 32;
    // The sky counts as changed when any channel of the sky or horizon colour moved by more than this (linear light).
    constexpr float skyEnvironmentTolerance = 0.004f;

    // The sky's colour in a direction (any length): the horizon's at the horizon, the zenith's straight up, the ground's
    // below the horizon (the same sky as projectSky). A zero direction gives the horizon colour.
    inline vsg::vec3 skyRadiance(
        const vsg::vec3& zenith, const vsg::vec3& horizon, const vsg::vec3& ground, const vsg::vec3& dir)
    {
        const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (!(length > 1e-20f))
            return horizon;
        const float z = dir.z / length;
        return z >= 0.f ? horizon + (zenith - horizon) * z : ground;
    }

    // The probe (at the origin; the sky is infinitely far) of the sky, `resolution` texels wide.
    inline ReflectionProbe makeSkyEnvironmentProbe(const vsg::vec3& zenith, const vsg::vec3& horizon,
        const vsg::vec3& ground, std::uint32_t resolution = skyEnvironmentResolution)
    {
        ReflectionProbe probe;
        probe.texels.reserve(static_cast<std::size_t>(resolution) * resolution);
        for (std::uint32_t y = 0; y < resolution; ++y)
            for (std::uint32_t x = 0; x < resolution; ++x)
                probe.texels.push_back(
                    skyRadiance(zenith, horizon, ground, reflectionTexelDirection(resolution, x, y)));
        return probe;
    }

    inline bool skyEnvironmentChanged(const vsg::vec3& oldZenith, const vsg::vec3& oldHorizon,
        const vsg::vec3& newZenith, const vsg::vec3& newHorizon, float tolerance = skyEnvironmentTolerance)
    {
        const auto moved = [tolerance](const vsg::vec3& a, const vsg::vec3& b) {
            return std::abs(a.x - b.x) > tolerance || std::abs(a.y - b.y) > tolerance
                || std::abs(a.z - b.z) > tolerance;
        };
        return moved(oldZenith, newZenith) || moved(oldHorizon, newHorizon);
    }
}

#endif
