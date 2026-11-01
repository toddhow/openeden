#ifndef OPENEDEN_RENDER_COVERMATH_H
#define OPENEDEN_RENDER_COVERMATH_H

// OpenEden (task 0039): the maths of snow and ash cover. The shader twin is files/shaders/lib/material/cover.glsl;
// change both together. No game types.
//
// While it snows or ash falls, upward-facing outdoor surfaces gather a layer of it (`step` builds it, and melts or
// fades it afterwards). A pixel is covered by `amount x upwardness x exposure`, broken up with a noise so the edge is
// ragged (`coverMask`), and then drawn with the cover's colour and roughness (`colour`, `roughness`). Everything here
// is a pure function, so it is tested without a device.

#include <components/render/wetnessmath.hpp>

#include <algorithm>
#include <cmath>

namespace Render::Cover
{
    enum class Kind : int
    {
        None = 0,
        Snow = 1,
        Ash = 2,
    };

    // The default of `cover build time`, in seconds of game time: twenty game minutes of steady snow bury the ground.
    // By eye: the game's time scale is 30, so that is about forty real seconds; no source.
    constexpr float defaultBuildSeconds = 1200.f;
    // The default of `cover melt time`, in seconds of game time: two hours for snow to melt away once the weather has
    // cleared. By eye; no source.
    constexpr float defaultMeltSeconds = 7200.f;
    // Ash does not melt; it is blown and trodden away. It fades this many times slower than snow melts.
    constexpr float ashFadeFactor = 4.f;

    // Upward means the surface normal's Z (up) component between these: a slope of 37 degrees or less is fully
    // covered, one of 60 degrees or more not at all, and 45 degrees is covered about three parts in four (the spec's
    // D3).
    constexpr float upFrom = 0.5f;
    constexpr float upTo = 0.8f;
    // The softness of the cover's edge, in noise value.
    constexpr float edge = 0.08f;
    // The size of the biggest patches, in game units (about half a metre: Morrowind's unit is about 1.4 cm).
    constexpr float noiseCell = 36.f;
    // The cover amount is sent to the shader in this many steps (see `pack`).
    constexpr float coverSteps = 128.f;

    // The colour the cover gives a surface's diffuse colour (linear, not display values). Fresh snow reflects 80 to 90
    // percent of the light with a slight blue; fresh volcanic ash a dark grey of about 5 to 15 percent. Values by
    // eye inside those ranges.
    constexpr float snowR = 0.78f;
    constexpr float snowG = 0.80f;
    constexpr float snowB = 0.84f;
    constexpr float ashR = 0.075f;
    constexpr float ashG = 0.072f;
    constexpr float ashB = 0.07f;
    // Both are rough, ash more so (fine powder). By eye.
    constexpr float snowRoughness = 0.75f;
    constexpr float ashRoughness = 0.92f;

    struct Rgb
    {
        float r;
        float g;
        float b;
    };

    // How much of each kind is on the ground, 0 to 1.
    struct State
    {
        float snow = 0.f;
        float ash = 0.f;
    };

    struct Dominant
    {
        Kind kind;
        float amount;
    };

    using Wetness::saturate;
    using Wetness::smoothstep;

    // The kind of cover a weather builds, from the kind of particles the weather manager gives it: snow (and
    // blizzard) or ash (ash storm and blight). Snow wins if a mod makes a weather both.
    inline Kind kindOf(bool snowy, bool ashy)
    {
        if (snowy)
            return Kind::Snow;
        return ashy ? Kind::Ash : Kind::None;
    }

    // One kind's value moved towards its target. Rising takes `riseSeconds` from bare to buried, falling
    // `fallSeconds`; a time of zero or less is instant. `dt` and the times are game time. The result stays in 0 to 1
    // and never passes the target.
    inline float approach(float value, float targetValue, float dt, float riseSeconds, float fallSeconds)
    {
        value = saturate(value);
        targetValue = saturate(targetValue);
        if (!(dt > 0.f))
            return value;
        if (value < targetValue)
            return riseSeconds > 0.f ? std::min(value + dt / riseSeconds, targetValue) : targetValue;
        if (value > targetValue)
            return fallSeconds > 0.f ? std::max(value - dt / fallSeconds, targetValue) : targetValue;
        return value;
    }

    // One step of both kinds. The weather's own kind heads for the precipitation's strength (`precipitationAlpha`,
    // the weather result's fade as a transition passes), every other kind heads for nothing. Snow melts over
    // `meltSeconds`, ash fades over `meltSeconds` times `ashFadeFactor`.
    inline State step(
        const State& state, Kind weather, float precipitationAlpha, float dt, float buildSeconds, float meltSeconds)
    {
        const float alpha = saturate(precipitationAlpha);
        State out;
        out.snow = approach(state.snow, weather == Kind::Snow ? alpha : 0.f, dt, buildSeconds, meltSeconds);
        out.ash
            = approach(state.ash, weather == Kind::Ash ? alpha : 0.f, dt, buildSeconds, meltSeconds * ashFadeFactor);
        return out;
    }

    // The cover the renderer is told about: one kind and amount (a place where snow melts while ash falls shows the
    // larger; snow on a tie).
    inline Dominant dominant(const State& state)
    {
        const float snow = saturate(state.snow);
        const float ash = saturate(state.ash);
        if (ash > snow)
            return { Kind::Ash, ash };
        if (snow > 0.f)
            return { Kind::Snow, snow };
        return { Kind::None, 0.f };
    }

    // 1 for a surface facing up, 0 for a wall or an overhang; `normalZ` is the world normal's Z.
    inline float upwardness(float normalZ)
    {
        return smoothstep(upFrom, upTo, normalZ);
    }

    // A noise in [0, 1) at a world position (the wetness noise, with this task's patch size).
    inline float coverNoise(float worldX, float worldY)
    {
        return Wetness::noise(worldX / noiseCell, worldY / noiseCell);
    }

    // The share of the pixel that is covered. `amount` is how much cover the ground has, `up` the surface's
    // upwardness, `exposure` how open to the sky the place is (task 0037's `Wetness::exposure`, 0 under a roof). The
    // strength `amount x up x exposure` is compared with the noise, so at part cover some places are buried and others
    // bare, with a soft edge between; at full strength the whole surface is covered, at none nothing is.
    inline float coverMask(float amount, float up, float exposure, float noiseValue)
    {
        const float strength = saturate(saturate(amount) * saturate(up) * saturate(exposure));
        const float x = strength * (1.f + 2.f * edge) - edge;
        return smoothstep(noiseValue - edge, noiseValue + edge, x);
    }

    inline Rgb colour(Kind kind)
    {
        switch (kind)
        {
            case Kind::Snow:
                return { snowR, snowG, snowB };
            case Kind::Ash:
                return { ashR, ashG, ashB };
            case Kind::None:
                break;
        }
        return { 0.f, 0.f, 0.f };
    }

    inline float roughness(Kind kind)
    {
        switch (kind)
        {
            case Kind::Snow:
                return snowRoughness;
            case Kind::Ash:
                return ashRoughness;
            case Kind::None:
                break;
        }
        return 0.f;
    }

    // The scene block has no spare float, so the cover rides in the ambient colour's alpha beside the wetness (task
    // 0037 sent 1 + wetness). The alpha is 1 + wetness + 2 x code. The code is 0 for bare ground, 1 to `coverSteps`
    // for snow (the amount in steps) and `coverSteps` + 1 to 2 x `coverSteps` for ash. Without cover the alpha is
    // exactly the old 1 + wetness, so the shaders of the wetness task read it unchanged.
    inline float pack(float wetness, Kind kind, float amount)
    {
        const float wet = saturate(wetness);
        const float steps = std::floor(saturate(amount) * coverSteps + 0.5f);
        if (kind == Kind::None || !(steps >= 1.f))
            return 1.f + wet;
        const float code = kind == Kind::Snow ? steps : coverSteps + steps;
        return 1.f + wet + 2.f * code;
    }

    struct Unpacked
    {
        float wetness;
        Kind kind;
        float amount;
    };

    // What the shader does with the alpha (`coverUnpack` in cover.glsl). An alpha at or below 1 is dry and bare: it is
    // what the other views leave there.
    inline Unpacked unpack(float alpha)
    {
        const float raw = alpha - 1.f;
        if (!(raw > 0.f))
            return { 0.f, Kind::None, 0.f };
        const float code = std::floor(raw * 0.5f);
        const float wet = saturate(raw - 2.f * code);
        if (code < 1.f)
            return { wet, Kind::None, 0.f };
        if (code <= coverSteps)
            return { wet, Kind::Snow, code / coverSteps };
        return { wet, Kind::Ash, std::min(code - coverSteps, coverSteps) / coverSteps };
    }
}

#endif
