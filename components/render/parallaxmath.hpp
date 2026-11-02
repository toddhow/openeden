#ifndef OPENEDEN_RENDER_PARALLAXMATH_H
#define OPENEDEN_RENDER_PARALLAXMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include <vsg/vk/vulkan.h>

namespace Render::Parallax
{
    // The True PBR height file next to a diffuse texture: <name>_p, one channel (BC4 in Faultier's sets), 1 = the top.
    constexpr std::string_view heightMapSuffix = "_p";

    /*
     * The pure parts of parallax occlusion mapping (task 0045). The shaders repeat them
     * (files/shaders/lib/material/parallax.glsl, `parallaxMarch`): change both together.
     *
     * The idea: a surface with a height map (1 = the top, 0 = the deepest point) is drawn flat, but each pixel looks
     * for where the view ray, going down into the surface, meets the height field, and reads its colours there. The
     * ray is described by its depth d from 0 (at the surface) to 1 (the deepest point the map can have); the texture
     * coordinate at depth d is uv - eyeDir.xy / max(eyeDir.z, minViewZ) * d * uvDepth, and the ray is below the surface
     * once d >= 1 - height(uv(d)). The march takes steps at depth i/N, finds the first one below the surface and
     * refines the hit between that step and the one before.
     */

    // Where a material's height comes from (toddhow's rule, task 0045): the parallax is drawn only with a real height
    // source, never from a constant. A `_p` file that was found but cannot be read or is not a height image counts as
    // absent. A normal map's alpha counts only where the engine's convention says it holds height: the `_nh` file
    // (`AutoMaps::normalHeight`), not an alpha that is just opacity, and not a two-channel normal map.
    enum class HeightSource
    {
        None,
        HeightFile,
        NormalAlpha
    };

    // Formats whose first (red) channel can be read as a height: one-channel files and the ordinary colour formats a
    // tool may have exported a grey picture in. Not signed, floating point, two-channel (normal) or sRGB ones.
    inline bool usableHeightFormat(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R16_UNORM:
            case VK_FORMAT_BC4_UNORM_BLOCK:
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_R8G8B8_UNORM:
            case VK_FORMAT_B8G8R8_UNORM:
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            case VK_FORMAT_BC3_UNORM_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:
                return true;
            default:
                return false;
        }
    }

    inline HeightSource decideHeightSource(
        bool heightFileFound, bool heightFileUsable, bool normalAlphaIsHeight, bool twoChannelNormal)
    {
        if (heightFileFound && heightFileUsable)
            return HeightSource::HeightFile;
        if (normalAlphaIsHeight && !twoChannelNormal)
            return HeightSource::NormalAlpha;
        return HeightSource::None;
    }

    // How the march trades steps against cost (the setting `[Rendering] parallax quality`).
    enum class Quality
    {
        Low,
        Medium,
        High
    };

    inline std::optional<Quality> parseQuality(std::string_view name)
    {
        if (name == "low")
            return Quality::Low;
        if (name == "medium")
            return Quality::Medium;
        if (name == "high")
            return Quality::High;
        return std::nullopt;
    }

    struct QualityLimits
    {
        int minSteps; // looking straight at the surface
        int maxSteps; // looking along it
        int refineTaps; // bisection taps after the march (the linear interpolation costs none)
    };

    // 8 to 48 steps in all (the high tier takes more at grazing angles); the shader repeats these three rows.
    inline QualityLimits limits(Quality quality)
    {
        switch (quality)
        {
            case Quality::Low:
                return { 8, 12, 0 };
            case Quality::High:
                return { 16, 48, 4 };
            case Quality::Medium:
            default:
                return { 10, 20, 4 };
        }
    }

    // More steps the more the view runs along the surface. viewCosine is |eyeDir.z| of the normalized view direction
    // in tangent space; anything that is not a number is taken as the most grazing view.
    inline int stepCount(Quality quality, float viewCosine)
    {
        const QualityLimits lim = limits(quality);
        const float c = std::isnan(viewCosine) ? 0.f : std::clamp(viewCosine, 0.f, 1.f);
        const float steps
            = static_cast<float>(lim.minSteps) + static_cast<float>(lim.maxSteps - lim.minSteps) * (1.f - c);
        return std::clamp(static_cast<int>(std::ceil(steps)), lim.minSteps, lim.maxSteps);
    }

    // The march fades out between these distances from the camera (game units, about 70 per metre): beyond fadeEnd a
    // pixel is not marched at all.
    constexpr float fadeStart = 700.f;
    constexpr float fadeEnd = 1800.f;

    inline float distanceFade(float distance)
    {
        if (std::isnan(distance))
            return 0.f;
        const float t = std::clamp((distance - fadeStart) / (fadeEnd - fadeStart), 0.f, 1.f);
        return 1.f - t * t * (3.f - 2.f * t);
    }

    // The ray never leaves further than 1 / minViewZ times its depth, so a view almost along the surface does not
    // smear the texture over the whole wall.
    constexpr float minViewZ = 0.1f;

    inline float rayScale(float eyeZ)
    {
        if (std::isnan(eyeZ))
            return 1.f / minViewZ;
        return 1.f / std::max(eyeZ, minViewZ);
    }

    // The ray's slant (|eye.xy| / eye.z, the tangent of the view angle from the normal) never counts for more than
    // this: a view almost along the surface, or a texture already stretched by a steep face, cannot smear the picture
    // over many times the depth.
    constexpr float maxSlant = 2.f;

    // The slant of the ray after the limits: |xy| / max(z, minViewZ), at most maxSlant. A bad number counts as the
    // limit.
    inline float limitedSlant(float xyLength, float eyeZ)
    {
        if (std::isnan(xyLength) || std::isnan(eyeZ))
            return maxSlant;
        return std::min(std::max(xyLength, 0.f) / std::max(eyeZ, minViewZ), maxSlant);
    }

    // The height file is read blurred, at the pixel's own mip level plus a bias and never sharper than a floor, so a
    // mortar joint is a slope and not a cliff (a cliff sent the ray and the texture across the joint and stretched them
    // into strips). footprintSquared: the larger squared length, in texels, of the pixel's step in x or y.
    constexpr float blurBias = 1.f;
    constexpr float blurMinLod = 1.5f;

    inline float blurLod(float footprintSquared)
    {
        const float f = std::isnan(footprintSquared) ? 1e-8f : std::max(footprintSquared, 1e-8f);
        return std::max(0.5f * std::log2(f) + blurBias, blurMinLod);
    }

    // Where the height field is steep (the slope in world terms: depth per distance across) the offset is pulled back
    // toward the unshifted coordinates, as a colour read on the wall of a cliff is a stretched strip. Returns the share
    // of the offset that is kept, 1 on gentle ground down to steepKeep on a cliff.
    constexpr float steepFrom = 1.f;
    constexpr float steepTo = 4.f;
    constexpr float steepKeep = 0.25f;

    inline float steepSoftening(float steepness)
    {
        if (std::isnan(steepness))
            return steepKeep;
        const float t = std::clamp((steepness - steepFrom) / (steepTo - steepFrom), 0.f, 1.f);
        return 1.f + (steepKeep - 1.f) * (t * t * (3.f - 2.f * t));
    }

    // The slope added to the shading normal comes from the height field's gradient and is limited in length.
    constexpr float maxNormalSlope = 1.5f;

    // A little occlusion in recesses: the share of colour taken away at a cavity of 1 (the deepest).
    constexpr float cavityShadeMax = 0.3f;

    inline float cavityShade(float cavity)
    {
        return 1.f - cavityShadeMax * std::clamp(std::isnan(cavity) ? 0.f : cavity, 0.f, 1.f);
    }

    // The terrain's texture coordinates come from above, so on a steep face they are already stretched and a march in
    // that frame blows up (streaks of "waterfall" relief on cliffs). The march fades out with the slope: `up` is the
    // geometric normal's upward component (1 flat ground): full at 30 degrees or less, none at 45 degrees or more.
    constexpr float slopeFadeFull = 0.8660254f; // cos 30 degrees
    constexpr float slopeFadeNone = 0.7071068f; // cos 45 degrees

    inline float slopeFade(float up)
    {
        if (std::isnan(up))
            return 0.f;
        const float t = std::clamp((up - slopeFadeNone) / (slopeFadeFull - slopeFadeNone), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // Depth is a length in the world, so it does not depend on how a mesh's or the terrain's texture is mapped: a
    // displacement scale of 1 is this many game units (about 70 to the metre; Faultier's stone wall has 0.5, which
    // makes 16 units across the whole height range, about 23 centimetres, of which a block face against a joint is
    // about a fifth). The shader works out how many game units one unit of texture coordinates covers at the pixel
    // (from the derivatives of the position and of the coordinates) and divides. A fixed fraction of a tile, as the
    // first version used, was a 50 unit deep step on a terrain tile and made a small rock mesh a smeared mess.
    constexpr float worldDepthPerDisplacement = 32.f;
    // The depth in texture coordinates never exceeds this fraction of a tile, whatever the mapping.
    constexpr float maxUvDepth = 0.1f;
    // Used when a material file sets no scale.
    constexpr float defaultDisplacementScale = 0.5f;
    constexpr float maxDisplacementScale = 8.f;
    constexpr float maxParallaxScale = 4.f;

    // displacementScale: the material's (not a number: not set, the default is used); userScale: `parallax scale`
    // (not a number: 1). The result is the depth scale the shader multiplies by worldDepthPerDisplacement.
    inline float depthScale(float displacementScale, float userScale)
    {
        const float material = std::isnan(displacementScale) ? defaultDisplacementScale
                                                             : std::clamp(displacementScale, 0.f, maxDisplacementScale);
        const float user = std::isnan(userScale) ? 1.f : std::clamp(userScale, 0.f, maxParallaxScale);
        return material * user;
    }

    // The depth in texture coordinates for a depth scale, at a pixel where one unit of texture coordinates covers
    // worldPerUv game units. A number that is not usable gives no depth.
    inline float uvDepthFromWorld(float scale, float worldPerUv)
    {
        if (std::isnan(scale) || std::isnan(worldPerUv) || !(worldPerUv > 0.f))
            return 0.f;
        return std::clamp(scale * worldDepthPerDisplacement / worldPerUv, 0.f, maxUvDepth);
    }

    // IEEE half floats for non-negative values: not a number and negative values become 0, large ones the largest
    // finite half (65504). Rounds to nearest.
    inline uint16_t floatToHalf(float value)
    {
        if (!(value > 0.f))
            return 0;
        if (value >= 65520.f)
            return 0x7bff;
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const int exponent = static_cast<int>((bits >> 23) & 0xff) - 127 + 15;
        const uint32_t mantissa = bits & 0x7fffff;
        if (exponent <= 0)
        {
            // A subnormal half (or zero): the value in units of 2^-24.
            const float units = value * 16777216.f; // 2^24
            return static_cast<uint16_t>(std::min(std::lround(units), 0x400l));
        }
        uint32_t half = (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
        const uint32_t rest = mantissa & 0x1fff;
        if (rest > 0x1000 || (rest == 0x1000 && (half & 1u)))
            ++half; // a carry into the exponent is right: it is the next power of two
        return static_cast<uint16_t>(std::min(half, 0x7bffu));
    }

    inline float halfToFloat(uint16_t half)
    {
        const int exponent = (half >> 10) & 0x1f;
        const float mantissa = static_cast<float>(half & 0x3ff);
        if (exponent == 0)
            return mantissa / 16777216.f;
        return std::ldexp(1.f + mantissa / 1024.f, exponent - 15);
    }

    // The material's Material::align_dummy2 float (files/shaders/lib/material/data.glsl) carries two scales as two
    // half floats: the roughness scale of task 0044 in the low half and the displacement scale in the high half. Zero
    // in both is the float 0.0, "neither set", as before. A displacement scale of 0 reads back as "not set", so a
    // material cannot switch its depth off completely: give it a tiny one (0.001). The displacement part is the
    // material's scale times `parallax scale`, so it may reach maxDisplacementScale * maxParallaxScale. The largest
    // finite high half is 0x7bff, so the pattern is never a not-a-number (that needs 0x7f80 or more) and survives a
    // uniform buffer.
    // The ray's depth at which the surface "sits" while the amplitude changes. A texture offset of -rayStep * depth
    // moves every pixel below the top, so when the amplitude fades with distance (or the setting changes) the whole
    // texture would slide: the picture seemed to swim as the camera came closer. Measuring the offset from the map's
    // mean depth (read from its smallest mip level) leaves the average surface where it is, and only the relief moves.
    inline float pivotDepth(float meanHeight)
    {
        if (std::isnan(meanHeight))
            return 0.5f;
        return 1.f - std::clamp(meanHeight, 0.f, 1.f);
    }

    // The texture coordinate offset, in units of the ray step (uv offset = -rayStep * this).
    inline float offsetFraction(float hitDepth, float pivot)
    {
        return hitDepth - pivot;
    }

    struct Scales
    {
        float roughness = 0.f; // 0: none
        float displacement = 0.f; // 0: not set
    };

    inline float packScales(float roughnessScale, float displacementScale)
    {
        const uint32_t bits = static_cast<uint32_t>(floatToHalf(roughnessScale))
            | (static_cast<uint32_t>(floatToHalf(std::min(displacementScale, maxDisplacementScale * maxParallaxScale)))
                << 16);
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    inline Scales unpackScales(float packed)
    {
        uint32_t bits;
        std::memcpy(&bits, &packed, sizeof(bits));
        return { halfToFloat(static_cast<uint16_t>(bits & 0xffff)), halfToFloat(static_cast<uint16_t>(bits >> 16)) };
    }

    struct MarchResult
    {
        float depth = 1.f; // where the ray meets the surface, 0 (the top) to 1 (nothing found: the bottom)
    };

    // The reference march. heightAt(d) is the height field's value (1 top, 0 bottom) under the ray at depth d. Steps:
    // N taps at i/N for i = 1..N after one at d = 0; refineTaps bisection taps in the interval of the first hit;
    // `interpolate` then places the hit by a straight line through the two ends of the final interval.
    template <class HeightFn>
    MarchResult march(HeightFn&& heightAt, int steps, int refineTaps, bool interpolate = true)
    {
        steps = std::max(steps, 1);
        // How far the ray is above the surface: positive above, zero or negative once it is under it.
        const auto above = [&heightAt](float d) { return (1.f - heightAt(d)) - d; };
        float previousDepth = 0.f;
        float previousAbove = above(0.f);
        if (previousAbove <= 0.f)
            return { 0.f };
        for (int i = 1; i <= steps; ++i)
        {
            const float depth = static_cast<float>(i) / static_cast<float>(steps);
            const float a = above(depth);
            if (a <= 0.f)
            {
                float loDepth = previousDepth, loAbove = previousAbove;
                float hiDepth = depth, hiAbove = a;
                for (int k = 0; k < refineTaps; ++k)
                {
                    const float mid = 0.5f * (loDepth + hiDepth);
                    const float m = above(mid);
                    if (m <= 0.f)
                    {
                        hiDepth = mid;
                        hiAbove = m;
                    }
                    else
                    {
                        loDepth = mid;
                        loAbove = m;
                    }
                }
                if (!interpolate)
                    return { hiDepth };
                const float span = loAbove - hiAbove;
                const float t = span > 0.f ? std::clamp(loAbove / span, 0.f, 1.f) : 1.f;
                return { loDepth + t * (hiDepth - loDepth) };
            }
            previousDepth = depth;
            previousAbove = a;
        }
        return { 1.f };
    }
}

#endif
