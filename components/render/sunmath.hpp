#ifndef OPENEDEN_RENDER_SUNMATH_H
#define OPENEDEN_RENDER_SUNMATH_H

// OpenEden (task 0034 decision 4, task 0047): maths of the sun's colours. No game types.
//
// tableElevation: Morrowind's colour tables (sky, fog, ambient and sun colours for sunrise, day, sunset and night)
// blend in windows around the sunrise and sunset hours, and show their pure "sunset" colours at the middle of the
// sunset window (19:00 with the default INI values). The sun that is drawn, though, sinks until 20:00 (the night
// start), so a physical sky driven by the drawn sun's height shows sunset colours an hour or more too late. The
// elevation computed here is the height a sun would have if it crossed the horizon at the middle of the two windows,
// so a model driven by it is at its sunset at the tables' sunset. It is used for colours only: the drawn sun, the
// light direction and the shadows keep following the weather manager's own orbit.

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
}

#endif
