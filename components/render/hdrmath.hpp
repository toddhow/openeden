#ifndef OPENEDEN_RENDER_HDRMATH_H
#define OPENEDEN_RENDER_HDRMATH_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace Render::Hdr
{
    /*
     * The pure parts of the linear HDR path (task 0025, F2): colour conversion, tonemap curves and the decisions that
     * need no Vulkan. The shaders (files/shaders/lib/view/linear.glsl and the tonemap shader) repeat the constants and
     * curves; the unit tests check these functions.
     */

    // A pure power law, not the piecewise sRGB curve: the swapchain holds the lighting's numbers as they are today and
    // the display shows them with a curve of about this exponent, so this is the closest match to the default path, a
    // product of encoded values equals the encoded product of linear ones, and the two directions are exact inverses.
    constexpr float displayGamma = 2.2f;

    // The white point of the extended Reinhard curve, in linear light.
    constexpr float reinhardWhitePoint = 4.f;

    // The largest finite value of a 16-bit float, which is what the HDR target holds.
    constexpr float maxTargetValue = 65504.f;

    // Sign preserving: Morrowind has negative (darkness) point lights, and pow of a negative number is NaN.
    inline float toLinear(float encoded)
    {
        return std::copysign(std::pow(std::fabs(encoded), displayGamma), encoded);
    }

    inline float toEncoded(float linear)
    {
        return std::copysign(std::pow(std::fabs(linear), 1.f / displayGamma), linear);
    }

    inline vsg::vec3 toLinear(const vsg::vec3& v)
    {
        return vsg::vec3(toLinear(v.x), toLinear(v.y), toLinear(v.z));
    }

    // The alpha channel is not a colour and stays as it is.
    inline vsg::vec4 toLinear(const vsg::vec4& v)
    {
        return vsg::vec4(toLinear(v.x), toLinear(v.y), toLinear(v.z), v.w);
    }

    inline vsg::vec3 toEncoded(const vsg::vec3& v)
    {
        return vsg::vec3(toEncoded(v.x), toEncoded(v.y), toEncoded(v.z));
    }

    enum class Tonemap
    {
        Vanilla,
        Reinhard,
        Aces,
    };

    inline std::optional<Tonemap> parseTonemap(std::string_view name)
    {
        if (name == "vanilla")
            return Tonemap::Vanilla;
        if (name == "reinhard")
            return Tonemap::Reinhard;
        if (name == "aces")
            return Tonemap::Aces;
        return std::nullopt;
    }

    inline float exposureScale(float stops)
    {
        return std::exp2(stops);
    }

    // The curve of one channel, from linear light after exposure to linear light in [0, 1]. NaN stays NaN.
    inline float tonemapChannel(Tonemap curve, float linear)
    {
        if (std::isnan(linear))
            return linear;
        // Capped at the largest 16-bit float, the largest value the target holds: the curves square their input, which
        // overflows a float for huge values and would turn them into NaN.
        const float x = std::clamp(linear, 0.f, maxTargetValue);
        float result = x;
        switch (curve)
        {
            case Tonemap::Vanilla:
                break;
            case Tonemap::Reinhard:
                // Reinhard's extended form; the white point maps to 1.
                result = x * (1.f + x / (reinhardWhitePoint * reinhardWhitePoint)) / (1.f + x);
                break;
            case Tonemap::Aces:
                // Narkowicz's fit of the ACES filmic curve.
                result = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
                break;
        }
        return std::clamp(result, 0.f, 1.f);
    }

    struct TonemapChoice
    {
        Tonemap curve;
        float exposureStops;
        bool steppedAside;
    };

    // A post-processing technique that sets `hdr` expects the scene as it is and tonemaps for itself: the built-in
    // tonemap then does no more than the clip and the encode.
    inline TonemapChoice resolveTonemap(Tonemap curve, float exposureStops, bool techniqueWantsHdr)
    {
        if (techniqueWantsHdr)
            return { Tonemap::Vanilla, 0.f, true };
        return { curve, exposureStops, false };
    }

    // The path needs a window's swapchain to write to: the headless engine never takes it, and the hosted editor engine
    // does not read the setting.
    inline bool useHdrPath(bool settingOn, bool hostedByWindow)
    {
        return settingOn && hostedByWindow;
    }
}

#endif
