// Tests for the subsurface scattering maths, Render::Sss (task 0033, stage a). Written from the task spec before the
// stage's shader and settings.

#include <components/render/sssmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace Render::Sss
{
    namespace
    {
        std::vector<Tap> field(const vsg::vec3& colour, float width, float validity = 1.f)
        {
            std::vector<Tap> taps;
            for (int i = 0; i < tapCount; ++i)
            {
                const auto offset = tapOffset(i, tapCount, reachInWidths * width, 0.f);
                taps.push_back({ colour, std::sqrt(offset.x * offset.x + offset.y * offset.y), validity });
            }
            return taps;
        }
    }

    TEST(RenderSssTest, needs_the_setting_the_gbuffer_and_the_material_output)
    {
        EXPECT_TRUE(useSss(true, true, true));
        EXPECT_FALSE(useSss(false, true, true));
        EXPECT_FALSE(useSss(true, false, true));
        EXPECT_FALSE(useSss(true, true, false));
    }

    TEST(RenderSssTest, a_uniform_neighbourhood_is_unchanged)
    {
        const vsg::vec3 colour(0.6f, 0.4f, 0.3f);
        const auto taps = field(colour, 1.5f);
        const auto out = scatter(colour, taps.data(), static_cast<int>(taps.size()), 1.f, 1.5f);
        EXPECT_NEAR(out.x, colour.x, 1e-5f);
        EXPECT_NEAR(out.y, colour.y, 1e-5f);
        EXPECT_NEAR(out.z, colour.z, 1e-5f);
    }

    TEST(RenderSssTest, strength_zero_returns_the_pixel_itself)
    {
        auto taps = field(vsg::vec3(1.f, 0.f, 0.f), 1.5f);
        const vsg::vec3 centre(0.2f, 0.3f, 0.4f);
        const auto out = scatter(centre, taps.data(), static_cast<int>(taps.size()), 0.f, 1.5f);
        EXPECT_EQ(out, centre);
    }

    TEST(RenderSssTest, taps_that_are_not_skin_or_not_on_the_surface_do_not_count)
    {
        const vsg::vec3 centre(0.5f, 0.5f, 0.5f);
        const auto taps = field(vsg::vec3(5.f, 5.f, 5.f), 1.5f, 0.f);
        const auto out = scatter(centre, taps.data(), static_cast<int>(taps.size()), 1.f, 1.5f);
        EXPECT_NEAR(out.x, centre.x, 1e-6f);
        EXPECT_NEAR(out.z, centre.z, 1e-6f);
    }

    TEST(RenderSssTest, red_light_reaches_farther_than_blue)
    {
        // A dark pixel in a bright neighbourhood: the red channel brightens more than the blue.
        const vsg::vec3 centre(0.f, 0.f, 0.f);
        const auto taps = field(vsg::vec3(1.f, 1.f, 1.f), 1.5f);
        const auto out = scatter(centre, taps.data(), static_cast<int>(taps.size()), 1.f, 1.5f);
        EXPECT_GT(out.x, out.y);
        EXPECT_GT(out.y, out.z);
        EXPECT_GT(out.x, 0.f);
    }

    TEST(RenderSssTest, a_stronger_setting_moves_the_result_further)
    {
        const vsg::vec3 centre(0.f, 0.f, 0.f);
        const auto taps = field(vsg::vec3(1.f, 1.f, 1.f), 1.5f);
        const int n = static_cast<int>(taps.size());
        const auto half = scatter(centre, taps.data(), n, 0.5f, 1.5f);
        const auto full = scatter(centre, taps.data(), n, 1.f, 1.5f);
        EXPECT_GT(full.x, half.x);
        EXPECT_NEAR(half.x * 2.f, full.x, 1e-5f);
    }

    TEST(RenderSssTest, no_channel_goes_below_zero)
    {
        // A bright pixel in a dark neighbourhood, at a strength above what the setting allows.
        const vsg::vec3 centre(1.f, 1.f, 1.f);
        const auto taps = field(vsg::vec3(0.f, 0.f, 0.f), 1.5f);
        const auto out = scatter(centre, taps.data(), static_cast<int>(taps.size()), 4.f, 1.5f);
        EXPECT_GE(out.x, 0.f);
        EXPECT_GE(out.y, 0.f);
        EXPECT_GE(out.z, 0.f);
    }

    TEST(RenderSssTest, zero_taps_return_the_pixel)
    {
        const vsg::vec3 centre(0.1f, 0.2f, 0.3f);
        EXPECT_EQ(scatter(centre, nullptr, 0, 1.f, 1.5f), centre);
    }

    TEST(RenderSssTest, the_gaussian_is_one_at_the_centre_and_falls_with_distance)
    {
        EXPECT_FLOAT_EQ(gaussian(0.f, 1.f), 1.f);
        EXPECT_LT(gaussian(1.f, 1.f), 1.f);
        EXPECT_LT(gaussian(2.f, 1.f), gaussian(1.f, 1.f));
        EXPECT_NEAR(gaussian(1.f, 1.f), std::exp(-0.5f), 1e-6f);
    }

    TEST(RenderSssTest, a_gaussian_of_no_width_admits_only_the_centre)
    {
        EXPECT_FLOAT_EQ(gaussian(0.f, 0.f), 1.f);
        EXPECT_FLOAT_EQ(gaussian(0.5f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(gaussian(0.5f, -1.f), 0.f);
    }

    TEST(RenderSssTest, the_spiral_stays_inside_the_reach_and_is_deterministic)
    {
        for (int i = 0; i < tapCount; ++i)
        {
            const auto a = tapOffset(i, tapCount, 4.f, 0.3f);
            const auto b = tapOffset(i, tapCount, 4.f, 0.3f);
            EXPECT_EQ(a, b);
            EXPECT_LE(std::sqrt(a.x * a.x + a.y * a.y), 4.f + 1e-5f);
        }
    }

    TEST(RenderSssTest, the_spiral_covers_the_disc_in_every_direction)
    {
        int quadrants[4] = {};
        for (int i = 0; i < tapCount; ++i)
        {
            const auto a = tapOffset(i, tapCount, 4.f, 0.f);
            ++quadrants[(a.x >= 0.f ? 1 : 0) + (a.y >= 0.f ? 2 : 0)];
        }
        for (int q : quadrants)
            EXPECT_GE(q, 2);
    }

    TEST(RenderSssTest, a_rotation_turns_the_pattern_without_changing_its_radii)
    {
        for (int i = 0; i < tapCount; ++i)
        {
            const auto a = tapOffset(i, tapCount, 4.f, 0.f);
            const auto b = tapOffset(i, tapCount, 4.f, 1.f);
            EXPECT_NEAR(std::hypot(a.x, a.y), std::hypot(b.x, b.y), 1e-5f);
        }
    }

    TEST(RenderSssTest, the_surface_weight_is_one_on_the_same_surface_and_zero_across_a_gap)
    {
        EXPECT_NEAR(surfaceWeight(0.f, 1.5f, 1.f), 1.f, 1e-6f);
        EXPECT_NEAR(surfaceWeight(0.5f, 1.5f, 0.99f), 1.f, 1e-6f);
        EXPECT_NEAR(surfaceWeight(4.f, 1.5f, 1.f), 0.f, 1e-6f);
        EXPECT_NEAR(surfaceWeight(-4.f, 1.5f, 1.f), 0.f, 1e-6f);
        EXPECT_NEAR(surfaceWeight(0.f, 1.5f, 0.f), 0.f, 1e-6f);
        EXPECT_NEAR(surfaceWeight(0.f, 1.5f, -1.f), 0.f, 1e-6f);
    }

    TEST(RenderSssTest, the_surface_weight_falls_smoothly_with_the_depth_gap)
    {
        float previous = 2.f;
        for (float d = 0.f; d <= 4.f; d += 0.25f)
        {
            const float w = surfaceWeight(d, 1.5f, 1.f);
            EXPECT_LE(w, previous + 1e-6f);
            previous = w;
        }
    }

    TEST(RenderSssTest, a_tiny_width_does_not_divide_by_zero)
    {
        const float w = surfaceWeight(1.f, 0.f, 1.f);
        EXPECT_TRUE(std::isfinite(w));
    }

    TEST(RenderSssTest, pixels_per_unit_follows_the_depth)
    {
        const float focal = focalPixels(1.0471976f, 720.f); // 60 degrees
        EXPECT_NEAR(focal, 623.54f, 0.1f);
        EXPECT_NEAR(pixelsPerUnit(100.f, focal), 6.2354f, 1e-3f);
        EXPECT_NEAR(pixelsPerUnit(200.f, focal) * 2.f, pixelsPerUnit(100.f, focal), 1e-4f);
        EXPECT_FLOAT_EQ(pixelsPerUnit(0.f, focal), 0.f);
        EXPECT_FLOAT_EQ(pixelsPerUnit(-1.f, focal), 0.f);
    }
}
