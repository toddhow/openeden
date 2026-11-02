#ifndef OPENEDEN_RENDER_SUNMATH_H
#define OPENEDEN_RENDER_SUNMATH_H

// OpenEden (task 0034 decision 4, task 0047): maths of the sun's colours. No game types.
//
// tableElevation: Morrowind's colour tables (sky, fog, ambient and sun colours for sunrise, day, sunset and night)
// blend in windows around the sunrise and sunset hours, and show their pure "sunset" colours at the middle of the
// sunset window of the Sky colour table (about 18:30 with the default INI values). The sun that is drawn, though,
// sinks until 20:00 (the night start), so a physical sky driven by the drawn sun's height shows sunset colours an
// hour or more too late. The elevation computed here is the height a sun would have if it crossed the horizon at the
// middle of the two windows, so a model driven by it is at its sunset at the tables' sunset. It is used for colours
// only: the drawn sun, the light direction and the shadows keep following the weather manager's own orbit.

#include <algorithm>
#include <cmath>

namespace Render::Sun
{
    constexpr float pi = 3.14159265358979f;

    // The sun's height at noon in the weather manager's orbit: the orbit is tilted by "approx tan(-15 degrees)"
    // (-0.268, `WeatherManager::update`), so the noon sun is atan(1 / 0.268), about 75 degrees, up.
    constexpr float noonTilt = 0.268f;
    inline const float maxElevation = std::atan2(1.f, noonTilt);

    // The windows of the colour tables, in game hours, as `TimeOfDaySettings` holds them: sunriseStart = mNightEnd,
    // sunriseEnd = mDayStart, sunsetStart = mDayEnd, sunsetEnd = mNightStart.
    struct TimeOfDayWindows
    {
        float sunriseStart = 6.f;
        float sunriseEnd = 8.f;
        float sunsetStart = 18.f;
        float sunsetEnd = 20.f;
    };

    // The elevation in radians for a game hour: 0 at the middle of the sunrise window and of the sunset window, the
    // sine of the elevation shaped as a sine of the hour between them (so it is smooth and has the right slope at the
    // horizon), highest at the middle of the day (maxElevation), and the mirror image below the horizon at night.
    // The hour may be any number (it wraps at 24). A window that makes no sense gives 0.
    inline float tableElevation(float gameHour, const TimeOfDayWindows& w)
    {
        const float rise = 0.5f * (w.sunriseStart + w.sunriseEnd);
        const float set = 0.5f * (w.sunsetStart + w.sunsetEnd);
        if (!std::isfinite(gameHour) || !std::isfinite(rise) || !std::isfinite(set))
            return 0.f;
        const auto wrap = [](float h) {
            h = std::fmod(h, 24.f);
            return h < 0.f ? h + 24.f : h;
        };
        float dayLength = wrap(set - rise);
        if (dayLength < 1e-3f || dayLength > 24.f - 1e-3f)
            return 0.f;
        const float sinceRise = wrap(gameHour - rise);
        const float peak = std::sin(maxElevation);
        if (sinceRise < dayLength)
            return std::asin(std::clamp(peak * std::sin(pi * sinceRise / dayLength), -1.f, 1.f));
        const float nightLength = 24.f - dayLength;
        return -std::asin(std::clamp(peak * std::sin(pi * (sinceRise - dayLength) / nightLength), -1.f, 1.f));
    }
    // ---- The procedural sun disc (task 0047). The shader files/shaders/sky/sun/proceduralsun.slang repeats these
    // constants and formulas; change both together (a test reads the constants back).
    //
    // The disc is drawn on the sun's quad (a camera-facing square, 1 unit wide, scaled by 450 at 1000 units away).
    // `r` below is a pixel's distance from the quad's centre in units of the quad's half width (0 at the centre, 1 at
    // the middle of an edge); `radius` is the disc's radius in the same unit.

    // Limb darkening: the disc is dimmer at its rim than in its middle, because the rim shows cooler, shallower layers
    // of the sun. I(r) = 1 - u (1 - sqrt(1 - r^2)) is the usual one-parameter ("linear") law for it; the value of u
    // (0.6) is chosen by eye, not taken from a measurement.
    constexpr float limbCoefficient = 0.6f;
    // The share of the quad's half width the disc covers at `procedural sun size` 1 (chosen by eye so that the disc is
    // a little smaller than the texture's, and the glow has room), and the most it may grow to.
    constexpr float discRadiusAtSizeOne = 0.18f;
    constexpr float maxDiscRadius = 0.45f;
    constexpr float minDiscRadius = 0.01f;
    // The soft edge's half width in units of the disc radius (the shader adds one pixel's width on top, so that the
    // edge never aliases).
    constexpr float edgeSoftness = 0.04f;
    // The glow's strength relative to the disc's brightness, and where (in `r`) it starts to fade to zero so that the
    // quad's square outline never shows. By eye.
    constexpr float glowGain = 0.12f;
    constexpr float glowWindowStart = 0.5f;
    // HDR intensity of the disc: how many times a white surface it is (the sun is about 100000 times, which the HDR
    // target could not hold). The cap keeps it far inside the half float range of the HDR target.
    constexpr float defaultIntensity = 30.f;
    constexpr float maxIntensity = 100.f;

    // The glow is the sum of two falloffs: a tight bright one and a wide faint one (light scattered by the air near the
    // sun). `x` is the distance in disc radii.
    struct GlowParams
    {
        float tightAmount = 0.5f;
        float tightWidth = 1.2f;
        float wideAmount = 0.15f;
        float wideWidth = 6.f;
        float scale = 1.f; // `procedural sun glow`
    };

    inline float smoothstep01(float edge0, float edge1, float x)
    {
        if (!(edge1 > edge0))
            return x < edge0 ? 0.f : 1.f;
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // 1 at the middle, `1 - u` at the rim, 0 outside; negative r counts as its absolute value; NaN gives 0.
    inline float limbDarkening(float r, float u)
    {
        if (std::isnan(r))
            return 0.f;
        r = std::abs(r);
        if (r > 1.f)
            return 0.f;
        if (std::isnan(u))
            u = 0.f;
        u = std::clamp(u, 0.f, 1.f);
        return 1.f - u * (1.f - std::sqrt(std::max(0.f, 1.f - r * r)));
    }

    // 1 inside the disc, 0 outside, a smooth ramp of half width `softness` round the radius.
    inline float edgeCoverage(float r, float radius, float softness)
    {
        if (std::isnan(r) || std::isnan(radius))
            return 0.f;
        softness = std::isnan(softness) ? 0.f : std::max(0.f, softness);
        if (softness <= 0.f)
            return r <= radius ? 1.f : 0.f;
        return 1.f - smoothstep01(radius - softness, radius + softness, r);
    }

    // The glow's brightness at `r`, relative to the disc's centre brightness of 1. Never negative, falls with r, is
    // continuous at the disc's edge, and is 0 from the quad's edge outwards.
    inline float glowFalloff(float r, float radius, const GlowParams& p)
    {
        if (!(radius > 0.f) || std::isnan(r) || !(p.scale > 0.f))
            return 0.f;
        r = std::abs(r);
        const float x = r / radius;
        const float shape = p.tightAmount / (1.f + (x / p.tightWidth) * (x / p.tightWidth))
            + p.wideAmount * std::exp(-x / p.wideWidth);
        const float window = 1.f - smoothstep01(glowWindowStart, 1.f, r);
        return std::max(0.f, p.scale * glowGain * shape * window);
    }

    // Disc plus glow at one pixel, relative to the disc's centre brightness. May be above 1 inside the disc.
    inline float pixelBrightness(float r, float radius, float softness, const GlowParams& p)
    {
        const float x = radius > 0.f ? r / radius : 2.f;
        return edgeCoverage(r, radius, softness) * limbDarkening(x, limbCoefficient) + glowFalloff(r, radius, p);
    }

    // The sun is drawn with alpha blending, so a pixel's alpha is its brightness (at most 1) and its colour is scaled
    // so that colour * scale * alpha = colour * intensity * brightness: the disc may be many times brighter than white
    // while the glow still fades into the sky.
    inline float pixelAlpha(float brightness)
    {
        return std::isnan(brightness) ? 0.f : std::clamp(brightness, 0.f, 1.f);
    }

    inline float colourScale(float intensity, float brightness)
    {
        if (!(brightness > 0.f))
            return 0.f;
        return intensity * std::max(brightness, 1.f);
    }

    inline float clampIntensity(float intensity)
    {
        if (std::isnan(intensity))
            return defaultIntensity;
        return std::clamp(intensity, 1.f, maxIntensity);
    }

    // The disc's radius for `procedural sun size`.
    inline float discRadius(float size)
    {
        if (std::isnan(size))
            return discRadiusAtSizeOne;
        return std::clamp(discRadiusAtSizeOne * size, minDiscRadius, maxDiscRadius);
    }

    // `procedural sun`, which needs the HDR path (its intensity is meaningless without it).
    inline bool useProceduralSun(bool setting, bool hdr)
    {
        return setting && hdr;
    }
}

#endif
