// Tests for the pure maths of parallax occlusion mapping (task 0045): quality, step count, distance fade, depth scale,
// the packing of the two per-material scales, and a reference march. Written from the spec
// (docs/tasks/0045-rendering-parallax-height-maps.md) before the implementation. No GPU, window or game data.

#include <components/render/parallaxmath.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Parallax;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN();

    TEST(RenderParallaxTest, quality_names_parse_and_unknown_ones_do_not)
    {
        EXPECT_EQ(parseQuality("low"), Quality::Low);
        EXPECT_EQ(parseQuality("medium"), Quality::Medium);
        EXPECT_EQ(parseQuality("high"), Quality::High);
        EXPECT_FALSE(parseQuality("ultra").has_value());
        EXPECT_FALSE(parseQuality("").has_value());
    }

    TEST(RenderParallaxTest, step_counts_stay_in_the_quality_s_bounds_and_grow_toward_grazing_views)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const QualityLimits lim = limits(q);
            EXPECT_GE(lim.minSteps, 8);
            EXPECT_LE(lim.maxSteps, 32);
            EXPECT_LE(lim.minSteps, lim.maxSteps);
            int previous = 0;
            for (float c = 1.f; c >= 0.f; c -= 0.05f)
            {
                const int n = stepCount(q, c);
                EXPECT_GE(n, lim.minSteps);
                EXPECT_LE(n, lim.maxSteps);
                EXPECT_GE(n, previous);
                previous = n;
            }
            EXPECT_EQ(stepCount(q, 1.f), lim.minSteps);
            EXPECT_EQ(stepCount(q, 0.f), lim.maxSteps);
        }
        EXPECT_LT(limits(Quality::Low).maxSteps, limits(Quality::High).maxSteps);
    }

    TEST(RenderParallaxTest, a_bad_view_cosine_still_gives_a_bounded_count)
    {
        EXPECT_EQ(stepCount(Quality::High, nan), limits(Quality::High).maxSteps);
        EXPECT_EQ(stepCount(Quality::High, -3.f), limits(Quality::High).maxSteps);
        EXPECT_EQ(stepCount(Quality::High, 7.f), limits(Quality::High).minSteps);
    }

    TEST(RenderParallaxTest, the_distance_fade_is_one_near_zero_far_and_never_leaves_the_unit_range)
    {
        EXPECT_FLOAT_EQ(distanceFade(0.f), 1.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeStart), 1.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeEnd), 0.f);
        EXPECT_FLOAT_EQ(distanceFade(fadeEnd * 10.f), 0.f);
        const float mid = distanceFade(0.5f * (fadeStart + fadeEnd));
        EXPECT_GT(mid, 0.f);
        EXPECT_LT(mid, 1.f);
        EXPECT_FLOAT_EQ(distanceFade(nan), 0.f);
        EXPECT_FLOAT_EQ(distanceFade(-5.f), 1.f);
    }

    TEST(RenderParallaxTest, the_uv_depth_is_the_material_scale_times_the_setting_with_clamps)
    {
        EXPECT_FLOAT_EQ(uvDepth(0.5f, 1.f), 0.5f * uvDepthPerDisplacement);
        EXPECT_FLOAT_EQ(uvDepth(0.5f, 2.f), 1.0f * uvDepthPerDisplacement);
        EXPECT_FLOAT_EQ(uvDepth(0.5f, 0.f), 0.f);
        // Out of range values clamp; nan means "not set" for the material and "one" for the setting.
        EXPECT_FLOAT_EQ(uvDepth(100.f, 1.f), maxDisplacementScale * uvDepthPerDisplacement);
        EXPECT_FLOAT_EQ(uvDepth(1.f, 100.f), maxParallaxScale * uvDepthPerDisplacement);
        EXPECT_FLOAT_EQ(uvDepth(-1.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(uvDepth(nan, 1.f), defaultDisplacementScale * uvDepthPerDisplacement);
        EXPECT_FLOAT_EQ(uvDepth(0.5f, nan), 0.5f * uvDepthPerDisplacement);
    }

    TEST(RenderParallaxTest, half_floats_round_trip_the_values_the_scales_use)
    {
        for (float v : { 0.f, 0.001f, 0.1f, 0.5f, 1.f, 1.5f, 4.f, 8.f })
        {
            const float back = halfToFloat(floatToHalf(v));
            EXPECT_NEAR(back, v, v * 0.001f + 1e-6f) << v;
        }
        EXPECT_EQ(floatToHalf(0.f), 0u);
        EXPECT_EQ(floatToHalf(1.f), 0x3c00u);
        EXPECT_EQ(floatToHalf(nan), 0u);
        EXPECT_EQ(floatToHalf(-2.f), 0u); // the scales are never negative
        EXPECT_EQ(floatToHalf(1e9f), 0x7bffu); // the largest finite half, never an infinity or a NaN
    }

    TEST(RenderParallaxTest, the_two_scales_share_one_float_and_zero_still_means_neither)
    {
        EXPECT_EQ(packScales(0.f, 0.f), 0.f);
        const Scales none = unpackScales(0.f);
        EXPECT_FLOAT_EQ(none.roughness, 0.f);
        EXPECT_FLOAT_EQ(none.displacement, 0.f);

        const Scales both = unpackScales(packScales(1.5f, 0.5f));
        EXPECT_NEAR(both.roughness, 1.5f, 0.002f);
        EXPECT_NEAR(both.displacement, 0.5f, 0.001f);

        const Scales roughOnly = unpackScales(packScales(0.75f, 0.f));
        EXPECT_NEAR(roughOnly.roughness, 0.75f, 0.001f);
        EXPECT_FLOAT_EQ(roughOnly.displacement, 0.f);

        const Scales dispOnly = unpackScales(packScales(0.f, 2.f));
        EXPECT_FLOAT_EQ(dispOnly.roughness, 0.f);
        EXPECT_NEAR(dispOnly.displacement, 2.f, 0.002f);
    }

    TEST(RenderParallaxTest, a_packed_float_is_never_a_nan_or_an_infinity)
    {
        // The float travels through a uniform buffer: a NaN pattern could be changed on the way.
        for (float r : { 0.f, 0.5f, 8.f, 1e9f })
            for (float d : { 0.f, 0.001f, 8.f, 1e9f })
                EXPECT_TRUE(std::isfinite(packScales(r, d))) << r << " " << d;
    }

    // A height field along the ray: heights as a function of the ray's depth, 0 at the surface to 1 at the bottom.
    TEST(RenderParallaxTest, a_flat_field_at_the_top_hits_at_depth_zero)
    {
        const MarchResult r = march([](float) { return 1.f; }, 8, 4);
        EXPECT_NEAR(r.depth, 0.f, 1e-5f);
    }

    TEST(RenderParallaxTest, a_field_with_no_height_is_never_hit_and_gives_the_full_depth)
    {
        const MarchResult r = march([](float) { return 0.f; }, 8, 4);
        EXPECT_FLOAT_EQ(r.depth, 1.f);
    }

    TEST(RenderParallaxTest, a_flat_field_at_half_height_is_hit_at_half_depth_and_refinement_makes_it_exact)
    {
        // The ray at depth d is below the surface once d >= 1 - h, so h = 0.5 is crossed at d = 0.5.
        const auto field = [](float) { return 0.5f; };
        const MarchResult stepped = march(field, 7, 0, false);
        const MarchResult linear = march(field, 7, 0, true);
        const MarchResult bisected = march(field, 7, 4, true);
        EXPECT_GT(std::abs(stepped.depth - 0.5f), 1e-3f);
        EXPECT_NEAR(linear.depth, 0.5f, 1e-4f);
        EXPECT_NEAR(bisected.depth, 0.5f, 1e-4f);
    }

    TEST(RenderParallaxTest, refining_a_sloped_field_gets_closer_to_the_true_hit_than_the_steps_alone)
    {
        // h(d) = 0.8 - 0.4 d: the surface meets the ray where d = 1 - h, that is d = 0.2 + 0.4 d, so d = 1/3.
        const auto field = [](float d) { return 0.8f - 0.4f * d; };
        const float truth = 1.f / 3.f;
        const MarchResult stepped = march(field, 5, 0, false);
        const MarchResult linear = march(field, 5, 0, true);
        const MarchResult bisected = march(field, 5, 4, true);
        EXPECT_LT(std::abs(linear.depth - truth), std::abs(stepped.depth - truth));
        EXPECT_LT(std::abs(bisected.depth - truth), 1e-3f);
    }

    TEST(RenderParallaxTest, the_march_never_samples_more_than_its_step_count_plus_the_refining_taps)
    {
        int taps = 0;
        const auto field = [&taps](float d) {
            ++taps;
            return 0.7f - 0.3f * d;
        };
        march(field, 10, 4, true);
        EXPECT_LE(taps, 10 + 1 + 4);
        taps = 0;
        march(field, 10, 0, false);
        EXPECT_LE(taps, 11);
    }

    TEST(RenderParallaxTest, a_ray_offset_is_bounded_at_grazing_angles)
    {
        // eye.xy / max(eye.z, minViewZ): a view almost along the surface does not send the sample far away.
        EXPECT_FLOAT_EQ(rayScale(1.f), 1.f);
        EXPECT_FLOAT_EQ(rayScale(0.5f), 2.f);
        EXPECT_FLOAT_EQ(rayScale(0.f), 1.f / minViewZ);
        EXPECT_FLOAT_EQ(rayScale(-0.5f), 1.f / minViewZ);
        EXPECT_FLOAT_EQ(rayScale(nan), 1.f / minViewZ);
    }

    // The shader twin: the constants of Render::Parallax that parallax.glsl repeats are read back from its text. The
    // quality rows are in the `#if` branches, so they are read in order of appearance: low, high, then medium.
    TEST(RenderParallaxTest, the_shader_twin_holds_the_same_constants)
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "lib" / "material" / "parallax.glsl";
        std::ifstream in(source);
        if (!in)
            GTEST_SKIP() << "the shader source is not beside the test sources";
        std::stringstream text;
        text << in.rdbuf();
        const std::string all = text.str();
        const std::regex line(R"(const\s+(?:float|int)\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;)");
        std::map<std::string, std::vector<float>> found;
        for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
            found[(*it)[1]].push_back(std::stof((*it)[2]));
        const auto one = [&found](const char* name, float value) {
            ASSERT_TRUE(found.count(name)) << name;
            EXPECT_FLOAT_EQ(found.at(name).front(), value) << name;
        };
        one("POM_MIN_VIEW_Z", minViewZ);
        one("POM_FADE_START", fadeStart);
        one("POM_FADE_END", fadeEnd);
        one("POM_UV_DEPTH_PER_DISPLACEMENT", uvDepthPerDisplacement);
        one("POM_DEFAULT_DISPLACEMENT", defaultDisplacementScale);
        // low, high, medium
        const Quality order[] = { Quality::Low, Quality::High, Quality::Medium };
        for (int i = 0; i < 3; ++i)
        {
            const QualityLimits lim = limits(order[i]);
            ASSERT_EQ(found["POM_MIN_STEPS"].size(), 3u);
            EXPECT_EQ(static_cast<int>(found["POM_MIN_STEPS"][i]), lim.minSteps);
            EXPECT_EQ(static_cast<int>(found["POM_MAX_STEPS"][i]), lim.maxSteps);
            EXPECT_EQ(static_cast<int>(found["POM_REFINE_TAPS"][i]), lim.refineTaps);
        }
    }

    // toddhow's rule (task 0045): no height source, no parallax.
    TEST(RenderParallaxTest, a_material_without_a_height_source_has_no_parallax)
    {
        EXPECT_EQ(decideHeightSource(false, false, false, false), HeightSource::None);
        // A normal map's alpha that is just opacity (not the `_nh` file) is not a height source.
        EXPECT_EQ(decideHeightSource(false, false, false, false), HeightSource::None);
        // A height file that was found but cannot be used counts as absent.
        EXPECT_EQ(decideHeightSource(true, false, false, false), HeightSource::None);
    }

    TEST(RenderParallaxTest, a_usable_height_file_or_an_explicit_normal_height_map_is_a_source)
    {
        EXPECT_EQ(decideHeightSource(true, true, false, false), HeightSource::HeightFile);
        EXPECT_EQ(decideHeightSource(false, false, true, false), HeightSource::NormalAlpha);
        // The height file wins over the normal map's alpha, and an unusable one falls back to the alpha.
        EXPECT_EQ(decideHeightSource(true, true, true, false), HeightSource::HeightFile);
        EXPECT_EQ(decideHeightSource(true, false, true, false), HeightSource::NormalAlpha);
        // A two-channel normal map has no alpha to read.
        EXPECT_EQ(decideHeightSource(false, false, true, true), HeightSource::None);
    }

    TEST(RenderParallaxTest, only_unsigned_one_channel_and_plain_colour_formats_are_height_images)
    {
        for (VkFormat f : { VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_R8_UNORM, VK_FORMAT_R16_UNORM,
                 VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK })
            EXPECT_TRUE(usableHeightFormat(f)) << f;
        for (VkFormat f : { VK_FORMAT_UNDEFINED, VK_FORMAT_BC4_SNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK,
                 VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8A8_SRGB })
            EXPECT_FALSE(usableHeightFormat(f)) << f;
    }
}
