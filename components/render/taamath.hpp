#ifndef OPENEDEN_RENDER_TAAMATH_H
#define OPENEDEN_RENDER_TAAMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

/*
 * The pure parts of temporal anti-aliasing (task 0075, docs/tasks/0075-temporal-anti-aliasing.md; the technique and its
 * sources are in docs/research/0037-temporal-anti-aliasing.md). Header-only, no Vulkan. The shader
 * files/shaders/post/taa.slang repeats the constants and the formulas of resolveMain and outputMain: change them
 * together (a test reads the constants back from the file).
 *
 * Conventions: UV is 0 to 1 across the image, v downward (Vulkan); a velocity is `currentUV - previousUV`; the clip
 * jitter is added to NDC, whose y also points down, so a pixel offset in image coordinates is the NDC offset directly.
 */
namespace Render::Taa
{
    constexpr std::uint32_t jitterPeriod = 16;
    // The share of the previous frame in the resolved colour: Salvi 2016 suggests 0.9 ("typically we blend 10% of the
    // current frame with 90% of the past"); decision 0012 raised the default to 0.95 (half the still-camera shimmer).
    constexpr float defaultHistoryWeight = 0.95f;
    // At this pixel speed the history weight has fallen to `movingWeightShare` of its base. A chosen value: a bilinear
    // or even a bicubic lookup at a moving position is blurrier the further it moves (docs/research/0037, section 6).
    constexpr float velocityFullPixels = 16.f;
    constexpr float movingWeightShare = 0.5f;
    // The disocclusion test works on log2(1 + depth): below the low difference (about 2% of the depth) the history's
    // depth agrees, above the high one (about 9%) it is rejected. Chosen values, tuned by frames.
    constexpr float depthRejectLow = 0.03f;
    constexpr float depthRejectHigh = 0.12f;
    // Salvi 2016: "We typically use gamma = 1".
    constexpr float varianceGamma = 1.0f;
    // The camera moved further than this (game units) in one frame: a teleport. Velocity's own 4096 is for its purpose.
    constexpr double resetDistance = 512.0;
    // The view direction turned more than 60 degrees in one frame: a scripted camera cut.
    constexpr double resetViewCos = 0.5;
    // More than this between two frames: a menu, a loading screen, a loaded save (the same quarter second as
    // RenderManager::updateVelocity).
    constexpr double resetGapSeconds = 0.25;

    // The van der Corput radical inverse of `index` in `base` (Halton 1960): the digits of the index in that base,
    // mirrored about the point.
    inline double radicalInverse(std::uint32_t index, std::uint32_t base)
    {
        double result = 0.0;
        double fraction = 1.0 / static_cast<double>(base);
        while (index > 0)
        {
            result += fraction * static_cast<double>(index % base);
            index /= base;
            fraction /= static_cast<double>(base);
        }
        return result;
    }

    // The jitter of a frame in pixels, each axis in [-0.5, 0.5): the Halton (2, 3) sample number frame % period + 1
    // (sample 0 would be the pixel centre every period).
    inline vsg::dvec2 haltonJitter(std::uint32_t frame)
    {
        const std::uint32_t i = frame % jitterPeriod + 1;
        return { radicalInverse(i, 2) - 0.5, radicalInverse(i, 3) - 0.5 };
    }

    inline vsg::dvec2 jitterPixels(std::uint32_t frame, float scale)
    {
        const double s = std::clamp(static_cast<double>(scale), 0.0, 1.0);
        const vsg::dvec2 h = haltonJitter(frame);
        return { h.x * s, h.y * s };
    }

    inline vsg::dvec2 jitterNdc(const vsg::dvec2& pixels, std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return { 0.0, 0.0 };
        return { 2.0 * pixels.x / static_cast<double>(width), 2.0 * pixels.y / static_cast<double>(height) };
    }

    // Identity with the NDC offset in the fourth column: applied to a clip position (x, y, z, w) it adds
    // (ndc.x * w, ndc.y * w), a constant shift of the picture. VSG indexes (column, row).
    inline vsg::dmat4 jitterMatrix(const vsg::dvec2& ndc)
    {
        vsg::dmat4 m;
        m(3, 0) = ndc.x;
        m(3, 1) = ndc.y;
        return m;
    }

    // TAA runs only with the HDR path and the G-buffer (the velocity and linear depth) and a single-sampled scene.
    inline bool useTaa(bool settingOn, bool hdrPath, bool gbuffer, std::uint32_t samples)
    {
        return settingOn && hdrPath && gbuffer && samples == 1;
    }

    enum class Clipping
    {
        Variance,
        MinMax
    };

    // The default is the plain min/max box: the variance box is covered by US10116916B2, so it is an opt-in (decision
    // 0012).
    constexpr Clipping defaultClipping = Clipping::MinMax;

    inline std::optional<Clipping> parseClipping(std::string_view name)
    {
        if (name == "variance")
            return Clipping::Variance;
        if (name == "minmax")
            return Clipping::MinMax;
        return std::nullopt;
    }

    enum class DebugView
    {
        None,
        Weight,
        Reject,
        Velocity
    };

    inline std::optional<DebugView> parseDebugView(std::string_view name)
    {
        if (name == "none")
            return DebugView::None;
        if (name == "weight")
            return DebugView::Weight;
        if (name == "reject")
            return DebugView::Reject;
        if (name == "velocity")
            return DebugView::Velocity;
        return std::nullopt;
    }

    struct ResetInput
    {
        bool havePrevious;
        bool sizeChanged;
        double gapSeconds;
        double cameraDistance;
        double viewDirectionCos;
    };

    inline bool historyReset(const ResetInput& in)
    {
        return !in.havePrevious || in.sizeChanged || in.gapSeconds > resetGapSeconds
            || in.cameraDistance > resetDistance || in.viewDirectionCos < resetViewCos;
    }

    inline float luma(const vsg::vec3& rgb)
    {
        return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
    }

    // A reversible squeeze of linear HDR light into [0, 1) by its luma, so that a lamp or the sun a thousand times
    // brighter than its neighbours cannot dominate a box or an average (Karis 2014, docs/research/0037, section 5).
    inline vsg::vec3 compress(const vsg::vec3& c)
    {
        const vsg::vec3 p(std::max(c.x, 0.f), std::max(c.y, 0.f), std::max(c.z, 0.f));
        return p * (1.f / (1.f + luma(p)));
    }

    inline vsg::vec3 expand(const vsg::vec3& c)
    {
        const vsg::vec3 p(std::max(c.x, 0.f), std::max(c.y, 0.f), std::max(c.z, 0.f));
        const float l = std::min(luma(p), 0.9999f);
        return p * (1.f / (1.f - l));
    }

    inline vsg::vec3 rgbToYCoCg(const vsg::vec3& c)
    {
        return { 0.25f * (c.x + 2.f * c.y + c.z), 0.5f * (c.x - c.z), 0.25f * (-c.x + 2.f * c.y - c.z) };
    }

    inline vsg::vec3 ycocgToRgb(const vsg::vec3& c)
    {
        return { c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z };
    }

    struct Box
    {
        vsg::vec3 min;
        vsg::vec3 max;
    };

    // Variance clipping (Salvi 2016): the box round the neighbourhood's mean, gamma standard deviations wide.
    inline Box varianceBox(const vsg::vec3& mean, const vsg::vec3& meanOfSquares, float gamma)
    {
        auto half = [gamma](float m, float m2) { return gamma * std::sqrt(std::max(m2 - m * m, 0.f)); };
        const vsg::vec3 h(half(mean.x, meanOfSquares.x), half(mean.y, meanOfSquares.y), half(mean.z, meanOfSquares.z));
        return { vsg::vec3(mean.x - h.x, mean.y - h.y, mean.z - h.z),
            vsg::vec3(mean.x + h.x, mean.y + h.y, mean.z + h.z) };
    }

    namespace Detail
    {
        inline void intersectAxis(float aMin, float aMax, float bMin, float bMax, float& lo, float& hi)
        {
            lo = std::max(aMin, bMin);
            hi = std::min(aMax, bMax);
            if (lo > hi)
            {
                // Disjoint: the midpoint of the two edges that face each other.
                const float mid = aMax < bMin ? 0.5f * (aMax + bMin) : 0.5f * (bMax + aMin);
                lo = hi = mid;
            }
        }
    }

    inline Box intersect(const Box& a, const Box& b)
    {
        Box r;
        Detail::intersectAxis(a.min.x, a.max.x, b.min.x, b.max.x, r.min.x, r.max.x);
        Detail::intersectAxis(a.min.y, a.max.y, b.min.y, b.max.y, r.min.y, r.max.y);
        Detail::intersectAxis(a.min.z, a.max.z, b.min.z, b.max.z, r.min.z, r.max.z);
        return r;
    }

    // Moves `colour` toward the box centre until it is on the box (Karis 2014, Pedersen 2016, Salvi 2016: a clip, not a
    // clamp, so that the colour keeps its direction). `clippedShare` receives the share of the way to the centre that
    // was taken, 0 for a colour inside.
    inline vsg::vec3 clipToBox(const vsg::vec3& colour, const Box& box, float* clippedShare = nullptr)
    {
        const vsg::vec3 centre(
            0.5f * (box.min.x + box.max.x), 0.5f * (box.min.y + box.max.y), 0.5f * (box.min.z + box.max.z));
        const vsg::vec3 half(
            0.5f * (box.max.x - box.min.x), 0.5f * (box.max.y - box.min.y), 0.5f * (box.max.z - box.min.z));
        const vsg::vec3 d(colour.x - centre.x, colour.y - centre.y, colour.z - centre.z);
        constexpr float tiny = 1e-8f;
        auto ratio = [](float offset, float extent) {
            const float a = std::abs(offset);
            if (extent > tiny)
                return a / extent;
            return a > tiny ? 1e8f : 0.f;
        };
        const float t = std::max({ ratio(d.x, half.x), ratio(d.y, half.y), ratio(d.z, half.z) });
        if (t <= 1.f)
        {
            if (clippedShare)
                *clippedShare = 0.f;
            return colour;
        }
        if (clippedShare)
            *clippedShare = 1.f - 1.f / t;
        return vsg::vec3(centre.x + d.x / t, centre.y + d.y / t, centre.z + d.z / t);
    }

    inline float saturate(float x)
    {
        return std::clamp(x, 0.f, 1.f);
    }

    inline float smoothstep(float lo, float hi, float x)
    {
        const float t = saturate((x - lo) / (hi - lo));
        return t * t * (3.f - 2.f * t);
    }

    // The history weight after the pixel's motion: full for a still pixel, `movingWeightShare` of it from
    // `velocityFullPixels` a frame.
    inline float historyWeightFor(float baseWeight, float speedPixels)
    {
        return baseWeight * (1.f - (1.f - movingWeightShare) * saturate(speedPixels / velocityFullPixels));
    }

    // What the history's alpha holds: the surface's distance along the view axis on a log scale (so that half-float
    // keeps its relative precision to the far plane), -1 for a pixel with no opaque surface.
    inline float encodeDepth(float linearDepth)
    {
        return linearDepth > 0.f ? std::log2(1.f + linearDepth) : -1.f;
    }

    // 0 where the history's surface is the one now seen, 1 where it is not (a disocclusion). `expected` is this
    // surface's distance in the previous camera, `stored` the history's encoded depth at the reprojected pixel.
    inline float depthReject(float expectedPreviousLinearDepth, float storedEncodedDepth)
    {
        if (storedEncodedDepth < 0.f && expectedPreviousLinearDepth > 0.f)
            return 1.f;
        return smoothstep(
            depthRejectLow, depthRejectHigh, std::abs(encodeDepth(expectedPreviousLinearDepth) - storedEncodedDepth));
    }

    // The history's depths over the pixels round the reprojected one, as their smallest and largest encoded values:
    // the surface is accepted when its depth lies in the range (the closest depth of a 3 by 3 changes with the jitter
    // at a silhouette, so the single value at the reprojected pixel rejects edge pixels at random). The same as
    // depthReject for a range of one value.
    inline float depthRejectRange(float expectedPreviousLinearDepth, float storedMin, float storedMax)
    {
        if (storedMax < 0.f && expectedPreviousLinearDepth > 0.f)
            return 1.f;
        const float e = encodeDepth(expectedPreviousLinearDepth);
        const float outside = std::max(std::max(storedMin - e, e - storedMax), 0.f);
        return smoothstep(depthRejectLow, depthRejectHigh, outside);
    }

    inline float finalHistoryWeight(float baseWeight, float speedPixels, float depthRejectAmount, bool historyUsable)
    {
        if (!historyUsable)
            return 0.f;
        return historyWeightFor(baseWeight, speedPixels) * (1.f - depthRejectAmount);
    }

    // The exponential average, done in the compressed domain so that a bright pixel weighs by its compressed value
    // and not by its HDR value.
    inline vsg::vec3 resolveColour(const vsg::vec3& current, const vsg::vec3& history, float historyWeight)
    {
        const vsg::vec3 c = compress(current);
        const vsg::vec3 h = compress(history);
        return expand(vsg::vec3(
            c.x + (h.x - c.x) * historyWeight, c.y + (h.y - c.y) * historyWeight, c.z + (h.z - c.z) * historyWeight));
    }

    // An unsharp mask on the plus-shaped neighbourhood in the compressed domain, each channel limited to the range of
    // the five samples so that it cannot ring or overshoot. Applied to the picture shown, never to the history.
    inline vsg::vec3 sharpenCross(const vsg::vec3& centre, const vsg::vec3& north, const vsg::vec3& south,
        const vsg::vec3& east, const vsg::vec3& west, float sharpness)
    {
        if (!(sharpness > 0.f))
            return centre;
        const vsg::vec3 c = compress(centre);
        const vsg::vec3 n = compress(north);
        const vsg::vec3 s = compress(south);
        const vsg::vec3 e = compress(east);
        const vsg::vec3 w = compress(west);
        auto one = [sharpness](float c0, float n0, float s0, float e0, float w0) {
            const float mean = 0.25f * (n0 + s0 + e0 + w0);
            const float lo = std::min({ c0, n0, s0, e0, w0 });
            const float hi = std::max({ c0, n0, s0, e0, w0 });
            return std::clamp(c0 + sharpness * (c0 - mean), lo, hi);
        };
        return expand(
            vsg::vec3(one(c.x, n.x, s.x, e.x, w.x), one(c.y, n.y, s.y, e.y, w.y), one(c.z, n.z, s.z, e.z, w.z)));
    }

    struct CatmullRom5
    {
        vsg::vec2 offset[5];
        float weight[5];
    };

    namespace Detail
    {
        // The four Catmull-Rom weights for the taps at -1, 0, 1 and 2 texels from the base texel.
        inline void catmullRomWeights(float t, float w[4])
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            w[0] = -0.5f * t3 + t2 - 0.5f * t;
            w[1] = 1.5f * t3 - 2.5f * t2 + 1.f;
            w[2] = -1.5f * t3 + 2.f * t2 + 0.5f * t;
            w[3] = 0.5f * t3 - 0.5f * t2;
        }
    }

    // The 5-tap form of a Catmull-Rom reconstruction read with bilinear filtering: the two middle taps of an axis are
    // merged into one bilinear tap at their weighted position, and the four corner taps (the smallest, and negative)
    // are dropped, with the rest renormalised. `fraction` is the sample's fractional texel coordinate after taking
    // the centre of the base texel (floor(position - 0.5)) as the origin; the offsets are from that texel's centre.
    inline CatmullRom5 catmullRom5(const vsg::vec2& fraction)
    {
        float wx[4];
        float wy[4];
        Detail::catmullRomWeights(fraction.x, wx);
        Detail::catmullRomWeights(fraction.y, wy);
        const float wx12 = wx[1] + wx[2];
        const float wy12 = wy[1] + wy[2];
        const float ox = wx[2] / wx12;
        const float oy = wy[2] / wy12;
        CatmullRom5 r;
        r.offset[0] = vsg::vec2(ox, oy);
        r.weight[0] = wx12 * wy12;
        r.offset[1] = vsg::vec2(-1.f, oy);
        r.weight[1] = wx[0] * wy12;
        r.offset[2] = vsg::vec2(2.f, oy);
        r.weight[2] = wx[3] * wy12;
        r.offset[3] = vsg::vec2(ox, -1.f);
        r.weight[3] = wx12 * wy[0];
        r.offset[4] = vsg::vec2(ox, 2.f);
        r.weight[4] = wx12 * wy[3];
        const float sum = r.weight[0] + r.weight[1] + r.weight[2] + r.weight[3] + r.weight[4];
        for (float& w : r.weight)
            w /= sum;
        return r;
    }

    inline vsg::vec2 previousUv(const vsg::vec2& uv, const vsg::vec2& velocity)
    {
        return { uv.x - velocity.x, uv.y - velocity.y };
    }

    // As velocity.frag: the view-space position of the pixel at `uv` whose surface is `linearDepth` in front of the
    // camera (view space looks down -Z); `projectionXY` are the projection's [0][0] and [1][1].
    inline vsg::vec3 viewPositionFromDepth(const vsg::vec2& uv, float linearDepth, const vsg::vec2& projectionXY)
    {
        return { (uv.x * 2.f - 1.f) * linearDepth / projectionXY.x, (uv.y * 2.f - 1.f) * linearDepth / projectionXY.y,
            -linearDepth };
    }

    // The w row of `toPreviousClip * (p, 1)`: for a perspective projection, the distance along the view axis in the
    // previous camera. (VSG indexes matrices (column, row).)
    inline float expectedPreviousDepth(const vsg::vec3& p, const vsg::mat4& m)
    {
        return m(0, 3) * p.x + m(1, 3) * p.y + m(2, 3) * p.z + m(3, 3);
    }

    // The sky has no depth and, being at infinity, no translation: the previous UV of its direction, found with the
    // rotation part of the same matrix (the direction has w = 0). False when the direction is behind the previous
    // camera.
    inline bool skyPreviousUv(const vsg::vec2& uv, const vsg::vec2& projectionXY, const vsg::mat4& m, vsg::vec2& out)
    {
        const float dx = (uv.x * 2.f - 1.f) / projectionXY.x;
        const float dy = (uv.y * 2.f - 1.f) / projectionXY.y;
        const float dz = -1.f;
        const float w = m(0, 3) * dx + m(1, 3) * dy + m(2, 3) * dz;
        if (!(w > 0.f))
            return false;
        const float x = m(0, 0) * dx + m(1, 0) * dy + m(2, 0) * dz;
        const float y = m(0, 1) * dx + m(1, 1) * dy + m(2, 1) * dz;
        out = vsg::vec2(x / w * 0.5f + 0.5f, y / w * 0.5f + 0.5f);
        return true;
    }
}

#endif
