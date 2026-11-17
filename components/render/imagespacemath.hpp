#ifndef OPENEDEN_RENDER_IMAGESPACEMATH_H
#define OPENEDEN_RENDER_IMAGESPACEMATH_H

#include <algorithm>
#include <array>
#include <cmath>

// The grade of a game's image space on the tonemapped picture (OpenEden task 0127, stage W-S4): saturation, a tint and
// contrast, applied to the display value (after the tonemap curve and the encode, so 0.5 is perceptual mid grey). The
// shaders post/tonemap.frag and post/tonemap.slang repeat this maths under GRADE (a pipeline without the define is the
// old pipeline): change them together; apps/components_tests/render/testimagespace.cpp pins both.
//
// The order (saturation, tint, contrast) and the tint rule are GUESSES made from what the data says: Skyrim's image
// space has a saturation, a brightness (applied as exposure, before the tonemap: MWWorld::WeatherManager), a contrast
// and a tint of an amount and a colour. The game's own formulas are not published; these are the plain versions.
//   saturation: c = luma + saturation * (c - luma)
//   tint: the picture's grey colourised by the tint colour at the tint's own brightness, blended by the amount:
//         c = mix(c, luma(c) * tint / luma(tint), amount)   (a tint with no brightness is not a colour: skipped)
//   contrast: c = (c - 0.5) * contrast + 0.5
// and the result is clamped to 0 to 1. A value that is not a number is taken as the neutral one.

namespace Render::ImageSpace
{
    using Rgb = std::array<float, 3>;

    /// Rec. 709 weights, the same as the shaders'.
    inline constexpr float kLumaR = 0.2126f;
    inline constexpr float kLumaG = 0.7152f;
    inline constexpr float kLumaB = 0.0722f;
    inline constexpr float kContrastPivot = 0.5f;

    struct Grade
    {
        float saturation = 1.f;
        float contrast = 1.f;
        float tintAmount = 0.f;
        Rgb tint{ 1.f, 1.f, 1.f };
    };

    inline float luma(const Rgb& c)
    {
        return kLumaR * c[0] + kLumaG * c[1] + kLumaB * c[2];
    }

    namespace detail
    {
        inline float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }
    }

    inline Rgb apply(const Rgb& input, const Grade& grade)
    {
        Rgb c = input;
        for (float& v : c)
            v = detail::finiteOr(v, 0.f);
        const float saturation = std::max(detail::finiteOr(grade.saturation, 1.f), 0.f);
        const float contrast = std::max(detail::finiteOr(grade.contrast, 1.f), 0.f);
        const float amount = std::clamp(detail::finiteOr(grade.tintAmount, 0.f), 0.f, 1.f);

        const float l = luma(c);
        for (float& v : c)
            v = l + saturation * (v - l);

        const Rgb tint{ detail::finiteOr(grade.tint[0], 0.f), detail::finiteOr(grade.tint[1], 0.f),
            detail::finiteOr(grade.tint[2], 0.f) };
        const float tintLuma = luma(tint);
        if (amount > 0.f && tintLuma > 1e-4f)
        {
            const float grey = luma(c);
            for (std::size_t i = 0; i < 3; ++i)
            {
                const float target = grey * tint[i] / tintLuma;
                c[i] = c[i] + (target - c[i]) * amount;
            }
        }

        for (float& v : c)
            v = std::clamp((v - kContrastPivot) * contrast + kContrastPivot, 0.f, 1.f);
        return c;
    }

    // ---- a modifier on top of the weather's image space (OpenEden task 0257) --------------------------------------
    //
    // A game's spells and effects can change the picture while they run (Skyrim's Night Eye). The pack describes the
    // change as a multiply and an add per value (the form the game's image space modifiers have), and `combine` puts
    // it on what the weather already gives: value' = value * mult + add. Brightness is a linear multiplier of the
    // exposure (the weather manager hands the renderer its logarithm, in stops). A modifier that is not active returns
    // the weather's values untouched, bit for bit, so a session without one draws what it always drew.

    struct Modifier
    {
        bool active = false;
        float saturationMult = 1.f;
        float saturationAdd = 0.f;
        float brightnessMult = 1.f;
        float brightnessAdd = 0.f;
        float contrastMult = 1.f;
        float contrastAdd = 0.f;
        float tintAmount = 0.f;
        Rgb tint{ 1.f, 1.f, 1.f };
    };

    /// What the renderer holds: whether a grade applies at all, the exposure in stops and the grade.
    struct Effective
    {
        bool on = false;
        float stops = 0.f;
        Grade grade;
    };

    inline constexpr float kMinBrightness = 0.25f; ///< the same floor the weather manager gives the brightness
    inline constexpr float kMaxStops = 2.f; ///< RenderManager::setImageSpaceStops clamps to this
    inline constexpr float kMaxGradeValue = 4.f; ///< saturation and contrast are clamped to 0 .. this

    inline Effective combine(const Effective& weather, const Modifier& modifier)
    {
        if (!modifier.active)
            return weather;
        const auto finite = [](float value, float fallback) { return detail::finiteOr(value, fallback); };

        Effective out;
        out.on = true;
        // The weather's own values when it has no grade are the neutral ones (Effective's defaults).
        const float weatherStops = weather.on ? finite(weather.stops, 0.f) : 0.f;
        const Grade base = weather.on ? weather.grade : Grade{};

        const float saturation
            = finite(base.saturation, 1.f) * finite(modifier.saturationMult, 1.f) + finite(modifier.saturationAdd, 0.f);
        const float contrast
            = finite(base.contrast, 1.f) * finite(modifier.contrastMult, 1.f) + finite(modifier.contrastAdd, 0.f);
        out.grade.saturation = std::clamp(saturation, 0.f, kMaxGradeValue);
        out.grade.contrast = std::clamp(contrast, 0.f, kMaxGradeValue);

        const float brightness
            = std::exp2(weatherStops) * finite(modifier.brightnessMult, 1.f) + finite(modifier.brightnessAdd, 0.f);
        out.stops = std::clamp(std::log2(std::max(brightness, kMinBrightness)), -kMaxStops, kMaxStops);

        const float weatherAmount = std::clamp(finite(base.tintAmount, 0.f), 0.f, 1.f);
        const float modifierAmount = std::clamp(finite(modifier.tintAmount, 0.f), 0.f, 1.f);
        const float amount = modifierAmount + weatherAmount * (1.f - modifierAmount);
        out.grade.tintAmount = amount;
        out.grade.tint = base.tint;
        if (amount > 0.f)
        {
            const float weatherShare = weatherAmount * (1.f - modifierAmount);
            for (std::size_t i = 0; i < 3; ++i)
                out.grade.tint[i] = (std::clamp(finite(modifier.tint[i], 0.f), 0.f, 1.f) * modifierAmount
                                        + std::clamp(finite(base.tint[i], 0.f), 0.f, 1.f) * weatherShare)
                    / amount;
        }
        return out;
    }
}

#endif
