// Task 0096 (stage c, first slice): the pure maths of far-field wind sway. Which models may sway (a name list), a phase
// from an object's position, and the displacement of a point at a height above the object's base: anchored at the base,
// capped, deterministic, periodic, finite for any input. No files, no VFS, no game data, no GPU.
#include <gtest/gtest.h>

#include <components/lod/clustersway.hpp>

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
    EXPECT_TRUE(swayCapableModel("Meshes\f\Flora_Tree_Ash_01.nif", "flora_tree"));
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
