#ifndef OPENEDEN_COMPONENTS_RENDER_FOGCURVE_H
#define OPENEDEN_COMPONENTS_RENDER_FOGCURVE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// OpenEden (task 0355, follow-up of stage d): the fog CURVE of a game pack's interior. Skyrim SE's cells carry a fog
// power and a fog maximum besides the near and far distances; the fog's share of the colour at a distance d is
//   pow(clamp((d - near) / (far - near), 0, 1), power) * max
// where power 1 and max 1 are the plain linear fog every other view has. The shaders (lib/view/fog.glsl and its Slang
// twin; change all three together, a test reads the constants back) apply it to the linear fog value.
//
// How the numbers reach the shader: the scene uniform may not grow (lib/view/data.glsl), so they ride in the fraction of
// scene.fogColor.w, the marker of the linear view (lib/view/linear.glsl). The marker is 1 (no HDR) or 2 (HDR) and every
// reader tests `> 1.5`, so a code below 0.5 added to it changes no other reader. The code is a 16 bit integer,
// power code * 256 + max code, divided by 131072 (an exact float at both markers: 22 fraction bits at 2, 23 at 1);
// code 0 (nothing added, bit for bit the old marker) means "no curve". Power is quantised in 1/64 (1/64 to 3.98),
// max in 1/255; either code 0 would mean "none", so both are clamped to at least 1.
namespace Render::FogCurve
{
    inline constexpr float kPowerScale = 64.f;
    inline constexpr float kMaxScale = 255.f;
    inline constexpr float kCodeScale = 131072.f;
    inline constexpr float kMinPower = 1.f / 64.f;
    inline constexpr float kMaxPower = 255.f / 64.f;

    struct Curve
    {
        float power = 1.f;
        float max = 1.f;

        friend bool operator==(const Curve&, const Curve&) = default;
    };

    /// True when the curve is the plain linear fog (the marker then carries nothing).
    inline bool isLinear(const Curve& c)
    {
        return !(c.power > 0.f) || !std::isfinite(c.power) || !std::isfinite(c.max) || (c.power == 1.f && c.max == 1.f);
    }

    /// The scene marker (1 or 2) with the curve added in its fraction. A linear curve (or one that is not a number)
    /// returns the marker unchanged.
    inline float encodeMarker(float marker, const Curve& c)
    {
        if (isLinear(c))
            return marker;
        const float p = std::clamp(std::round(c.power * kPowerScale), 1.f, 255.f);
        const float m = std::clamp(std::round(c.max * kMaxScale), 1.f, 255.f);
        return marker + (p * 256.f + m) / kCodeScale;
    }

    /// What the shader reads back from scene.fogColor.w: the linear curve for a plain marker (and for the 0 or an
    /// alpha up to 1 other views leave there).
    inline Curve decode(float w)
    {
        if (!(w > 1.f))
            return {};
        const float base = w > 1.5f ? 2.f : 1.f;
        const float code = std::floor((w - base) * kCodeScale + 0.5f);
        if (code < 1.f)
            return {};
        const float p = std::floor(code / 256.f);
        const float m = code - p * 256.f;
        if (p < 1.f || m < 1.f)
            return {};
        return { p / kPowerScale, m / kMaxScale };
    }

    /// The fog share for the linear share `linear` (0 to 1).
    inline float apply(float linear, const Curve& c)
    {
        return std::pow(std::max(linear, 0.f), c.power) * c.max;
    }
}

#endif
