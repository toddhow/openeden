#ifndef OPENEDEN_RENDER_WETNESSMATH_H
#define OPENEDEN_RENDER_WETNESSMATH_H

// OpenEden (task 0037): the maths of rain wetness and puddles. The shader twin is
// files/shaders/lib/material/wetness.glsl; change both together. No game types.
//
// A wetness value (0 dry to 1 soaked) rises while the weather rains and falls slowly after (`step`). Where it is
// applied a wet surface is darker (`darkening`), glossier (`roughness`), and flat ground collects puddles
// (`puddleMask`) that mirror the sky with ripples while it rains (`ripple`, `waterFresnel`). A place under cover stays
// dry (`exposure`). Everything here is a pure function, so it is tested without a device.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <vsg/maths/vec2.h>

namespace Render::Wetness
{
    // How long, in seconds of game time, a surface takes to go from dry to soaked while it rains hard (the spec's D1,
    // "about 20 seconds"). The rain's own fade-in (the weather's precipitation alpha) is the slower part in practice.
    constexpr float riseSeconds = 20.f;
    // The default of `wetness dry time`, in seconds of game time: a quarter of an hour. By eye: stone and packed earth
    // stay dark for a while after a shower and are plainly dry within about this long; no source.
    constexpr float defaultDrySeconds = 900.f;

    // The most a fully wet, fully rough surface darkens (its diffuse colour times 1 minus this). Water in the pores of
    // a porous surface absorbs light; by eye (real measurements of soil and stone show a darkening of 20 to 50 %).
    constexpr float maxDarkening = 0.45f;
    // The share of that darkening a perfectly smooth (roughness 0) surface still gets; polished things darken less
    // than dirt and plaster.
    constexpr float smoothDarkeningShare = 0.4f;
    // The roughness a surface under a thin water film tends to. Water films are smooth, but not mirrors: wet stone and
    // earth show soft highlights.
    constexpr float wetRoughness = 0.18f;
    // The roughness of a puddle (nearly a mirror) and the extra darkening of the ground under it.
    constexpr float puddleRoughness = 0.03f;
    constexpr float puddleDarkening = 0.7f;
    // Fresnel reflectance of water at normal incidence: (n - 1)^2 / (n + 1)^2 with n = 1.333.
    constexpr float waterF0 = 0.02f;
    // The share of the sky a puddle shows, on top of the Fresnel term: the sky seen in water over dark ground is dimmer
    // than the sky itself (the film absorbs a little and the horizon is brightened by haze). By eye, no source.
    constexpr float puddleReflection = 0.5f;

    // Where puddles start and how much of flat ground they cover at most.
    constexpr float puddleStartWetness = 0.5f;
    constexpr float puddleMaxCoverage = 0.4f;
    // The softness of a puddle's edge, in noise value.
    constexpr float puddleEdge = 0.06f;
    // Flat means the surface normal's Z (up) component between these: a slope of 8 degrees or less is fully flat, one
    // of 20 degrees or more is not at all.
    constexpr float flatFrom = 0.94f;
    constexpr float flatTo = 0.99f;
    // The size of the biggest puddle patches, in game units (about two metres: Morrowind's unit is about 1.4 cm).
    constexpr float puddleNoiseCell = 120.f;

    // Cover: a place whose sky visibility straight up (the share a flat surface facing up would see, 1 in the open) is
    // below `coverBelow` is fully covered; above `coverAbove` it is fully exposed. The sky visibility field is coarse
    // (256 units between probes), so an eave narrower than that is not resolved.
    constexpr float coverBelow = 0.2f;
    constexpr float coverAbove = 0.6f;

    // Rain ripples on puddles: one ring in each square of this size, growing for `ripplePeriod` seconds. The biggest
    // radius plus the ring's wobble stays inside the square, so neighbouring squares never need each other.
    constexpr float rippleCell = 48.f;
    constexpr float rippleMaxRadius = 16.f;
    constexpr float ripplePeriod = 1.1f;
    constexpr float rippleWidth = 3.f;
    constexpr float rippleWavelength = 6.f;
    // The largest slope a ripple gives the surface normal.
    constexpr float rippleMaxSlope = 0.22f;
    constexpr float twoPi = 6.28318530717959f;

    inline float saturate(float x)
    {
        // A NaN compares false and falls to zero.
        return x > 0.f ? (x < 1.f ? x : 1.f) : 0.f;
    }

    inline float smoothstep(float a, float b, float x)
    {
        const float t = saturate((x - a) / (b - a));
        return t * t * (3.f - 2.f * t);
    }

    // The wetness the weather is heading for: the rain's strength while the weather has a rain effect (rain and
    // thunderstorm), nothing otherwise (snow, ash, clear, cloudy). `precipitationAlpha` is the weather result's own
    // fade of the precipitation as a transition passes the point where the rain starts or stops.
    inline float target(bool hasRainEffect, float precipitationAlpha)
    {
        return hasRainEffect ? saturate(precipitationAlpha) : 0.f;
    }

    // One step of the curve. Rising to the target takes `riseSeconds` from dry to soaked; falling takes `drySeconds`
    // from soaked to dry (a rate, so a half wet surface dries in half the time). The result stays in 0 to 1 and never
    // passes the target. `dt` and `drySeconds` are game time.
    inline float step(float wet, float targetWet, float dt, float drySeconds)
    {
        wet = saturate(wet);
        targetWet = saturate(targetWet);
        if (!(dt > 0.f))
            return wet;
        if (wet < targetWet)
            return std::min(wet + dt / riseSeconds, targetWet);
        if (wet > targetWet)
        {
            if (!(drySeconds > 0.f))
                return targetWet;
            return std::max(wet - dt / drySeconds, targetWet);
        }
        return wet;
    }

    // The factor on the diffuse colour. Rougher (more porous) surfaces darken more.
    inline float darkening(float wet, float surfaceRoughness)
    {
        const float rough = saturate(surfaceRoughness);
        return 1.f - maxDarkening * saturate(wet) * (smoothDarkeningShare + (1.f - smoothDarkeningShare) * rough);
    }

    // The roughness under a water film: towards `wetRoughness`, never rougher than it was.
    inline float roughness(float base, float wet)
    {
        if (base <= wetRoughness)
            return base;
        return base - (base - wetRoughness) * saturate(wet);
    }

    // 1 for a near horizontal surface (`normalZ` is the world normal's Z), 0 for a slope.
    inline float flatness(float normalZ)
    {
        return smoothstep(flatFrom, flatTo, normalZ);
    }

    // The share of flat ground that holds water at this wetness.
    inline float puddleFill(float wet)
    {
        return puddleMaxCoverage * saturate((saturate(wet) - puddleStartWetness) / (1.f - puddleStartWetness));
    }

    // A 32 bit integer hash (Wang/Hash "lowbias32" by Chris Wellons, public domain), the same in the shader.
    inline std::uint32_t hash(std::uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    // A value in [0, 1) for a lattice point.
    inline float latticeValue(std::int32_t ix, std::int32_t iy, std::uint32_t salt)
    {
        const std::uint32_t h
            = hash(static_cast<std::uint32_t>(ix) * 73856093u ^ static_cast<std::uint32_t>(iy) * 19349663u ^ salt);
        return static_cast<float>(h >> 8) * (1.f / 16777216.f);
    }

    // Value noise on the lattice (smooth interpolation), in 0 to 1.
    inline float valueNoise(float x, float y, std::uint32_t salt)
    {
        const float fx = std::floor(x);
        const float fy = std::floor(y);
        const std::int32_t ix = static_cast<std::int32_t>(fx);
        const std::int32_t iy = static_cast<std::int32_t>(fy);
        const float tx = x - fx;
        const float ty = y - fy;
        const float sx = tx * tx * (3.f - 2.f * tx);
        const float sy = ty * ty * (3.f - 2.f * ty);
        const float a = latticeValue(ix, iy, salt);
        const float b = latticeValue(ix + 1, iy, salt);
        const float c = latticeValue(ix, iy + 1, salt);
        const float d = latticeValue(ix + 1, iy + 1, salt);
        return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
    }

    // Two octaves of value noise with its contrast stretched (value noise gathers around one half), in [0, 1). Its
    // coordinates are in lattice cells; `puddleNoise` takes game units.
    inline float noise(float x, float y)
    {
        const float n = 0.65f * valueNoise(x, y, 0x1234u) + 0.35f * valueNoise(x * 2.f + 17.f, y * 2.f + 5.f, 0x5678u);
        const float stretched = (n - 0.5f) * 1.8f + 0.5f;
        return std::min(std::max(stretched, 0.f), 0.999999f);
    }

    inline float puddleNoise(float worldX, float worldY)
    {
        return noise(worldX / puddleNoiseCell, worldY / puddleNoiseCell);
    }

    // The share of the pixel that is puddle: flat ground, wet enough, where the noise is high. As the fill grows the
    // threshold falls, so patches spread from the highest points.
    inline float puddleMask(float wet, float normalZ, float noiseValue)
    {
        const float fill = puddleFill(wet);
        if (!(fill > 0.f))
            return 0.f;
        const float threshold = 1.f - fill;
        return flatness(normalZ) * smoothstep(threshold - puddleEdge, threshold + puddleEdge, noiseValue);
    }

    // How exposed to the rain a place is, from the share of the sky a flat surface facing up sees there
    // (`visibilityUp`, 1 in the open) and the weight of the baked data behind the value (`dataWeight`, 0 where no cell
    // has a file, so that nothing says the place is covered). 0 is fully under cover.
    inline float exposure(float visibilityUp, float dataWeight)
    {
        const float open = smoothstep(coverBelow, coverAbove, visibilityUp);
        const float weight = saturate(dataWeight);
        return 1.f + (open - 1.f) * weight;
    }

    // Schlick's Fresnel for water, `cosTheta` the cosine between the normal and the view.
    inline float waterFresnel(float cosTheta)
    {
        const float t = std::pow(1.f - saturate(cosTheta), 5.f);
        return waterF0 + (1.f - waterF0) * t;
    }

    // The slope (x, y) that rain ripples give a puddle's surface at a world position, `rain` in 0 to 1 the strength of
    // the rain. One expanding ring in each square, each starting at its own moment, fading as it grows.
    inline vsg::vec2 ripple(float worldX, float worldY, float time, float rain)
    {
        const float strength = saturate(rain);
        if (!(strength > 0.f))
            return vsg::vec2(0.f, 0.f);
        const float cx = std::floor(worldX / rippleCell);
        const float cy = std::floor(worldY / rippleCell);
        const std::int32_t ix = static_cast<std::int32_t>(cx);
        const std::int32_t iy = static_cast<std::int32_t>(cy);
        // The ring's centre: near the middle of its square.
        const float jitterX = (latticeValue(ix, iy, 0x9e37u) - 0.5f) * 0.3f;
        const float jitterY = (latticeValue(ix, iy, 0x79b9u) - 0.5f) * 0.3f;
        const float centreX = (cx + 0.5f + jitterX) * rippleCell;
        const float centreY = (cy + 0.5f + jitterY) * rippleCell;
        // Its place in the cycle.
        const float phase = latticeValue(ix, iy, 0x7f4au);
        const float age01 = time / ripplePeriod + phase - std::floor(time / ripplePeriod + phase);
        const float dx = worldX - centreX;
        const float dy = worldY - centreY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float u = dist - age01 * rippleMaxRadius;
        const float envelope = std::exp(-(u * u) / (rippleWidth * rippleWidth));
        const float fade = (1.f - age01) * (1.f - age01);
        const float magnitude = rippleMaxSlope * strength * fade * envelope * std::cos(u * twoPi / rippleWavelength);
        if (!(dist > 1e-4f))
            return vsg::vec2(0.f, 0.f);
        return vsg::vec2(magnitude * dx / dist, magnitude * dy / dist);
    }
}

#endif
