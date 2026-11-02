// Tests for the pure parts of the parallax review findings (task 0045, section "2026-11-02, review findings", items 1
// to 6): the march and the read on one line, the back-face fade, depth per mesh, the mip rule, the cavity and the steps
// that follow the distance travelled. Written from the spec (docs/tasks/0045-rendering-parallax-height-maps.md) before
// the implementation. No GPU, window, files or game data.

#include <components/render/parallaxmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    using namespace Render::Parallax;

    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr float twoPi = 6.2831853f;

    // A march fine enough that its refinement error is far below the tolerances used here.
    constexpr int fineSteps = 64;
    constexpr int fineTaps = 10;

    struct Ray
    {
        float u;
        float v;
    };

    const std::vector<Ray> rays = { { 0.f, 0.f }, { 0.1f, 0.f }, { 0.f, -0.1f }, { 0.07f, 0.05f }, { -0.2f, 0.15f },
        { 1.f, -0.5f }, { -10.f, 10.f } };

    float sineField(float u, float v)
    {
        return 0.5f + 0.25f * std::sin(twoPi * 3.f * u) + 0.15f * std::sin(twoPi * 2.f * v);
    }

    // The step: low ground left of u = 0.5, a plateau right of it.
    constexpr float stepLow = 0.2f;
    constexpr float stepHigh = 0.8f;
    float stepField(float u, float /*v*/)
    {
        return u < 0.5f ? stepLow : stepHigh;
    }

    void expectOffsetIsTheRayAtTheHit(const RayHit& hit, Ray ray, float pivot)
    {
        EXPECT_NEAR(hit.offsetU, ray.u * (pivot - hit.depth), 1e-5f);
        EXPECT_NEAR(hit.offsetV, ray.v * (pivot - hit.depth), 1e-5f);
    }

    // Item 1: the march and the read are the same line.

    TEST(RenderParallaxRayTest, flat_map_with_pivot_at_its_surface_gives_no_offset_at_any_view)
    {
        for (float h : { 0.f, 0.1f, 0.25f, 0.5f, 0.77f, 1.f })
            for (int steps : { 8, 30, fineSteps })
                for (const Ray& ray : rays)
                {
                    const RayHit hit = marchOffset(
                        0.3f, 0.6f, ray.u, ray.v, 1.f - h, steps, fineTaps, [h](float, float) { return h; });
                    // The refinement narrows one layer by half per tap at worst.
                    const float depthError = 1.f / static_cast<float>(steps * (1 << fineTaps)) + 1e-5f;
                    EXPECT_NEAR(hit.depth, 1.f - h, depthError) << "h " << h << " steps " << steps;
                    EXPECT_NEAR(hit.offsetU, 0.f, std::abs(ray.u) * depthError + 1e-6f) << "h " << h;
                    EXPECT_NEAR(hit.offsetV, 0.f, std::abs(ray.v) * depthError + 1e-6f) << "h " << h;
                }
    }

    TEST(RenderParallaxRayTest, without_refinement_a_flat_map_is_within_one_layer)
    {
        constexpr int steps = 16;
        for (float h : { 0.f, 0.3f, 0.5f, 1.f })
        {
            const RayHit hit = marchOffset(0.5f, 0.5f, 0.1f, 0.1f, 1.f - h, steps, 0, [h](float, float) { return h; });
            EXPECT_NEAR(hit.depth, 1.f - h, 1.f / steps + 1e-5f) << "h " << h;
        }
    }

    TEST(RenderParallaxRayTest, flat_map_with_another_pivot_moves_along_the_ray_by_the_difference)
    {
        constexpr float h = 0.5f;
        constexpr Ray ray{ 0.1f, -0.2f };
        for (float pivot : { 0.f, 0.25f, 1.f })
        {
            const RayHit hit
                = marchOffset(0.5f, 0.5f, ray.u, ray.v, pivot, fineSteps, fineTaps, [](float, float) { return h; });
            EXPECT_NEAR(hit.depth, 1.f - h, 1e-3f);
            EXPECT_NEAR(hit.offsetU, ray.u * (pivot - (1.f - h)), 1e-3f);
            EXPECT_NEAR(hit.offsetV, ray.v * (pivot - (1.f - h)), 1e-3f);
        }
    }

    TEST(RenderParallaxRayTest, on_a_sine_field_the_read_lies_on_the_surface)
    {
        const Ray sineRays[] = { { 0.05f, 0.f }, { 0.f, 0.05f }, { -0.1f, 0.08f }, { 0.2f, 0.2f } };
        for (const Ray& ray : sineRays)
            for (float pivot : { 0.f, 0.3f, 0.5f, 1.f })
                for (int iu = 0; iu < 12; ++iu)
                    for (int iv = 0; iv < 12; ++iv)
                    {
                        const float u = static_cast<float>(iu) / 12.f + 0.013f;
                        const float v = static_cast<float>(iv) / 12.f + 0.029f;
                        const RayHit hit = marchOffset(u, v, ray.u, ray.v, pivot, fineSteps, fineTaps, sineField);
                        EXPECT_NEAR(sineField(u + hit.offsetU, v + hit.offsetV), 1.f - hit.depth, 2e-3f)
                            << "uv " << u << " " << v << " pivot " << pivot;
                        EXPECT_GE(hit.depth, 0.f);
                        EXPECT_LE(hit.depth, 1.f);
                        expectOffsetIsTheRayAtTheHit(hit, ray, pivot);
                    }
    }

    TEST(RenderParallaxRayTest, on_a_step_a_ray_landing_on_the_plateau_reads_the_plateau)
    {
        // With this ray, u grows toward the top of the volume: u(d) = 0.9 + 0.3 * (0.5 - d), always right of the edge.
        constexpr Ray ray{ 0.3f, 0.f };
        const RayHit hit = marchOffset(0.9f, 0.5f, ray.u, ray.v, 0.5f, fineSteps, fineTaps, stepField);
        EXPECT_NEAR(hit.depth, 1.f - stepHigh, 1e-3f);
        EXPECT_NEAR(stepField(0.9f + hit.offsetU, 0.5f + hit.offsetV), 1.f - hit.depth, 1e-3f);
        expectOffsetIsTheRayAtTheHit(hit, ray, 0.5f);
    }

    TEST(RenderParallaxRayTest, on_a_step_a_ray_landing_on_the_low_ground_reads_the_low_ground)
    {
        constexpr Ray ray{ 0.3f, 0.f };
        const RayHit hit = marchOffset(0.1f, 0.5f, ray.u, ray.v, 0.5f, fineSteps, fineTaps, stepField);
        EXPECT_NEAR(hit.depth, 1.f - stepLow, 1e-3f);
        EXPECT_NEAR(stepField(0.1f + hit.offsetU, 0.5f + hit.offsetV), 1.f - hit.depth, 1e-3f);
        expectOffsetIsTheRayAtTheHit(hit, ray, 0.5f);
    }

    TEST(RenderParallaxRayTest, on_a_step_a_ray_passing_over_the_ledge_lands_below_it)
    {
        // u(d) = 0.52 - 0.3 * d leaves the plateau at d = 0.067, above its surface at depth 0.2, and lands on the low
        // ground at depth 0.8.
        constexpr Ray ray{ 0.3f, 0.f };
        const RayHit hit = marchOffset(0.52f, 0.5f, ray.u, ray.v, 0.f, fineSteps, fineTaps, stepField);
        EXPECT_NEAR(hit.depth, 1.f - stepLow, 1e-3f);
        EXPECT_NEAR(stepField(0.52f + hit.offsetU, 0.5f + hit.offsetV), 1.f - hit.depth, 1e-3f);
        expectOffsetIsTheRayAtTheHit(hit, ray, 0.f);
    }

    TEST(RenderParallaxRayTest, on_a_step_a_ray_striking_the_wall_stops_at_the_edge)
    {
        // A wall has no single height, so "the read lies on the surface" means the read is at the edge, at the depth
        // where the ray reaches it. u(d) = 0.45 - 0.3 * (0.5 - d) = 0.3 + 0.3 * d reaches 0.5 at d = 2/3, between the
        // plateau's depth 0.2 and the low ground's depth 0.8.
        constexpr Ray ray{ -0.3f, 0.f };
        const RayHit hit = marchOffset(0.45f, 0.5f, ray.u, ray.v, 0.5f, fineSteps, fineTaps, stepField);
        const float layer = 1.f / fineSteps;
        EXPECT_NEAR(hit.depth, 2.f / 3.f, layer);
        EXPECT_NEAR(0.45f + hit.offsetU, 0.5f, 0.3f * layer);
        expectOffsetIsTheRayAtTheHit(hit, ray, 0.5f);
    }

    TEST(RenderParallaxRayTest, zero_ray_gives_zero_offset_on_any_field)
    {
        for (float pivot : { 0.f, 0.5f, 1.f })
        {
            const RayHit onSine = marchOffset(0.2f, 0.7f, 0.f, 0.f, pivot, fineSteps, fineTaps, sineField);
            EXPECT_EQ(onSine.offsetU, 0.f);
            EXPECT_EQ(onSine.offsetV, 0.f);
            const RayHit onStep = marchOffset(0.5f, 0.5f, 0.f, 0.f, pivot, fineSteps, fineTaps, stepField);
            EXPECT_EQ(onStep.offsetU, 0.f);
            EXPECT_EQ(onStep.offsetV, 0.f);
        }
    }

    TEST(RenderParallaxRayTest, every_height_tap_is_on_the_ray_through_uv_at_the_pivot)
    {
        constexpr float u0 = 0.4f;
        constexpr float v0 = 0.6f;
        constexpr Ray ray{ 0.12f, -0.08f };
        for (float pivot : { 0.f, 0.35f, 1.f })
        {
            int taps = 0;
            static_cast<void>(marchOffset(u0, v0, ray.u, ray.v, pivot, 16, 6, [&](float u, float v) {
                ++taps;
                const float depthU = pivot - (u - u0) / ray.u;
                const float depthV = pivot - (v - v0) / ray.v;
                EXPECT_NEAR(depthU, depthV, 1e-3f) << "off the ray at " << u << " " << v;
                EXPECT_GE(depthU, -1e-3f);
                EXPECT_LE(depthU, 1.f + 1e-3f);
                return sineField(u, v);
            }));
            EXPECT_GT(taps, 0);
        }
    }

    TEST(RenderParallaxRayTest, ray_on_a_field_with_nothing_above_the_bottom_still_returns_a_depth_in_range)
    {
        const RayHit hit = marchOffset(0.5f, 0.5f, 0.1f, 0.1f, 0.5f, 8, 4, [](float, float) { return 0.f; });
        EXPECT_NEAR(hit.depth, 1.f, 1.f / 8.f);
        EXPECT_LE(hit.depth, 1.f);
    }

    // Item 2: back faces.

    TEST(RenderParallaxRayTest, view_fade_is_zero_behind_one_past_the_limit_and_linear_between)
    {
        EXPECT_EQ(viewFade(0.f), 0.f);
        EXPECT_EQ(viewFade(-0.f), 0.f);
        EXPECT_EQ(viewFade(-0.5f), 0.f);
        EXPECT_EQ(viewFade(-1.f), 0.f);
        EXPECT_EQ(viewFade(-inf), 0.f);
        EXPECT_FLOAT_EQ(viewFade(minViewZ), 1.f);
        EXPECT_EQ(viewFade(0.5f), 1.f);
        EXPECT_EQ(viewFade(1.f), 1.f);
        EXPECT_EQ(viewFade(inf), 1.f);
        EXPECT_NEAR(viewFade(minViewZ * 0.25f), 0.25f, 1e-5f);
        EXPECT_NEAR(viewFade(minViewZ * 0.5f), 0.5f, 1e-5f);
        EXPECT_NEAR(viewFade(minViewZ * 0.75f), 0.75f, 1e-5f);
    }

    TEST(RenderParallaxRayTest, view_fade_of_not_a_number_is_zero)
    {
        EXPECT_EQ(viewFade(nan), 0.f);
    }

    TEST(RenderParallaxRayTest, a_ray_multiplied_by_the_fade_has_no_offset_from_behind_and_shrinks_to_the_silhouette)
    {
        constexpr Ray ray{ 0.2f, 0.1f };
        const float behind = viewFade(-0.3f);
        const RayHit back
            = marchOffset(0.3f, 0.3f, ray.u * behind, ray.v * behind, 0.f, fineSteps, fineTaps, sineField);
        EXPECT_EQ(back.offsetU, 0.f);
        EXPECT_EQ(back.offsetV, 0.f);

        const float nearEdge = viewFade(minViewZ * 1e-3f);
        const RayHit edge
            = marchOffset(0.3f, 0.3f, ray.u * nearEdge, ray.v * nearEdge, 0.f, fineSteps, fineTaps, sineField);
        EXPECT_LE(std::abs(edge.offsetU), std::abs(ray.u) * 2e-3f);
        EXPECT_LE(std::abs(edge.offsetV), std::abs(ray.v) * 2e-3f);
    }

    // Item 3: depth per mesh.

    struct P3
    {
        float x;
        float y;
        float z;
    };

    struct P2
    {
        float x;
        float y;
    };

    using Indices = std::vector<std::vector<std::uint32_t>>;

    // A 4 by 2 quad mapped to 2 by 1 in texture coordinates: 2 world units per unit of texture on both axes.
    const std::vector<P3> quadPositions = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 2, 0 }, { 0, 2, 0 } };
    const std::vector<P2> quadUvs = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 0, 1 } };
    constexpr float quadScale = 2.f;

    TEST(RenderParallaxRayTest, world_per_uv_of_a_quad_does_not_depend_on_how_it_is_cut)
    {
        const Indices oneDiagonal = { { 0, 1, 2, 0, 2, 3 } };
        const Indices otherDiagonal = { { 0, 1, 3, 1, 2, 3 } };
        const Indices twoLists = { { 0, 1, 2 }, { 0, 2, 3 } };
        const Indices strip = { { 0, 1, 3, 2 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, oneDiagonal, false), quadScale);
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, otherDiagonal, false), quadScale);
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, twoLists, false), quadScale);
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, strip, true), quadScale);

        std::vector<P3> fanPositions = quadPositions;
        std::vector<P2> fanUvs = quadUvs;
        fanPositions.push_back({ 2, 1, 0 });
        fanUvs.push_back({ 1, 0.5f });
        const Indices fan = { { 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(fanPositions, fanUvs, fan, false), quadScale);
    }

    TEST(RenderParallaxRayTest, world_per_uv_accepts_other_index_types)
    {
        const std::vector<std::vector<std::uint16_t>> shorts = { { 0, 1, 2, 0, 2, 3 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, shorts, false), quadScale);
    }

    TEST(RenderParallaxRayTest, world_per_uv_measures_area_in_three_dimensions)
    {
        // The same quad tilted out of the plane: 3-4-5 along its long side, so 5 by 2, area 10, against texture area 2.
        const std::vector<P3> tilted = { { 0, 0, 0 }, { 3, 0, 4 }, { 3, 2, 4 }, { 0, 2, 0 } };
        const Indices list = { { 0, 1, 2, 0, 2, 3 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(tilted, quadUvs, list, false), std::sqrt(5.f));

        // Uneven mapping: the square root of the area ratio. 4 by 2 onto a unit square.
        const std::vector<P2> unitUvs = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, unitUvs, list, false), std::sqrt(8.f));
    }

    // Two triangles with different scales, so that a skipped or a miscounted triangle changes the answer.
    // Triangle 0, 1, 2: area 2 over texture area 0.5. Triangle 3, 4, 5: area 18 over texture area 0.5.
    const std::vector<P3> mixedPositions
        = { { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 1 }, { 6, 0, 1 }, { 0, 6, 1 } };
    const std::vector<P2> mixedUvs = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 0 }, { 1, 0 }, { 0, 1 } };
    const float smallOnly = 2.f; // sqrt(2 / 0.5)
    const float bothTotal = std::sqrt(20.f); // sqrt(20 / 1): totals, not a mean of the two triangles' scales

    TEST(RenderParallaxRayTest, world_per_uv_divides_totals_over_all_lists)
    {
        const Indices one = { { 0, 1, 2, 3, 4, 5 } };
        const Indices two = { { 0, 1, 2 }, { 3, 4, 5 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, one, false), bothTotal);
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, two, false), bothTotal);
    }

    TEST(RenderParallaxRayTest, world_per_uv_does_not_join_separate_strips)
    {
        const Indices strips = { { 0, 1, 2 }, { 3, 4, 5 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, strips, true), bothTotal);
    }

    TEST(RenderParallaxRayTest, world_per_uv_skips_out_of_range_indices)
    {
        const Indices list = { { 0, 1, 2, 3, 4, 99 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, list, false), smallOnly);

        // Every strip triangle touching 99 goes; those after it still count.
        const Indices strip = { { 0, 1, 2, 99, 3, 4, 5 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, strip, true), bothTotal);

        // A negative index of a signed type is out of range too.
        const std::vector<std::vector<int>> negative = { { 0, 1, 2, -1, 4, 5 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, negative, false), smallOnly);
    }

    TEST(RenderParallaxRayTest, world_per_uv_checks_indices_against_both_arrays)
    {
        const Indices list = { { 0, 1, 2, 3, 4, 5 } };
        const std::vector<P2> shortUvs(mixedUvs.begin(), mixedUvs.begin() + 3);
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, shortUvs, list, false), smallOnly);
        const std::vector<P3> shortPositions(mixedPositions.begin(), mixedPositions.begin() + 3);
        EXPECT_FLOAT_EQ(meshWorldPerUv(shortPositions, mixedUvs, list, false), smallOnly);
    }

    TEST(RenderParallaxRayTest, world_per_uv_ignores_a_list_s_trailing_indices)
    {
        const Indices oneOver = { { 0, 1, 2, 3 } };
        const Indices twoOver = { { 0, 1, 2, 3, 4 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, oneOver, false), smallOnly);
        EXPECT_FLOAT_EQ(meshWorldPerUv(mixedPositions, mixedUvs, twoOver, false), smallOnly);
    }

    TEST(RenderParallaxRayTest, world_per_uv_skips_a_triangle_degenerate_in_either_space)
    {
        // Collapsed in the mesh, with texture area: must not add texture area.
        std::vector<P3> positions = mixedPositions;
        std::vector<P2> uvs = mixedUvs;
        positions.resize(3);
        uvs.resize(3);
        positions.push_back({ 1, 1, 1 });
        positions.push_back({ 1, 1, 1 });
        positions.push_back({ 1, 1, 1 });
        uvs.push_back({ 0, 0 });
        uvs.push_back({ 5, 0 });
        uvs.push_back({ 0, 5 });
        const Indices list = { { 0, 1, 2, 3, 4, 5 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(positions, uvs, list, false), smallOnly);

        // Collapsed in the texture, with mesh area: must not add mesh area.
        positions = mixedPositions;
        uvs = mixedUvs;
        uvs[3] = uvs[4] = uvs[5] = P2{ 0.5f, 0.5f };
        EXPECT_FLOAT_EQ(meshWorldPerUv(positions, uvs, list, false), smallOnly);

        // A strip's repeated index makes triangles with no area; the others still count.
        const Indices strip = { { 0, 1, 2, 2, 3 } };
        EXPECT_FLOAT_EQ(meshWorldPerUv(quadPositions, quadUvs, strip, true), quadScale);
    }

    TEST(RenderParallaxRayTest, world_per_uv_of_nothing_usable_is_zero)
    {
        const Indices list = { { 0, 1, 2, 0, 2, 3 } };
        const std::vector<P3> noPositions;
        const std::vector<P2> noUvs;

        EXPECT_EQ(meshWorldPerUv(noPositions, noUvs, Indices{}, false), 0.f);
        EXPECT_EQ(meshWorldPerUv(noPositions, noUvs, list, false), 0.f);
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{}, false), 0.f);
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{ {} }, false), 0.f);
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{ {} }, true), 0.f);

        // Too few indices for a triangle.
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{ { 0, 1 } }, false), 0.f);
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{ { 0, 1 } }, true), 0.f);

        // All indices out of range.
        EXPECT_EQ(meshWorldPerUv(quadPositions, quadUvs, Indices{ { 7, 8, 9 } }, false), 0.f);

        // No texture area, then no mesh area.
        const std::vector<P2> sameUv(4, P2{ 0.5f, 0.5f });
        EXPECT_EQ(meshWorldPerUv(quadPositions, sameUv, list, false), 0.f);
        const std::vector<P3> samePosition(4, P3{ 1, 2, 3 });
        EXPECT_EQ(meshWorldPerUv(samePosition, quadUvs, list, false), 0.f);

        // Texture coordinates all on one line.
        const std::vector<P2> lineUvs = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 } };
        EXPECT_EQ(meshWorldPerUv(quadPositions, lineUvs, list, false), 0.f);
    }

    // Item 4: mips.

    TEST(RenderParallaxRayTest, compressed_height_needs_a_chain_reaching_four_texels)
    {
        for (VkFormat format : { VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC4_SNORM_BLOCK, VK_FORMAT_BC1_RGB_UNORM_BLOCK,
                 VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK })
        {
            EXPECT_TRUE(heightMipsUsable(format, 1024, 1024, 11)) << "full chain";
            EXPECT_TRUE(heightMipsUsable(format, 1024, 1024, 9)) << "chain to 4x4";
            EXPECT_FALSE(heightMipsUsable(format, 1024, 1024, 8)) << "chain to 8x8";
            EXPECT_FALSE(heightMipsUsable(format, 1024, 1024, 1)) << "single level";
            EXPECT_FALSE(heightMipsUsable(format, 1024, 1024, 0)) << "levels of 0 count as 1";
        }
    }

    TEST(RenderParallaxRayTest, compressed_height_goes_by_the_longer_side)
    {
        constexpr VkFormat format = VK_FORMAT_BC4_UNORM_BLOCK;
        EXPECT_TRUE(heightMipsUsable(format, 1024, 256, 9));
        EXPECT_FALSE(heightMipsUsable(format, 1024, 256, 8));
        EXPECT_TRUE(heightMipsUsable(format, 256, 1024, 9));
        EXPECT_FALSE(heightMipsUsable(format, 256, 1024, 8));
        // The short side reaching 4 is not enough.
        EXPECT_FALSE(heightMipsUsable(format, 1024, 4, 1));
        EXPECT_FALSE(heightMipsUsable(format, 4, 1024, 1));
    }

    TEST(RenderParallaxRayTest, compressed_height_small_enough_needs_no_chain)
    {
        constexpr VkFormat format = VK_FORMAT_BC4_UNORM_BLOCK;
        EXPECT_TRUE(heightMipsUsable(format, 4, 4, 1));
        EXPECT_TRUE(heightMipsUsable(format, 4, 4, 0));
        EXPECT_TRUE(heightMipsUsable(format, 2, 4, 1));
        EXPECT_FALSE(heightMipsUsable(format, 5, 4, 1));
        EXPECT_FALSE(heightMipsUsable(format, 8, 8, 1));
        EXPECT_TRUE(heightMipsUsable(format, 8, 8, 2));
    }

    TEST(RenderParallaxRayTest, compressed_height_with_a_level_count_past_the_shift_width_is_a_full_chain)
    {
        // A count read from a file can be any number; shifting a 32 bit value by 32 or more is not defined.
        EXPECT_TRUE(heightMipsUsable(VK_FORMAT_BC4_UNORM_BLOCK, 1024, 1024, 33));
        EXPECT_TRUE(heightMipsUsable(VK_FORMAT_BC4_UNORM_BLOCK, 1024, 1024, 40));
        EXPECT_TRUE(heightMipsUsable(VK_FORMAT_BC4_UNORM_BLOCK, 1024, 1024, 64));
        EXPECT_TRUE(heightMipsUsable(VK_FORMAT_BC4_UNORM_BLOCK, 1024, 1024, 65));
    }

    TEST(RenderParallaxRayTest, uncompressed_height_always_passes)
    {
        for (VkFormat format : { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM })
        {
            EXPECT_TRUE(heightMipsUsable(format, 2048, 2048, 1));
            EXPECT_TRUE(heightMipsUsable(format, 2048, 2048, 0));
            EXPECT_TRUE(heightMipsUsable(format, 2048, 2048, 12));
            EXPECT_TRUE(heightMipsUsable(format, 2048, 16, 1));
        }
    }

    // Item 5: cavity.

    TEST(RenderParallaxRayTest, cavity_is_the_fraction_of_the_way_from_pivot_to_the_bottom)
    {
        EXPECT_NEAR(cavityFromDepth(0.75f, 0.5f), 0.5f, 1e-6f);
        EXPECT_NEAR(cavityFromDepth(0.3f, 0.f), 0.3f, 1e-6f);
        EXPECT_NEAR(cavityFromDepth(0.6f, 0.2f), 0.5f, 1e-6f);
        EXPECT_EQ(cavityFromDepth(0.5f, 0.5f), 0.f);
        EXPECT_FLOAT_EQ(cavityFromDepth(1.f, 0.5f), 1.f);
    }

    TEST(RenderParallaxRayTest, cavity_is_clamped)
    {
        EXPECT_EQ(cavityFromDepth(0.1f, 0.5f), 0.f);
        EXPECT_EQ(cavityFromDepth(-3.f, 0.5f), 0.f);
        EXPECT_EQ(cavityFromDepth(2.f, 0.5f), 1.f);
        EXPECT_EQ(cavityFromDepth(inf, 0.5f), 1.f);
    }

    TEST(RenderParallaxRayTest, cavity_is_zero_at_a_pivot_of_one_or_more)
    {
        EXPECT_EQ(cavityFromDepth(0.5f, 1.f), 0.f);
        EXPECT_EQ(cavityFromDepth(1.f, 1.f), 0.f);
        EXPECT_EQ(cavityFromDepth(2.f, 1.f), 0.f);
        EXPECT_EQ(cavityFromDepth(0.5f, 1.5f), 0.f);
        EXPECT_EQ(cavityFromDepth(3.f, 1.5f), 0.f);
        EXPECT_EQ(cavityFromDepth(0.5f, inf), 0.f);
    }

    TEST(RenderParallaxRayTest, cavity_of_not_a_number_is_zero)
    {
        EXPECT_EQ(cavityFromDepth(nan, 0.5f), 0.f);
        EXPECT_EQ(cavityFromDepth(0.5f, nan), 0.f);
        EXPECT_EQ(cavityFromDepth(nan, nan), 0.f);
    }

    // Item 6: steps follow the distance travelled.

    TEST(RenderParallaxRayTest, travel_steps_is_the_rounded_up_travel_between_step_count_and_the_maximum)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const int maxSteps = limits(q).maxSteps;
            for (float cosine : { 1.f, 0.7f, 0.3f, 0.f })
            {
                const int base = stepCount(q, cosine);
                for (float travel : { 0.f, 0.5f, 1.f, 7.2f, 16.f, 31.01f, 32.f, 63.5f, 100.f, 200.5f, 1000.f })
                {
                    const int expected
                        = std::min(maxSteps, std::max(base, static_cast<int>(std::ceil(travel))));
                    EXPECT_EQ(travelSteps(q, cosine, travel), expected)
                        << "cosine " << cosine << " travel " << travel;
                }
            }
        }
    }

    TEST(RenderParallaxRayTest, travel_steps_rounds_up_just_above_the_view_s_count)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const int base = stepCount(q, 1.f);
            ASSERT_LT(base, limits(q).maxSteps) << "head-on views must leave room below the maximum";
            EXPECT_EQ(travelSteps(q, 1.f, static_cast<float>(base)), base);
            EXPECT_EQ(travelSteps(q, 1.f, static_cast<float>(base) + 0.25f), base + 1);
        }
    }

    TEST(RenderParallaxRayTest, travel_steps_clamps_at_both_ends)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            const int maxSteps = limits(q).maxSteps;
            EXPECT_EQ(travelSteps(q, 1.f, static_cast<float>(maxSteps)), maxSteps);
            EXPECT_EQ(travelSteps(q, 1.f, static_cast<float>(maxSteps) + 1.f), maxSteps);
            EXPECT_EQ(travelSteps(q, 1.f, 1e30f), maxSteps);
            EXPECT_EQ(travelSteps(q, 1.f, inf), maxSteps);
            EXPECT_EQ(travelSteps(q, 1.f, -5.f), stepCount(q, 1.f));
            EXPECT_EQ(travelSteps(q, 1.f, -inf), stepCount(q, 1.f));
            EXPECT_EQ(travelSteps(q, 0.f, 0.f), stepCount(q, 0.f));
        }
    }

    TEST(RenderParallaxRayTest, travel_of_not_a_number_counts_as_zero)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
            for (float cosine : { 1.f, 0.5f, 0.f })
                EXPECT_EQ(travelSteps(q, cosine, nan), travelSteps(q, cosine, 0.f));
    }

    TEST(RenderParallaxRayTest, travel_steps_never_falls_as_travel_grows)
    {
        for (Quality q : { Quality::Low, Quality::Medium, Quality::High })
        {
            int previous = 0;
            for (float travel = 0.f; travel < 400.f; travel += 0.75f)
            {
                const int steps = travelSteps(q, 0.6f, travel);
                EXPECT_GE(steps, previous);
                previous = steps;
            }
        }
    }

    // Review of the fixes: what the material's high half holds.
    TEST(RenderParallaxRayTest, depth_field_holds_the_mesh_depth_when_the_mesh_has_a_number)
    {
        const float field = meshDepthField(0.5f, 512.f);
        EXPECT_LT(field, 1.f);
        EXPECT_FLOAT_EQ(depthFromField(field, 9999.f), uvDepthFromWorld(0.5f, 512.f));
    }

    TEST(RenderParallaxRayTest, depth_field_keeps_the_scale_for_the_derivatives_without_a_number)
    {
        const float field = meshDepthField(2.f, 0.f);
        EXPECT_GE(field, 1.f);
        EXPECT_FLOAT_EQ(depthFromField(field, 1024.f), uvDepthFromWorld(2.f, 1024.f));
    }

    TEST(RenderParallaxRayTest, depth_field_never_reads_as_no_material_with_a_scale)
    {
        EXPECT_GT(meshDepthField(0.001f, 1e6f), 0.f);
        EXPECT_FLOAT_EQ(depthFromField(0.f, 1024.f), uvDepthFromWorld(defaultDisplacementScale, 1024.f));
    }

}
