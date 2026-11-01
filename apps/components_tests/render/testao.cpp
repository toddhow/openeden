// Tests for the ambient occlusion maths, Render::Ao (task 0029, stage a). Written from the task spec before the
// implementation.

#include <components/render/aomath.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace Render::Ao
{
    TEST(RenderAoTest, an_open_surface_facing_the_camera_is_fully_visible)
    {
        EXPECT_NEAR(gtaoSliceVisibility(-halfPi, halfPi, 0.f), 1.f, 1e-5f);
    }

    TEST(RenderAoTest, a_surface_with_a_wall_at_its_plane_is_fully_occluded)
    {
        EXPECT_NEAR(gtaoSliceVisibility(0.f, 0.f, 0.f), 0.f, 1e-5f);
    }

    TEST(RenderAoTest, visibility_grows_as_the_horizons_open)
    {
        float previous = -1.f;
        for (float h = 0.f; h <= halfPi + 1e-4f; h += 0.1f)
        {
            const float v = gtaoSliceVisibility(-h, h, 0.f);
            EXPECT_GE(v, previous - 1e-6f);
            previous = v;
        }
    }

    TEST(RenderAoTest, the_slice_is_symmetric_for_a_normal_facing_the_camera)
    {
        EXPECT_NEAR(gtaoSliceVisibility(-0.4f, 0.9f, 0.f), gtaoSliceVisibility(-0.9f, 0.4f, 0.f), 1e-5f);
    }

    TEST(RenderAoTest, visibility_stays_between_zero_and_one_for_any_horizons_and_normal)
    {
        for (float n = -1.2f; n <= 1.2f; n += 0.3f)
            for (float h1 = -halfPi; h1 <= 0.f; h1 += 0.3f)
                for (float h2 = 0.f; h2 <= halfPi; h2 += 0.3f)
                {
                    const float v = gtaoSliceVisibility(h1, h2, n);
                    EXPECT_GE(v, -1e-5f) << n << " " << h1 << " " << h2;
                    EXPECT_LE(v, 1.f + 1e-5f) << n << " " << h1 << " " << h2;
                }
    }

    TEST(RenderAoTest, horizons_beyond_the_hemisphere_of_the_normal_are_clamped)
    {
        // a horizon more than a quarter turn from the normal is no more open than one exactly a quarter turn away
        EXPECT_NEAR(gtaoSliceVisibility(-3.0f, 3.0f, 0.f), gtaoSliceVisibility(-halfPi, halfPi, 0.f), 1e-5f);
    }

    TEST(RenderAoTest, a_sample_directly_in_front_of_the_pixel_along_the_view_ray_is_a_horizon_of_one)
    {
        // the view vector points from the pixel to the camera; a sample straight towards the camera is cosine 1
        EXPECT_NEAR(horizonCosine(vsg::vec3(0.f, 0.f, 10.f), 1000.f, 0.f), 1.f, 1e-5f);
    }

    TEST(RenderAoTest, a_sample_level_with_the_pixel_is_a_horizon_of_zero)
    {
        EXPECT_NEAR(horizonCosine(vsg::vec3(10.f, 0.f, 0.f), 1000.f, 0.f), 0.f, 1e-5f);
    }

    TEST(RenderAoTest, a_sample_beyond_the_radius_does_not_occlude)
    {
        // minus one is the "nothing there" value
        EXPECT_FLOAT_EQ(horizonCosine(vsg::vec3(0.f, 0.f, 500.f), 100.f, 1.f), -1.f);
    }

    TEST(RenderAoTest, the_falloff_fades_a_far_occluder_towards_nothing)
    {
        const vsg::vec3 delta(0.f, 0.f, 80.f);
        const float near0 = horizonCosine(delta, 100.f, 0.f);
        const float faded = horizonCosine(delta, 100.f, 1.f);
        EXPECT_NEAR(near0, 1.f, 1e-5f);
        EXPECT_LT(faded, near0);
        EXPECT_GE(faded, -1.f);
    }

    TEST(RenderAoTest, a_zero_offset_does_not_produce_a_non_finite_number)
    {
        EXPECT_TRUE(std::isfinite(horizonCosine(vsg::vec3(0.f, 0.f, 0.f), 100.f, 1.f)));
    }

    TEST(RenderAoTest, the_curve_is_the_plain_value_at_strength_one_and_no_occlusion_at_zero)
    {
        EXPECT_FLOAT_EQ(aoCurve(0.37f, 1.f), 0.37f);
        EXPECT_FLOAT_EQ(aoCurve(0.37f, 0.f), 1.f);
        EXPECT_LT(aoCurve(0.5f, 2.f), 0.5f);
        EXPECT_FLOAT_EQ(aoCurve(0.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(aoCurve(1.f, 3.f), 1.f);
    }

    TEST(RenderAoTest, the_filter_keeps_matching_neighbours_and_drops_different_surfaces)
    {
        EXPECT_NEAR(bilateralWeight(0.f, 100.f, 1.f), 1.f, 1e-5f);
        // a neighbour a tenth of the depth away is another surface
        EXPECT_EQ(bilateralWeight(10.f, 100.f, 1.f), 0.f);
        // a neighbour facing away is another surface
        EXPECT_EQ(bilateralWeight(0.f, 100.f, 0.2f), 0.f);
    }

    TEST(RenderAoTest, the_filter_weight_falls_smoothly_between_matching_and_different)
    {
        float previous = 2.f;
        for (float d = 0.f; d <= 6.f; d += 0.5f)
        {
            const float w = bilateralWeight(d, 100.f, 1.f);
            EXPECT_LE(w, previous + 1e-6f);
            EXPECT_GE(w, 0.f);
            EXPECT_LE(w, 1.f);
            previous = w;
        }
    }

    TEST(RenderAoTest, taking_the_occlusion_out_removes_only_the_ambient_share)
    {
        const vsg::vec3 colour(1.f, 0.8f, 0.6f);
        const vsg::vec3 ambient(0.2f, 0.2f, 0.2f);
        const vsg::vec3 same = removeOcclusion(colour, ambient, 1.f);
        EXPECT_EQ(same, colour);
        const vsg::vec3 half = removeOcclusion(colour, ambient, 0.5f);
        EXPECT_NEAR(half.x, 0.9f, 1e-6f);
        EXPECT_NEAR(half.y, 0.7f, 1e-6f);
        EXPECT_NEAR(half.z, 0.5f, 1e-6f);
        const vsg::vec3 all = removeOcclusion(colour, ambient, 0.f);
        EXPECT_NEAR(all.x, 0.8f, 1e-6f);
    }

    TEST(RenderAoTest, the_result_never_goes_below_zero_and_no_ambient_means_no_change)
    {
        const vsg::vec3 dark = removeOcclusion(vsg::vec3(0.1f, 0.1f, 0.1f), vsg::vec3(0.5f, 0.5f, 0.5f), 0.f);
        EXPECT_GE(dark.x, 0.f);
        const vsg::vec3 colour(0.3f, 0.2f, 0.1f);
        EXPECT_EQ(removeOcclusion(colour, vsg::vec3(0.f, 0.f, 0.f), 0.f), colour);
    }
}
