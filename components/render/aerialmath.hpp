#ifndef OPENEDEN_RENDER_AERIALMATH_H
#define OPENEDEN_RENDER_AERIALMATH_H

#include <algorithm>
#include <cmath>
#include <utility>

/*
 * OpenEden (task 0066, stage c): the pure maths of the aerial perspective, the haze of the whole-map view. No VSG or
 * game types.
 *
 * Far land fades into the air's colour the way it does to an eye: the further the ray, the more air it crosses, and
 * the air is thick near the ground and thin high up. The air's density falls off exponentially with height h above
 * the ground level (world z = 0): density(h) = beta * exp(-h / H), with H the scale height. A straight ray from the
 * camera at height h0 with direction z-component dz, length s, crosses an optical depth
 *
 *     tau = beta * integral over t in [0, s] of exp(-max(h0 + t * dz, 0) / H) dt
 *
 * which has a closed form, and the light that survives is T = exp(-tau); the fog's share is 1 - T. The air does not
 * thin below the ground level (a ray that descends to height 0 keeps adding beta per unit from there on).
 *
 * The shader `files/shaders/lib/view/fog.glsl` repeats `opticalDepth`, `weatherBeta` and `fogDepthFromRange`: change
 * them together (a test reads the shader's constants back).
 *
 * How the shaders know to use it: with aerial perspective on, `Scene::fogScale` is stored NEGATIVE (its size is the
 * linear fog's, so the linear fog range, and with it the weather's fog depth, can still be read back).
 */
namespace Render::Aerial
{
    /// The fog depth of Morrowind's clear, cloudy and overcast weather (`Weather_*_Land_Fog_Day_Depth` is 0.69 to
    /// 0.72): at or below it the air has its base density; more than that is weather.
    inline constexpr float clearFogDepth = 0.7f;
    /// The most a weather's fog depth counts beyond `clearFogDepth`. Blizzards have depths of 2.8 and 3.0; without the
    /// cap their density would be astronomical.
    inline constexpr float maxWeatherExcess = 0.42f;
    /// A camera higher than this many scale heights is treated as being at this height (the air there is thinner than
    /// e^-30, and exp must not overflow in the shader's float).
    inline constexpr float maxCameraHeightScales = 30.f;

    struct Params
    {
        /// The distance at which a horizontal ray at the ground keeps 10 percent of the light in clear weather.
        float mVisibility = 100000.f;
        /// H, in world units.
        float mScaleHeight = 4000.f;
        /// How steeply weather thickens the air: beta is multiplied by exp(gain * (fog depth - clear)) up to the cap.
        float mWeatherGain = 3.f;
    };

    /// The density at the ground in clear weather: ln(10) / visibility, so that T = 0.1 at the visibility.
    inline float baseBeta(float visibility)
    {
        return std::log(10.f) / std::max(visibility, 1.f);
    }

    /// The density at the ground for a weather whose linear fog has depth `fogDepth` (FogManager: start = range * (1 -
    /// depth), end = range).
    inline float weatherBeta(const Params& params, float fogDepth)
    {
        const float excess = std::clamp(fogDepth - clearFogDepth, 0.f, maxWeatherExcess);
        return baseBeta(params.mVisibility) * std::exp(params.mWeatherGain * excess);
    }

    /// The weather's fog depth from the linear fog's range; 0 when the range has no usable end.
    inline float fogDepthFromRange(float start, float end)
    {
        if (!(end > 0.f))
            return 0.f;
        return 1.f - start / end;
    }

    /// The optical depth of a ray of length `distance` leaving height `cameraHeight` with the z component `rayDirZ` of
    /// its unit direction, in air of ground density `beta` and scale height `scaleHeight`.
    inline float opticalDepth(float beta, float scaleHeight, float cameraHeight, float rayDirZ, float distance)
    {
        if (!(distance > 0.f) || !(beta > 0.f))
            return 0.f;
        const double H = std::max(scaleHeight, 1.f);
        const double h0 = std::clamp(static_cast<double>(cameraHeight), 0.0, maxCameraHeightScales * H);
        const double dz = std::clamp(static_cast<double>(rayDirZ), -1.0, 1.0);
        double s = distance;
        double extra = 0; // the part of the ray that lies at or below the ground level, at full density
        if (dz < 0.0)
        {
            const double toGround = h0 / -dz;
            if (s > toGround)
            {
                extra = s - toGround;
                s = toGround;
            }
        }
        const double k = dz / H;
        const double x = k * s; // the exponent's size over the whole ray
        // integral of exp(-(h0 + t dz) / H) over [0, s] = exp(-h0 / H) * s * (1 - exp(-x)) / x, with a series for small
        // x so that it does not divide by zero.
        double along;
        if (std::fabs(x) < 1e-5)
            along = s * (1.0 - 0.5 * x);
        else
            along = -std::expm1(-x) / k;
        const double depth = std::exp(-h0 / H) * along + extra;
        return static_cast<float>(static_cast<double>(beta) * depth);
    }

    inline float transmittance(float beta, float scaleHeight, float cameraHeight, float rayDirZ, float distance)
    {
        return std::exp(-opticalDepth(beta, scaleHeight, cameraHeight, rayDirZ, distance));
    }

    /// The fog's share of the colour: 1 - T.
    inline float fogAmount(float beta, float scaleHeight, float cameraHeight, float rayDirZ, float distance)
    {
        return -std::expm1(-opticalDepth(beta, scaleHeight, cameraHeight, rayDirZ, distance));
    }

    /// What View::Scene::setFogRange stores: the start and the scale. Without aerial perspective exactly the linear
    /// fog's, as before this stage; with it the same size with a negative sign.
    inline std::pair<float, float> sceneFogRange(float start, float end, bool aerial)
    {
        const float scale = 1.f / std::max(0.001f, end - start);
        return { start, aerial ? -scale : scale };
    }

    /// True when the scale was stored by sceneFogRange with aerial perspective on.
    inline bool isAerial(float fogScale)
    {
        return fogScale < 0.f;
    }
}

#endif
