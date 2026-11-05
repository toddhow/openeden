#ifndef OPENEDEN_RENDER_AOTEMPORALMATH_H
#define OPENEDEN_RENDER_AOTEMPORALMATH_H

// OpenEden (task 0076): the maths of the temporal accumulation of ambient occlusion. The pass's shader twin is
// files/shaders/post/aotemporal.slang; change both together. No game types. Docs: docs/tasks/0076-ao-temporal-
// accumulation.md and docs/research/0038-temporal-ambient-occlusion.md (sources named there; no code of any engine).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace Render::Ao::Temporal
{
    // Roberts' R2 recurrence (2018): alpha1 = 1 / p and alpha2 = 1 / p^2 for the plastic constant p (p^3 = p + 1). The
    // noise offset of a frame is the 2D point n * alpha (mod 1), which stays well spread over any few frames.
    constexpr std::uint32_t noiseCycle = 64;
    constexpr double noiseAlpha1 = 0.7548776662466927;
    constexpr double noiseAlpha2 = 0.5698402909980532;
    // The weight the history settles at (the setting's default and upper limit).
    constexpr float defaultHistoryWeight = 0.9f;
    constexpr float maxHistoryWeight = 0.98f;
    // The count of frames behind a value stops growing here (it only has to pass 1 / (1 - maxHistoryWeight) = 50).
    constexpr float maxCount = 255.f;
    // log2(1 + depth) differences: below the low one the depths agree (about 1.4 percent), above the high one it is
    // another surface (about 7 percent). The range of TAA's disocclusion test, a little tighter (it judges one tap).
    constexpr float depthRejectLow = 0.02f;
    constexpr float depthRejectHigh = 0.1f;
    // The dot product of the current normal (rotated into the previous view) and the stored one: 0.8 is 37 degrees,
    // 0.95 is 18 degrees. The bilateral weight of the spatial filter (Render::Ao::bilateralWeight) uses 0.707 and 0.9.
    constexpr float normalRejectLow = 0.8f;
    constexpr float normalRejectHigh = 0.95f;
    // The frames after a build that take nothing from the history (its images are put in their layout on them).
    constexpr std::uint32_t warmupFrames = 3;
    // The default width of the neighbourhood clamp in standard deviations: 0, none (decision 0012: the variance box is
    // covered by US10116916B2, so it is an opt-in; task 0076 stage d measured 1.0, Salvi 2016's gamma, as the best
    // width; frames: 1.5 left a trail behind a moving rigid object about a quarter bigger, 0.7 raised the mean level).
    constexpr float defaultClampGamma = 0.f;

    inline float fract(double x)
    {
        return static_cast<float>(x - std::floor(x));
    }

    inline vsg::vec2 noiseOffset(std::uint32_t frame)
    {
        const double n = static_cast<double>(frame % noiseCycle);
        return vsg::vec2(fract(0.5 + n * noiseAlpha1), fract(0.5 + n * noiseAlpha2));
    }

    // The pass adds the offset to the pixel's noise (both in [0, 1)) and wraps.
    inline float rotateNoise(float noise, float offset)
    {
        const float sum = noise + offset;
        return sum - std::floor(sum);
    }

    inline float smoothStep(float low, float high, float x)
    {
        const float t = std::clamp((x - low) / (high - low), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    inline float encodeDepth(float linearDepth)
    {
        return linearDepth > 0.f ? std::log2(1.f + linearDepth) : -1.f;
    }

    // 0: the stored depth is the surface that is here now, 1: another one. `expected` is the current surface's linear
    // depth in the previous camera, `stored` the history's encoded depth (-1: nothing was drawn there).
    inline float depthMismatch(float expectedPreviousLinearDepth, float storedEncodedDepth)
    {
        if (storedEncodedDepth < 0.f)
            return expectedPreviousLinearDepth > 0.f ? 1.f : 0.f;
        if (!(expectedPreviousLinearDepth > 0.f))
            return 1.f;
        return smoothStep(
            depthRejectLow, depthRejectHigh, std::abs(encodeDepth(expectedPreviousLinearDepth) - storedEncodedDepth));
    }

    inline float normalMatch(float dot)
    {
        return smoothStep(normalRejectLow, normalRejectHigh, dot);
    }

    // Octahedral encoding of a unit vector, 12 bits per axis, x in the high bits (4096 * x + y < 2^24, exact in a
    // float).
    constexpr std::uint32_t normalBits = 12;
    constexpr std::uint32_t normalLevels = (1u << normalBits) - 1; // 4095

    inline std::uint32_t packNormal(const vsg::vec3& normal)
    {
        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        float x = 0.f, y = 0.f, z = 1.f;
        if (length > 0.f)
        {
            x = normal.x / length;
            y = normal.y / length;
            z = normal.z / length;
        }
        const float l1 = std::abs(x) + std::abs(y) + std::abs(z);
        float u = x / l1;
        float v = y / l1;
        if (z < 0.f)
        {
            const float ou = u;
            u = (1.f - std::abs(v)) * (ou >= 0.f ? 1.f : -1.f);
            v = (1.f - std::abs(ou)) * (v >= 0.f ? 1.f : -1.f);
        }
        const auto quantise = [](float value) {
            const float scaled = (std::clamp(value, -1.f, 1.f) * 0.5f + 0.5f) * static_cast<float>(normalLevels);
            return static_cast<std::uint32_t>(std::lround(scaled));
        };
        return quantise(u) * (normalLevels + 1) + quantise(v);
    }

    inline vsg::vec3 unpackNormal(std::uint32_t packed)
    {
        const std::uint32_t qu = (packed / (normalLevels + 1)) & normalLevels;
        const std::uint32_t qv = packed & normalLevels;
        const float u = static_cast<float>(qu) / static_cast<float>(normalLevels) * 2.f - 1.f;
        const float v = static_cast<float>(qv) / static_cast<float>(normalLevels) * 2.f - 1.f;
        float x = u;
        float y = v;
        const float z = 1.f - std::abs(u) - std::abs(v);
        float zz = z;
        if (z < 0.f)
        {
            x = (1.f - std::abs(v)) * (u >= 0.f ? 1.f : -1.f);
            y = (1.f - std::abs(u)) * (v >= 0.f ? 1.f : -1.f);
            zz = z;
        }
        const float length = std::sqrt(x * x + y * y + zz * zz);
        return length > 0.f ? vsg::vec3(x / length, y / length, zz / length) : vsg::vec3(0.f, 0.f, 1.f);
    }

    // Row major: result[i] = sum_j m[i][j] * v[j].
    struct Rotation3
    {
        float m[3][3];
    };

    // R_previous * transpose(R_current), the rotation parts of the two view matrices: it takes a direction in the
    // current view space to the previous one. vsg indexes (column, row).
    inline Rotation3 relativeRotation(const vsg::dmat4& currentView, const vsg::dmat4& previousView)
    {
        Rotation3 result{};
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k)
                    sum += previousView(k, i) * currentView(k, j);
                result.m[i][j] = static_cast<float>(sum);
            }
        }
        return result;
    }

    inline vsg::vec3 apply(const Rotation3& rotation, const vsg::vec3& v)
    {
        return vsg::vec3(rotation.m[0][0] * v.x + rotation.m[0][1] * v.y + rotation.m[0][2] * v.z,
            rotation.m[1][0] * v.x + rotation.m[1][1] * v.y + rotation.m[1][2] * v.z,
            rotation.m[2][0] * v.x + rotation.m[2][1] * v.y + rotation.m[2][2] * v.z);
    }

    // The TAA jitter of a frame shifts the picture by jitterPixels (clip x += 2 jx / width * w, so uv += jx / width).
    // The surface at uv now was at uv - velocity (an unjittered camera's), and its history texel was written with the
    // previous frame's shift: the position to read is uv - velocity + (previous - current).
    inline vsg::vec2 jitterDeltaUv(const vsg::dvec2& jitterPixels, const vsg::dvec2& previousJitterPixels,
        std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return vsg::vec2(0.f, 0.f);
        return vsg::vec2(static_cast<float>((previousJitterPixels.x - jitterPixels.x) / width),
            static_cast<float>((previousJitterPixels.y - jitterPixels.y) / height));
    }

    inline vsg::vec2 historyUv(const vsg::vec2& uv, const vsg::vec2& velocity, const vsg::vec2& jitterDelta)
    {
        return vsg::vec2(uv.x - velocity.x + jitterDelta.x, uv.y - velocity.y + jitterDelta.y);
    }

    struct Tap
    {
        int x;
        int y;
        float weight;
    };

    inline std::array<Tap, 4> bilinearTaps(const vsg::vec2& uv, std::uint32_t width, std::uint32_t height)
    {
        const float px = uv.x * static_cast<float>(width) - 0.5f;
        const float py = uv.y * static_cast<float>(height) - 0.5f;
        const float fx0 = std::floor(px);
        const float fy0 = std::floor(py);
        const float fx = px - fx0;
        const float fy = py - fy0;
        const int x0 = static_cast<int>(fx0);
        const int y0 = static_cast<int>(fy0);
        const float w[4] = { (1.f - fx) * (1.f - fy), fx * (1.f - fy), (1.f - fx) * fy, fx * fy };
        const int dx[4] = { 0, 1, 0, 1 };
        const int dy[4] = { 0, 0, 1, 1 };
        const int maxX = static_cast<int>(width) - 1;
        const int maxY = static_cast<int>(height) - 1;
        std::array<Tap, 4> taps{};
        for (int i = 0; i < 4; ++i)
        {
            const int x = x0 + dx[i];
            const int y = y0 + dy[i];
            const bool inside = x >= 0 && y >= 0 && x <= maxX && y <= maxY;
            taps[i]
                = Tap{ std::clamp(x, 0, std::max(maxX, 0)), std::clamp(y, 0, std::max(maxY, 0)), inside ? w[i] : 0.f };
        }
        return taps;
    }

    inline float tapWeight(float bilinear, float depthMismatchAmount, float normalMatchAmount)
    {
        return bilinear * (1.f - depthMismatchAmount) * normalMatchAmount;
    }

    // `count` is the number of frames behind the history value, `confidence` the share of the history that survived.
    inline float historyBlend(float baseWeight, float count, float confidence)
    {
        if (!(count > 0.f) || !(confidence > 0.f))
            return 0.f;
        const float base = std::clamp(baseWeight, 0.f, maxHistoryWeight);
        return confidence * std::min(base, count / (count + 1.f));
    }

    inline float nextCount(float count, float confidence)
    {
        return std::min(1.f + std::max(count, 0.f) * std::clamp(confidence, 0.f, 1.f), maxCount);
    }

    inline float accumulate(float current, float history, float blend)
    {
        return current + (history - current) * std::clamp(blend, 0.f, 1.f);
    }

    struct Range
    {
        float lo;
        float hi;
    };

    inline Range varianceRange(float mean, float meanOfSquares, float gamma)
    {
        if (!(gamma > 0.f))
            return Range{ 0.f, 1.f };
        const float deviation = std::sqrt(std::max(meanOfSquares - mean * mean, 0.f));
        return Range{ mean - gamma * deviation, mean + gamma * deviation };
    }

    inline float clampToRange(float value, const Range& range)
    {
        return std::clamp(value, range.lo, range.hi);
    }

    inline bool useAoTemporal(bool settingOn, bool aoPass, bool gbuffer)
    {
        return settingOn && aoPass && gbuffer;
    }

    enum class DebugView
    {
        None,
        Raw,
        Weight,
        Reject,
        Static
    };

    inline std::optional<DebugView> parseDebugView(std::string_view name)
    {
        if (name == "none")
            return DebugView::None;
        if (name == "raw")
            return DebugView::Raw;
        if (name == "weight")
            return DebugView::Weight;
        if (name == "reject")
            return DebugView::Reject;
        if (name == "static")
            return DebugView::Static;
        return std::nullopt;
    }

    inline bool historyUsable(bool reset, std::uint32_t framesSinceBuild)
    {
        return !reset && framesSinceBuild >= warmupFrames;
    }
}

#endif
