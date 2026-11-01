#include <gtest/gtest.h>

#include <components/render/hdrmath.hpp>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cmath>
#include <limits>
#include <optional>

namespace
{
    using namespace Render::Hdr;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN();

    TEST(RenderHdrMathTest, displayGamma_is_2_2)
    {
        EXPECT_FLOAT_EQ(displayGamma, 2.2f);
    }

    TEST(RenderHdrMathTest, toLinear_fixes_zero_and_one)
    {
        EXPECT_EQ(toLinear(0.f), 0.f);
        EXPECT_NEAR(toLinear(1.f), 1.f, 1e-6f);
    }

    TEST(RenderHdrMathTest, toEncoded_fixes_zero_and_one)
    {
        EXPECT_EQ(toEncoded(0.f), 0.f);
        EXPECT_NEAR(toEncoded(1.f), 1.f, 1e-6f);
    }

    TEST(RenderHdrMathTest, toLinear_is_a_pure_power_law)
    {
        for (float x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
            EXPECT_NEAR(toLinear(x), std::pow(x, 2.2f), 1e-5f) << x;
        // 0.5^2.2 is about 0.2176
        EXPECT_NEAR(toLinear(0.5f), 0.2176f, 1e-4f);
    }

    TEST(RenderHdrMathTest, toEncoded_is_the_inverse_power_law)
    {
        for (float x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
            EXPECT_NEAR(toEncoded(x), std::pow(x, 1.f / 2.2f), 1e-5f) << x;
    }

    TEST(RenderHdrMathTest, toLinear_accepts_values_above_one)
    {
        EXPECT_NEAR(toLinear(2.f), std::pow(2.f, 2.2f), 1e-4f);
    }

    TEST(RenderHdrMathTest, round_trip_encoded_to_linear_to_encoded)
    {
        for (int i = 0; i <= 200; ++i)
        {
            const float x = static_cast<float>(i) / 100.f; // 0 to 2
            EXPECT_NEAR(toEncoded(toLinear(x)), x, 1e-4f) << x;
        }
    }

    TEST(RenderHdrMathTest, round_trip_linear_to_encoded_to_linear)
    {
        for (int i = 0; i <= 200; ++i)
        {
            const float x = static_cast<float>(i) / 100.f;
            EXPECT_NEAR(toLinear(toEncoded(x)), x, 1e-4f) << x;
        }
    }

    TEST(RenderHdrMathTest, negative_inputs_keep_their_sign)
    {
        EXPECT_LT(toLinear(-0.5f), 0.f);
        EXPECT_LT(toEncoded(-0.5f), 0.f);
        EXPECT_NEAR(toLinear(-0.5f), -toLinear(0.5f), 1e-6f);
        EXPECT_NEAR(toEncoded(-0.5f), -toEncoded(0.5f), 1e-6f);
    }

    TEST(RenderHdrMathTest, negative_finite_inputs_give_no_nan)
    {
        for (float x : { -1e-6f, -0.01f, -0.5f, -1.f, -4.f, -100.f })
        {
            EXPECT_FALSE(std::isnan(toLinear(x))) << x;
            EXPECT_FALSE(std::isnan(toEncoded(x))) << x;
        }
    }

    TEST(RenderHdrMathTest, negative_round_trip)
    {
        for (float x : { -0.1f, -0.5f, -1.f, -2.f })
            EXPECT_NEAR(toEncoded(toLinear(x)), x, 1e-4f) << x;
    }

    TEST(RenderHdrMathTest, nan_in_gives_nan_out)
    {
        EXPECT_TRUE(std::isnan(toLinear(nan)));
        EXPECT_TRUE(std::isnan(toEncoded(nan)));
    }

    TEST(RenderHdrMathTest, vec3_overloads_convert_per_channel)
    {
        const vsg::vec3 encoded(0.2f, 0.5f, 0.9f);
        const vsg::vec3 linear = toLinear(encoded);
        EXPECT_NEAR(linear.x, toLinear(0.2f), 1e-6f);
        EXPECT_NEAR(linear.y, toLinear(0.5f), 1e-6f);
        EXPECT_NEAR(linear.z, toLinear(0.9f), 1e-6f);

        const vsg::vec3 back = toEncoded(linear);
        EXPECT_NEAR(back.x, 0.2f, 1e-5f);
        EXPECT_NEAR(back.y, 0.5f, 1e-5f);
        EXPECT_NEAR(back.z, 0.9f, 1e-5f);
    }

    TEST(RenderHdrMathTest, vec3_toEncoded_matches_the_scalar)
    {
        const vsg::vec3 linear(0.05f, 0.3f, 1.f);
        const vsg::vec3 encoded = toEncoded(linear);
        EXPECT_NEAR(encoded.x, toEncoded(0.05f), 1e-6f);
        EXPECT_NEAR(encoded.y, toEncoded(0.3f), 1e-6f);
        EXPECT_NEAR(encoded.z, toEncoded(1.f), 1e-6f);
    }

    TEST(RenderHdrMathTest, vec4_toLinear_leaves_alpha_untouched)
    {
        const vsg::vec4 encoded(0.2f, 0.5f, 0.9f, 0.5f);
        const vsg::vec4 linear = toLinear(encoded);
        EXPECT_NEAR(linear.x, toLinear(0.2f), 1e-6f);
        EXPECT_NEAR(linear.y, toLinear(0.5f), 1e-6f);
        EXPECT_NEAR(linear.z, toLinear(0.9f), 1e-6f);
        EXPECT_EQ(linear.w, 0.5f);
    }

    TEST(RenderHdrMathTest, vec4_toLinear_keeps_alpha_outside_zero_to_one)
    {
        EXPECT_EQ(toLinear(vsg::vec4(0.5f, 0.5f, 0.5f, 0.f)).w, 0.f);
        EXPECT_EQ(toLinear(vsg::vec4(0.5f, 0.5f, 0.5f, 1.f)).w, 1.f);
        EXPECT_EQ(toLinear(vsg::vec4(0.5f, 0.5f, 0.5f, 3.f)).w, 3.f);
    }

    TEST(RenderHdrMathTest, product_of_encoded_values_is_the_encoded_product_of_linear_ones)
    {
        for (float a : { 0.1f, 0.4f, 0.8f, 1.f })
            for (float b : { 0.2f, 0.5f, 0.9f, 1.f })
                EXPECT_NEAR(toEncoded(toLinear(a) * toLinear(b)), a * b, 1e-4f) << a << " " << b;
    }

    TEST(RenderHdrMathTest, parseTonemap_accepts_the_three_names)
    {
        ASSERT_TRUE(parseTonemap("vanilla").has_value());
        ASSERT_TRUE(parseTonemap("reinhard").has_value());
        ASSERT_TRUE(parseTonemap("aces").has_value());
        EXPECT_EQ(*parseTonemap("vanilla"), Tonemap::Vanilla);
        EXPECT_EQ(*parseTonemap("reinhard"), Tonemap::Reinhard);
        EXPECT_EQ(*parseTonemap("aces"), Tonemap::Aces);
    }

    TEST(RenderHdrMathTest, parseTonemap_refuses_unknown_empty_and_uppercase)
    {
        EXPECT_EQ(parseTonemap("filmic"), std::nullopt);
        EXPECT_EQ(parseTonemap(""), std::nullopt);
        EXPECT_EQ(parseTonemap("Vanilla"), std::nullopt);
        EXPECT_EQ(parseTonemap("ACES"), std::nullopt);
        EXPECT_EQ(parseTonemap("Reinhard"), std::nullopt);
        EXPECT_EQ(parseTonemap(" aces"), std::nullopt);
        EXPECT_EQ(parseTonemap("aces "), std::nullopt);
    }

    TEST(RenderHdrMathTest, exposureScale_is_two_to_the_stops)
    {
        EXPECT_FLOAT_EQ(exposureScale(0.f), 1.f);
        EXPECT_FLOAT_EQ(exposureScale(1.f), 2.f);
        EXPECT_FLOAT_EQ(exposureScale(-1.f), 0.5f);
        EXPECT_FLOAT_EQ(exposureScale(3.f), 8.f);
        EXPECT_FLOAT_EQ(exposureScale(-2.f), 0.25f);
    }

    TEST(RenderHdrMathTest, tonemap_vanilla_is_a_clamp)
    {
        EXPECT_EQ(tonemapChannel(Tonemap::Vanilla, 0.f), 0.f);
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Vanilla, 0.5f), 0.5f);
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Vanilla, 0.25f), 0.25f);
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Vanilla, 1.f), 1.f);
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Vanilla, 1.5f), 1.f);
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Vanilla, 1e6f), 1.f);
    }

    TEST(RenderHdrMathTest, tonemap_reinhard_matches_the_extended_formula_by_hand)
    {
        // x * (1 + x / 16) / (1 + x)
        EXPECT_EQ(tonemapChannel(Tonemap::Reinhard, 0.f), 0.f);
        EXPECT_NEAR(tonemapChannel(Tonemap::Reinhard, 1.f), 0.53125f, 1e-5f);
        EXPECT_NEAR(tonemapChannel(Tonemap::Reinhard, 0.5f), 0.34375f, 1e-5f);
        EXPECT_NEAR(tonemapChannel(Tonemap::Reinhard, 2.f), 0.75f, 1e-5f);
        // The white point maps to exactly 1.
        EXPECT_NEAR(tonemapChannel(Tonemap::Reinhard, 4.f), 1.f, 1e-5f);
        // Beyond the white point the formula exceeds 1 (16 gives 32/17) and is clamped.
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Reinhard, 16.f), 1.f);
    }

    TEST(RenderHdrMathTest, tonemap_aces_matches_the_narkowicz_fit_by_hand)
    {
        // x * (2.51 x + 0.03) / (x * (2.43 x + 0.59) + 0.14)
        EXPECT_EQ(tonemapChannel(Tonemap::Aces, 0.f), 0.f);
        // 1: 2.54 / 3.16
        EXPECT_NEAR(tonemapChannel(Tonemap::Aces, 1.f), 2.54f / 3.16f, 1e-5f);
        // 0.5: 0.6425 / 1.0425
        EXPECT_NEAR(tonemapChannel(Tonemap::Aces, 0.5f), 0.6425f / 1.0425f, 1e-5f);
        // The fit exceeds 1 for large input (limit 2.51 / 2.43) and is clamped.
        EXPECT_FLOAT_EQ(tonemapChannel(Tonemap::Aces, 100.f), 1.f);
    }

    TEST(RenderHdrMathTest, tonemap_negative_input_gives_zero)
    {
        for (Tonemap curve : { Tonemap::Vanilla, Tonemap::Reinhard, Tonemap::Aces })
        {
            EXPECT_EQ(tonemapChannel(curve, -0.001f), 0.f);
            EXPECT_EQ(tonemapChannel(curve, -1.f), 0.f);
            EXPECT_EQ(tonemapChannel(curve, -1e6f), 0.f);
        }
    }

    TEST(RenderHdrMathTest, tonemap_output_is_in_zero_to_one_for_finite_inputs)
    {
        for (Tonemap curve : { Tonemap::Vanilla, Tonemap::Reinhard, Tonemap::Aces })
        {
            for (float x : { -1e6f, -3.f, 0.f, 1e-6f, 0.01f, 0.5f, 1.f, 3.f, 4.f, 10.f, 1000.f, 1e6f, 1e30f })
            {
                const float y = tonemapChannel(curve, x);
                EXPECT_FALSE(std::isnan(y)) << x;
                EXPECT_GE(y, 0.f) << x;
                EXPECT_LE(y, 1.f) << x;
            }
        }
    }

    TEST(RenderHdrMathTest, tonemap_is_monotonic_non_decreasing)
    {
        for (Tonemap curve : { Tonemap::Vanilla, Tonemap::Reinhard, Tonemap::Aces })
        {
            float previous = tonemapChannel(curve, -1.f);
            for (int i = -100; i <= 4000; ++i)
            {
                const float x = static_cast<float>(i) * 0.01f; // -1 to 40
                const float y = tonemapChannel(curve, x);
                EXPECT_GE(y, previous - 1e-6f) << "curve " << static_cast<int>(curve) << " at " << x;
                previous = y;
            }
        }
    }

    TEST(RenderHdrMathTest, resolveTonemap_steps_aside_for_an_hdr_technique)
    {
        const TonemapChoice choice = resolveTonemap(Tonemap::Aces, 2.5f, true);
        EXPECT_EQ(choice.curve, Tonemap::Vanilla);
        EXPECT_EQ(choice.exposureStops, 0.f);
        EXPECT_TRUE(choice.steppedAside);
    }

    TEST(RenderHdrMathTest, resolveTonemap_passes_through_otherwise)
    {
        const TonemapChoice choice = resolveTonemap(Tonemap::Reinhard, -1.5f, false);
        EXPECT_EQ(choice.curve, Tonemap::Reinhard);
        EXPECT_EQ(choice.exposureStops, -1.5f);
        EXPECT_FALSE(choice.steppedAside);

        const TonemapChoice vanilla = resolveTonemap(Tonemap::Vanilla, 0.f, false);
        EXPECT_EQ(vanilla.curve, Tonemap::Vanilla);
        EXPECT_EQ(vanilla.exposureStops, 0.f);
        EXPECT_FALSE(vanilla.steppedAside);
    }

    TEST(RenderHdrMathTest, useHdrPath_is_true_only_when_both_are_true)
    {
        EXPECT_TRUE(useHdrPath(true, true));
        EXPECT_FALSE(useHdrPath(true, false));
        EXPECT_FALSE(useHdrPath(false, true));
        EXPECT_FALSE(useHdrPath(false, false));
    }
}
