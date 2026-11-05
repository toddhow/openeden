// Tests for the pure maths of the temporal accumulation of ambient occlusion, Render::Ao::Temporal (task 0076). Written
// from the task spec alone ("Interfaces (pure parts, for tests written from this spec alone)" and "Design" of
// docs/tasks/0076-ao-temporal-accumulation.md), without seeing any implementation. Every expected number is worked out
// by hand from the spec's formulas, with the arithmetic in a comment. Pure CPU maths: no device, window or game data.

#include <components/render/aotemporalmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace
{
    using namespace Render::Ao::Temporal;

    constexpr float tol = 1e-5f;

    void expectVec2Near(const vsg::vec2& actual, const vsg::vec2& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
    }

    void expectVec3Near(const vsg::vec3& actual, const vsg::vec3& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    // The angle in radians between two vectors, in double. atan2 of the cross product's length and the dot product
    // stays accurate for tiny angles, where acos of a float dot product loses everything.
    double angleBetween(const vsg::vec3& a, const vsg::vec3& b)
    {
        const double ax = a.x;
        const double ay = a.y;
        const double az = a.z;
        const double bx = b.x;
        const double by = b.y;
        const double bz = b.z;
        const double cx = ay * bz - az * by;
        const double cy = az * bx - ax * bz;
        const double cz = ax * by - ay * bx;
        const double crossLength = std::sqrt(cx * cx + cy * cy + cz * cz);
        const double dot = ax * bx + ay * by + az * bz;
        return std::atan2(crossLength, dot);
    }

    vsg::vec3 normalised(double x, double y, double z)
    {
        const double length = std::sqrt(x * x + y * y + z * z);
        return vsg::vec3(
            static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length));
    }

    // A fixed linear congruential generator (Numerical Recipes constants), so that "random" inputs are the same on
    // every machine and every run. It returns values in [-1, 1).
    class Lcg
    {
    public:
        float next()
        {
            mState = mState * 1664525u + 1013904223u;
            return static_cast<float>(mState >> 8) / 8388608.f - 1.f;
        }

    private:
        std::uint32_t mState = 12345u;
    };

    double fractionalPart(double v)
    {
        return v - std::floor(v);
    }

    // Row i of a Rotation3 as a vector.
    vsg::vec3 row(const Rotation3& r, int i)
    {
        return vsg::vec3(r.m[i][0], r.m[i][1], r.m[i][2]);
    }

    float dot3(const vsg::vec3& a, const vsg::vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    void expectOrthonormal(const Rotation3& r)
    {
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(dot3(row(r, i), row(r, i)), 1.f, tol) << "row " << i;
            for (int j = i + 1; j < 3; ++j)
            {
                EXPECT_NEAR(dot3(row(r, i), row(r, j)), 0.f, tol) << "rows " << i << " and " << j;
            }
        }
    }

    // The rotation of `angle` radians about the unit `axis` applied to a direction, as the matrix does it.
    vsg::vec3 rotateDirection(const vsg::dmat4& rotation, const vsg::vec3& v)
    {
        const vsg::dvec4 r = rotation * vsg::dvec4(v.x, v.y, v.z, 0.0);
        return vsg::vec3(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.z));
    }

    // Runs the history loop of one pixel: blend, accumulate, count. Frame 0 starts with count 0 and no history.
    std::vector<float> runAccumulation(const std::vector<float>& samples, float base, float confidence)
    {
        std::vector<float> values;
        float value = 0.f;
        float count = 0.f;
        for (const float sample : samples)
        {
            const float blend = historyBlend(base, count, confidence);
            value = accumulate(sample, value, blend);
            count = nextCount(count, confidence);
            values.push_back(value);
        }
        return values;
    }

    double varianceOf(const std::vector<float>& values, std::size_t begin, std::size_t end)
    {
        double mean = 0.0;
        for (std::size_t i = begin; i < end; ++i)
        {
            mean += values[i];
        }
        mean /= static_cast<double>(end - begin);
        double sum = 0.0;
        for (std::size_t i = begin; i < end; ++i)
        {
            const double d = values[i] - mean;
            sum += d * d;
        }
        return sum / static_cast<double>(end - begin);
    }

    // ---- constants ---------------------------------------------------------------------------------------------

    TEST(RenderAoTemporalConstants, hold_the_values_the_spec_lists)
    {
        EXPECT_EQ(noiseCycle, 64u);
        EXPECT_FLOAT_EQ(defaultHistoryWeight, 0.9f);
        EXPECT_FLOAT_EQ(maxHistoryWeight, 0.98f);
        EXPECT_FLOAT_EQ(maxCount, 255.f);
        EXPECT_FLOAT_EQ(depthRejectLow, 0.02f);
        EXPECT_FLOAT_EQ(depthRejectHigh, 0.1f);
        EXPECT_FLOAT_EQ(normalRejectLow, 0.8f);
        EXPECT_FLOAT_EQ(normalRejectHigh, 0.95f);
        EXPECT_EQ(warmupFrames, 3u);
        EXPECT_FLOAT_EQ(defaultClampGamma, 0.f);
    }

    // ---- noise offset ------------------------------------------------------------------------------------------

    TEST(RenderAoTemporalNoise, the_constants_are_the_inverse_powers_of_the_plastic_constant)
    {
        // p^3 = p + 1 has the real root p = cbrt(1 + p); the fixed point iteration converges quickly.
        double p = 1.0;
        for (int i = 0; i < 200; ++i)
        {
            p = std::cbrt(1.0 + p);
        }
        EXPECT_NEAR(p * p * p, p + 1.0, 1e-12);
        EXPECT_NEAR(noiseAlpha1, 1.0 / p, 1e-12);
        EXPECT_NEAR(noiseAlpha2, 1.0 / (p * p), 1e-12);
    }

    TEST(RenderAoTemporalNoise, frame_zero_is_the_middle_of_the_unit_square)
    {
        // n = 0: frac(0.5 + 0) = 0.5 on both axes.
        const vsg::vec2 o = noiseOffset(0);
        EXPECT_NEAR(o.x, 0.5f, 1e-6f);
        EXPECT_NEAR(o.y, 0.5f, 1e-6f);
    }

    TEST(RenderAoTemporalNoise, frame_one_adds_one_step_of_each_constant)
    {
        // n = 1: x = frac(0.5 + 0.7548776662466927) = 0.2548776662466927,
        //        y = frac(0.5 + 0.5698402909980532) = 0.0698402909980532.
        const vsg::vec2 o = noiseOffset(1);
        EXPECT_NEAR(o.x, 0.2548776662466927f, 1e-6f);
        EXPECT_NEAR(o.y, 0.0698402909980532f, 1e-6f);
    }

    TEST(RenderAoTemporalNoise, frame_seven_matches_the_hand_arithmetic)
    {
        // n = 7: 7 * 0.7548776662466927 = 5.2841436637268489; + 0.5 = 5.7841436637268489; frac = 0.78414366.
        //        7 * 0.5698402909980532 = 3.9888820369863724; + 0.5 = 4.4888820369863724; frac = 0.48888204.
        const vsg::vec2 o = noiseOffset(7);
        EXPECT_NEAR(o.x, 0.78414366f, 1e-6f);
        EXPECT_NEAR(o.y, 0.48888204f, 1e-6f);
    }

    TEST(RenderAoTemporalNoise, both_components_stay_in_the_half_open_unit_interval)
    {
        for (std::uint32_t frame = 0; frame < 1000; ++frame)
        {
            const vsg::vec2 o = noiseOffset(frame);
            EXPECT_GE(o.x, 0.f) << frame;
            EXPECT_LT(o.x, 1.f) << frame;
            EXPECT_GE(o.y, 0.f) << frame;
            EXPECT_LT(o.y, 1.f) << frame;
        }
    }

    TEST(RenderAoTemporalNoise, the_sequence_repeats_after_the_cycle)
    {
        for (std::uint32_t frame = 0; frame < 200; ++frame)
        {
            const vsg::vec2 a = noiseOffset(frame);
            const vsg::vec2 b = noiseOffset(frame + noiseCycle);
            EXPECT_EQ(a.x, b.x) << frame;
            EXPECT_EQ(a.y, b.y) << frame;
        }
        // Frame 5 and frame 69 are the same offset.
        EXPECT_EQ(noiseOffset(5).x, noiseOffset(69).x);
        EXPECT_EQ(noiseOffset(5).y, noiseOffset(69).y);
    }

    TEST(RenderAoTemporalNoise, consecutive_frames_differ)
    {
        for (std::uint32_t frame = 0; frame < 200; ++frame)
        {
            const vsg::vec2 a = noiseOffset(frame);
            const vsg::vec2 b = noiseOffset(frame + 1);
            EXPECT_TRUE(a.x != b.x || a.y != b.y) << frame;
        }
    }

    TEST(RenderAoTemporalNoise, one_cycle_is_spread_evenly)
    {
        std::set<float> xs;
        std::set<float> ys;
        double sumX = 0.0;
        double sumY = 0.0;
        for (std::uint32_t frame = 0; frame < noiseCycle; ++frame)
        {
            const vsg::vec2 o = noiseOffset(frame);
            xs.insert(o.x);
            ys.insert(o.y);
            sumX += o.x;
            sumY += o.y;
        }
        // Irrational steps never revisit a value within 64 frames.
        EXPECT_EQ(xs.size(), static_cast<std::size_t>(noiseCycle));
        EXPECT_EQ(ys.size(), static_cast<std::size_t>(noiseCycle));
        // A low discrepancy sequence has its mean near the middle.
        EXPECT_NEAR(sumX / noiseCycle, 0.5, 0.05);
        EXPECT_NEAR(sumY / noiseCycle, 0.5, 0.05);
    }

    TEST(RenderAoTemporalNoise, the_offset_is_the_fractional_part_of_the_formula)
    {
        // Compares with the spec's formula written out in double for every frame of a cycle.
        for (std::uint32_t n = 0; n < noiseCycle; ++n)
        {
            const vsg::vec2 o = noiseOffset(n);
            EXPECT_NEAR(o.x, fractionalPart(0.5 + n * noiseAlpha1), 1e-6) << n;
            EXPECT_NEAR(o.y, fractionalPart(0.5 + n * noiseAlpha2), 1e-6) << n;
        }
    }

    TEST(RenderAoTemporalRotateNoise, an_offset_of_zero_returns_the_noise)
    {
        for (int i = 0; i < 16; ++i)
        {
            const float noise = static_cast<float>(i) / 16.f;
            EXPECT_EQ(rotateNoise(noise, 0.f), noise) << i;
        }
        EXPECT_EQ(rotateNoise(0.3f, 0.f), 0.3f);
    }

    TEST(RenderAoTemporalRotateNoise, wraps_past_one)
    {
        // 0.75 + 0.5 = 1.25; frac = 0.25 (all three are exact in binary).
        EXPECT_FLOAT_EQ(rotateNoise(0.75f, 0.5f), 0.25f);
        // 0.5 + 0.5 = 1.0; frac = 0.
        EXPECT_FLOAT_EQ(rotateNoise(0.5f, 0.5f), 0.f);
        // No wrap: 0.25 + 0.5 = 0.75.
        EXPECT_FLOAT_EQ(rotateNoise(0.25f, 0.5f), 0.75f);
    }

    TEST(RenderAoTemporalRotateNoise, results_stay_in_the_half_open_unit_interval)
    {
        // A grid in sixteenths: every sum is exact, so the expected value is ((i + j) mod 16) / 16.
        for (int i = 0; i < 16; ++i)
        {
            for (int j = 0; j < 16; ++j)
            {
                const float result = rotateNoise(static_cast<float>(i) / 16.f, static_cast<float>(j) / 16.f);
                EXPECT_GE(result, 0.f);
                EXPECT_LT(result, 1.f);
                EXPECT_FLOAT_EQ(result, static_cast<float>((i + j) % 16) / 16.f) << i << ", " << j;
            }
        }
    }

    // ---- depth and normal agreement ----------------------------------------------------------------------------

    TEST(RenderAoTemporalDepth, encodeDepth_is_log2_of_one_plus_the_depth)
    {
        EXPECT_NEAR(encodeDepth(1.f), 1.f, 1e-6f); // log2(2)
        EXPECT_NEAR(encodeDepth(3.f), 2.f, 1e-6f); // log2(4)
        EXPECT_NEAR(encodeDepth(7.f), 3.f, 1e-6f); // log2(8)
        EXPECT_NEAR(encodeDepth(255.f), 8.f, 1e-5f); // log2(256)
    }

    TEST(RenderAoTemporalDepth, encodeDepth_marks_no_surface_as_minus_one)
    {
        EXPECT_EQ(encodeDepth(0.f), -1.f);
        EXPECT_EQ(encodeDepth(-5.f), -1.f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_with_nothing_stored)
    {
        // stored < 0 and something expected: another surface.
        EXPECT_EQ(depthMismatch(3.f, -1.f), 1.f);
        EXPECT_EQ(depthMismatch(0.001f, -1.f), 1.f);
        // stored < 0 and nothing expected: the same (empty) place.
        EXPECT_EQ(depthMismatch(0.f, -1.f), 0.f);
        EXPECT_EQ(depthMismatch(-4.f, -1.f), 0.f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_with_nothing_expected_but_something_stored)
    {
        EXPECT_EQ(depthMismatch(0.f, 0.f), 1.f);
        EXPECT_EQ(depthMismatch(0.f, 2.f), 1.f);
        EXPECT_EQ(depthMismatch(-1.f, 2.f), 1.f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_is_zero_for_equal_depths)
    {
        // expected 3 encodes to log2(4) = 2.
        EXPECT_NEAR(depthMismatch(3.f, 2.f), 0.f, 1e-6f);
        EXPECT_NEAR(depthMismatch(1.f, encodeDepth(1.f)), 0.f, 1e-6f);
        EXPECT_NEAR(depthMismatch(1000.f, encodeDepth(1000.f)), 0.f, 1e-6f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_ignores_a_difference_up_to_the_low_limit)
    {
        // expected 3 encodes to 2. A difference of 0.01 or 0.02 is below or at depthRejectLow.
        EXPECT_NEAR(depthMismatch(3.f, 2.01f), 0.f, 1e-6f);
        EXPECT_NEAR(depthMismatch(3.f, 2.f + depthRejectLow), 0.f, 1e-4f);
        EXPECT_NEAR(depthMismatch(3.f, 1.99f), 0.f, 1e-6f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_rejects_a_difference_from_the_high_limit)
    {
        EXPECT_NEAR(depthMismatch(3.f, 2.f + depthRejectHigh), 1.f, 1e-4f);
        EXPECT_NEAR(depthMismatch(3.f, 2.2f), 1.f, 1e-6f);
        EXPECT_NEAR(depthMismatch(3.f, 1.8f), 1.f, 1e-6f);
        EXPECT_NEAR(depthMismatch(3.f, 5.f), 1.f, 1e-6f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_between_the_limits_is_the_smoothstep)
    {
        // A difference of 0.06 is halfway from 0.02 to 0.1: t = 0.5, smoothstep(0.5) = 0.5. Either sign of the
        // difference gives the same answer (the spec takes the absolute value).
        EXPECT_NEAR(depthMismatch(3.f, 2.06f), 0.5f, 1e-3f);
        EXPECT_NEAR(depthMismatch(3.f, 1.94f), 0.5f, 1e-3f);
        // A difference of 0.04: t = 0.25, smoothstep = 0.25^2 * (3 - 2 * 0.25) = 0.0625 * 2.5 = 0.15625.
        EXPECT_NEAR(depthMismatch(3.f, 2.04f), 0.15625f, 1e-3f);
        // A difference of 0.08: t = 0.75, smoothstep = 0.5625 * (3 - 1.5) = 0.84375.
        EXPECT_NEAR(depthMismatch(3.f, 2.08f), 0.84375f, 1e-3f);
    }

    TEST(RenderAoTemporalDepth, depthMismatch_never_decreases_as_the_difference_grows)
    {
        float previous = -1.f;
        for (int k = 0; k <= 40; ++k)
        {
            const float stored = 2.f + 0.005f * static_cast<float>(k);
            const float m = depthMismatch(3.f, stored);
            EXPECT_GE(m, 0.f);
            EXPECT_LE(m, 1.f);
            EXPECT_GE(m, previous) << stored;
            previous = m;
        }
        // Strictly inside the open range at a difference between the limits.
        const float inside = depthMismatch(3.f, 2.05f);
        EXPECT_GT(inside, 0.f);
        EXPECT_LT(inside, 1.f);
    }

    TEST(RenderAoTemporalNormal, normalMatch_is_zero_at_or_below_the_low_limit)
    {
        EXPECT_NEAR(normalMatch(normalRejectLow), 0.f, 1e-6f);
        EXPECT_EQ(normalMatch(0.5f), 0.f);
        EXPECT_EQ(normalMatch(0.f), 0.f);
        EXPECT_EQ(normalMatch(-1.f), 0.f);
    }

    TEST(RenderAoTemporalNormal, normalMatch_is_one_at_or_above_the_high_limit)
    {
        EXPECT_NEAR(normalMatch(normalRejectHigh), 1.f, 1e-6f);
        EXPECT_EQ(normalMatch(1.f), 1.f);
        EXPECT_EQ(normalMatch(0.99f), 1.f);
    }

    TEST(RenderAoTemporalNormal, normalMatch_is_the_smoothstep_between_the_limits)
    {
        // The midpoint 0.875: t = (0.875 - 0.8) / 0.15 = 0.5, smoothstep(0.5) = 0.5.
        EXPECT_NEAR(normalMatch(0.875f), 0.5f, 1e-4f);
        // 0.8375: t = 0.25, smoothstep = 0.15625. 0.9125: t = 0.75, smoothstep = 0.84375.
        EXPECT_NEAR(normalMatch(0.8375f), 0.15625f, 1e-4f);
        EXPECT_NEAR(normalMatch(0.9125f), 0.84375f, 1e-4f);
    }

    TEST(RenderAoTemporalNormal, normalMatch_never_decreases_as_the_dot_grows)
    {
        float previous = -1.f;
        for (int k = 0; k <= 100; ++k)
        {
            const float d = 0.75f + 0.0025f * static_cast<float>(k);
            const float m = normalMatch(d);
            EXPECT_GE(m, 0.f);
            EXPECT_LE(m, 1.f);
            EXPECT_GE(m, previous) << d;
            previous = m;
        }
    }

    // ---- packed normals ----------------------------------------------------------------------------------------

    constexpr std::uint32_t twoToThe24 = 1u << 24;
    constexpr double maxNormalError = 0.002;

    TEST(RenderAoTemporalPackNormal, axes_survive_the_round_trip)
    {
        const std::array<vsg::vec3, 6> axes = { vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(-1.f, 0.f, 0.f),
            vsg::vec3(0.f, 1.f, 0.f), vsg::vec3(0.f, -1.f, 0.f), vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(0.f, 0.f, -1.f) };
        for (const vsg::vec3& axis : axes)
        {
            const std::uint32_t packed = packNormal(axis);
            EXPECT_LT(packed, twoToThe24);
            EXPECT_LT(angleBetween(unpackNormal(packed), axis), maxNormalError)
                << axis.x << ", " << axis.y << ", " << axis.z;
        }
    }

    TEST(RenderAoTemporalPackNormal, diagonals_and_negative_z_survive_the_round_trip)
    {
        std::vector<vsg::vec3> normals;
        for (const double sx : { -1.0, 1.0 })
        {
            for (const double sy : { -1.0, 1.0 })
            {
                for (const double sz : { -1.0, 1.0 })
                {
                    normals.push_back(normalised(sx, sy, sz));
                }
            }
        }
        normals.push_back(normalised(0.3, 0.2, -0.9));
        normals.push_back(normalised(-0.7, 0.1, -0.2));
        normals.push_back(normalised(0.01, -0.02, -1.0));
        normals.push_back(normalised(1.0, 1.0, 0.0));
        normals.push_back(normalised(-1.0, 0.5, 0.0));
        for (const vsg::vec3& n : normals)
        {
            const std::uint32_t packed = packNormal(n);
            EXPECT_LT(packed, twoToThe24);
            EXPECT_LT(angleBetween(unpackNormal(packed), n), maxNormalError) << n.x << ", " << n.y << ", " << n.z;
        }
    }

    TEST(RenderAoTemporalPackNormal, two_thousand_pseudo_random_unit_vectors_survive_the_round_trip)
    {
        Lcg lcg;
        int tested = 0;
        double worst = 0.0;
        while (tested < 2000)
        {
            const double x = lcg.next();
            const double y = lcg.next();
            const double z = lcg.next();
            if (x * x + y * y + z * z < 0.01)
            {
                continue;
            }
            const vsg::vec3 n = normalised(x, y, z);
            const std::uint32_t packed = packNormal(n);
            ASSERT_LT(packed, twoToThe24);
            worst = std::max(worst, angleBetween(unpackNormal(packed), n));
            ++tested;
        }
        EXPECT_LT(worst, maxNormalError);
    }

    TEST(RenderAoTemporalPackNormal, the_unpacked_vector_is_a_unit_vector)
    {
        Lcg lcg;
        for (int i = 0; i < 200; ++i)
        {
            const vsg::vec3 n = normalised(lcg.next() + 2.0, lcg.next(), lcg.next());
            const vsg::vec3 u = unpackNormal(packNormal(n));
            EXPECT_NEAR(std::sqrt(dot3(u, u)), 1.f, 1e-4f);
        }
    }

    TEST(RenderAoTemporalPackNormal, the_zero_vector_packs_as_straight_up)
    {
        const std::uint32_t packed = packNormal(vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_LT(packed, twoToThe24);
        EXPECT_EQ(packed, packNormal(vsg::vec3(0.f, 0.f, 1.f)));
        EXPECT_LT(angleBetween(unpackNormal(packed), vsg::vec3(0.f, 0.f, 1.f)), maxNormalError);
    }

    TEST(RenderAoTemporalPackNormal, an_input_that_is_not_unit_length_is_normalised_first)
    {
        // Scaling an axis changes nothing, exactly.
        EXPECT_EQ(packNormal(vsg::vec3(0.f, 0.f, 5.f)), packNormal(vsg::vec3(0.f, 0.f, 1.f)));
        EXPECT_EQ(packNormal(vsg::vec3(3.f, 0.f, 0.f)), packNormal(vsg::vec3(1.f, 0.f, 0.f)));
        EXPECT_EQ(packNormal(vsg::vec3(0.f, -0.25f, 0.f)), packNormal(vsg::vec3(0.f, -1.f, 0.f)));
        // (2, 4, -4) has length 6, so its direction is (1/3, 2/3, -2/3).
        const vsg::vec3 expected = normalised(2.0, 4.0, -4.0);
        EXPECT_LT(angleBetween(unpackNormal(packNormal(vsg::vec3(2.f, 4.f, -4.f))), expected), maxNormalError);
        // Tiny but non-zero vectors still give their direction.
        EXPECT_LT(angleBetween(unpackNormal(packNormal(vsg::vec3(1e-3f, 0.f, 0.f))), vsg::vec3(1.f, 0.f, 0.f)),
            maxNormalError);
    }

    // ---- relative rotation -------------------------------------------------------------------------------------

    vsg::dmat4 viewA()
    {
        return vsg::lookAt(vsg::dvec3(10.0, -5.0, 3.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
    }

    vsg::dmat4 viewB()
    {
        return vsg::lookAt(vsg::dvec3(-3.0, 8.0, 20.0), vsg::dvec3(1.0, 2.0, 3.0), vsg::dvec3(0.0, 1.0, 0.0));
    }

    TEST(RenderAoTemporalRelativeRotation, equal_views_give_the_identity)
    {
        for (const vsg::dmat4& view : { viewA(), viewB() })
        {
            const Rotation3 r = relativeRotation(view, view);
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    EXPECT_NEAR(r.m[i][j], i == j ? 1.f : 0.f, tol) << i << ", " << j;
                }
            }
        }
    }

    TEST(RenderAoTemporalRelativeRotation, a_view_rotated_by_a_known_rotation_gives_that_rotation)
    {
        const double inverseRoot14 = 1.0 / std::sqrt(14.0);
        const vsg::dvec3 diagonalAxis(1.0 * inverseRoot14, 2.0 * inverseRoot14, 3.0 * inverseRoot14);
        const std::vector<std::pair<double, vsg::dvec3>> rotations
            = { { 0.3, vsg::dvec3(0.0, 0.0, 1.0) }, { 1.1, vsg::dvec3(1.0, 0.0, 0.0) },
                  { -0.8, vsg::dvec3(0.0, 1.0, 0.0) }, { 2.5, diagonalAxis }, { -0.05, diagonalAxis } };
        const std::vector<vsg::vec3> probes = { vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(0.f, 1.f, 0.f),
            vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(0.3f, -0.4f, 0.5f), vsg::vec3(-2.f, 1.f, 3.f) };

        for (const auto& [angle, axis] : rotations)
        {
            const vsg::dmat4 rotation = vsg::rotate(angle, axis);
            const vsg::dmat4 current = viewA();
            const vsg::dmat4 previous = rotation * current;
            const Rotation3 r = relativeRotation(current, previous);
            for (const vsg::vec3& v : probes)
            {
                expectVec3Near(apply(r, v), rotateDirection(rotation, v), tol);
            }
        }
    }

    TEST(RenderAoTemporalRelativeRotation, the_eye_position_does_not_change_the_rotation)
    {
        const vsg::dmat4 rotation = vsg::rotate(0.9, vsg::dvec3(0.0, 0.0, 1.0));
        const Rotation3 a = relativeRotation(viewA(), rotation * viewA());
        const Rotation3 b = relativeRotation(viewB(), rotation * viewB());
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                EXPECT_NEAR(a.m[i][j], b.m[i][j], tol) << i << ", " << j;
            }
        }
    }

    TEST(RenderAoTemporalRelativeRotation, a_pure_translation_between_the_views_gives_the_identity)
    {
        const vsg::dmat4 current = viewA();
        vsg::dmat4 previous = viewA();
        // The translation is in the fourth column (vsg indexes (column, row)).
        previous(3, 0) += 100.0;
        previous(3, 1) -= 25.0;
        previous(3, 2) += 7.0;
        const Rotation3 r = relativeRotation(current, previous);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                EXPECT_NEAR(r.m[i][j], i == j ? 1.f : 0.f, tol) << i << ", " << j;
            }
        }
    }

    TEST(RenderAoTemporalRelativeRotation, the_result_is_orthonormal)
    {
        expectOrthonormal(relativeRotation(viewA(), viewB()));
        expectOrthonormal(relativeRotation(viewB(), viewA()));
        expectOrthonormal(relativeRotation(viewA(), vsg::rotate(1.3, vsg::dvec3(0.0, 1.0, 0.0)) * viewA()));
        expectOrthonormal(relativeRotation(viewA(), viewA()));
    }

    TEST(RenderAoTemporalRelativeRotation, the_opposite_order_gives_the_transpose)
    {
        const Rotation3 forward = relativeRotation(viewA(), viewB());
        const Rotation3 backward = relativeRotation(viewB(), viewA());
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                EXPECT_NEAR(forward.m[i][j], backward.m[j][i], tol) << i << ", " << j;
            }
        }
    }

    TEST(RenderAoTemporalApply, uses_the_rows_of_the_matrix)
    {
        // result[i] = sum_j m[i][j] * v[j]. A quarter turn about z: (x, y, z) -> (-y, x, z).
        const Rotation3 r{ { { 0.f, -1.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 0.f, 1.f } } };
        expectVec3Near(apply(r, vsg::vec3(1.f, 2.f, 3.f)), vsg::vec3(-2.f, 1.f, 3.f), 1e-6f);
        // A matrix that is not symmetric and not a rotation shows the row-major order:
        // row 0 = (1, 2, 3) . (1, 1, 1) = 6; row 1 = (4, 5, 6) . (1, 1, 1) = 15; row 2 = (7, 8, 9) . (1, 1, 1) = 24.
        const Rotation3 s{ { { 1.f, 2.f, 3.f }, { 4.f, 5.f, 6.f }, { 7.f, 8.f, 9.f } } };
        expectVec3Near(apply(s, vsg::vec3(1.f, 1.f, 1.f)), vsg::vec3(6.f, 15.f, 24.f), 1e-5f);
    }

    // ---- jitter and history position ---------------------------------------------------------------------------

    TEST(RenderAoTemporalJitter, delta_is_previous_minus_current_over_the_extent)
    {
        // x: (0.5 - 0.25) / 100 = 0.0025. y: (-0.25 - 0.25) / 200 = -0.0025.
        const vsg::vec2 d = jitterDeltaUv(vsg::dvec2(0.25, 0.25), vsg::dvec2(0.5, -0.25), 100, 200);
        expectVec2Near(d, vsg::vec2(0.0025f, -0.0025f), 1e-7f);
    }

    TEST(RenderAoTemporalJitter, the_sign_follows_previous_minus_current)
    {
        const vsg::vec2 a = jitterDeltaUv(vsg::dvec2(1.0, 1.0), vsg::dvec2(0.0, 0.0), 10, 10);
        EXPECT_NEAR(a.x, -0.1f, 1e-6f);
        EXPECT_NEAR(a.y, -0.1f, 1e-6f);
        const vsg::vec2 b = jitterDeltaUv(vsg::dvec2(0.0, 0.0), vsg::dvec2(1.0, 1.0), 10, 10);
        EXPECT_NEAR(b.x, 0.1f, 1e-6f);
        EXPECT_NEAR(b.y, 0.1f, 1e-6f);
    }

    TEST(RenderAoTemporalJitter, equal_jitters_give_no_delta)
    {
        const vsg::vec2 d = jitterDeltaUv(vsg::dvec2(0.3, -0.7), vsg::dvec2(0.3, -0.7), 1280, 720);
        EXPECT_EQ(d.x, 0.f);
        EXPECT_EQ(d.y, 0.f);
    }

    TEST(RenderAoTemporalJitter, a_zero_extent_gives_zero)
    {
        const vsg::vec2 d = jitterDeltaUv(vsg::dvec2(0.25, 0.25), vsg::dvec2(0.5, -0.25), 0, 0);
        EXPECT_EQ(d.x, 0.f);
        EXPECT_EQ(d.y, 0.f);
    }

    TEST(RenderAoTemporalHistoryUv, is_uv_minus_velocity_plus_the_jitter_delta)
    {
        // x: 0.5 - 0.1 + 0.01 = 0.41. y: 0.5 - (-0.2) + 0.02 = 0.72.
        expectVec2Near(historyUv(vsg::vec2(0.5f, 0.5f), vsg::vec2(0.1f, -0.2f), vsg::vec2(0.01f, 0.02f)),
            vsg::vec2(0.41f, 0.72f), tol);
    }

    TEST(RenderAoTemporalHistoryUv, no_motion_and_no_jitter_returns_the_same_position)
    {
        expectVec2Near(
            historyUv(vsg::vec2(0.3f, 0.8f), vsg::vec2(0.f, 0.f), vsg::vec2(0.f, 0.f)), vsg::vec2(0.3f, 0.8f), 1e-7f);
    }

    // ---- bilinear taps -----------------------------------------------------------------------------------------

    TEST(RenderAoTemporalBilinear, a_texel_centre_puts_all_the_weight_on_that_texel)
    {
        // 4 by 4, texel (1, 2): uv = (1.5 / 4, 2.5 / 4) = (0.375, 0.625). p = uv * 4 - 0.5 = (1, 2), f = (0, 0).
        const std::array<Tap, 4> taps = bilinearTaps(vsg::vec2(0.375f, 0.625f), 4, 4);
        EXPECT_EQ(taps[0].x, 1);
        EXPECT_EQ(taps[0].y, 2);
        EXPECT_NEAR(taps[0].weight, 1.f, 1e-6f);
        EXPECT_EQ(taps[1].x, 2);
        EXPECT_EQ(taps[1].y, 2);
        EXPECT_NEAR(taps[1].weight, 0.f, 1e-6f);
        EXPECT_EQ(taps[2].x, 1);
        EXPECT_EQ(taps[2].y, 3);
        EXPECT_NEAR(taps[2].weight, 0.f, 1e-6f);
        EXPECT_EQ(taps[3].x, 2);
        EXPECT_EQ(taps[3].y, 3);
        EXPECT_NEAR(taps[3].weight, 0.f, 1e-6f);
    }

    TEST(RenderAoTemporalBilinear, halfway_between_four_texels_weighs_them_equally)
    {
        // 4 by 4, uv = (0.5, 0.5): p = (1.5, 1.5), x0 = y0 = 1, f = (0.5, 0.5).
        const std::array<Tap, 4> taps = bilinearTaps(vsg::vec2(0.5f, 0.5f), 4, 4);
        const int expectedX[4] = { 1, 2, 1, 2 };
        const int expectedY[4] = { 1, 1, 2, 2 };
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(taps[i].x, expectedX[i]) << i;
            EXPECT_EQ(taps[i].y, expectedY[i]) << i;
            EXPECT_NEAR(taps[i].weight, 0.25f, 1e-6f) << i;
        }
    }

    TEST(RenderAoTemporalBilinear, weights_follow_the_listed_order_for_an_off_centre_position)
    {
        // 8 by 8, p = (2.25, 3.75): x0 = 2, y0 = 3, f = (0.25, 0.75).
        // uv = (p + 0.5) / 8 = (2.75 / 8, 4.25 / 8) = (0.34375, 0.53125) (exact in binary).
        // weights: (1-fx)(1-fy) = 0.75 * 0.25 = 0.1875; fx (1-fy) = 0.25 * 0.25 = 0.0625;
        //          (1-fx) fy = 0.75 * 0.75 = 0.5625;    fx fy = 0.25 * 0.75 = 0.1875.
        const std::array<Tap, 4> taps = bilinearTaps(vsg::vec2(0.34375f, 0.53125f), 8, 8);
        const int expectedX[4] = { 2, 3, 2, 3 };
        const int expectedY[4] = { 3, 3, 4, 4 };
        const float expectedWeight[4] = { 0.1875f, 0.0625f, 0.5625f, 0.1875f };
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(taps[i].x, expectedX[i]) << i;
            EXPECT_EQ(taps[i].y, expectedY[i]) << i;
            EXPECT_NEAR(taps[i].weight, expectedWeight[i], 1e-5f) << i;
        }
    }

    TEST(RenderAoTemporalBilinear, interior_weights_sum_to_one)
    {
        // 16 by 9: positions with p inside [0, size - 1) on both axes.
        const std::uint32_t width = 16;
        const std::uint32_t height = 9;
        for (int i = 1; i < 30; ++i)
        {
            for (int j = 1; j < 30; ++j)
            {
                const vsg::vec2 uv(
                    0.03f + 0.94f * static_cast<float>(i) / 30.f, 0.06f + 0.88f * static_cast<float>(j) / 30.f);
                const std::array<Tap, 4> taps = bilinearTaps(uv, width, height);
                float sum = 0.f;
                for (const Tap& tap : taps)
                {
                    EXPECT_GE(tap.weight, 0.f);
                    sum += tap.weight;
                }
                EXPECT_NEAR(sum, 1.f, 1e-5f) << uv.x << ", " << uv.y;
            }
        }
    }

    TEST(RenderAoTemporalBilinear, taps_past_the_left_and_top_edge_have_no_weight_and_are_clamped)
    {
        // 4 by 4, uv = (0, 0.5): p = (-0.5, 1.5), x0 = -1, y0 = 1, f = (0.5, 0.5).
        // Tap 0 (-1, 1) and tap 2 (-1, 2) are outside: weight 0, x clamped to 0.
        const std::array<Tap, 4> taps = bilinearTaps(vsg::vec2(0.f, 0.5f), 4, 4);
        EXPECT_EQ(taps[0].x, 0);
        EXPECT_EQ(taps[0].y, 1);
        EXPECT_EQ(taps[0].weight, 0.f);
        EXPECT_EQ(taps[1].x, 0);
        EXPECT_EQ(taps[1].y, 1);
        EXPECT_NEAR(taps[1].weight, 0.25f, 1e-6f);
        EXPECT_EQ(taps[2].x, 0);
        EXPECT_EQ(taps[2].y, 2);
        EXPECT_EQ(taps[2].weight, 0.f);
        EXPECT_EQ(taps[3].x, 0);
        EXPECT_EQ(taps[3].y, 2);
        EXPECT_NEAR(taps[3].weight, 0.25f, 1e-6f);
    }

    TEST(RenderAoTemporalBilinear, taps_past_the_right_edge_have_no_weight_and_are_clamped)
    {
        // 4 by 4, uv = (1, 0.5): p = (3.5, 1.5), x0 = 3, y0 = 1, f = (0.5, 0.5).
        // Tap 1 (4, 1) and tap 3 (4, 2) are outside: weight 0, x clamped to 3.
        const std::array<Tap, 4> taps = bilinearTaps(vsg::vec2(1.f, 0.5f), 4, 4);
        EXPECT_EQ(taps[0].x, 3);
        EXPECT_EQ(taps[0].y, 1);
        EXPECT_NEAR(taps[0].weight, 0.25f, 1e-6f);
        EXPECT_EQ(taps[1].x, 3);
        EXPECT_EQ(taps[1].y, 1);
        EXPECT_EQ(taps[1].weight, 0.f);
        EXPECT_EQ(taps[2].x, 3);
        EXPECT_EQ(taps[2].y, 2);
        EXPECT_NEAR(taps[2].weight, 0.25f, 1e-6f);
        EXPECT_EQ(taps[3].x, 3);
        EXPECT_EQ(taps[3].y, 2);
        EXPECT_EQ(taps[3].weight, 0.f);
    }

    TEST(RenderAoTemporalBilinear, a_position_wholly_outside_has_no_weight_anywhere)
    {
        const std::array<Tap, 4> left = bilinearTaps(vsg::vec2(-1.f, 0.5f), 4, 4);
        for (const Tap& tap : left)
        {
            EXPECT_EQ(tap.weight, 0.f);
        }
        const std::array<Tap, 4> below = bilinearTaps(vsg::vec2(0.5f, 2.f), 4, 4);
        for (const Tap& tap : below)
        {
            EXPECT_EQ(tap.weight, 0.f);
        }
    }

    TEST(RenderAoTemporalBilinear, every_tap_is_clamped_into_the_image)
    {
        const std::uint32_t width = 5;
        const std::uint32_t height = 3;
        const std::array<vsg::vec2, 7> positions = { vsg::vec2(-0.5f, -0.5f), vsg::vec2(0.f, 0.f), vsg::vec2(1.f, 1.f),
            vsg::vec2(1.5f, 0.2f), vsg::vec2(0.5f, 0.5f), vsg::vec2(0.99f, 0.01f), vsg::vec2(-3.f, 4.f) };
        for (const vsg::vec2& uv : positions)
        {
            for (const Tap& tap : bilinearTaps(uv, width, height))
            {
                EXPECT_GE(tap.x, 0);
                EXPECT_LT(tap.x, static_cast<int>(width));
                EXPECT_GE(tap.y, 0);
                EXPECT_LT(tap.y, static_cast<int>(height));
            }
        }
    }

    // ---- weights, blend and count ------------------------------------------------------------------------------

    TEST(RenderAoTemporalWeights, tapWeight_is_the_product)
    {
        // 0.5 * (1 - 0.25) * 0.8 = 0.5 * 0.75 * 0.8 = 0.3.
        EXPECT_NEAR(tapWeight(0.5f, 0.25f, 0.8f), 0.3f, 1e-6f);
        EXPECT_NEAR(tapWeight(1.f, 0.f, 1.f), 1.f, 1e-6f);
        EXPECT_NEAR(tapWeight(1.f, 1.f, 1.f), 0.f, 1e-6f);
        EXPECT_NEAR(tapWeight(1.f, 0.f, 0.f), 0.f, 1e-6f);
        EXPECT_NEAR(tapWeight(0.f, 0.f, 1.f), 0.f, 1e-6f);
    }

    TEST(RenderAoTemporalBlend, no_history_or_no_confidence_gives_no_blend)
    {
        EXPECT_EQ(historyBlend(0.9f, 0.f, 1.f), 0.f);
        EXPECT_EQ(historyBlend(0.9f, -3.f, 1.f), 0.f);
        EXPECT_EQ(historyBlend(0.9f, 5.f, 0.f), 0.f);
        EXPECT_EQ(historyBlend(0.9f, 5.f, -0.5f), 0.f);
    }

    TEST(RenderAoTemporalBlend, the_first_frames_average_what_has_been_seen)
    {
        // count 1: min(0.9, 1 / 2) = 0.5. count 2: min(0.9, 2 / 3) = 0.6667. count 4: 4 / 5 = 0.8.
        EXPECT_NEAR(historyBlend(0.9f, 1.f, 1.f), 0.5f, 1e-6f);
        EXPECT_NEAR(historyBlend(0.9f, 2.f, 1.f), 2.f / 3.f, 1e-6f);
        EXPECT_NEAR(historyBlend(0.9f, 4.f, 1.f), 0.8f, 1e-6f);
        // Confidence scales the result: 0.5 * 0.6 = 0.3.
        EXPECT_NEAR(historyBlend(0.9f, 1.f, 0.6f), 0.3f, 1e-6f);
    }

    TEST(RenderAoTemporalBlend, settles_at_the_base_weight)
    {
        // count 9: 9 / 10 = 0.9 = base. A large count: base.
        EXPECT_NEAR(historyBlend(0.9f, 9.f, 1.f), 0.9f, 1e-6f);
        EXPECT_NEAR(historyBlend(0.9f, 9.f, 0.5f), 0.45f, 1e-6f);
        EXPECT_NEAR(historyBlend(0.9f, 1000.f, 1.f), 0.9f, 1e-6f);
        EXPECT_NEAR(historyBlend(0.5f, 255.f, 1.f), 0.5f, 1e-6f);
    }

    TEST(RenderAoTemporalBlend, the_base_is_limited_to_the_maximum)
    {
        EXPECT_NEAR(historyBlend(0.99f, 1000.f, 1.f), maxHistoryWeight, 1e-6f);
        EXPECT_NEAR(historyBlend(5.f, 1000.f, 1.f), maxHistoryWeight, 1e-6f);
        // A negative base cannot give a negative blend.
        EXPECT_EQ(historyBlend(-0.5f, 5.f, 1.f), 0.f);
        // A base of zero (a reset) gives no blend whatever the count.
        EXPECT_EQ(historyBlend(0.f, 255.f, 1.f), 0.f);
    }

    TEST(RenderAoTemporalBlend, never_decreases_as_the_count_grows)
    {
        float previous = -1.f;
        for (int count = 0; count <= 300; ++count)
        {
            const float blend = historyBlend(0.9f, static_cast<float>(count), 1.f);
            EXPECT_GE(blend, previous) << count;
            EXPECT_LE(blend, 0.9f + 1e-6f) << count;
            previous = blend;
        }
    }

    TEST(RenderAoTemporalCount, the_first_frame_has_a_count_of_one_whatever_the_confidence)
    {
        EXPECT_FLOAT_EQ(nextCount(0.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(nextCount(0.f, 0.3f), 1.f);
        EXPECT_FLOAT_EQ(nextCount(0.f, 1.f), 1.f);
    }

    TEST(RenderAoTemporalCount, full_confidence_adds_one_each_frame)
    {
        // 1 + count * 1.
        EXPECT_FLOAT_EQ(nextCount(1.f, 1.f), 2.f);
        EXPECT_FLOAT_EQ(nextCount(5.f, 1.f), 6.f);
        float count = 0.f;
        for (int frame = 1; frame <= 10; ++frame)
        {
            count = nextCount(count, 1.f);
            EXPECT_FLOAT_EQ(count, static_cast<float>(frame));
        }
    }

    TEST(RenderAoTemporalCount, no_confidence_starts_again_at_one)
    {
        EXPECT_FLOAT_EQ(nextCount(100.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(nextCount(255.f, 0.f), 1.f);
    }

    TEST(RenderAoTemporalCount, partial_confidence_keeps_part_of_the_count)
    {
        // 1 + 10 * 0.5 = 6.
        EXPECT_FLOAT_EQ(nextCount(10.f, 0.5f), 6.f);
    }

    TEST(RenderAoTemporalCount, stops_growing_at_the_maximum)
    {
        EXPECT_FLOAT_EQ(nextCount(254.f, 1.f), 255.f);
        EXPECT_FLOAT_EQ(nextCount(255.f, 1.f), 255.f);
        EXPECT_FLOAT_EQ(nextCount(1000.f, 1.f), 255.f);
    }

    TEST(RenderAoTemporalCount, a_negative_count_is_treated_as_zero)
    {
        EXPECT_FLOAT_EQ(nextCount(-5.f, 1.f), 1.f);
        EXPECT_FLOAT_EQ(nextCount(-0.5f, 0.5f), 1.f);
    }

    TEST(RenderAoTemporalCount, confidence_is_clamped_to_the_unit_interval)
    {
        // clamp(2) = 1: 1 + 10. clamp(-1) = 0: 1.
        EXPECT_FLOAT_EQ(nextCount(10.f, 2.f), 11.f);
        EXPECT_FLOAT_EQ(nextCount(10.f, -1.f), 1.f);
    }

    TEST(RenderAoTemporalAccumulate, blend_zero_is_the_current_value_and_one_is_the_history)
    {
        EXPECT_FLOAT_EQ(accumulate(0.2f, 0.6f, 0.f), 0.2f);
        EXPECT_FLOAT_EQ(accumulate(0.2f, 0.6f, 1.f), 0.6f);
    }

    TEST(RenderAoTemporalAccumulate, a_half_blend_is_the_midpoint)
    {
        // 0.2 + (0.6 - 0.2) * 0.5 = 0.4.
        EXPECT_NEAR(accumulate(0.2f, 0.6f, 0.5f), 0.4f, 1e-6f);
        // 0.2 + 0.4 * 0.25 = 0.3.
        EXPECT_NEAR(accumulate(0.2f, 0.6f, 0.25f), 0.3f, 1e-6f);
    }

    TEST(RenderAoTemporalAccumulate, the_blend_is_clamped)
    {
        EXPECT_FLOAT_EQ(accumulate(0.2f, 0.6f, 2.f), 0.6f);
        EXPECT_FLOAT_EQ(accumulate(0.2f, 0.6f, -1.f), 0.2f);
    }

    // ---- convergence -------------------------------------------------------------------------------------------

    // A fixed noisy sequence with an exact mean of 0.5: a four frame cycle of offsets +0.3, -0.1, -0.3, +0.1, whose
    // sum is zero. Its variance is (0.09 + 0.01 + 0.09 + 0.01) / 4 = 0.05, its mean absolute error 0.2.
    std::vector<float> noisySamples(std::size_t frames)
    {
        const std::array<float, 4> offsets = { 0.3f, -0.1f, -0.3f, 0.1f };
        std::vector<float> samples;
        for (std::size_t i = 0; i < frames; ++i)
        {
            samples.push_back(0.5f + offsets[i % 4]);
        }
        return samples;
    }

    TEST(RenderAoTemporalConvergence, the_first_frame_is_exactly_the_sample)
    {
        const std::vector<float> samples = noisySamples(10);
        const std::vector<float> values = runAccumulation(samples, defaultHistoryWeight, 1.f);
        EXPECT_EQ(values[0], samples[0]);
    }

    TEST(RenderAoTemporalConvergence, the_first_frames_are_the_running_mean)
    {
        // With blend = count / (count + 1) and count = frames so far, each value is the mean of all samples so far,
        // until count / (count + 1) would pass the base weight 0.9 (at count 9).
        const std::vector<float> samples = noisySamples(10);
        const std::vector<float> values = runAccumulation(samples, defaultHistoryWeight, 1.f);
        double sum = 0.0;
        for (std::size_t k = 0; k < samples.size(); ++k)
        {
            sum += samples[k];
            EXPECT_NEAR(values[k], sum / static_cast<double>(k + 1), 1e-5) << k;
        }
    }

    TEST(RenderAoTemporalConvergence, the_accumulated_value_settles_near_the_mean_with_less_noise)
    {
        const std::vector<float> samples = noisySamples(200);
        const std::vector<float> values = runAccumulation(samples, defaultHistoryWeight, 1.f);

        // The error of the last value against the true mean 0.5 is smaller than a single sample's mean error 0.2.
        EXPECT_LT(std::abs(values.back() - 0.5f), 0.2f);
        EXPECT_LT(std::abs(values.back() - 0.5f), 0.05f);

        // The variance over the last 100 frames is a fraction of the raw samples' variance.
        const double rawVariance = varianceOf(samples, 100, 200);
        const double accumulatedVariance = varianceOf(values, 100, 200);
        EXPECT_NEAR(rawVariance, 0.05, 1e-4);
        EXPECT_LT(accumulatedVariance, 0.25 * rawVariance);
    }

    TEST(RenderAoTemporalConvergence, no_confidence_keeps_the_raw_samples)
    {
        // Confidence 0 every frame: blend 0, so each value is the frame's own sample.
        const std::vector<float> samples = noisySamples(20);
        const std::vector<float> values = runAccumulation(samples, defaultHistoryWeight, 0.f);
        for (std::size_t k = 0; k < samples.size(); ++k)
        {
            EXPECT_EQ(values[k], samples[k]) << k;
        }
    }

    // ---- variance range ----------------------------------------------------------------------------------------

    TEST(RenderAoTemporalVarianceRange, a_gamma_of_zero_or_less_does_not_clamp)
    {
        const Range zero = varianceRange(0.5f, 0.26f, 0.f);
        EXPECT_EQ(zero.lo, 0.f);
        EXPECT_EQ(zero.hi, 1.f);
        const Range negative = varianceRange(0.3f, 0.5f, -2.f);
        EXPECT_EQ(negative.lo, 0.f);
        EXPECT_EQ(negative.hi, 1.f);
    }

    TEST(RenderAoTemporalVarianceRange, is_the_mean_plus_and_minus_gamma_standard_deviations)
    {
        // mean 0.5, mean of squares 0.26: variance 0.26 - 0.25 = 0.01, deviation 0.1.
        const Range one = varianceRange(0.5f, 0.26f, 1.f);
        EXPECT_NEAR(one.lo, 0.4f, 1e-4f);
        EXPECT_NEAR(one.hi, 0.6f, 1e-4f);
        // gamma 2: 0.5 -/+ 0.2.
        const Range two = varianceRange(0.5f, 0.26f, 2.f);
        EXPECT_NEAR(two.lo, 0.3f, 1e-4f);
        EXPECT_NEAR(two.hi, 0.7f, 1e-4f);
    }

    TEST(RenderAoTemporalVarianceRange, a_slightly_negative_variance_collapses_to_the_mean)
    {
        // 0.24999 < 0.5^2: rounding in the sums. The variance is taken as zero.
        const Range r = varianceRange(0.5f, 0.24999f, 1.5f);
        EXPECT_EQ(r.lo, 0.5f);
        EXPECT_EQ(r.hi, 0.5f);
    }

    TEST(RenderAoTemporalVarianceRange, clampToRange_clamps_both_sides_and_leaves_the_inside_alone)
    {
        const Range range{ 0.4f, 0.6f };
        EXPECT_EQ(clampToRange(0.1f, range), 0.4f);
        EXPECT_EQ(clampToRange(0.9f, range), 0.6f);
        EXPECT_EQ(clampToRange(0.5f, range), 0.5f);
        EXPECT_EQ(clampToRange(0.4f, range), 0.4f);
        EXPECT_EQ(clampToRange(0.6f, range), 0.6f);
        EXPECT_EQ(clampToRange(-3.f, range), 0.4f);
    }

    TEST(RenderAoTemporalVarianceRange, the_no_clamp_range_leaves_a_unit_value_alone)
    {
        const Range none = varianceRange(0.5f, 0.26f, 0.f);
        EXPECT_EQ(clampToRange(0.f, none), 0.f);
        EXPECT_EQ(clampToRange(0.37f, none), 0.37f);
        EXPECT_EQ(clampToRange(1.f, none), 1.f);
    }

    // ---- switches ----------------------------------------------------------------------------------------------

    TEST(RenderAoTemporalSwitches, useAoTemporal_needs_all_three)
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool setting = (bits & 1) != 0;
            const bool aoPass = (bits & 2) != 0;
            const bool gbuffer = (bits & 4) != 0;
            const bool expected = setting && aoPass && gbuffer;
            EXPECT_EQ(useAoTemporal(setting, aoPass, gbuffer), expected) << bits;
        }
        EXPECT_TRUE(useAoTemporal(true, true, true));
        EXPECT_FALSE(useAoTemporal(false, false, false));
    }

    TEST(RenderAoTemporalSwitches, parseDebugView_knows_the_five_names)
    {
        ASSERT_TRUE(parseDebugView("none").has_value());
        EXPECT_EQ(*parseDebugView("none"), DebugView::None);
        ASSERT_TRUE(parseDebugView("raw").has_value());
        EXPECT_EQ(*parseDebugView("raw"), DebugView::Raw);
        ASSERT_TRUE(parseDebugView("weight").has_value());
        EXPECT_EQ(*parseDebugView("weight"), DebugView::Weight);
        ASSERT_TRUE(parseDebugView("reject").has_value());
        EXPECT_EQ(*parseDebugView("reject"), DebugView::Reject);
        ASSERT_TRUE(parseDebugView("static").has_value());
        EXPECT_EQ(*parseDebugView("static"), DebugView::Static);
    }

    TEST(RenderAoTemporalSwitches, parseDebugView_refuses_other_text)
    {
        EXPECT_FALSE(parseDebugView("Raw").has_value());
        EXPECT_FALSE(parseDebugView("NONE").has_value());
        EXPECT_FALSE(parseDebugView("Static").has_value());
        EXPECT_FALSE(parseDebugView("").has_value());
        EXPECT_FALSE(parseDebugView("foo").has_value());
        EXPECT_FALSE(parseDebugView(" raw").has_value());
        EXPECT_FALSE(parseDebugView("raw ").has_value());
        EXPECT_FALSE(parseDebugView("velocity").has_value());
    }

    TEST(RenderAoTemporalSwitches, historyUsable_waits_for_the_warm_up_and_a_clean_start)
    {
        // A reset never uses the history.
        EXPECT_FALSE(historyUsable(true, 0));
        EXPECT_FALSE(historyUsable(true, warmupFrames));
        EXPECT_FALSE(historyUsable(true, 100));
        // Without a reset, the first warmupFrames (3) frames after a build are not usable.
        EXPECT_FALSE(historyUsable(false, 0));
        EXPECT_FALSE(historyUsable(false, 1));
        EXPECT_FALSE(historyUsable(false, 2));
        EXPECT_TRUE(historyUsable(false, 3));
        EXPECT_TRUE(historyUsable(false, 4));
        EXPECT_TRUE(historyUsable(false, 100000));
    }
}
