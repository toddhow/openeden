#ifndef OPENEDEN_RENDER_SSSMATH_H
#define OPENEDEN_RENDER_SSSMATH_H

// OpenEden (task 0033, stage a): the maths of the screen-space subsurface scattering step (R7, tier 2). The shader
// twin is the SSS block in the tonemap's fragment shader (components/render/tonemap.cpp); change both together. No game
// types.
//
// The idea is the one in Jimenez, Whyte, Sakamoto and Karis, "Separable Subsurface Scattering" (2015): light that
// enters skin comes out a little way off, and red travels farther than green and green farther than blue. This is a
// reimplementation from that description and no code of theirs; unlike their pass it is one non-separable gather with a
// fixed spiral of taps, because the cheapest tier is meant to cost one fullscreen read.
//
// The result for each colour channel is: the pixel's own colour times a gain, where the gain is the wide Gaussian
// average of its neighbours on the same skin surface divided by a very narrow one (blended in by `strength`). A ratio,
// not a difference, because the engine stores no albedo: with a difference a black nose or a dark mouth took red from
// the bright fur around it (found by the frame check). With a ratio black stays black and detail finer than the narrow
// Gaussian is untouched; only soft, low frequency light is moved.

#include <algorithm>
#include <cmath>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace Render::Sss
{
    constexpr float pi = 3.14159265358979f;

    // The defaults of `sss strength` (0 to 1) and `sss width` (game units; Morrowind's unit is about 1.4 cm, so this is
    // about 1 cm for the red channel's Gaussian).
    constexpr float defaultStrength = 0.5f;
    constexpr float defaultWidth = 0.7f;

    // The Gaussian widths of the red, green and blue channels as fractions of `width`: red spreads farthest. These are
    // the ratios of skin's measured profile in the paper above, rounded; the tuning is by eye.
    constexpr vsg::vec3 channelSigma{ 1.f, 0.45f, 0.2f };
    // The narrow Gaussian that is subtracted, as a fraction of `width`.
    constexpr float narrowSigma = 0.12f;
    // The farthest tap, in widths.
    constexpr float reachInWidths = 3.f;
    // The taps in the spiral.
    constexpr int tapCount = 16;

    // The gain is limited to this range, so a bright patch next to a dark one cannot flare, and `epsilon` keeps the ratio
    // of two dark averages from jumping about.
    constexpr float gainMin = 0.5f;
    constexpr float gainMax = 2.f;
    constexpr float epsilon = 1e-3f;

    // The pass needs its setting and the material output (which holds the skin mark); that output exists only with the
    // G-buffer and the material model.
    inline bool useSss(bool settingOn, bool gbuffer, bool materialOutput)
    {
        return settingOn && gbuffer && materialOutput;
    }

    // Pixels per game unit, on a surface `depth` units from the camera, for a camera whose focal length is
    // `focalPixels`. 0 where there is no surface.
    inline float pixelsPerUnit(float depth, float focalPixels)
    {
        return depth > 0.f ? focalPixels / depth : 0.f;
    }

    // The focal length in pixels of a perspective camera with this vertical field of view (radians) and this height.
    inline float focalPixels(float verticalFov, float heightPixels)
    {
        return 0.5f * heightPixels / std::tan(0.5f * verticalFov);
    }

    // The gain of a neighbour `distance` units away for a Gaussian of width `sigma`; 1 at the centre. A width of zero
    // or less admits only the centre.
    inline float gaussian(float distance, float sigma)
    {
        if (!(sigma > 0.f))
            return distance == 0.f ? 1.f : 0.f;
        return std::exp(-distance * distance / (2.f * sigma * sigma));
    }

    // Tap `index` of `count`, as an offset in units from the pixel: a golden-angle spiral that covers the disc of radius
    // `reach` evenly. `rotation` (radians) turns the whole pattern, so that neighbouring pixels can use different ones.
    inline vsg::vec2 tapOffset(int index, int count, float reach, float rotation)
    {
        constexpr float goldenAngle = 2.39996323f;
        const float radius = reach * std::sqrt((static_cast<float>(index) + 0.5f) / static_cast<float>(count));
        const float angle = static_cast<float>(index) * goldenAngle + rotation;
        return vsg::vec2(radius * std::cos(angle), radius * std::sin(angle));
    }

    inline float smoothStep(float low, float high, float x)
    {
        const float t = std::clamp((x - low) / (high - low), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // How much a neighbour counts: 1 when it is on the same surface as the pixel, 0 when it is another surface. `depthDelta`
    // is the difference of the two linear depths (units), `width` the scatter width, `normalDot` the dot of the two
    // normals. A neighbour more than two widths nearer or farther, or facing more than about 70 degrees away, does not
    // count, so light does not leak across a jaw line or from a hand onto the cloth behind it.
    inline float surfaceWeight(float depthDelta, float width, float normalDot)
    {
        const float limit = std::max(width, 1e-4f);
        return (1.f - smoothStep(limit, 2.f * limit, std::abs(depthDelta))) * smoothStep(0.2f, 0.6f, normalDot);
    }

    struct Tap
    {
        vsg::vec3 colour; // the neighbour's scene colour, linear
        float distance;   // from the pixel, in game units, in the screen's plane
        float validity;   // surfaceWeight times 1 if the neighbour is marked as skin, 0 if not
    };

    // The scattered colour of one pixel. The pixel itself counts with weight 1 in both averages. `strength` 0 returns
    // `centre`; a centre of 0 stays 0. Never below 0 in any channel.
    inline vsg::vec3 scatter(const vsg::vec3& centre, const Tap* taps, int count, float strength, float width)
    {
        vsg::vec3 result = centre;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float wide = channelSigma[channel] * width;
            const float narrow = narrowSigma * width;
            float wideSum = centre[channel];
            float wideWeight = 1.f;
            float narrowSum = centre[channel];
            float narrowWeight = 1.f;
            for (int i = 0; i < count; ++i)
            {
                const float v = taps[i].validity;
                const float w1 = gaussian(taps[i].distance, wide) * v;
                const float w2 = gaussian(taps[i].distance, narrow) * v;
                wideSum += taps[i].colour[channel] * w1;
                wideWeight += w1;
                narrowSum += taps[i].colour[channel] * w2;
                narrowWeight += w2;
            }
            const float gain = std::clamp(
                (wideSum / wideWeight + epsilon) / (narrowSum / narrowWeight + epsilon), gainMin, gainMax);
            result[channel] = std::max(0.f, centre[channel] * (1.f + strength * (gain - 1.f)));
        }
        return result;
    }
}

#endif
