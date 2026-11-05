#ifndef OPENEDEN_RENDER_CLOUDCURVEMATH_H
#define OPENEDEN_RENDER_CLOUDCURVEMATH_H

#include <algorithm>
#include <array>
#include <cmath>

#include "cloudvolumemath.hpp"

/*
 * OpenEden (task 0042, follow-up of 2026-11-05): the pure maths of the curved cloud layer, the far fade and the large
 * scale variety of the volumetric clouds. No VSG or game types, no device. `files/shaders/comp/cloud/cloud.slang`
 * repeats these formulas (a test reads its constants back): change them together.
 *
 * The flat layer of `layerSpan` is hit only within about 0.7 degrees of the horizon at its 120 km cap, so low rays see
 * no cloud. Here the layer is a shell between two spheres of radius R + base and R + top, with the planet's centre
 * straight below the camera (the game world stays flat; only the cloud layer curves), so a ray just above the horizon
 * meets it at about sqrt(2 R h) (140 km for the base at R = 6371 km). Everything is in metres.
 */
namespace Render::CloudVolume
{
    // The default planet radius (a setting, in kilometres), and the smallest the shader accepts: below it the planet
    // is no planet (the layer is a few kilometres thick) and the layer stays flat.
    constexpr float defaultPlanetRadiusKm = 6371.f;
    constexpr float minPlanetRadiusMeters = 100000.f;

    // The farthest a ray is followed in the curved layer (metres): sqrt(2 R top) is 226 km for the real Earth, so every
    // ray of the shell ends before it. A bigger planet is cut here and fades out (`farFade`).
    constexpr float curvedMaxRayMeters = 300000.f;

    // The share of the cap where the fade begins.
    constexpr float farFadeStart = 0.6f;

    // The wind is wrapped by this tile when the variety is on (a whole number of the shape, detail and fine detail
    // tiles, so the clouds do not jump when the offset wraps): 7 shape tiles. The low frequency noise the variety reads
    // is one tile of the shape texture's noise over this length. The pattern still repeats at this length, which is
    // seven times the old one and shows two or three times across the curved layer's reach, hazed.
    constexpr float largeTileMeters = 84000.f;

    // The variety: the shape lookup's position is moved by up to this much by a large scale noise (so a 12 km tile's
    // features are not the same everywhere), and the coverage moves by up to this amount (clear patches and thick
    // ones, about 20 km across).
    constexpr float varietyWarpMeters = 6000.f;
    constexpr float varietyCoverageAmount = 0.2f;

    // How far the volume's cloud dome is lowered in horizon mode, in the dome model's units (cloudmath.hpp: the rim
    // ring is at height 100 and radius 1000, 5.7 degrees up; lowered by this its rim is 2 degrees below the horizon and
    // the dome covers everything above).
    constexpr float domeHorizonDrop = 135.f;

    namespace Detail
    {
        inline bool allFinite(float a, float b, float c, float d, float e, float f)
        {
            return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d) && std::isfinite(e)
                && std::isfinite(f);
        }

        // The positive distance along an upward ray from height `z0` to the sphere `h` above the surface, for z0 < h.
        // With b = (R + z0) dirZ and c = (z0 - h)(2R + z0 + h) (the difference of the heights, so that no two numbers
        // of the size R squared are subtracted) the roots of t^2 + 2 b t + c are q and c / q with q = -(b + sqrt(b^2 -
        // c)).
        inline float rootToHeight(float z0, float dirZ, float radius, float h)
        {
            const float b = (radius + z0) * dirZ;
            const float c = (z0 - h) * (2.f * radius + z0 + h);
            const float q = -(b + std::sqrt(b * b - c));
            return c / q;
        }

        inline float unitOrHalf(float v)
        {
            return std::isfinite(v) ? saturate(v) : 0.5f;
        }
    }

    // Where a ray from `cameraHeight` (metres above the surface) with the vertical component `dirZ` of its unit
    // direction crosses the shell between `base` and `top` (heights above the surface), for a planet of `radius`.
    // Only rays that rise (`dirZ` > 0) are followed, from below the layer or inside it; a camera above the layer, a
    // level or falling ray, a planet that is not a number or not positive, or any input that is not finite is a miss.
    // `maxDistance` caps the exit and an entry beyond it is a miss.
    inline Span curvedLayerSpan(float cameraHeight, float dirZ, float radius, float base, float top, float maxDistance)
    {
        Span span;
        if (!Detail::allFinite(cameraHeight, dirZ, radius, base, top, 0.f) || std::isnan(maxDistance))
            return span;
        if (radius <= 0.f || top <= base || dirZ <= 0.f || cameraHeight >= top)
            return span;
        const float cap = std::max(maxDistance, 0.f);
        span.enter = cameraHeight >= base ? 0.f : Detail::rootToHeight(cameraHeight, dirZ, radius, base);
        span.exit = std::min(Detail::rootToHeight(cameraHeight, dirZ, radius, top), cap);
        span.hit = std::isfinite(span.enter) && std::isfinite(span.exit) && span.exit > span.enter;
        if (!span.hit)
            span = Span{};
        return span;
    }

    // The height above the surface of the point at the offset (`relX`, `relY`) horizontally and `relZ` up from the
    // camera, which is `cameraHeight` up: sqrt(d^2 + (R + z)^2) - R written as (d^2 + z (2R + z)) / (r + R), exact
    // where the plain difference would lose every digit. A planet that is not positive and finite leaves the flat
    // height.
    inline float radialHeight(float relX, float relY, float cameraHeight, float relZ, float radius)
    {
        const float z = cameraHeight + relZ;
        const float flat = std::isfinite(z) ? z : 0.f;
        if (!std::isfinite(radius) || radius <= 0.f || !std::isfinite(relX) || !std::isfinite(relY)
            || !std::isfinite(z))
            return flat;
        const float d2 = relX * relX + relY * relY;
        const float r = std::sqrt(d2 + (radius + z) * (radius + z));
        return (d2 + z * (2.f * radius + z)) / (r + radius);
    }

    // The share of a cloud kept at `distance` when the march is cut at `cap`: 1 up to `farFadeStart` of the cap, then a
    // smooth fall to 0 at the cap. A bad number gives 0.
    inline float farFade(float distance, float cap)
    {
        if (!std::isfinite(distance) || !std::isfinite(cap) || cap <= 0.f)
            return 0.f;
        const float start = cap * farFadeStart;
        if (distance <= start)
            return 1.f;
        return 1.f - smoothstep(start, cap, distance);
    }

    // The sideways move of the shape lookup, in metres, from two values of the large scale noise (0 to 1, 0.5 = none).
    inline std::array<float, 2> varietyWarp(float noiseX, float noiseY)
    {
        return { (Detail::unitOrHalf(noiseX) - 0.5f) * 2.f * varietyWarpMeters,
            (Detail::unitOrHalf(noiseY) - 0.5f) * 2.f * varietyWarpMeters };
    }

    // The coverage moved by the large scale noise (0 to 1, 0.5 = none), kept in 0 to 1. A coverage that is not a number
    // gives 0.
    inline float varietyCoverage(float coverage, float noise)
    {
        if (!std::isfinite(coverage))
            return 0.f;
        return saturate(coverage + (Detail::unitOrHalf(noise) - 0.5f) * 2.f * varietyCoverageAmount);
    }
}

#endif
