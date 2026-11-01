#ifndef OPENEDEN_RENDER_PRECIPITATIONMATH_H
#define OPENEDEN_RENDER_PRECIPITATIONMATH_H

// OpenEden (task 0040, stage b): the light of rain and snow. The shader twin is files/shaders/lib/light/precipitation.glsl
// (rain, per pixel); the snow's colour is worked out here once a frame (`snowColour`). No game types.
//
// In plain words. A raindrop or a flake is a tiny thing that scatters the light that falls on it towards the viewer.
// So a streak is lit by what is around it, with no "surface" for the light to fall on:
//  - the ambient light of the scene lights it from every side;
//  - the sun lights it most when the viewer looks towards the sun (the drops are then backlit, as rain looks against a
//    low sun) and least when the sun is behind the viewer: the Henyey-Greenstein phase function again, with a softer
//    lobe than the haze's;
//  - every lamp or torch nearby lights it, whichever side the lamp is on.
// Far streaks fade out so they melt into the fog, and fast rain is drawn longer so that it reads as streaks.

#include <algorithm>
#include <cmath>

#include <vsg/maths/vec3.h>

#include <components/render/atmospheremath.hpp>

namespace Render::Precipitation
{
    // The share of the ambient light that reaches a streak, and of the lamps' light. A drop catches little of either;
    // chosen by eye so that rain is clearly visible against a bright sky and nearly gone in the dark.
    constexpr float ambientShare = 0.9f;
    constexpr float pointShare = 0.6f;
    // The sun's: times the phase relative to an even spread (1 when the viewer has no preference of direction). Rain
    // drops scatter forward strongly (a rainbow is the same effect); g = 0.5 is a soft version.
    constexpr float sunShare = 0.45f;
    constexpr float sunAnisotropy = 0.5f;
    // Streaks fade out between these distances from the eye, in game units. The weather's rain box is 600 units across
    // (Weather_*_Rain_Diameter), so drops at its edge are 300 away and at its corners 424.
    constexpr float fadeStart = 200.f;
    constexpr float fadeEnd = 380.f;
    // A streak is drawn this much longer at this rain speed (the one global rain speed of weather.cpp, so in practice a constant stretch), and in proportion below it. Chosen by
    // eye; the stretch is the lengthening of the streak along its fall.
    constexpr float maxStretch = 0.6f;
    constexpr float stretchSpeed = 800.f;
    // The share of the lit colour the snow takes from the sun's phase, which for flakes is flatter than for rain
    // (flakes are big and scatter in all directions). Mixed with 1 (even) by this amount.
    constexpr float snowPhaseShare = 0.35f;

    inline float saturate(float v)
    {
        return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f;
    }

    // The sun's phase for a streak: `cosTheta` is the cosine of the angle between the direction from the eye to the
    // streak and the direction to the sun (1 when the viewer looks at the sun).
    inline float sunPhase(float cosTheta)
    {
        return 4.f * Atmosphere::pi * Atmosphere::henyeyGreenstein(cosTheta, sunAnisotropy);
    }

    // The light on a streak, from the scene's ambient light, its sun colour, the cosine to the sun, and the lamps' summed
    // light at the streak (all in the same lighting space, linear in the HDR view). Never negative.
    inline vsg::vec3 lightColour(
        const vsg::vec3& ambient, const vsg::vec3& sun, float cosTheta, const vsg::vec3& points)
    {
        const float phase = sunShare * sunPhase(cosTheta);
        const vsg::vec3 total = ambient * ambientShare + sun * phase + points * pointShare;
        return vsg::vec3(std::max(total.x, 0.f), std::max(total.y, 0.f), std::max(total.z, 0.f));
    }

    // 1 up to `fadeStart`, 0 from `fadeEnd`, smooth between. A bad distance is 1 (drawn).
    inline float distanceFade(float distance)
    {
        if (!(distance == distance))
            return 1.f;
        const float t = saturate((distance - fadeStart) / (fadeEnd - fadeStart));
        return 1.f - t * t * (3.f - 2.f * t);
    }

    // The factor the streak's length is multiplied by: 1 at rest, 1 + maxStretch at `stretchSpeed` or faster.
    inline float streakStretch(float speed)
    {
        return 1.f + maxStretch * saturate(speed / stretchSpeed);
    }

    // Snow, one colour for the whole fall: the part of the flakes' light that comes from the sun, with the phase
    // flattened for flakes. The ambient part is the material's own (the shader multiplies the scene's ambient light by
    // the material's ambient colour), so it is not included; the effect replaces the NIF's constant emissive glow (0.3
    // on every flake, which lit them at night) with this. The flakes' own colour (white) multiplies it. Lamps are not
    // included (the snow is a single material for the whole effect; the lamps' light is rain's only). `cosTheta` is the
    // cosine between the camera's direction and the direction to the sun. Linear light in, linear light out.
    inline vsg::vec3 snowSunLight(const vsg::vec3& sun, float cosTheta)
    {
        const float phase = 1.f + snowPhaseShare * (sunPhase(cosTheta) - 1.f);
        const float scale = sunShare * std::max(phase, 0.f);
        return vsg::vec3(std::max(sun.x * scale, 0.f), std::max(sun.y * scale, 0.f), std::max(sun.z * scale, 0.f));
    }
}

#endif
