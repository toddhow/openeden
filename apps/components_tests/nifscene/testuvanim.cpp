// Task 0359: the UV controllers of a Skyrim effect shader as pure maths (components/nifscene/uvanim.*). Values are
// worked out by hand; no game asset is read.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <components/nifscene/uvanim.hpp>

namespace
{
    using namespace NifScene;

    FloatTrack ramp(std::uint32_t interpolation = 1, std::uint32_t cycle = 0)
    {
        FloatTrack t;
        t.mKeys = { { 0.f, 0.f, 0.f, 0.f }, { 4.f, 1.f, 0.f, 0.f } };
        t.mInterpolation = interpolation;
        t.mStart = 0.f;
        t.mStop = 4.f;
        t.mCycle = cycle;
        return t;
    }

    TEST(NifSceneUvAnim, a_linear_track_is_read_between_its_keys)
    {
        const FloatTrack t = ramp();
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 0.0), 0.f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 1.0), 0.25f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 3.0), 0.75f);
    }

    TEST(NifSceneUvAnim, a_cycle_wraps_back_to_the_start_of_its_window)
    {
        const FloatTrack t = ramp(1, 0);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 4.0), 0.f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 5.0), 0.25f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 13.0), 0.25f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, -1.0), 0.75f); // a negative time wraps the other way
    }

    TEST(NifSceneUvAnim, reverse_runs_the_window_forwards_then_backwards)
    {
        const FloatTrack t = ramp(1, 1);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 3.0), 0.75f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 5.0), 0.75f); // 3 seconds from the end on the way back
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 8.0), 0.f);
    }

    TEST(NifSceneUvAnim, constant_holds_the_last_value)
    {
        const FloatTrack t = ramp(1, 2);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 10.0), 1.f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, -3.0), 0.f);
    }

    TEST(NifSceneUvAnim, frequency_and_phase_move_the_controllers_own_clock)
    {
        FloatTrack t = ramp();
        t.mFrequency = 2.f;
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 1.0), 0.5f); // controller time 2
        t.mFrequency = 1.f;
        t.mPhase = 1.f;
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 0.0), 0.25f);
    }

    TEST(NifSceneUvAnim, a_quadratic_key_pair_with_flat_tangents_eases_in_and_out)
    {
        const FloatTrack t = ramp(2);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 2.0), 0.5f);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 1.0), 0.15625f); // 3t^2 - 2t^3 at t = 1/4
    }

    TEST(NifSceneUvAnim, a_quadratic_pair_whose_tangents_equal_the_slope_is_a_straight_line)
    {
        FloatTrack t;
        t.mKeys = { { 0.f, 0.f, 1.f, 1.f }, { 1.f, 1.f, 1.f, 1.f } };
        t.mInterpolation = 2;
        t.mStop = 1.f;
        EXPECT_NEAR(UvAnim::sample(t, 0.25), 0.25, 1e-6);
        EXPECT_NEAR(UvAnim::sample(t, 0.5), 0.5, 1e-6);
    }

    TEST(NifSceneUvAnim, a_constant_interpolation_holds_each_value_until_the_next_key)
    {
        const FloatTrack t = ramp(5);
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 3.9), 0.f);
    }

    TEST(NifSceneUvAnim, a_single_key_is_its_value_and_an_empty_track_is_zero)
    {
        FloatTrack one;
        one.mKeys = { { 0.f, 0.7f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(UvAnim::sample(one, 12.0), 0.7f);
        EXPECT_FLOAT_EQ(UvAnim::sample(FloatTrack{}, 1.0), 0.f);
    }

    TEST(NifSceneUvAnim, a_clock_that_is_not_a_number_gives_zero)
    {
        EXPECT_FLOAT_EQ(UvAnim::sample(ramp(), std::numeric_limits<double>::quiet_NaN()), 0.f);
        EXPECT_FLOAT_EQ(UvAnim::sample(ramp(), std::numeric_limits<double>::infinity()), 0.f);
    }

    TEST(NifSceneUvAnim, a_window_of_zero_length_is_its_first_key)
    {
        FloatTrack t = ramp();
        t.mStop = 0.f;
        EXPECT_FLOAT_EQ(UvAnim::sample(t, 2.0), 0.f);
    }

    TEST(NifSceneUvAnim, the_matrix_scrolls_v_and_keeps_the_stored_values_of_the_other_components)
    {
        UvAnimation a;
        a.mOffset[1] = ramp();
        a.mBaseOffset = { 0.1f, 0.9f };
        a.mBaseScale = { 2.f, 3.f };
        const auto m = UvAnim::matrixAt(a, 1.0);
        EXPECT_FLOAT_EQ(m[0], 2.f); // U scale, from the property
        EXPECT_FLOAT_EQ(m[5], 3.f); // V scale
        EXPECT_FLOAT_EQ(m[10], 1.f);
        EXPECT_FLOAT_EQ(m[15], 1.f);
        EXPECT_FLOAT_EQ(m[12], 0.1f); // U offset, from the property
        EXPECT_FLOAT_EQ(m[13], 0.25f); // V offset, from the controller (not the stored 0.9)
        EXPECT_FLOAT_EQ(m[1], 0.f);
        EXPECT_FLOAT_EQ(m[4], 0.f);
    }

    TEST(NifSceneUvAnim, scale_tracks_replace_the_stored_scale)
    {
        UvAnimation a;
        a.mScale[0] = ramp();
        const auto m = UvAnim::matrixAt(a, 2.0);
        EXPECT_FLOAT_EQ(m[0], 0.5f);
        EXPECT_FLOAT_EQ(m[5], 1.f);
    }

    TEST(NifSceneUvAnim, an_animation_with_no_track_is_not_animated)
    {
        EXPECT_FALSE(UvAnimation{}.any());
        UvAnimation a;
        a.mScale[1] = ramp();
        EXPECT_TRUE(a.any());
    }
}
