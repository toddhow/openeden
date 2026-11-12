// Task 0096 (stage c, first slice): the pure maths of far-field wind sway. Which models may sway (a name list), a phase
// from an object's position, and the displacement of a point at a height above the object's base: anchored at the base,
// capped, deterministic, periodic, finite for any input. No files, no VFS, no game data, no GPU.
#include <gtest/gtest.h>

#include <components/lod/clustersway.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using namespace Lod;

    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
    constexpr float twoPi = 6.28318530718f;

    struct Out
    {
        float v[2];
    };

    // lean, wind, height, objectHeight, phase, time
    Out sway(float lean, float wind, float height, float objectHeight, float phase, double seconds)
    {
        Out out{ { 123.f, 456.f } };
        swayDisplacement(lean, wind, height, objectHeight, phase, seconds, out.v);
        return out;
    }
}

TEST(LodClusterSway, the_name_list_matches_substrings_case_insensitively)
{
    EXPECT_TRUE(swayCapableModel("Meshes\\f\\Flora_Tree_Ash_01.nif", "flora_tree"));
    EXPECT_TRUE(swayCapableModel("meshes/f/furn_imp_flag_01.nif", "banner, flag"));
    EXPECT_TRUE(swayCapableModel("meshes/f/furn_imp_flag_01.nif", "BANNER;FLAG"));
    EXPECT_TRUE(swayCapableModel("meshes/f/ex_v_ban_01.nif", "flag\nex_v_ban"));
    EXPECT_FALSE(swayCapableModel("meshes/f/ex_common_house_01.nif", "flora_tree, flag"));
}

TEST(LodClusterSway, an_empty_list_or_empty_tokens_match_nothing)
{
    EXPECT_FALSE(swayCapableModel("meshes/f/flag.nif", ""));
    EXPECT_FALSE(swayCapableModel("meshes/f/flag.nif", " , ;\n ,"));
    EXPECT_FALSE(swayCapableModel("", "flag"));
}

TEST(LodClusterSway, the_phase_is_deterministic_and_in_range)
{
    EXPECT_EQ(swayPhase(1234.5f, -987.25f), swayPhase(1234.5f, -987.25f));
    for (int i = -50; i < 50; ++i)
    {
        const float phase = swayPhase(static_cast<float>(i) * 133.7f, static_cast<float>(i * i) * 17.1f);
        EXPECT_GE(phase, 0.f);
        EXPECT_LT(phase, twoPi);
    }
}

TEST(LodClusterSway, neighbouring_objects_do_not_share_one_phase)
{
    int different = 0;
    for (int i = 0; i < 64; ++i)
        if (swayPhase(static_cast<float>(i) * 200.f, 0.f) != swayPhase(static_cast<float>(i + 1) * 200.f, 0.f))
            ++different;
    EXPECT_GT(different, 55);
}

TEST(LodClusterSway, the_phase_of_a_bad_position_is_zero)
{
    EXPECT_EQ(swayPhase(kNan, 0.f), 0.f);
    EXPECT_EQ(swayPhase(0.f, kInf), 0.f);
}

TEST(LodClusterSway, the_base_does_not_move)
{
    for (double t = 0; t < 10; t += 0.37)
    {
        const Out out = sway(0.05f, 1.f, 0.f, 500.f, 1.f, t);
        EXPECT_EQ(out.v[0], 0.f);
        EXPECT_EQ(out.v[1], 0.f);
    }
}

TEST(LodClusterSway, displacement_grows_with_height_and_never_exceeds_the_lean_cap)
{
    float previousPeak = -1.f;
    for (const float height : { 50.f, 150.f, 300.f, 500.f })
    {
        float peak = 0.f;
        for (double t = 0; t < 12; t += 0.05)
        {
            const Out out = sway(maxSwayLean, 1.f, height, 500.f, 0.7f, t);
            peak = std::max(peak, std::fabs(out.v[0]));
            EXPECT_LE(std::fabs(out.v[0]), maxSwayLean * 500.f + 1e-3f);
            EXPECT_LE(std::fabs(out.v[1]), 0.25f * maxSwayLean * 500.f + 1e-3f);
        }
        EXPECT_GT(peak, previousPeak);
        previousPeak = peak;
    }
}

TEST(LodClusterSway, no_wind_or_no_lean_means_no_motion)
{
    for (double t = 0; t < 5; t += 0.5)
    {
        EXPECT_EQ(sway(0.05f, 0.f, 400.f, 500.f, 0.3f, t).v[0], 0.f);
        EXPECT_EQ(sway(0.f, 1.f, 400.f, 500.f, 0.3f, t).v[0], 0.f);
    }
}

TEST(LodClusterSway, a_lean_above_the_cap_is_clamped_and_wind_above_one_too)
{
    for (double t = 0; t < 8; t += 0.1)
    {
        const Out out = sway(5.f, 9.f, 500.f, 500.f, 0.f, t);
        EXPECT_LE(std::fabs(out.v[0]), maxSwayLean * 500.f + 1e-3f);
    }
}

TEST(LodClusterSway, the_sway_is_periodic_in_game_time)
{
    for (const double t : { 0.0, 0.4, 1.7, 2.9 })
    {
        const Out a = sway(0.05f, 0.8f, 300.f, 500.f, 2.f, t);
        const Out b = sway(0.05f, 0.8f, 300.f, 500.f, 2.f, t + swayPeriodSeconds * 1000.0);
        EXPECT_NEAR(a.v[0], b.v[0], 1e-3f);
        EXPECT_NEAR(a.v[1], b.v[1], 1e-3f);
    }
}

TEST(LodClusterSway, ten_hours_of_play_lose_no_precision)
{
    const double ten = 10.0 * 3600.0;
    const Out a = sway(0.05f, 1.f, 300.f, 500.f, 1.f, 0.25);
    const Out b = sway(0.05f, 1.f, 300.f, 500.f, 1.f, 0.25 + ten);
    // 36,000 s is 12,000 whole periods
    EXPECT_NEAR(a.v[0], b.v[0], 1e-3f);
}

TEST(LodClusterSway, different_phases_move_out_of_step)
{
    const Out a = sway(0.05f, 1.f, 400.f, 500.f, 0.f, 1.0);
    const Out b = sway(0.05f, 1.f, 400.f, 500.f, 3.f, 1.0);
    EXPECT_NE(a.v[0], b.v[0]);
}

TEST(LodClusterSway, the_wind_leans_the_object_one_way_more_than_the_other)
{
    // wind pushes: the mean along the wind over a period is positive
    double sum = 0;
    int n = 0;
    for (double t = 0; t < swayPeriodSeconds; t += swayPeriodSeconds / 300.0, ++n)
        sum += sway(0.05f, 1.f, 500.f, 500.f, 0.f, t).v[0];
    EXPECT_GT(sum / n, 0.5);
}

TEST(LodClusterSway, bad_inputs_give_zero_and_never_nan)
{
    for (const float bad : { kNan, kInf, -kInf })
    {
        for (int which = 0; which < 5; ++which)
        {
            float a[5] = { 0.05f, 1.f, 300.f, 500.f, 1.f };
            a[which] = bad;
            const Out out = sway(a[0], a[1], a[2], a[3], a[4], 1.0);
            EXPECT_EQ(out.v[0], 0.f) << which;
            EXPECT_EQ(out.v[1], 0.f) << which;
        }
    }
    EXPECT_EQ(sway(0.05f, 1.f, 300.f, 500.f, 1.f, std::numeric_limits<double>::quiet_NaN()).v[0], 0.f);
    EXPECT_EQ(sway(0.05f, 1.f, 300.f, 0.f, 1.f, 1.0).v[0], 0.f);
    EXPECT_EQ(sway(0.05f, 1.f, -10.f, 500.f, 1.f, 1.0).v[0], 0.f);
}

TEST(LodClusterSway, a_point_above_the_top_sways_no_further_than_the_top)
{
    for (double t = 0; t < 6; t += 0.3)
        EXPECT_EQ(sway(0.05f, 1.f, 900.f, 500.f, 0.4f, t).v[0], sway(0.05f, 1.f, 500.f, 500.f, 0.4f, t).v[0]);
}

TEST(LodClusterSwayWeight, the_byte_round_trips_the_height_share_within_a_step)
{
    for (const float objectHeight : { 100.f, 500.f, 1000.f, 2048.f })
        for (float h = 0; h <= objectHeight; h += objectHeight / 16.f)
        {
            const float t = std::min(h / objectHeight, 1.f);
            const float exact = objectHeight * t * t;
            const float decoded = swayDecodeWeight(swayEncodeWeight(h, objectHeight));
            EXPECT_NEAR(decoded, exact, std::max(1.5f, exact * 0.02f)) << h << " of " << objectHeight;
        }
}

TEST(LodClusterSwayWeight, the_base_is_zero_and_nothing_exceeds_255)
{
    EXPECT_EQ(swayEncodeWeight(0.f, 500.f), 0u);
    EXPECT_EQ(swayEncodeWeight(-3.f, 500.f), 0u);
    EXPECT_EQ(swayEncodeWeight(5000.f, 5000.f), 255u) << "a taller object than the scale clamps";
    EXPECT_EQ(swayEncodeWeight(kNan, 500.f), 0u);
    EXPECT_EQ(swayEncodeWeight(10.f, kInf), 0u);
    EXPECT_EQ(swayEncodeWeight(10.f, 0.f), 0u);
    EXPECT_EQ(swayDecodeWeight(255), swayWeightScale);
}

TEST(LodClusterSwayWeight, weight_with_the_wave_equals_the_displacement_of_the_maths)
{
    // What the vertex shader computes: lean * wind * swayDecodeWeight(byte) * wave, along the wind.
    const float lean = 0.05f, wind = 0.8f, objectHeight = 1500.f, height = 900.f;
    const std::uint8_t byte = swayEncodeWeight(height, objectHeight);
    for (double seconds = 0; seconds < 9; seconds += 0.7)
    {
        const float angle = swayClockAngle(seconds);
        const float viaShader = lean * wind * swayDecodeWeight(byte) * swayWave(angle);
        EXPECT_NEAR(viaShader, sway(lean, wind, height, objectHeight, 0.f, seconds).v[0], 1.5f) << seconds;
    }
}

TEST(LodClusterSwayClock, the_angle_is_one_period_in_double_precision)
{
    EXPECT_EQ(swayClockAngle(0.0), 0.f);
    EXPECT_NEAR(swayClockAngle(swayPeriodSeconds * 0.25), 6.2831853f * 0.25f, 1e-6f);
    EXPECT_NEAR(swayClockAngle(swayPeriodSeconds * 100000.0 + swayPeriodSeconds * 0.5), 3.14159265f, 1e-3f);
    EXPECT_EQ(swayClockAngle(std::numeric_limits<double>::quiet_NaN()), 0.f);
    EXPECT_EQ(swayClockAngle(-std::numeric_limits<double>::infinity()), 0.f);
    for (const double t : { -7.3, 0.0, 1e-9, 2.999999999, 3.0, 1e9 })
    {
        EXPECT_GE(swayClockAngle(t), 0.f);
        EXPECT_LT(swayClockAngle(t), 6.2831855f);
    }
}

TEST(LodClusterSwayWave, the_wave_stays_in_zero_to_one)
{
    for (double a = -20; a < 20; a += 0.01)
    {
        EXPECT_GE(swayWave(a), -1e-6f);
        EXPECT_LE(swayWave(a), 1.f + 1e-6f);
    }
}

TEST(LodClusterSwayShader, the_angle_the_shader_takes_is_the_clock_angle_plus_the_spatial_phase)
{
    // The vertex shader computes wave(clockAngle + swayVertexPhase(x, y)); swayDisplacement does the same with the
    // spatial part as its phase argument, so the two agree for any time and place.
    const float lean = 0.06f, wind = 0.7f, objectHeight = 1200.f, height = 800.f;
    const std::uint8_t byte = swayEncodeWeight(height, objectHeight);
    for (const double seconds : { 0.0, 0.4, 1.5, 2.99, 3.0, 61.25, 36000.7 })
        for (const float x : { -50000.f, 0.f, 1234.5f, 99999.f })
            for (const float y : { -70000.f, 0.f, 777.f })
            {
                const double angle = static_cast<double>(swayClockAngle(seconds)) + swayVertexPhase(x, y);
                const float shader = lean * wind * swayDecodeWeight(byte) * swayWave(angle);
                const float maths = sway(lean, wind, height, objectHeight, swayVertexPhase(x, y), seconds).v[0];
                EXPECT_NEAR(shader, maths, 2.0f) << seconds << " " << x << " " << y;
            }
}

TEST(LodClusterSwayShader, the_reach_bounds_every_displacement)
{
    // (The reach is for objects up to swayWeightScale tall, the most the weight byte can express.)
    float worst = 0;
    for (double t = 0; t < 12; t += 0.05)
        for (float phase = 0; phase < 6.3f; phase += 0.1f)
        {
            const Out o = sway(maxSwayLean, 1.f, swayWeightScale, swayWeightScale, phase, t);
            worst = std::max(worst, std::sqrt(o.v[0] * o.v[0] + o.v[1] * o.v[1]));
        }
    EXPECT_LE(worst, maxSwayReach);
    EXPECT_GT(worst, 0.5f * maxSwayReach);
}

TEST(LodClusterSwayBoost, grows_with_distance_between_one_and_the_maximum)
{
    EXPECT_FLOAT_EQ(swayDistanceBoost(0.f, swayBoostReference), 1.f);
    EXPECT_FLOAT_EQ(swayDistanceBoost(4000.f, swayBoostReference), 1.f);
    EXPECT_FLOAT_EQ(swayDistanceBoost(8000.f, swayBoostReference), 1.f);
    EXPECT_FLOAT_EQ(swayDistanceBoost(16000.f, swayBoostReference), 2.f);
    EXPECT_FLOAT_EQ(swayDistanceBoost(40000.f, swayBoostReference), 5.f);
    EXPECT_FLOAT_EQ(swayDistanceBoost(48000.f, swayBoostReference), swayBoostMax);
    EXPECT_FLOAT_EQ(swayDistanceBoost(1e9f, swayBoostReference), swayBoostMax);
}

TEST(LodClusterSwayBoost, is_monotonic)
{
    float previous = 0;
    for (float d = 0; d < 100000.f; d += 500.f)
    {
        const float b = swayDistanceBoost(d, swayBoostReference);
        EXPECT_GE(b, previous);
        previous = b;
    }
}

TEST(LodClusterSwayBoost, a_reference_of_zero_or_less_or_bad_numbers_give_no_boost)
{
    EXPECT_EQ(swayDistanceBoost(40000.f, 0.f), 1.f);
    EXPECT_EQ(swayDistanceBoost(40000.f, -5.f), 1.f);
    EXPECT_EQ(swayDistanceBoost(kNan, swayBoostReference), 1.f);
    EXPECT_EQ(swayDistanceBoost(40000.f, kNan), 1.f);
    EXPECT_EQ(swayDistanceBoost(kInf, swayBoostReference), swayBoostMax);
    EXPECT_EQ(swayDistanceBoost(-100.f, swayBoostReference), 1.f);
}

TEST(LodClusterSwayBoost, the_boosted_reach_stays_inside_the_bound_the_renderer_uses)
{
    float worst = 0;
    for (float d : { 1000.f, 8000.f, 20000.f, 60000.f })
        for (double t = 0; t < 6; t += 0.1)
        {
            const Out o = sway(maxSwayLean, 1.f, swayWeightScale, swayWeightScale, 0.3f, t);
            worst = std::max(worst, swayDistanceBoost(d, swayBoostReference) * std::sqrt(o.v[0] * o.v[0] + o.v[1] * o.v[1]));
        }
    EXPECT_LE(worst, swayBoostMax * maxSwayReach);
}
