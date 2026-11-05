// Tests for the pure maths of temporal anti-aliasing, Render::Taa (task 0075, stage a). Written from the task spec
// ("Interfaces (pure parts)", "Design" and "Acceptance criteria" of docs/tasks/0075-temporal-anti-aliasing.md) before
// the implementation. Every expected number below is worked out by hand from the spec's formulas, with the arithmetic
// in a comment. Pure CPU maths: no device, window or game data.

#include <components/render/taamath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace
{
    using namespace Render::Taa;

    constexpr float tol = 1e-5f;

    // Compares each component with a tolerance that is absolute below 1 and relative above it.
    void expectVec3Near(const vsg::vec3& actual, const vsg::vec3& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance * std::max(1.f, std::abs(expected.x)));
        EXPECT_NEAR(actual.y, expected.y, tolerance * std::max(1.f, std::abs(expected.y)));
        EXPECT_NEAR(actual.z, expected.z, tolerance * std::max(1.f, std::abs(expected.z)));
    }

    // The colour whose compressed form (c / (1 + luma c)) is the grey value g: c = g / (1 - g). Valid for a grey
    // colour, whose luma is its value because the Rec. 709 weights sum to 1. Worked out from the spec's compress.
    vsg::vec3 colourOfCompressedGrey(float g)
    {
        const float v = g / (1.f - g);
        return vsg::vec3(v, v, v);
    }

    // A perspective-shaped matrix as the engine's velocity code has: clip.x = 1.5 x, clip.y = -2 y, clip.w = -z.
    vsg::mat4 projectionLike()
    {
        vsg::mat4 p(0.f);
        p(0, 0) = 1.5f;
        p(1, 1) = -2.f;
        p(2, 2) = -1.f;
        p(3, 2) = -0.5f;
        p(2, 3) = -1.f;
        return p;
    }

    // ---- constants ---------------------------------------------------------------------------------------------

    TEST(RenderTaaConstants, hold_the_values_the_spec_lists)
    {
        EXPECT_EQ(jitterPeriod, 16u);
        EXPECT_FLOAT_EQ(defaultHistoryWeight, 0.95f);
        EXPECT_FLOAT_EQ(velocityFullPixels, 16.f);
        EXPECT_FLOAT_EQ(movingWeightShare, 0.5f);
        EXPECT_FLOAT_EQ(depthRejectLow, 0.03f);
        EXPECT_FLOAT_EQ(depthRejectHigh, 0.12f);
        EXPECT_FLOAT_EQ(varianceGamma, 1.0f);
        EXPECT_DOUBLE_EQ(resetDistance, 512.0);
        EXPECT_DOUBLE_EQ(resetViewCos, 0.5);
        EXPECT_DOUBLE_EQ(resetGapSeconds, 0.25);
    }

    // ---- the jitter sequence -------------------------------------------------------------------------------------

    TEST(RenderTaaJitter, radicalInverse_base_2_first_values)
    {
        // Digits of the index mirrored about the point: 1 = 1b -> 0.1b = 1/2, 2 = 10b -> 0.01b = 1/4,
        // 3 = 11b -> 0.11b = 3/4, 4 = 100b -> 1/8, 5 = 101b -> 0.101b = 5/8, 16 -> 1/32.
        EXPECT_DOUBLE_EQ(radicalInverse(0, 2), 0.0);
        EXPECT_DOUBLE_EQ(radicalInverse(1, 2), 0.5);
        EXPECT_DOUBLE_EQ(radicalInverse(2, 2), 0.25);
        EXPECT_DOUBLE_EQ(radicalInverse(3, 2), 0.75);
        EXPECT_DOUBLE_EQ(radicalInverse(4, 2), 0.125);
        EXPECT_DOUBLE_EQ(radicalInverse(5, 2), 0.625);
        EXPECT_DOUBLE_EQ(radicalInverse(16, 2), 1.0 / 32.0);
    }

    TEST(RenderTaaJitter, radicalInverse_base_3_first_values)
    {
        // 1 -> 1/3, 2 -> 2/3, 3 = 10 (base 3) -> 1/9, 4 = 11 -> 1/3 + 1/9 = 4/9,
        // 5 = 12 -> 2/3 + 1/9 = 7/9, 16 = 121 (base 3, least digit first 1, 2, 1) -> 1/3 + 2/9 + 1/27 = 16/27.
        EXPECT_DOUBLE_EQ(radicalInverse(0, 3), 0.0);
        EXPECT_NEAR(radicalInverse(1, 3), 1.0 / 3.0, 1e-12);
        EXPECT_NEAR(radicalInverse(2, 3), 2.0 / 3.0, 1e-12);
        EXPECT_NEAR(radicalInverse(3, 3), 1.0 / 9.0, 1e-12);
        EXPECT_NEAR(radicalInverse(4, 3), 4.0 / 9.0, 1e-12);
        EXPECT_NEAR(radicalInverse(5, 3), 7.0 / 9.0, 1e-12);
        EXPECT_NEAR(radicalInverse(16, 3), 16.0 / 27.0, 1e-12);
    }

    TEST(RenderTaaJitter, haltonJitter_first_sample_is_the_documented_one)
    {
        // frame 0 -> i = 1: (0.5 - 0.5, 1/3 - 0.5) = (0, -1/6).
        const vsg::dvec2 j = haltonJitter(0);
        EXPECT_NEAR(j.x, 0.0, 1e-12);
        EXPECT_NEAR(j.y, 1.0 / 3.0 - 0.5, 1e-12);
    }

    TEST(RenderTaaJitter, haltonJitter_next_samples)
    {
        // frame 1 -> i = 2: (0.25 - 0.5, 2/3 - 0.5) = (-0.25, 1/6).
        const vsg::dvec2 j1 = haltonJitter(1);
        EXPECT_NEAR(j1.x, -0.25, 1e-12);
        EXPECT_NEAR(j1.y, 2.0 / 3.0 - 0.5, 1e-12);

        // frame 2 -> i = 3: (0.75 - 0.5, 1/9 - 0.5) = (0.25, -7/18).
        const vsg::dvec2 j2 = haltonJitter(2);
        EXPECT_NEAR(j2.x, 0.25, 1e-12);
        EXPECT_NEAR(j2.y, 1.0 / 9.0 - 0.5, 1e-12);

        // frame 3 -> i = 4: (0.125 - 0.5, 4/9 - 0.5) = (-0.375, -1/18).
        const vsg::dvec2 j3 = haltonJitter(3);
        EXPECT_NEAR(j3.x, -0.375, 1e-12);
        EXPECT_NEAR(j3.y, 4.0 / 9.0 - 0.5, 1e-12);

        // frame 15 -> i = 16: (1/32 - 0.5, 16/27 - 0.5).
        const vsg::dvec2 j15 = haltonJitter(15);
        EXPECT_NEAR(j15.x, 1.0 / 32.0 - 0.5, 1e-12);
        EXPECT_NEAR(j15.y, 16.0 / 27.0 - 0.5, 1e-12);
    }

    TEST(RenderTaaJitter, haltonJitter_stays_in_the_half_open_unit_square_over_a_long_run)
    {
        for (std::uint32_t frame = 0; frame < 4 * jitterPeriod; ++frame)
        {
            const vsg::dvec2 j = haltonJitter(frame);
            EXPECT_GE(j.x, -0.5) << "frame " << frame;
            EXPECT_LT(j.x, 0.5) << "frame " << frame;
            EXPECT_GE(j.y, -0.5) << "frame " << frame;
            EXPECT_LT(j.y, 0.5) << "frame " << frame;
        }
    }

    TEST(RenderTaaJitter, haltonJitter_repeats_exactly_after_the_period)
    {
        for (std::uint32_t frame = 0; frame < jitterPeriod; ++frame)
        {
            const vsg::dvec2 first = haltonJitter(frame);
            const vsg::dvec2 second = haltonJitter(frame + jitterPeriod);
            const vsg::dvec2 later = haltonJitter(frame + 5 * jitterPeriod);
            EXPECT_EQ(first.x, second.x) << "frame " << frame;
            EXPECT_EQ(first.y, second.y) << "frame " << frame;
            EXPECT_EQ(first.x, later.x) << "frame " << frame;
            EXPECT_EQ(first.y, later.y) << "frame " << frame;
        }
    }

    TEST(RenderTaaJitter, jitterPixels_scale_zero_gives_no_jitter)
    {
        for (std::uint32_t frame = 0; frame < jitterPeriod; ++frame)
        {
            const vsg::dvec2 j = jitterPixels(frame, 0.f);
            EXPECT_NEAR(j.x, 0.0, 1e-12);
            EXPECT_NEAR(j.y, 0.0, 1e-12);
        }
    }

    TEST(RenderTaaJitter, jitterPixels_scale_one_is_the_halton_sample)
    {
        for (std::uint32_t frame = 0; frame < jitterPeriod; ++frame)
        {
            const vsg::dvec2 h = haltonJitter(frame);
            const vsg::dvec2 j = jitterPixels(frame, 1.f);
            EXPECT_NEAR(j.x, h.x, 1e-12);
            EXPECT_NEAR(j.y, h.y, 1e-12);
        }
    }

    TEST(RenderTaaJitter, jitterPixels_scale_half_halves_the_sample)
    {
        // frame 1 is (-0.25, 1/6); times 0.5 gives (-0.125, 1/12).
        const vsg::dvec2 j = jitterPixels(1, 0.5f);
        EXPECT_NEAR(j.x, -0.125, 1e-12);
        EXPECT_NEAR(j.y, 1.0 / 12.0, 1e-12);
    }

    TEST(RenderTaaJitter, jitterPixels_clamps_the_scale_to_0_and_1)
    {
        // Scale 2 clamps to 1: the plain sample. A negative scale clamps to 0.
        const vsg::dvec2 h = haltonJitter(2);
        const vsg::dvec2 big = jitterPixels(2, 2.f);
        EXPECT_NEAR(big.x, h.x, 1e-12);
        EXPECT_NEAR(big.y, h.y, 1e-12);

        const vsg::dvec2 negative = jitterPixels(2, -1.f);
        EXPECT_NEAR(negative.x, 0.0, 1e-12);
        EXPECT_NEAR(negative.y, 0.0, 1e-12);
    }

    TEST(RenderTaaJitter, jitterNdc_is_twice_the_pixels_over_the_extent)
    {
        // (2 * 0.5 / 1280, 2 * -0.25 / 720) = (1/1280, -1/1440).
        const vsg::dvec2 ndc = jitterNdc(vsg::dvec2(0.5, -0.25), 1280, 720);
        EXPECT_NEAR(ndc.x, 1.0 / 1280.0, 1e-12);
        EXPECT_NEAR(ndc.y, -1.0 / 1440.0, 1e-12);

        // One whole pixel across a 100 x 50 target: (2 / 100, 2 / 50) = (0.02, 0.04).
        const vsg::dvec2 whole = jitterNdc(vsg::dvec2(1.0, 1.0), 100, 50);
        EXPECT_NEAR(whole.x, 0.02, 1e-12);
        EXPECT_NEAR(whole.y, 0.04, 1e-12);
    }

    TEST(RenderTaaJitter, jitterNdc_of_a_zero_extent_is_zero)
    {
        const vsg::dvec2 ndc = jitterNdc(vsg::dvec2(0.4, -0.3), 0, 0);
        EXPECT_DOUBLE_EQ(ndc.x, 0.0);
        EXPECT_DOUBLE_EQ(ndc.y, 0.0);
        EXPECT_TRUE(std::isfinite(ndc.x));
        EXPECT_TRUE(std::isfinite(ndc.y));
    }

    TEST(RenderTaaJitter, jitterMatrix_is_identity_with_the_ndc_shift_in_the_last_column)
    {
        const vsg::dmat4 m = jitterMatrix(vsg::dvec2(0.01, -0.02));
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                double expected = column == row ? 1.0 : 0.0;
                if (column == 3 && row == 0)
                {
                    expected = 0.01;
                }
                if (column == 3 && row == 1)
                {
                    expected = -0.02;
                }
                EXPECT_NEAR(m(column, row), expected, 1e-12) << "column " << column << " row " << row;
            }
        }
    }

    TEST(RenderTaaJitter, jitterMatrix_adds_ndc_times_w_to_a_clip_position)
    {
        const vsg::dmat4 m = jitterMatrix(vsg::dvec2(0.01, -0.02));
        const vsg::dvec4 clip(0.3, 0.4, 0.5, 2.0);
        const vsg::dvec4 shifted = m * clip;
        // x: 0.3 + 0.01 * 2 = 0.32; y: 0.4 - 0.02 * 2 = 0.36; z and w are untouched.
        EXPECT_NEAR(shifted.x, 0.32, 1e-12);
        EXPECT_NEAR(shifted.y, 0.36, 1e-12);
        EXPECT_NEAR(shifted.z, 0.5, 1e-12);
        EXPECT_NEAR(shifted.w, 2.0, 1e-12);
    }

    TEST(RenderTaaJitter, jitterMatrix_of_zero_is_the_identity_on_a_clip_position)
    {
        const vsg::dmat4 m = jitterMatrix(vsg::dvec2(0.0, 0.0));
        const vsg::dvec4 clip(-1.5, 2.25, 0.125, 3.0);
        const vsg::dvec4 same = m * clip;
        EXPECT_NEAR(same.x, clip.x, 1e-12);
        EXPECT_NEAR(same.y, clip.y, 1e-12);
        EXPECT_NEAR(same.z, clip.z, 1e-12);
        EXPECT_NEAR(same.w, clip.w, 1e-12);
    }

    // ---- settings ------------------------------------------------------------------------------------------------

    TEST(RenderTaaSettings, useTaa_is_true_only_when_all_four_conditions_hold)
    {
        for (int mask = 0; mask < 16; ++mask)
        {
            const bool settingOn = (mask & 1) != 0;
            const bool hdrPath = (mask & 2) != 0;
            const bool gbuffer = (mask & 4) != 0;
            const bool singleSampled = (mask & 8) != 0;
            const std::uint32_t samples = singleSampled ? 1u : 4u;
            const bool expected = mask == 15;
            EXPECT_EQ(useTaa(settingOn, hdrPath, gbuffer, samples), expected) << "mask " << mask;
        }
    }

    TEST(RenderTaaSettings, useTaa_needs_exactly_one_sample)
    {
        EXPECT_TRUE(useTaa(true, true, true, 1));
        EXPECT_FALSE(useTaa(true, true, true, 0));
        EXPECT_FALSE(useTaa(true, true, true, 2));
        EXPECT_FALSE(useTaa(true, true, true, 4));
        EXPECT_FALSE(useTaa(true, true, true, 8));
    }

    TEST(RenderTaaSettings, the_default_clipping_is_minmax)
    {
        // Decision 0012: the variance box is covered by a patent (US10116916B2), so it is an opt-in.
        EXPECT_TRUE(defaultClipping == Clipping::MinMax);
    }

    TEST(RenderTaaSettings, parseClipping_accepts_the_two_names)
    {
        const auto variance = parseClipping("variance");
        ASSERT_TRUE(variance.has_value());
        EXPECT_TRUE(*variance == Clipping::Variance);

        const auto minmax = parseClipping("minmax");
        ASSERT_TRUE(minmax.has_value());
        EXPECT_TRUE(*minmax == Clipping::MinMax);
    }

    TEST(RenderTaaSettings, parseClipping_is_case_sensitive_and_rejects_other_names)
    {
        EXPECT_FALSE(parseClipping("Variance").has_value());
        EXPECT_FALSE(parseClipping("VARIANCE").has_value());
        EXPECT_FALSE(parseClipping("MinMax").has_value());
        EXPECT_FALSE(parseClipping("minMax").has_value());
        EXPECT_FALSE(parseClipping("").has_value());
        EXPECT_FALSE(parseClipping("min max").has_value());
        EXPECT_FALSE(parseClipping("box").has_value());
        EXPECT_FALSE(parseClipping(" variance").has_value());
        EXPECT_FALSE(parseClipping("variance ").has_value());
    }

    TEST(RenderTaaSettings, parseDebugView_accepts_the_four_names)
    {
        const auto none = parseDebugView("none");
        ASSERT_TRUE(none.has_value());
        EXPECT_TRUE(*none == DebugView::None);

        const auto weight = parseDebugView("weight");
        ASSERT_TRUE(weight.has_value());
        EXPECT_TRUE(*weight == DebugView::Weight);

        const auto reject = parseDebugView("reject");
        ASSERT_TRUE(reject.has_value());
        EXPECT_TRUE(*reject == DebugView::Reject);

        const auto velocity = parseDebugView("velocity");
        ASSERT_TRUE(velocity.has_value());
        EXPECT_TRUE(*velocity == DebugView::Velocity);
    }

    TEST(RenderTaaSettings, parseDebugView_is_case_sensitive_and_rejects_other_names)
    {
        EXPECT_FALSE(parseDebugView("None").has_value());
        EXPECT_FALSE(parseDebugView("WEIGHT").has_value());
        EXPECT_FALSE(parseDebugView("Reject").has_value());
        EXPECT_FALSE(parseDebugView("Velocity").has_value());
        EXPECT_FALSE(parseDebugView("").has_value());
        EXPECT_FALSE(parseDebugView("depth").has_value());
        EXPECT_FALSE(parseDebugView("off").has_value());
    }

    // ---- history resets ------------------------------------------------------------------------------------------

    // A frame in which nothing happens: a previous frame exists, the size is the same, 16 ms passed, the camera moved
    // one unit and kept its direction.
    ResetInput calmFrame()
    {
        ResetInput input{};
        input.havePrevious = true;
        input.sizeChanged = false;
        input.gapSeconds = 0.016;
        input.cameraDistance = 1.0;
        input.viewDirectionCos = 1.0;
        return input;
    }

    TEST(RenderTaaReset, a_calm_frame_keeps_the_history)
    {
        EXPECT_FALSE(historyReset(calmFrame()));
    }

    TEST(RenderTaaReset, no_previous_frame_resets)
    {
        ResetInput input = calmFrame();
        input.havePrevious = false;
        EXPECT_TRUE(historyReset(input));
    }

    TEST(RenderTaaReset, a_size_change_resets)
    {
        ResetInput input = calmFrame();
        input.sizeChanged = true;
        EXPECT_TRUE(historyReset(input));
    }

    TEST(RenderTaaReset, a_gap_over_a_quarter_of_a_second_resets)
    {
        ResetInput input = calmFrame();
        input.gapSeconds = 0.3;
        EXPECT_TRUE(historyReset(input));
        // The rule is "gapSeconds > resetGapSeconds": exactly the limit keeps the history.
        input.gapSeconds = resetGapSeconds;
        EXPECT_FALSE(historyReset(input));
    }

    TEST(RenderTaaReset, a_jump_over_512_units_resets)
    {
        ResetInput input = calmFrame();
        input.cameraDistance = 600.0;
        EXPECT_TRUE(historyReset(input));
        // "cameraDistance > resetDistance": exactly the limit keeps the history.
        input.cameraDistance = resetDistance;
        EXPECT_FALSE(historyReset(input));
    }

    TEST(RenderTaaReset, a_turn_over_60_degrees_resets)
    {
        ResetInput input = calmFrame();
        input.viewDirectionCos = 0.4; // cos 66 degrees, a turn of more than 60
        EXPECT_TRUE(historyReset(input));
        // "viewDirectionCos < resetViewCos": exactly cos 60 degrees keeps the history.
        input.viewDirectionCos = resetViewCos;
        EXPECT_FALSE(historyReset(input));
        // A turn of 10 degrees (cos about 0.985) is fine.
        input.viewDirectionCos = 0.985;
        EXPECT_FALSE(historyReset(input));
    }

    // ---- colour space helpers ------------------------------------------------------------------------------------

    TEST(RenderTaaColour, luma_uses_the_rec709_weights)
    {
        EXPECT_NEAR(luma(vsg::vec3(1.f, 0.f, 0.f)), 0.2126f, tol);
        EXPECT_NEAR(luma(vsg::vec3(0.f, 1.f, 0.f)), 0.7152f, tol);
        EXPECT_NEAR(luma(vsg::vec3(0.f, 0.f, 1.f)), 0.0722f, tol);
        // 0.2126 + 0.7152 + 0.0722 = 1
        EXPECT_NEAR(luma(vsg::vec3(1.f, 1.f, 1.f)), 1.f, tol);
        // 0.5 * 0.2126 + 0.25 * 0.7152 + 0.125 * 0.0722 = 0.1063 + 0.1788 + 0.009025 = 0.294125
        EXPECT_NEAR(luma(vsg::vec3(0.5f, 0.25f, 0.125f)), 0.294125f, tol);
        EXPECT_NEAR(luma(vsg::vec3(0.f, 0.f, 0.f)), 0.f, tol);
    }

    TEST(RenderTaaColour, compress_divides_by_one_plus_luma)
    {
        // Grey 1: luma 1, so 1 / 2 = 0.5. Grey 3: 3 / 4 = 0.75. Black stays black.
        expectVec3Near(compress(vsg::vec3(1.f, 1.f, 1.f)), vsg::vec3(0.5f, 0.5f, 0.5f), tol);
        expectVec3Near(compress(vsg::vec3(3.f, 3.f, 3.f)), vsg::vec3(0.75f, 0.75f, 0.75f), tol);
        expectVec3Near(compress(vsg::vec3(0.f, 0.f, 0.f)), vsg::vec3(0.f, 0.f, 0.f), tol);

        // (1, 0, 0): luma 0.2126, so (1 / 1.2126, 0, 0).
        expectVec3Near(compress(vsg::vec3(1.f, 0.f, 0.f)), vsg::vec3(1.f / 1.2126f, 0.f, 0.f), tol);
    }

    TEST(RenderTaaColour, compress_takes_negative_components_as_zero_first)
    {
        // (-1, 1, 1) -> (0, 1, 1): luma 0.7152 + 0.0722 = 0.7874, so (0, 1 / 1.7874, 1 / 1.7874).
        const vsg::vec3 c = compress(vsg::vec3(-1.f, 1.f, 1.f));
        expectVec3Near(c, vsg::vec3(0.f, 1.f / 1.7874f, 1.f / 1.7874f), tol);
    }

    TEST(RenderTaaColour, compress_keeps_the_luma_below_one_even_for_very_bright_colours)
    {
        const std::vector<vsg::vec3> bright = { vsg::vec3(1e4f, 5e3f, 2e3f), vsg::vec3(1e4f, 0.f, 0.f),
            vsg::vec3(0.f, 1e4f, 0.f), vsg::vec3(100.f, 100.f, 100.f), vsg::vec3(1.f, 1.f, 1.f) };
        for (const auto& c : bright)
        {
            const float l = luma(compress(c));
            EXPECT_LT(l, 1.f);
            EXPECT_GE(l, 0.f);
        }
    }

    TEST(RenderTaaColour, expand_divides_by_one_minus_luma)
    {
        // Grey 0.5: luma 0.5, so 0.5 / 0.5 = 1. Grey 0.75: 0.75 / 0.25 = 3.
        expectVec3Near(expand(vsg::vec3(0.5f, 0.5f, 0.5f)), vsg::vec3(1.f, 1.f, 1.f), tol);
        expectVec3Near(expand(vsg::vec3(0.75f, 0.75f, 0.75f)), vsg::vec3(3.f, 3.f, 3.f), tol);
        expectVec3Near(expand(vsg::vec3(0.f, 0.f, 0.f)), vsg::vec3(0.f, 0.f, 0.f), tol);
    }

    TEST(RenderTaaColour, expand_limits_the_luma_so_the_result_stays_finite)
    {
        // Grey 1 has luma 1; limited to 0.9999 the divisor is 1 - 0.9999 = 1e-4, so the result is about 1e4.
        const vsg::vec3 limited = expand(vsg::vec3(1.f, 1.f, 1.f));
        EXPECT_TRUE(std::isfinite(limited.x));
        EXPECT_TRUE(std::isfinite(limited.y));
        EXPECT_TRUE(std::isfinite(limited.z));
        EXPECT_NEAR(limited.x, 1e4f, 1e4f * 2e-3f);

        // A luma above one (2 -> limited to 0.9999) is finite as well: 2 / 1e-4 = 2e4.
        const vsg::vec3 over = expand(vsg::vec3(2.f, 2.f, 2.f));
        EXPECT_TRUE(std::isfinite(over.x));
        EXPECT_NEAR(over.x, 2e4f, 2e4f * 2e-3f);
    }

    TEST(RenderTaaColour, expand_of_compress_is_the_identity_for_non_negative_colours)
    {
        const std::vector<vsg::vec3> colours = { vsg::vec3(0.05f, 0.02f, 0.01f), vsg::vec3(0.5f, 0.4f, 0.3f),
            vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(3.f, 1.f, 7.f),
            // luma 5846.4 (1e4 * 0.2126 + 5e3 * 0.7152 + 2e3 * 0.0722): compressed luma 1 - 1 / 5847.4, which is
            // under the 0.9999 limit, so the round trip is not affected by it.
            vsg::vec3(1e4f, 5e3f, 2e3f), vsg::vec3(1e4f, 0.f, 0.f) };
        for (const auto& c : colours)
        {
            expectVec3Near(expand(compress(c)), c, 5e-3f);
        }
    }

    TEST(RenderTaaColour, rgbToYCoCg_follows_the_spec_formula)
    {
        // (1, 0, 0): Y = (1 + 0 + 0) / 4 = 0.25, Co = (1 - 0) / 2 = 0.5, Cg = (-1 + 0 - 0) / 4 = -0.25.
        expectVec3Near(rgbToYCoCg(vsg::vec3(1.f, 0.f, 0.f)), vsg::vec3(0.25f, 0.5f, -0.25f), tol);
        // (0, 1, 0): Y = 2 / 4 = 0.5, Co = 0, Cg = 2 / 4 = 0.5.
        expectVec3Near(rgbToYCoCg(vsg::vec3(0.f, 1.f, 0.f)), vsg::vec3(0.5f, 0.f, 0.5f), tol);
        // (0, 0, 1): Y = 0.25, Co = -0.5, Cg = -0.25.
        expectVec3Near(rgbToYCoCg(vsg::vec3(0.f, 0.f, 1.f)), vsg::vec3(0.25f, -0.5f, -0.25f), tol);
        // (0.8, 0.4, 0.2): Y = (0.8 + 0.8 + 0.2) / 4 = 0.45, Co = (0.8 - 0.2) / 2 = 0.3,
        // Cg = (-0.8 + 0.8 - 0.2) / 4 = -0.05.
        expectVec3Near(rgbToYCoCg(vsg::vec3(0.8f, 0.4f, 0.2f)), vsg::vec3(0.45f, 0.3f, -0.05f), tol);
        // A grey has no chroma: (g, g, g) -> Y = (g + 2 g + g) / 4 = g, Co = 0, Cg = 0.
        expectVec3Near(rgbToYCoCg(vsg::vec3(0.6f, 0.6f, 0.6f)), vsg::vec3(0.6f, 0.f, 0.f), tol);
    }

    TEST(RenderTaaColour, ycocgToRgb_is_the_inverse_of_the_formula)
    {
        // The inverse of the example above: r = Y + Co - Cg = 0.45 + 0.3 + 0.05, g = Y + Cg = 0.45 - 0.05,
        // b = Y - Co - Cg = 0.45 - 0.3 + 0.05.
        expectVec3Near(ycocgToRgb(vsg::vec3(0.45f, 0.3f, -0.05f)), vsg::vec3(0.8f, 0.4f, 0.2f), tol);
        expectVec3Near(ycocgToRgb(vsg::vec3(0.25f, 0.5f, -0.25f)), vsg::vec3(1.f, 0.f, 0.f), tol);
    }

    TEST(RenderTaaColour, ycocg_round_trip)
    {
        const std::vector<vsg::vec3> colours
            = { vsg::vec3(0.8f, 0.4f, 0.2f), vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(1.f, 1.f, 1.f),
                  vsg::vec3(0.1f, 0.9f, 0.3f), vsg::vec3(5.f, 0.2f, 40.f), vsg::vec3(-0.2f, 0.3f, 0.1f) };
        for (const auto& c : colours)
        {
            expectVec3Near(ycocgToRgb(rgbToYCoCg(c)), c, tol);
        }
    }

    // ---- the neighbourhood box -----------------------------------------------------------------------------------

    TEST(RenderTaaBox, varianceBox_exact_example)
    {
        // Three channels, two samples each: {1, 3}, {2, 2} and {0, 4}.
        // Means (2, 2, 2). Means of squares: (1 + 9) / 2 = 5, (4 + 4) / 2 = 4, (0 + 16) / 2 = 8.
        // Variances: 5 - 4 = 1, 4 - 4 = 0, 8 - 4 = 4. Standard deviations: 1, 0, 2.
        const vsg::vec3 mean(2.f, 2.f, 2.f);
        const vsg::vec3 meanOfSquares(5.f, 4.f, 8.f);

        // gamma 1: mean +- sigma = [1, 3], [2, 2], [0, 4].
        const Box one = varianceBox(mean, meanOfSquares, 1.f);
        expectVec3Near(one.min, vsg::vec3(1.f, 2.f, 0.f), tol);
        expectVec3Near(one.max, vsg::vec3(3.f, 2.f, 4.f), tol);
    }

    TEST(RenderTaaBox, varianceBox_scales_with_gamma)
    {
        const vsg::vec3 mean(2.f, 2.f, 2.f);
        const vsg::vec3 meanOfSquares(5.f, 4.f, 8.f);

        // gamma 0.5: [2 - 0.5, 2 + 0.5], [2, 2], [2 - 1, 2 + 1].
        const Box half = varianceBox(mean, meanOfSquares, 0.5f);
        expectVec3Near(half.min, vsg::vec3(1.5f, 2.f, 1.f), tol);
        expectVec3Near(half.max, vsg::vec3(2.5f, 2.f, 3.f), tol);

        // gamma 2: [2 - 2, 2 + 2], [2, 2], [2 - 4, 2 + 4].
        const Box two = varianceBox(mean, meanOfSquares, 2.f);
        expectVec3Near(two.min, vsg::vec3(0.f, 2.f, -2.f), tol);
        expectVec3Near(two.max, vsg::vec3(4.f, 2.f, 6.f), tol);

        // gamma 0: the box is the mean.
        const Box zero = varianceBox(mean, meanOfSquares, 0.f);
        expectVec3Near(zero.min, mean, tol);
        expectVec3Near(zero.max, mean, tol);
    }

    TEST(RenderTaaBox, varianceBox_with_zero_variance_is_a_point_at_the_mean)
    {
        // Values chosen so that mean * mean is exact in floating point: 0.5^2 = 0.25, 0.25^2 = 0.0625, 2^2 = 4.
        const vsg::vec3 mean(0.5f, 0.25f, 2.f);
        const vsg::vec3 meanOfSquares(0.25f, 0.0625f, 4.f);
        const Box box = varianceBox(mean, meanOfSquares, varianceGamma);
        expectVec3Near(box.min, mean, 1e-6f);
        expectVec3Near(box.max, mean, 1e-6f);
    }

    TEST(RenderTaaBox, varianceBox_clamps_a_negative_variance_to_zero)
    {
        // mean 1, mean of squares 0.5: variance 0.5 - 1 = -0.5, taken as 0, so the box is the point 1.
        const Box box = varianceBox(vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(0.5f, 0.5f, 0.5f), 1.f);
        expectVec3Near(box.min, vsg::vec3(1.f, 1.f, 1.f), tol);
        expectVec3Near(box.max, vsg::vec3(1.f, 1.f, 1.f), tol);
        EXPECT_TRUE(std::isfinite(box.min.x));
        EXPECT_TRUE(std::isfinite(box.max.x));
    }

    TEST(RenderTaaBox, intersect_of_overlapping_boxes_is_the_overlap)
    {
        // x: [0, 4] with [1, 3] -> [1, 3]. y: [0, 2] with [1, 5] -> [1, 2]. z: [0, 1] with [-1, 0.5] -> [0, 0.5].
        const Box a{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(4.f, 2.f, 1.f) };
        const Box b{ vsg::vec3(1.f, 1.f, -1.f), vsg::vec3(3.f, 5.f, 0.5f) };
        const Box both = intersect(a, b);
        expectVec3Near(both.min, vsg::vec3(1.f, 1.f, 0.f), tol);
        expectVec3Near(both.max, vsg::vec3(3.f, 2.f, 0.5f), tol);

        // Symmetric.
        const Box swapped = intersect(b, a);
        expectVec3Near(swapped.min, both.min, tol);
        expectVec3Near(swapped.max, both.max, tol);
    }

    TEST(RenderTaaBox, intersect_of_an_empty_component_collapses_to_the_midpoint_of_the_nearest_edges)
    {
        // z: a is [0, 1], b is [3, 5]. The max of the mins is 3 and the min of the maxes is 1: empty. The nearest
        // edges are a's top (1) and b's bottom (3); their midpoint is 2. The other components overlap as usual.
        const Box a{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(4.f, 2.f, 1.f) };
        const Box b{ vsg::vec3(1.f, 1.f, 3.f), vsg::vec3(3.f, 5.f, 5.f) };
        const Box both = intersect(a, b);
        EXPECT_NEAR(both.min.z, 2.f, tol);
        EXPECT_NEAR(both.max.z, 2.f, tol);
        EXPECT_NEAR(both.min.x, 1.f, tol);
        EXPECT_NEAR(both.max.x, 3.f, tol);
        EXPECT_NEAR(both.min.y, 1.f, tol);
        EXPECT_NEAR(both.max.y, 2.f, tol);

        // The mirrored case: a is [3, 5], b is [0, 1]: nearest edges 3 and 1, midpoint 2.
        const Box c{ vsg::vec3(0.f, 0.f, 3.f), vsg::vec3(4.f, 2.f, 5.f) };
        const Box d{ vsg::vec3(1.f, 1.f, 0.f), vsg::vec3(3.f, 5.f, 1.f) };
        const Box mirrored = intersect(c, d);
        EXPECT_NEAR(mirrored.min.z, 2.f, tol);
        EXPECT_NEAR(mirrored.max.z, 2.f, tol);
    }

    TEST(RenderTaaBox, clipToBox_returns_an_inside_colour_unchanged_with_no_share)
    {
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 2.f, 2.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(1.5f, 0.5f, 1.f), box, &share);
        expectVec3Near(result, vsg::vec3(1.5f, 0.5f, 1.f), tol);
        EXPECT_NEAR(share, 0.f, tol);

        // A colour exactly on the surface is inside.
        float surfaceShare = -1.f;
        const vsg::vec3 onSurface = clipToBox(vsg::vec3(2.f, 1.f, 1.f), box, &surfaceShare);
        expectVec3Near(onSurface, vsg::vec3(2.f, 1.f, 1.f), tol);
        EXPECT_NEAR(surfaceShare, 0.f, tol);
    }

    TEST(RenderTaaBox, clipToBox_without_a_share_pointer_still_clips)
    {
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 2.f, 2.f) };
        const vsg::vec3 result = clipToBox(vsg::vec3(3.f, 1.f, 1.f), box);
        expectVec3Near(result, vsg::vec3(2.f, 1.f, 1.f), tol);
    }

    TEST(RenderTaaBox, clipToBox_moves_an_outside_colour_along_the_segment_from_the_centre)
    {
        // Box [0, 2]^3, centre (1, 1, 1). The colour (3, 1, 1) is two units from the centre along x; the segment
        // leaves the box at x = 2, one unit from the centre: result (2, 1, 1), moved distance 1 of 2: share 0.5.
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 2.f, 2.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(3.f, 1.f, 1.f), box, &share);
        expectVec3Near(result, vsg::vec3(2.f, 1.f, 1.f), tol);
        EXPECT_NEAR(share, 0.5f, tol);

        // (5, 1, 1) is four units from the centre; the point is still (2, 1, 1). The colour moves from x = 5 to
        // x = 2, a distance of 3, over the centre-to-colour distance of 4: share 0.75.
        float distantShare = -1.f;
        const vsg::vec3 distant = clipToBox(vsg::vec3(5.f, 1.f, 1.f), box, &distantShare);
        expectVec3Near(distant, vsg::vec3(2.f, 1.f, 1.f), tol);
        EXPECT_NEAR(distantShare, 0.75f, tol);
    }

    TEST(RenderTaaBox, clipToBox_diagonal_example)
    {
        // Centre (1, 1, 1); the colour (3, 3, 1) lies along (2, 2, 0). The segment p(t) = (1 + 2t, 1 + 2t, 1)
        // reaches x = 2 at t = 0.5: the point (2, 2, 1). It has moved from t = 1 to t = 0.5, half the way: share 0.5.
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 2.f, 2.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(3.f, 3.f, 1.f), box, &share);
        expectVec3Near(result, vsg::vec3(2.f, 2.f, 1.f), tol);
        EXPECT_NEAR(share, 0.5f, tol);
    }

    TEST(RenderTaaBox, clipToBox_non_cubic_box_example)
    {
        // Box [0, 2] x [0, 4] x [0, 2], centre (1, 2, 1). The colour (5, 3, 1) lies along (4, 1, 0). The segment
        // p(t) = (1 + 4t, 2 + t, 1) reaches x = 2 at t = 0.25 (y would reach 4 only at t = 2): the point
        // (2, 2.25, 1). Moved from t = 1 to t = 0.25: three quarters of the way: share 0.75.
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 4.f, 2.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(5.f, 3.f, 1.f), box, &share);
        expectVec3Near(result, vsg::vec3(2.f, 2.25f, 1.f), tol);
        EXPECT_NEAR(share, 0.75f, tol);
    }

    TEST(RenderTaaBox, clipToBox_result_is_inside_the_box_and_on_the_segment)
    {
        const Box box{ vsg::vec3(-1.f, 0.f, 2.f), vsg::vec3(1.f, 3.f, 3.f) };
        const vsg::vec3 centre(0.f, 1.5f, 2.5f);
        const std::vector<vsg::vec3> colours = { vsg::vec3(10.f, 1.f, 2.5f), vsg::vec3(-4.f, 8.f, 0.f),
            vsg::vec3(0.2f, 1.5f, 40.f), vsg::vec3(-30.f, -30.f, -30.f) };
        for (const auto& colour : colours)
        {
            float share = -1.f;
            const vsg::vec3 result = clipToBox(colour, box, &share);
            // Inside (with a small tolerance for rounding).
            EXPECT_GE(result.x, box.min.x - 1e-4f);
            EXPECT_LE(result.x, box.max.x + 1e-4f);
            EXPECT_GE(result.y, box.min.y - 1e-4f);
            EXPECT_LE(result.y, box.max.y + 1e-4f);
            EXPECT_GE(result.z, box.min.z - 1e-4f);
            EXPECT_LE(result.z, box.max.z + 1e-4f);
            // The colour was outside, so the share is strictly between 0 and 1.
            EXPECT_GT(share, 0.f);
            EXPECT_LT(share, 1.f);
            // On the segment from the centre to the colour: result = centre + s (colour - centre) with
            // s = 1 - share (the part of the way that was kept).
            const float kept = 1.f - share;
            EXPECT_NEAR(result.x, centre.x + kept * (colour.x - centre.x), 1e-3f);
            EXPECT_NEAR(result.y, centre.y + kept * (colour.y - centre.y), 1e-3f);
            EXPECT_NEAR(result.z, centre.z + kept * (colour.z - centre.z), 1e-3f);
        }
    }

    TEST(RenderTaaBox, clipToBox_of_the_centre_is_the_centre)
    {
        const Box box{ vsg::vec3(0.f, 0.f, 0.f), vsg::vec3(2.f, 2.f, 2.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(1.f, 1.f, 1.f), box, &share);
        expectVec3Near(result, vsg::vec3(1.f, 1.f, 1.f), tol);
        EXPECT_NEAR(share, 0.f, tol);
    }

    TEST(RenderTaaBox, clipToBox_with_a_degenerate_box_gives_the_finite_point)
    {
        // The box of a zero-variance neighbourhood is a point; the history clips to it. The spec does not spell this
        // case out; the result must at least be finite and be the point (the only point of the box).
        const Box point{ vsg::vec3(1.f, 2.f, 3.f), vsg::vec3(1.f, 2.f, 3.f) };
        float share = -1.f;
        const vsg::vec3 result = clipToBox(vsg::vec3(4.f, 2.f, 3.f), point, &share);
        EXPECT_TRUE(std::isfinite(result.x));
        EXPECT_TRUE(std::isfinite(result.y));
        EXPECT_TRUE(std::isfinite(result.z));
        expectVec3Near(result, vsg::vec3(1.f, 2.f, 3.f), 1e-4f);
    }

    // ---- the history weight --------------------------------------------------------------------------------------

    TEST(RenderTaaWeight, historyWeightFor_at_rest_is_the_base_weight)
    {
        EXPECT_NEAR(historyWeightFor(0.9f, 0.f), 0.9f, tol);
        EXPECT_NEAR(historyWeightFor(0.5f, 0.f), 0.5f, tol);
        EXPECT_NEAR(historyWeightFor(0.f, 0.f), 0.f, tol);
    }

    TEST(RenderTaaWeight, historyWeightFor_at_full_speed_is_half_the_base_weight)
    {
        // 0.9 * (1 - (1 - 0.5) * saturate(16 / 16)) = 0.9 * 0.5 = 0.45.
        EXPECT_NEAR(historyWeightFor(0.9f, velocityFullPixels), 0.45f, tol);
    }

    TEST(RenderTaaWeight, historyWeightFor_saturates_beyond_the_full_speed)
    {
        EXPECT_NEAR(historyWeightFor(0.9f, 32.f), 0.45f, tol);
        EXPECT_NEAR(historyWeightFor(0.9f, 1000.f), 0.45f, tol);
    }

    TEST(RenderTaaWeight, historyWeightFor_midway)
    {
        // Speed 8: saturate(8 / 16) = 0.5; 0.9 * (1 - 0.5 * 0.5) = 0.9 * 0.75 = 0.675.
        EXPECT_NEAR(historyWeightFor(0.9f, 8.f), 0.675f, tol);
        // Speed 4: saturate = 0.25; 0.9 * (1 - 0.5 * 0.25) = 0.9 * 0.875 = 0.7875.
        EXPECT_NEAR(historyWeightFor(0.9f, 4.f), 0.7875f, tol);
    }

    TEST(RenderTaaWeight, encodeDepth_is_log2_of_one_plus_depth_and_minus_one_for_sky)
    {
        EXPECT_NEAR(encodeDepth(0.f), -1.f, tol);
        EXPECT_NEAR(encodeDepth(-3.f), -1.f, tol);
        // log2(1 + 1) = 1, log2(1 + 3) = 2, log2(1 + 255) = 8, log2(1 + 0.5) = 0.5849625.
        EXPECT_NEAR(encodeDepth(1.f), 1.f, tol);
        EXPECT_NEAR(encodeDepth(3.f), 2.f, tol);
        EXPECT_NEAR(encodeDepth(255.f), 8.f, tol);
        EXPECT_NEAR(encodeDepth(0.5f), 0.5849625f, tol);
    }

    TEST(RenderTaaWeight, depthReject_of_a_sky_history_under_a_surface_is_a_full_reject)
    {
        EXPECT_NEAR(depthReject(10.f, -1.f), 1.f, tol);
        EXPECT_NEAR(depthReject(0.001f, -1.f), 1.f, tol);
    }

    TEST(RenderTaaWeight, depthReject_of_equal_depths_keeps_the_history)
    {
        // Expected depth 3 encodes to log2(4) = 2; a stored 2 is the same depth.
        EXPECT_NEAR(depthReject(3.f, 2.f), 0.f, tol);
        // Sky under sky: expected 0 encodes to -1, stored -1: equal.
        EXPECT_NEAR(depthReject(0.f, -1.f), 0.f, tol);
    }

    TEST(RenderTaaWeight, depthReject_at_the_low_edge_is_zero)
    {
        // |2 - 2.03| = 0.03 = depthRejectLow: smoothstep(0.03, 0.12, 0.03) = 0.
        EXPECT_NEAR(depthReject(3.f, 2.f + depthRejectLow), 0.f, 1e-4f);
        // Just inside: 0.01 under the low edge.
        EXPECT_NEAR(depthReject(3.f, 2.f + 0.02f), 0.f, tol);
    }

    TEST(RenderTaaWeight, depthReject_at_the_high_edge_is_one)
    {
        // |2 - 2.12| = 0.12 = depthRejectHigh: smoothstep(0.03, 0.12, 0.12) = 1.
        EXPECT_NEAR(depthReject(3.f, 2.f + depthRejectHigh), 1.f, 1e-4f);
        // Well beyond it.
        EXPECT_NEAR(depthReject(3.f, 2.5f), 1.f, tol);
        // The absolute difference counts: a stored depth smaller than expected is rejected as well.
        EXPECT_NEAR(depthReject(3.f, 1.5f), 1.f, tol);
    }

    TEST(RenderTaaWeight, depthReject_midway_is_the_smoothstep)
    {
        // Difference 0.075 is the middle of [0.03, 0.12]: t = (0.075 - 0.03) / 0.09 = 0.5; smoothstep(t) =
        // t * t * (3 - 2 t) = 0.25 * 2 = 0.5.
        EXPECT_NEAR(depthReject(3.f, 2.f + 0.075f), 0.5f, 1e-4f);
        // Stored below expected by 0.1: t = (0.1 - 0.03) / 0.09 = 7/9; smoothstep = (49/81) * (3 - 14/9) =
        // (49/81) * (13/9) = 637/729.
        EXPECT_NEAR(depthReject(3.f, 2.f - 0.1f), 637.f / 729.f, 1e-4f);
    }

    TEST(RenderTaaWeight,
        depthReject_of_a_surface_over_a_sky_history_handled_as_a_difference_when_expected_is_not_positive)
    {
        // Expected 0 (sky now) against a stored surface depth 0.5: encodeDepth(0) = -1, so the difference is
        // |-1 - 0.5| = 1.5, far above the high edge: a full reject.
        EXPECT_NEAR(depthReject(0.f, 0.5f), 1.f, tol);
    }

    TEST(RenderTaaWeight, finalHistoryWeight_is_zero_when_the_history_is_not_usable)
    {
        EXPECT_FLOAT_EQ(finalHistoryWeight(0.9f, 0.f, 0.f, false), 0.f);
        EXPECT_FLOAT_EQ(finalHistoryWeight(0.9f, 8.f, 0.3f, false), 0.f);
    }

    TEST(RenderTaaWeight, finalHistoryWeight_is_zero_for_a_full_reject)
    {
        EXPECT_NEAR(finalHistoryWeight(0.9f, 0.f, 1.f, true), 0.f, tol);
        EXPECT_NEAR(finalHistoryWeight(0.9f, 12.f, 1.f, true), 0.f, tol);
    }

    TEST(RenderTaaWeight, finalHistoryWeight_is_the_speed_weight_times_one_minus_the_reject)
    {
        // Reject 0: the speed weight alone: 0.9 * 0.75 = 0.675 at speed 8.
        EXPECT_NEAR(finalHistoryWeight(0.9f, 8.f, 0.f, true), 0.675f, tol);
        // Reject 0.25 at speed 8: 0.675 * 0.75 = 0.50625.
        EXPECT_NEAR(finalHistoryWeight(0.9f, 8.f, 0.25f, true), 0.50625f, tol);
        EXPECT_NEAR(finalHistoryWeight(0.9f, 8.f, 0.25f, true), historyWeightFor(0.9f, 8.f) * 0.75f, tol);
        // At rest with no reject the base weight comes through.
        EXPECT_NEAR(finalHistoryWeight(0.9f, 0.f, 0.f, true), 0.9f, tol);
    }

    // ---- the resolve and the sharpen -----------------------------------------------------------------------------

    TEST(RenderTaaResolve, weight_zero_gives_the_current_colour)
    {
        const vsg::vec3 current(0.2f, 0.5f, 0.1f);
        const vsg::vec3 history(4.f, 0.1f, 2.f);
        expectVec3Near(resolveColour(current, history, 0.f), current, tol);
    }

    TEST(RenderTaaResolve, weight_one_gives_the_history_colour)
    {
        const vsg::vec3 current(0.2f, 0.5f, 0.1f);
        const vsg::vec3 history(4.f, 0.1f, 2.f);
        expectVec3Near(resolveColour(current, history, 1.f), history, tol);
    }

    TEST(RenderTaaResolve, a_mid_weight_example_by_hand)
    {
        // current (1, 1, 1) compresses to 1 / (1 + 1) = 0.5, history (3, 3, 3) to 3 / (1 + 3) = 0.75.
        // Weight 0.25: 0.5 + 0.25 * (0.75 - 0.5) = 0.5625. Expand: 0.5625 / (1 - 0.5625) = (9/16) / (7/16) = 9/7.
        const vsg::vec3 result = resolveColour(vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(3.f, 3.f, 3.f), 0.25f);
        expectVec3Near(result, vsg::vec3(9.f / 7.f, 9.f / 7.f, 9.f / 7.f), tol);

        // Weight 0.5: lerp = 0.625; expand: 0.625 / 0.375 = 5/3 (a plain lerp of the colours would give 2).
        const vsg::vec3 half = resolveColour(vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(3.f, 3.f, 3.f), 0.5f);
        expectVec3Near(half, vsg::vec3(5.f / 3.f, 5.f / 3.f, 5.f / 3.f), tol);
    }

    TEST(RenderTaaResolve, a_very_bright_history_does_not_dominate_a_dim_current_pixel)
    {
        // current grey 0.1 compresses to 0.1 / 1.1 = 0.0909091; history grey 1e4 to 1e4 / 10001 = 0.99990001.
        // Weight 0.5: the mean is 0.5454045; expand: 0.5454045 / (1 - 0.5454045) = about 1.1998.
        // A plain lerp of the colours would give (0.1 + 1e4) / 2 = 5000.05: a lamp pixel in the history would
        // light the neighbourhood for many frames.
        const double a = 0.1 / 1.1;
        const double b = 1e4 / 10001.0;
        const double m = 0.5 * (a + b);
        const double expected = m / (1.0 - m);

        const vsg::vec3 result = resolveColour(vsg::vec3(0.1f, 0.1f, 0.1f), vsg::vec3(1e4f, 1e4f, 1e4f), 0.5f);
        EXPECT_NEAR(result.x, expected, expected * 2e-3);
        EXPECT_NEAR(result.y, expected, expected * 2e-3);
        EXPECT_NEAR(result.z, expected, expected * 2e-3);
        EXPECT_LT(result.x, 2.f);
        EXPECT_GT(result.x, 0.1f);
    }

    TEST(RenderTaaResolve, sharpenCross_with_zero_sharpness_returns_the_centre)
    {
        const vsg::vec3 centre(0.3f, 0.6f, 0.1f);
        const vsg::vec3 other(5.f, 0.f, 2.f);
        expectVec3Near(sharpenCross(centre, other, other, other, other, 0.f), centre, tol);
        // A negative sharpness is "<= 0" as well.
        expectVec3Near(sharpenCross(centre, other, other, other, other, -1.f), centre, tol);
    }

    TEST(RenderTaaResolve, sharpenCross_leaves_a_flat_neighbourhood_unchanged)
    {
        const vsg::vec3 flat(0.3f, 0.6f, 0.1f);
        expectVec3Near(sharpenCross(flat, flat, flat, flat, flat, 1.f), flat, tol);
        expectVec3Near(sharpenCross(flat, flat, flat, flat, flat, 0.25f), flat, tol);
        const vsg::vec3 bright(40.f, 20.f, 10.f);
        expectVec3Near(sharpenCross(bright, bright, bright, bright, bright, 1.f), bright, 1e-4f);
    }

    TEST(RenderTaaResolve, sharpenCross_brightens_a_centre_that_is_above_the_mean_of_its_neighbours)
    {
        // Compressed greys (c = g / (1 - g) is the colour of the compressed grey g):
        // centre 0.4, north 0.8, south, east and west 0.1. Mean of the four: (0.8 + 0.1 + 0.1 + 0.1) / 4 = 0.275.
        // With sharpness 1 (k = 1): 0.4 + (0.4 - 0.275) = 0.525, inside the range [0.1, 0.8] of the five, so not
        // limited. Expanded: 0.525 / 0.475.
        const vsg::vec3 centre = colourOfCompressedGrey(0.4f);
        const vsg::vec3 north = colourOfCompressedGrey(0.8f);
        const vsg::vec3 low = colourOfCompressedGrey(0.1f);
        const vsg::vec3 result = sharpenCross(centre, north, low, low, low, 1.f);

        const vsg::vec3 expected = colourOfCompressedGrey(0.525f);
        expectVec3Near(result, expected, 1e-4f);
        EXPECT_GT(result.x, centre.x);
    }

    TEST(RenderTaaResolve, sharpenCross_darkens_a_centre_that_is_below_the_mean_of_its_neighbours)
    {
        // Centre 0.4, north 0.1, south, east and west 0.8. Mean: (0.1 + 0.8 + 0.8 + 0.8) / 4 = 0.625.
        // With k = 1: 0.4 + (0.4 - 0.625) = 0.175, inside the range [0.1, 0.8]. Expanded: 0.175 / 0.825.
        const vsg::vec3 centre = colourOfCompressedGrey(0.4f);
        const vsg::vec3 north = colourOfCompressedGrey(0.1f);
        const vsg::vec3 high = colourOfCompressedGrey(0.8f);
        const vsg::vec3 result = sharpenCross(centre, north, high, high, high, 1.f);

        const vsg::vec3 expected = colourOfCompressedGrey(0.175f);
        expectVec3Near(result, expected, 1e-4f);
        EXPECT_LT(result.x, centre.x);
    }

    TEST(RenderTaaResolve, sharpenCross_never_goes_above_the_range_of_the_five)
    {
        // Centre 0.8, north 0.9, the rest 0.1. Mean: (0.9 + 0.1 + 0.1 + 0.1) / 4 = 0.3. With k = 1:
        // 0.8 + (0.8 - 0.3) = 1.3, over the largest of the five (0.9): limited to 0.9, which expands to 9. Even
        // with a smaller k (0.25 or more) the value is at or above 0.9 before the limit, so it ends at 0.9.
        const vsg::vec3 centre = colourOfCompressedGrey(0.8f);
        const vsg::vec3 north = colourOfCompressedGrey(0.9f);
        const vsg::vec3 low = colourOfCompressedGrey(0.1f);
        const vsg::vec3 result = sharpenCross(centre, north, low, low, low, 1.f);

        const vsg::vec3 ceiling = colourOfCompressedGrey(0.9f);
        EXPECT_LE(result.x, ceiling.x * (1.f + 1e-4f));
        expectVec3Near(result, ceiling, 1e-4f);
        EXPECT_GE(result.x, centre.x);
    }

    TEST(RenderTaaResolve, sharpenCross_never_goes_below_the_range_of_the_five)
    {
        // Centre 0.2, north 0.1, the rest 0.9. Mean: (0.1 + 0.9 + 0.9 + 0.9) / 4 = 0.7. With k = 1:
        // 0.2 + (0.2 - 0.7) = -0.3, under the smallest of the five (0.1): limited to 0.1, which expands to 1/9.
        const vsg::vec3 centre = colourOfCompressedGrey(0.2f);
        const vsg::vec3 north = colourOfCompressedGrey(0.1f);
        const vsg::vec3 high = colourOfCompressedGrey(0.9f);
        const vsg::vec3 result = sharpenCross(centre, north, high, high, high, 1.f);

        const vsg::vec3 floorColour = colourOfCompressedGrey(0.1f);
        EXPECT_GE(result.x, floorColour.x * (1.f - 1e-4f));
        expectVec3Near(result, floorColour, 1e-4f);
        EXPECT_LE(result.x, centre.x);
        EXPECT_GE(result.x, 0.f);
    }

    // ---- the history filter --------------------------------------------------------------------------------------

    TEST(RenderTaaFilter, catmullRom5_weights_sum_to_one)
    {
        const std::vector<vsg::vec2> fractions
            = { vsg::vec2(0.f, 0.f), vsg::vec2(0.25f, 0.25f), vsg::vec2(0.5f, 0.5f), vsg::vec2(0.9f, 0.1f),
                  vsg::vec2(0.1f, 0.7f), vsg::vec2(0.999f, 0.5f), vsg::vec2(0.f, 0.5f), vsg::vec2(0.5f, 0.f) };
        for (const auto& fraction : fractions)
        {
            const CatmullRom5 taps = catmullRom5(fraction);
            float sum = 0.f;
            for (int i = 0; i < 5; ++i)
            {
                sum += taps.weight[i];
            }
            EXPECT_NEAR(sum, 1.f, 1e-5f) << "fraction " << fraction.x << ", " << fraction.y;
        }
    }

    TEST(RenderTaaFilter, catmullRom5_at_a_texel_centre_reads_the_centre_texel)
    {
        // Fraction (0, 0): the sum of weight * offset is (0, 0) and the weights sum to 1.
        const CatmullRom5 taps = catmullRom5(vsg::vec2(0.f, 0.f));
        float sumWeights = 0.f;
        float meanX = 0.f;
        float meanY = 0.f;
        for (int i = 0; i < 5; ++i)
        {
            sumWeights += taps.weight[i];
            meanX += taps.weight[i] * taps.offset[i].x;
            meanY += taps.weight[i] * taps.offset[i].y;
        }
        EXPECT_NEAR(sumWeights, 1.f, 1e-5f);
        EXPECT_NEAR(meanX, 0.f, 1e-5f);
        EXPECT_NEAR(meanY, 0.f, 1e-5f);
    }

    TEST(RenderTaaFilter, catmullRom5_on_a_constant_image_gives_the_constant)
    {
        // Bilinear reads of a constant image return the constant at every tap, so the filtered value is the
        // constant times the sum of the weights.
        const float constant = 0.37f;
        const std::vector<vsg::vec2> fractions
            = { vsg::vec2(0.f, 0.f), vsg::vec2(0.3f, 0.6f), vsg::vec2(0.75f, 0.25f) };
        for (const auto& fraction : fractions)
        {
            const CatmullRom5 taps = catmullRom5(fraction);
            float value = 0.f;
            for (int i = 0; i < 5; ++i)
            {
                value += taps.weight[i] * constant;
            }
            EXPECT_NEAR(value, constant, 1e-5f) << "fraction " << fraction.x << ", " << fraction.y;
        }
    }

    // ---- reprojection --------------------------------------------------------------------------------------------

    TEST(RenderTaaReproject, previousUv_is_uv_minus_velocity)
    {
        // (0.5, 0.25) - (0.1, -0.05) = (0.4, 0.3).
        const vsg::vec2 previous = previousUv(vsg::vec2(0.5f, 0.25f), vsg::vec2(0.1f, -0.05f));
        EXPECT_NEAR(previous.x, 0.4f, tol);
        EXPECT_NEAR(previous.y, 0.3f, tol);

        const vsg::vec2 still = previousUv(vsg::vec2(0.2f, 0.9f), vsg::vec2(0.f, 0.f));
        EXPECT_NEAR(still.x, 0.2f, tol);
        EXPECT_NEAR(still.y, 0.9f, tol);
    }

    TEST(RenderTaaReproject, viewPositionFromDepth_hand_example)
    {
        // projectionXY (1.5, -2), uv (0.75, 0.25), depth 10:
        // x = (2 * 0.75 - 1) * 10 / 1.5 = 0.5 * 10 / 1.5 = 10/3; y = (2 * 0.25 - 1) * 10 / -2 = -5 / -2 = 2.5; z = -10.
        const vsg::vec3 p = viewPositionFromDepth(vsg::vec2(0.75f, 0.25f), 10.f, vsg::vec2(1.5f, -2.f));
        EXPECT_NEAR(p.x, 10.f / 3.f, tol);
        EXPECT_NEAR(p.y, 2.5f, tol);
        EXPECT_NEAR(p.z, -10.f, tol);
    }

    TEST(RenderTaaReproject, viewPositionFromDepth_at_the_screen_centre_is_on_the_view_axis)
    {
        const vsg::vec3 p = viewPositionFromDepth(vsg::vec2(0.5f, 0.5f), 7.f, vsg::vec2(1.5f, -2.f));
        EXPECT_NEAR(p.x, 0.f, tol);
        EXPECT_NEAR(p.y, 0.f, tol);
        EXPECT_NEAR(p.z, -7.f, tol);
    }

    TEST(RenderTaaReproject, expectedPreviousDepth_with_the_identity_is_one)
    {
        // identity * (p, 1) has w = 1 whatever p is.
        const vsg::mat4 identity;
        EXPECT_NEAR(expectedPreviousDepth(vsg::vec3(1.f, 2.f, -3.f), identity), 1.f, tol);
        EXPECT_NEAR(expectedPreviousDepth(vsg::vec3(-40.f, 7.f, 100.f), identity), 1.f, tol);
    }

    TEST(RenderTaaReproject, expectedPreviousDepth_reads_the_w_row)
    {
        // w = m(0,3) x + m(1,3) y + m(2,3) z + m(3,3) with (column, row) indexing: row 3 of the matrix.
        // 0.5 * 1 + 0.25 * 2 + (-1) * (-3) + 2 = 0.5 + 0.5 + 3 + 2 = 6.
        vsg::mat4 m;
        m(0, 3) = 0.5f;
        m(1, 3) = 0.25f;
        m(2, 3) = -1.f;
        m(3, 3) = 2.f;
        EXPECT_NEAR(expectedPreviousDepth(vsg::vec3(1.f, 2.f, -3.f), m), 6.f, tol);
    }

    TEST(RenderTaaReproject, expectedPreviousDepth_of_a_perspective_matrix_is_the_distance_along_the_view_axis)
    {
        // clip.w = -z: the point (x, y, -10) is 10 units in front of the camera.
        EXPECT_NEAR(expectedPreviousDepth(vsg::vec3(3.f, 2.f, -10.f), projectionLike()), 10.f, tol);
        EXPECT_NEAR(expectedPreviousDepth(vsg::vec3(-1.f, 0.f, -2.5f), projectionLike()), 2.5f, tol);
    }

    TEST(RenderTaaReproject, skyPreviousUv_with_an_unmoved_camera_returns_the_same_uv)
    {
        // uv (0.75, 0.25) with projectionXY (1.5, -2) is the direction (x, y, -1, 0) with x = 0.5 / 1.5 = 1/3 and
        // y = -0.5 / -2 = 0.25. The projection-like matrix sends it to clip (1.5 / 3, -2 * 0.25, ., 1) =
        // (0.5, -0.5, ., 1): ndc (0.5, -0.5), uv (0.75, 0.25).
        vsg::vec2 out(-100.f, -100.f);
        const bool ok = skyPreviousUv(vsg::vec2(0.75f, 0.25f), vsg::vec2(1.5f, -2.f), projectionLike(), out);
        EXPECT_TRUE(ok);
        EXPECT_NEAR(out.x, 0.75f, tol);
        EXPECT_NEAR(out.y, 0.25f, tol);
    }

    TEST(RenderTaaReproject, skyPreviousUv_ignores_the_translation_of_the_matrix)
    {
        // The direction has w = 0, so a translation in the matrix's last column does not move it: only the
        // rotation part counts.
        const vsg::mat4 moved = projectionLike() * vsg::translate(5.f, 6.f, 7.f);
        vsg::vec2 out(-100.f, -100.f);
        const bool ok = skyPreviousUv(vsg::vec2(0.75f, 0.25f), vsg::vec2(1.5f, -2.f), moved, out);
        EXPECT_TRUE(ok);
        EXPECT_NEAR(out.x, 0.75f, tol);
        EXPECT_NEAR(out.y, 0.25f, tol);
    }

    TEST(RenderTaaReproject, skyPreviousUv_at_the_screen_centre_is_the_centre)
    {
        vsg::vec2 out(-100.f, -100.f);
        const bool ok = skyPreviousUv(vsg::vec2(0.5f, 0.5f), vsg::vec2(1.5f, -2.f), projectionLike(), out);
        EXPECT_TRUE(ok);
        EXPECT_NEAR(out.x, 0.5f, tol);
        EXPECT_NEAR(out.y, 0.5f, tol);
    }

    TEST(RenderTaaReproject, skyPreviousUv_fails_when_the_direction_is_behind_the_previous_camera)
    {
        // The w of the direction through the matrix is 0 for the identity (nothing feeds the w row) and negative
        // when the perspective sign is flipped (m(2,3) = +1 gives w = +z = -1 for z = -1): both are w <= 0.
        vsg::vec2 out(-100.f, -100.f);
        const vsg::mat4 identity;
        EXPECT_FALSE(skyPreviousUv(vsg::vec2(0.75f, 0.25f), vsg::vec2(1.5f, -2.f), identity, out));

        vsg::mat4 flipped = projectionLike();
        flipped(2, 3) = 1.f;
        EXPECT_FALSE(skyPreviousUv(vsg::vec2(0.75f, 0.25f), vsg::vec2(1.5f, -2.f), flipped, out));
    }
}
