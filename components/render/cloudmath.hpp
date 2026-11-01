#ifndef OPENEDEN_RENDER_CLOUDMATH_H
#define OPENEDEN_RENDER_CLOUDMATH_H

// OpenEden (task 0038): the maths of cloud shading and cloud shadows. The shader twins are
// files/shaders/sky/clouds/lit.glsl (the lit clouds) and files/shaders/lib/view/cloudshadow.glsl (the shadow on the
// ground); change them together. No game types.
//
// Morrowind's sky has two flat layers of a repeating cloud picture. A pixel of that picture has a colour and an alpha;
// `thickness` turns them into one number, 0 for thin and 1 for thick. Lit clouds shade by that number and by where the
// sun is (`litMultiplier`): thick parts are darker, thin parts towards the sun glow, the way the edge of a cloud does
// (a "silver lining"). Cloud shadows project the same picture along the sun's direction onto the ground
// (`shadowDirection`, `domeUv`), and darken the direct sun only (`shadowFactor`). Everything is a pure function, so it
// is tested without a device.
//
// The dome: the sky's cloud model is Morrowind's meshes/sky_clouds_01.nif, a dome around the camera. Its texture
// coordinates are a flat projection of the vertex's X and Y (read from the file's vertices: u = 2.5 + 0.0024975 x,
// v = -1.5 - 0.0024975 y, so one repeat of the picture is about 400 units wide), and the weather scrolls v by a growing amount
// (`Sky::update`, mCloudAnimationTimer). `domePoint` finds where a ray from the camera meets the dome.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace Render::Cloud
{
    // Settings (D4): the lit look needs the HDR path; shadows also need the atmosphere's sun.
    inline bool useClouds(bool setting, bool hdr)
    {
        return setting && hdr;
    }

    inline bool useShadows(bool setting, bool hdr, bool atmosphere)
    {
        return setting && hdr && atmosphere;
    }

    // `clouds look`: `lit` (the default) or `flat` (the vanilla picture). Anything else is `lit`.
    enum class Look
    {
        Flat,
        Lit
    };

    inline Look parseLook(std::string_view text)
    {
        return text == "flat" ? Look::Flat : Look::Lit;
    }

    // --- Thickness -------------------------------------------------------------------------------------------------

    // A brighter pixel of a cloud picture is thinner cloud: the overcast picture is opaque everywhere and its
    // structure is in its brightness, the clear picture's structure is in its alpha. `pivot` is the brightness (0 to
    // 1, as stored) that leaves the alpha unchanged; each unit of brightness below it adds `gain` times the alpha.
    // By eye on Morrowind's three pictures (clear, cloudy, overcast); no source.
    constexpr float brightnessPivot = 0.55f;
    constexpr float brightnessGain = 2.f;

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

    inline float luminance(float r, float g, float b)
    {
        // Rec. 709 weights applied to the stored (display encoded) values: the picture's own brightness.
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    inline float thickness(float brightness, float alpha)
    {
        return saturate(saturate(alpha) * (1.f + brightnessGain * (brightnessPivot - saturate(brightness))));
    }

    // --- Lit clouds ------------------------------------------------------------------------------------------------

    // The extinction of a cloud per unit of thickness (Beer-Lambert: light through thickness t keeps exp(-k t)). 
    // With 4, a fully thick cloud passes 2 % of the light.
    constexpr float extinction = 4.f;
    // How much a fully thick cloud's underside is darkened (multiplier 1 - this) when the sun is high.
    constexpr float maxShade = 0.65f;
    // The silver lining: Henyey-Greenstein's forward scattering `g`, and the lining's brightness at the sun.
    constexpr float forwardG = 0.7f;
    constexpr float silverGain = 1.6f;
    // The luminance of the scene's direct light at which the lit look reaches full strength (the moon's light at
    // night is far below, the day's sun far above).
    constexpr float lightFrom = 0.08f;
    constexpr float lightTo = 0.5f;
    // Overcast and foggy weathers have little glare; they shade at no less than this share (D3).
    constexpr float minWeatherShare = 0.5f;

    inline float transmittance(float thick)
    {
        return std::exp(-extinction * saturate(thick));
    }

    // Henyey-Greenstein, scaled so that it is 1 looking straight at the sun and falls off to a small value away from
    // it. `cosTheta` is the cosine of the angle between the direction to the cloud and the direction to the sun.
    inline float forwardPhase(float cosTheta)
    {
        const float g = forwardG;
        const float c = std::clamp(cosTheta, -1.f, 1.f);
        const float hg = (1.f - g * g) / std::pow(1.f + g * g - 2.f * g * c, 1.5f);
        const float peak = (1.f - g * g) / std::pow((1.f - g) * (1.f - g), 1.5f);
        return hg / peak;
    }

    // The silver lining: bright where the cloud is thin (its transmittance is high) and the sun is behind it.
    inline float silverLining(float thick, float cosTheta)
    {
        return silverGain * forwardPhase(cosTheta) * transmittance(thick);
    }

    // The dark underside: more the thicker the cloud, and less as the sun gets low (a low sun lights undersides from
    // the side). `sunUp` is the sun direction's component along up.
    inline float undersideShade(float thick, float sunUp)
    {
        return maxShade * (1.f - transmittance(thick)) * (0.4f + 0.6f * saturate(sunUp));
    }

    // How much of the lit look applies: it needs direct light (`lightLuminance` is the scene's direct light colour's
    // luminance; the night's mirrored light is dim and gives none) and a weather that has a sun. `weatherShare` is
    // `weatherShare(glare, isStorm)`.
    inline float strength(float lightLuminance, float weatherShareValue)
    {
        return smoothstep(lightFrom, lightTo, lightLuminance) * saturate(weatherShareValue);
    }

    // Storm weathers (ash, blight and blizzard have winds over the storm speed) keep their look; the rest shade by
    // their glare, no less than `minWeatherShare`.
    inline float weatherShare(float glareView, bool isStorm)
    {
        return isStorm ? 0.f : minWeatherShare + (1.f - minWeatherShare) * saturate(glareView);
    }

    // The factor the cloud colour is multiplied by. 1 when `amount` is 0; may pass 1 in a silver lining.
    inline float litMultiplier(float thick, float cosTheta, float sunUp, float amount)
    {
        const float lit = 1.f - undersideShade(thick, sunUp) + silverLining(thick, cosTheta);
        return 1.f + saturate(amount) * (lit - 1.f);
    }

    // --- The dome ---------------------------------------------------------------------------------------------------

    constexpr float uvCenterU = 2.5f;
    constexpr float uvCenterV = -1.5f;
    constexpr float uvPerUnit = 0.0024975f;

    // The dome's profile read from meshes/sky_clouds_01.nif's vertices: horizontal distance from the centre and
    // height of each ring (the first ring is the apex).
    constexpr std::size_t domeRings = 5;
    constexpr std::array<float, domeRings> domeRadius{ 0.f, 275.f, 462.f, 688.f, 1000.f };
    constexpr std::array<float, domeRings> domeHeight{ 306.733f, 288.429f, 254.262f, 200.076f, 100.f };

    // Where a ray from the camera along `direction` (unit, Z up, in the dome's own frame) meets the dome, as the
    // dome's X and Y. Rays below the lowest ring meet the rim.
    inline vsg::vec2 domePoint(const vsg::vec3& direction)
    {
        const float horizontal = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        if (horizontal < 1e-6f)
            return vsg::vec2(0.f, 0.f);
        // The ray's slope; the dome's slope z / r falls from apex to rim, so the first segment whose far end is
        // lower than the ray is the one it crosses.
        const float slope = std::max(direction.z, 0.f) / horizontal;
        float radius = domeRadius[domeRings - 1];
        for (std::size_t i = 1; i < domeRings; ++i)
        {
            const float far = domeHeight[i] / domeRadius[i];
            if (far > slope)
                continue;
            // z = z0 + s (r - r0) = slope r on this segment.
            const float s = (domeHeight[i] - domeHeight[i - 1]) / (domeRadius[i] - domeRadius[i - 1]);
            radius = (domeHeight[i - 1] - s * domeRadius[i - 1]) / (slope - s);
            radius = std::clamp(radius, domeRadius[i - 1], domeRadius[i]);
            break;
        }
        return vsg::vec2(direction.x / horizontal * radius, direction.y / horizontal * radius);
    }

    // The cloud picture's texture coordinate at the dome's X and Y, with the layer's scroll (the weather's growing
    // `scroll`, taken off v as the sky's texture matrix does). Not wrapped.
    inline vsg::vec2 domeUv(const vsg::vec2& point, float scroll)
    {
        return vsg::vec2(uvCenterU + uvPerUnit * point.x, uvCenterV - uvPerUnit * point.y - scroll);
    }

    // --- Shadows ---------------------------------------------------------------------------------------------------

    // The height, in game units above the camera, of the layer the shadows come from. A real cloud layer is
    // a kilometre or so up and one Morrowind unit is about 1.4 cm, so a real layer would be about 70000 units up, but
    // then one repeat of the picture would be a kilometre across and a scene would lie inside one shadow. 15000 units
    // (about 200 m) makes one repeat about 270 m across (by eye; no source), so the pattern shows over a view.
    constexpr float layerHeight = 15000.f;
    // The sun must be this far above the horizon (the direction's Z) before shadows fade in; they are full by the
    // second. A low sun would throw them kilometres away.
    constexpr float sunUpFrom = 0.08f;
    constexpr float sunUpTo = 0.3f;
    // The extinction used for the sun's direct light through a cloud.
    constexpr float shadowExtinction = 2.f;
    // The grid the cloud picture is reduced to for the ground's lookup: one repeat of the picture in this many
    // squares on a side (the shadow's softness: a square is 8 pixels of the 512 pixel pictures).
    constexpr std::size_t gridSize = 64;

    // The direction from the camera, in world axes, towards the cloud that would shade `groundPoint`: the point where
    // the sun's ray from the ground meets the layer, seen from the camera. All in game units, Z up. `toSun` is the unit
    // direction to the sun. For the ground point at the camera it is `toSun` itself.
    inline vsg::vec3 shadowDirection(const vsg::vec3& groundPoint, const vsg::vec3& toSun, const vsg::vec3& camera)
    {
        const float up = std::max(toSun.z, sunUpFrom * 0.5f);
        const float travel = std::max(layerHeight - (groundPoint.z - camera.z), 0.f) / up;
        vsg::vec3 target = (groundPoint - camera) + toSun * travel;
        const float length = std::sqrt(target.x * target.x + target.y * target.y + target.z * target.z);
        if (length < 1e-6f)
            return vsg::vec3(0.f, 0.f, 1.f);
        return vsg::vec3(target.x / length, target.y / length, target.z / length);
    }

    // The fade of shadows with the sun's height.
    inline float sunFade(float sunUp)
    {
        return smoothstep(sunUpFrom, sunUpTo, sunUp);
    }

    // The share of the direct sun that reaches the ground through cloud of `thick` thickness. `amount` is the shadow
    // strength (the setting times the sun's fade and the weather's share, 0 to 1). In 0 to 1; 1 when `amount` is 0.
    inline float shadowFactor(float thick, float amount)
    {
        return 1.f - saturate(amount) * (1.f - std::exp(-shadowExtinction * saturate(thick)));
    }

    // Reads the grid with wrapping and bilinear weights, the way the shader does. `u` and `v` are texture
    // coordinates; a grid cell holds the average thickness of its square and is centred on (i + 0.5) / gridSize.
    inline float sampleGrid(const float* grid, float u, float v)
    {
        const float n = static_cast<float>(gridSize);
        const float x = u * n - 0.5f;
        const float y = v * n - 0.5f;
        const float fx = std::floor(x);
        const float fy = std::floor(y);
        const float tx = x - fx;
        const float ty = y - fy;
        const auto wrap = [](float f) {
            long i = static_cast<long>(f) % static_cast<long>(gridSize);
            if (i < 0)
                i += static_cast<long>(gridSize);
            return static_cast<std::size_t>(i);
        };
        const std::size_t x0 = wrap(fx), x1 = wrap(fx + 1.f), y0 = wrap(fy), y1 = wrap(fy + 1.f);
        const float a = grid[y0 * gridSize + x0] * (1.f - tx) + grid[y0 * gridSize + x1] * tx;
        const float b = grid[y1 * gridSize + x0] * (1.f - tx) + grid[y1 * gridSize + x1] * tx;
        return a * (1.f - ty) + b * ty;
    }

    // Averages a picture into the grid. `pixels` is `width` by `height` of four bytes each, `blue`, `green`, `red` and
    // `alpha` in that order of memory when `bgra`, else `red` first; row 0 first, as the GPU reads it. `out` holds
    // gridSize * gridSize values.
    inline void buildGrid(const unsigned char* pixels, std::size_t width, std::size_t height, bool bgra, float* out)
    {
        for (std::size_t j = 0; j < gridSize; ++j)
            for (std::size_t i = 0; i < gridSize; ++i)
            {
                const std::size_t x0 = i * width / gridSize, x1 = std::max((i + 1) * width / gridSize, x0 + 1);
                const std::size_t y0 = j * height / gridSize, y1 = std::max((j + 1) * height / gridSize, y0 + 1);
                float sum = 0.f;
                std::size_t count = 0;
                for (std::size_t y = y0; y < y1 && y < height; ++y)
                    for (std::size_t x = x0; x < x1 && x < width; ++x)
                    {
                        const unsigned char* p = pixels + (y * width + x) * 4;
                        const float r = (bgra ? p[2] : p[0]) / 255.f, g = p[1] / 255.f, b = (bgra ? p[0] : p[2]) / 255.f;
                        sum += thickness(luminance(r, g, b), p[3] / 255.f);
                        ++count;
                    }
                out[j * gridSize + i] = count > 0 ? sum / static_cast<float>(count) : 0.f;
            }
    }
}

#endif
