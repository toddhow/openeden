// OpenEden (task 0095, stage W-a): tests of components/render/puddlelevel.hpp (the water level in a pit, the depth,
// coverage and damp ring, and the quantisation of the stored channels), plus the bowl that joins them to
// puddlefill.hpp, written from the spec before the code.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include <components/render/puddlefill.hpp>
#include <components/render/puddlelevel.hpp>

#include "puddletestgrids.hpp"

namespace
{
    using namespace Render::Puddle;
    using PuddleTest::Lcg;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    TEST(PuddleLevel, free_water_is_what_the_ground_does_not_soak_up)
    {
        EXPECT_FLOAT_EQ(freeWater(5.f, 2.f), 3.f);
        EXPECT_FLOAT_EQ(freeWater(1.f, 2.f), 0.f);
        EXPECT_FLOAT_EQ(freeWater(5.f, 0.f), 5.f);
        EXPECT_FLOAT_EQ(freeWater(5.f, -3.f), 5.f); // a negative absorption counts as none
        EXPECT_FLOAT_EQ(freeWater(-1.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(freeWater(nan, 1.f), 0.f);
        EXPECT_FLOAT_EQ(freeWater(5.f, nan), 5.f);
        EXPECT_FLOAT_EQ(freeWater(infinity, 1.f), infinity); // an infinite supply stays infinite; levelOf caps it
    }

    TEST(PuddleLevel, the_level_is_free_water_times_the_catchment_up_to_the_basin_maximum)
    {
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.5f, 6.f), 3.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.5f, 2.5f), 2.5f);
        EXPECT_FLOAT_EQ(levelOf(0.f, 4.f, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(-1.f, 4.f, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 0.f, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, -1.f, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.f, -3.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(nan, 1.f, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, nan, 6.f), 0.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.f, nan), 0.f);
        EXPECT_FLOAT_EQ(levelOf(infinity, 1.f, 6.f), 6.f);
        EXPECT_FLOAT_EQ(levelOf(1e30f, 1e30f, 6.f), 6.f); // the product overflows to infinity
        EXPECT_FLOAT_EQ(levelOf(2.f, infinity, 6.f), 6.f);
    }

    TEST(PuddleLevel, the_level_never_falls_when_more_water_is_supplied)
    {
        Lcg rng(5);
        for (int i = 0; i < 2000; ++i)
        {
            const float kappa = 0.25f + static_cast<float>(rng.below(64)) / 8.f;
            const float basinMax = static_cast<float>(rng.below(64)) / 4.f;
            float previous = 0.f;
            for (int step = 0; step <= 40; ++step)
            {
                const float level = levelOf(static_cast<float>(step) * 0.25f, kappa, basinMax);
                ASSERT_GE(level, previous);
                ASSERT_LE(level, basinMax);
                previous = level;
            }
        }
    }

    TEST(PuddleLevel, the_depth_at_a_texel_follows_its_height_above_the_floor)
    {
        // A pit of capacity 4 at its deepest. A texel that is 3 above the floor has capacity 1.
        EXPECT_FLOAT_EQ(depthAt(0.f, 4.f, 4.f), 0.f);
        EXPECT_FLOAT_EQ(depthAt(2.5f, 4.f, 4.f), 2.5f); // the deepest texel has depth equal to the level
        EXPECT_FLOAT_EQ(depthAt(2.5f, 4.f, 1.f), 0.f); // the texel 3 above the floor is still dry
        EXPECT_FLOAT_EQ(depthAt(3.5f, 4.f, 1.f), 0.5f);
        EXPECT_FLOAT_EQ(depthAt(4.f, 4.f, 1.f), 1.f); // full pit: depth is the capacity
        EXPECT_FLOAT_EQ(depthAt(9.f, 4.f, 1.f), 1.f); // a level above the maximum cannot overfill
        EXPECT_FLOAT_EQ(depthAt(3.f, 4.f, 0.f), 0.f); // a texel with no capacity is never under water
        EXPECT_FLOAT_EQ(depthAt(nan, 4.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(depthAt(3.f, 4.f, nan), 0.f);
    }

    TEST(PuddleLevel, the_depth_is_between_zero_and_the_capacity_and_grows_with_the_water)
    {
        Lcg rng(11);
        for (int i = 0; i < 2000; ++i)
        {
            const float basinMax = 0.5f + static_cast<float>(rng.below(48)) / 4.f;
            const float capacity = static_cast<float>(rng.below(static_cast<std::uint32_t>(basinMax * 4.f) + 1)) / 4.f;
            const float kappa = 0.25f + static_cast<float>(rng.below(32)) / 8.f;
            float previous = 0.f;
            for (int step = 0; step <= 48; ++step)
            {
                const float level = levelOf(static_cast<float>(step) * 0.25f, kappa, basinMax);
                const float d = depthAt(level, basinMax, capacity);
                ASSERT_GE(d, previous);
                ASSERT_GE(d, 0.f);
                ASSERT_LE(d, capacity);
                previous = d;
            }
            ASSERT_EQ(depthAt(0.f, basinMax, capacity), 0.f); // no water, no puddle
        }
    }

    TEST(PuddleLevel, coverage_ramps_from_dry_to_wet_over_the_edge)
    {
        EXPECT_FLOAT_EQ(coverage(0.f, 0.3f), 0.f);
        EXPECT_FLOAT_EQ(coverage(-1.f, 0.3f), 0.f);
        EXPECT_FLOAT_EQ(coverage(0.15f, 0.3f), 0.5f); // smoothstep is 1/2 at the middle
        EXPECT_FLOAT_EQ(coverage(0.3f, 0.3f), 1.f);
        EXPECT_FLOAT_EQ(coverage(7.f, 0.3f), 1.f);
        EXPECT_FLOAT_EQ(coverage(0.001f, 0.f), 1.f); // a hard step
        EXPECT_FLOAT_EQ(coverage(0.001f, -1.f), 1.f);
        EXPECT_FLOAT_EQ(coverage(0.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(coverage(nan, 0.3f), 0.f);
        float previous = 0.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float c = coverage(static_cast<float>(i) * 0.005f, 0.3f);
            ASSERT_GE(c, previous);
            previous = c;
        }
    }

    TEST(PuddleLevel, the_damp_ring_is_one_at_the_water_and_fades_to_nothing)
    {
        // A texel 2 above the pit's floor (basinMax 5, capacity 3), the water at level 0 to 2.
        EXPECT_FLOAT_EQ(damp(2.f, 5.f, 3.f, 4.f), 1.f); // water at the texel's floor
        EXPECT_FLOAT_EQ(damp(3.f, 5.f, 3.f, 4.f), 1.f); // under water
        EXPECT_FLOAT_EQ(damp(1.f, 5.f, 3.f, 4.f), 0.75f); // 1 below: 1 - 1/4
        EXPECT_FLOAT_EQ(damp(0.f, 5.f, 3.f, 4.f), 0.5f);
        EXPECT_FLOAT_EQ(damp(-2.f, 5.f, 3.f, 4.f), 0.f); // range below: dry
        EXPECT_FLOAT_EQ(damp(0.f, 5.f, 3.f, 0.f), 0.f); // no range: a hard edge
        EXPECT_FLOAT_EQ(damp(0.f, 5.f, 0.f, 4.f), 0.f); // not part of a pit
        EXPECT_FLOAT_EQ(damp(nan, 5.f, 3.f, 4.f), 0.f);
        float previous = 0.f;
        for (int i = 0; i <= 40; ++i)
        {
            const float v = damp(static_cast<float>(i) * 0.1f - 1.f, 5.f, 3.f, 4.f);
            ASSERT_GE(v, previous);
            ASSERT_LE(v, 1.f);
            previous = v;
        }
    }

    TEST(PuddleLevel, a_bowl_fills_from_the_bottom_and_the_zero_contour_is_the_analytic_circle)
    {
        // The paraboloid H = k r^2 (r in texels), centre on a texel: the water at level L covers exactly the texels
        // with k r^2 < L, a disc of radius sqrt(L / k).
        const int n = 65;
        const float k = 0.01f;
        PuddleTest::Grid g(n, n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                g.at(x, y) = k * static_cast<float>((x - 32) * (x - 32) + (y - 32) * (y - 32));
        const FillResult f = fillDepressions(g.heights, g.sinks, n, n, infinity);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        const float basinMax = b.basins[0].maxCapacity;
        const float kappa = catchmentRatio(b, f, accumulateFlow(f)).ratio[0];
        // The slopes outside the circle that lead toward it drain into the bowl, so it takes more than its own rain.
        EXPECT_GE(kappa, 1.f);
        EXPECT_LT(kappa, 2.f);

        std::vector<bool> previousWet(g.heights.size(), false);
        for (const float supply : { 0.f, 0.5f, 1.f, 2.f, 4.f, 7.f, 10.f, 20.f })
        {
            const float level = levelOf(freeWater(supply, 0.f), kappa, basinMax);
            const float radius = std::sqrt(std::min(level, basinMax) / k);
            int wetCount = 0;
            float farthestWet = 0.f;
            float nearestDry = 1000.f;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const std::size_t i = g.index(x, y);
                    const float d = depthAt(level, basinMax, f.capacity[i]);
                    const float r = std::sqrt(static_cast<float>((x - 32) * (x - 32) + (y - 32) * (y - 32)));
                    if (d > 0.f)
                    {
                        ++wetCount;
                        farthestWet = std::max(farthestWet, r);
                    }
                    else if (r < 32.f)
                        nearestDry = std::min(nearestDry, r);
                    // The nested sets only grow with the supply.
                    if (previousWet[i])
                    {
                        ASSERT_GT(d, 0.f) << "supply " << supply;
                    }
                    // The depth at the centre equals the level.
                    if (x == 32 && y == 32)
                    {
                        EXPECT_FLOAT_EQ(d, level);
                    }
                    previousWet[i] = d > 0.f;
                }
            if (level <= 0.f)
            {
                EXPECT_EQ(wetCount, 0) << "no water, no puddle";
                continue;
            }
            // The contour is within one texel of the analytic circle, and the wet area is a disc of that radius.
            EXPECT_LE(farthestWet, radius + 1e-3f);
            EXPECT_GE(farthestWet, radius - 1.f);
            EXPECT_GE(nearestDry, radius - 1e-3f);
            const float area = 3.14159265f * radius * radius;
            EXPECT_NEAR(static_cast<float>(wetCount), area, std::max(2.f * radius * 2.f + 4.f, 0.08f * area))
                << "supply " << supply;
        }
    }

    TEST(PuddleLevel, a_basin_with_a_large_catchment_fills_sooner_than_one_with_a_small_one)
    {
        // Two pits of the same capacity, one fed by three times the ground.
        const float supply = 1.f;
        EXPECT_GT(levelOf(supply, 3.f, 6.f), levelOf(supply, 1.f, 6.f));
        EXPECT_FLOAT_EQ(levelOf(supply, 6.f, 6.f), 6.f);
    }

    TEST(PuddleLevel, drying_retraces_growing_exactly)
    {
        // The picture is a function of the water supplied alone: the same supply gives the same depth whether it was
        // reached by rising or by falling.
        const float basinMax = 5.f;
        const float capacity = 2.f;
        const float kappa = 1.5f;
        float rising[41];
        for (int i = 0; i <= 40; ++i)
            rising[i]
                = depthAt(levelOf(freeWater(static_cast<float>(i) * 0.1f, 0.5f), kappa, basinMax), basinMax, capacity);
        for (int i = 40; i >= 0; --i)
            ASSERT_EQ(
                depthAt(levelOf(freeWater(static_cast<float>(i) * 0.1f, 0.5f), kappa, basinMax), basinMax, capacity),
                rising[i]);
    }

    TEST(PuddleLevel, capacity_survives_its_eight_bit_round_trip)
    {
        for (int q = 0; q < 256; ++q)
            ASSERT_EQ(quantiseCapacity(dequantiseCapacity(static_cast<std::uint8_t>(q))), q);
        Lcg rng(3);
        for (int i = 0; i < 5000; ++i)
        {
            const float c = static_cast<float>(rng.below(15900)) / 1000.f; // 0 to 15.9, inside the range
            const float back = dequantiseCapacity(quantiseCapacity(c));
            ASSERT_LE(std::abs(back - c), 1.f / 32.f + 1e-6f) << c;
        }
        EXPECT_EQ(quantiseCapacity(0.f), 0);
        EXPECT_EQ(quantiseCapacity(-3.f), 0);
        EXPECT_EQ(quantiseCapacity(nan), 0);
        EXPECT_EQ(quantiseCapacity(1000.f), 255);
        EXPECT_EQ(quantiseCapacity(infinity), 255);
        EXPECT_EQ(quantiseCapacity(-infinity), 0);
        EXPECT_FLOAT_EQ(dequantiseCapacity(255), 15.9375f);
        EXPECT_EQ(quantiseCapacity(12.f), 192); // the default cap
        // The cap of 12 is inside the range with room to spare.
        EXPECT_LT(defaultCapacityCap, maxQuantisedCapacity);
    }

    TEST(PuddleLevel, the_catchment_ratio_survives_its_eight_bit_round_trip)
    {
        // A catchment ratio is at least 1 by construction, so the 256 codes span log2 from 0 to 17 (a ratio of 1 to
        // 131,072; real Morrowind land has basins near 99,000): 15 codes an octave, 2.3 per cent at worst.
        for (int q = 0; q < 256; ++q)
        {
            ASSERT_EQ(quantiseKappa(dequantiseKappa(static_cast<std::uint8_t>(q))), q);
        }
        for (float kappa = 1.f; kappa <= 131072.f; kappa *= 1.01f)
        {
            const float back = dequantiseKappa(quantiseKappa(kappa));
            ASSERT_LE(std::abs(std::log2(back) - std::log2(kappa)), 1.f / 30.f + 1e-4f) << kappa;
        }
        EXPECT_FLOAT_EQ(dequantiseKappa(quantiseKappa(0.01f)), 1.f); // clamped at log2 = 0
        EXPECT_FLOAT_EQ(dequantiseKappa(quantiseKappa(1e9f)), 131072.f); // clamped at log2 = 17
        EXPECT_EQ(quantiseKappa(1.f), 0);
        EXPECT_EQ(quantiseKappa(2.f), 15);
        EXPECT_EQ(quantiseKappa(8.f), 45);
        EXPECT_EQ(quantiseKappa(131072.f), 255);
        EXPECT_GT(dequantiseKappa(quantiseKappa(99000.f)), 90000.f); // the real maximum is not clipped
        EXPECT_EQ(quantiseKappa(nan), 0); // an invalid ratio counts as neutral
        EXPECT_EQ(quantiseKappa(-1.f), 0);
        EXPECT_EQ(quantiseKappa(0.f), 0);
        EXPECT_EQ(quantiseKappa(infinity), 255);
        EXPECT_EQ(quantiseKappa(-infinity), 0);
    }

    TEST(PuddleLevel, every_quantiser_is_monotonic_and_idempotent_over_all_its_codes)
    {
        for (int q = 1; q < 256; ++q)
        {
            ASSERT_GT(
                dequantiseCapacity(static_cast<std::uint8_t>(q)), dequantiseCapacity(static_cast<std::uint8_t>(q - 1)));
            ASSERT_GT(dequantiseKappa(static_cast<std::uint8_t>(q)), dequantiseKappa(static_cast<std::uint8_t>(q - 1)));
        }
        for (int q = 1; q < 65536; ++q)
        {
            ASSERT_GT(dequantiseHeight(static_cast<std::uint16_t>(q), 10.f),
                dequantiseHeight(static_cast<std::uint16_t>(q - 1), 10.f));
        }
        int previousCapacity = 0;
        int previousKappa = 0;
        int previousHeight = 0;
        for (int i = 0; i <= 100000; ++i)
        {
            const float x = static_cast<float>(i) * 0.0002f - 1.f; // -1 to 19
            const int c = quantiseCapacity(x);
            const int k = quantiseKappa(std::exp2(x));
            const int h = quantiseHeight(x * 500.f, -100.f);
            ASSERT_GE(c, previousCapacity);
            ASSERT_GE(k, previousKappa);
            ASSERT_GE(h, previousHeight);
            previousCapacity = c;
            previousKappa = k;
            previousHeight = h;
            ASSERT_EQ(quantiseCapacity(dequantiseCapacity(static_cast<std::uint8_t>(c))), c);
            ASSERT_EQ(quantiseKappa(dequantiseKappa(static_cast<std::uint8_t>(k))), k);
            ASSERT_EQ(quantiseHeight(dequantiseHeight(static_cast<std::uint16_t>(h), -100.f), -100.f), h);
        }
    }

    TEST(PuddleLevel, not_a_number_or_infinity_in_any_argument_never_means_wet)
    {
        const float values[] = { nan, infinity, -infinity };
        for (const float v : values)
        {
            EXPECT_FALSE(std::isnan(freeWater(v, 1.f)));
            EXPECT_FALSE(std::isnan(freeWater(2.f, v)));
            EXPECT_FALSE(std::isnan(levelOf(v, 1.f, 5.f)));
            EXPECT_FALSE(std::isnan(levelOf(2.f, v, 5.f)));
            EXPECT_FALSE(std::isnan(levelOf(2.f, 1.f, v)));
            EXPECT_FALSE(std::isnan(depthAt(v, 5.f, 3.f)));
            EXPECT_FALSE(std::isnan(depthAt(2.f, v, 3.f)));
            EXPECT_FALSE(std::isnan(depthAt(2.f, 5.f, v)));
            EXPECT_FALSE(std::isnan(coverage(v, 0.3f)));
            EXPECT_FALSE(std::isnan(coverage(0.5f, v)));
            EXPECT_FALSE(std::isnan(damp(v, 5.f, 3.f, 4.f)));
            EXPECT_FALSE(std::isnan(damp(1.f, v, 3.f, 4.f)));
            EXPECT_FALSE(std::isnan(damp(1.f, 5.f, v, 4.f)));
            EXPECT_FALSE(std::isnan(damp(1.f, 5.f, 3.f, v)));
        }
        // A pit whose maximum or capacity is not a finite number is never wet.
        EXPECT_FLOAT_EQ(depthAt(2.f, nan, 3.f), 0.f);
        EXPECT_FLOAT_EQ(depthAt(2.f, infinity, 3.f), 0.f);
        EXPECT_FLOAT_EQ(depthAt(2.f, 5.f, infinity), 0.f);
        EXPECT_FLOAT_EQ(damp(1.f, nan, 3.f, 4.f), 0.f);
        EXPECT_FLOAT_EQ(damp(1.f, infinity, 3.f, 4.f), 0.f);
        EXPECT_FLOAT_EQ(damp(1.f, 5.f, infinity, 4.f), 0.f);
        EXPECT_FLOAT_EQ(damp(1.f, 5.f, 3.f, infinity), 0.f); // an infinite range is no ring
        // Infinite water is full water.
        EXPECT_FLOAT_EQ(coverage(infinity, 0.3f), 1.f);
        EXPECT_FLOAT_EQ(coverage(infinity, infinity), 1.f);
        EXPECT_FLOAT_EQ(coverage(0.5f, infinity), 1.f); // an infinite edge is a hard step
        EXPECT_FLOAT_EQ(coverage(-infinity, 0.3f), 0.f);
        EXPECT_FLOAT_EQ(damp(infinity, 5.f, 3.f, 4.f), 1.f);
        EXPECT_FLOAT_EQ(depthAt(infinity, 5.f, 3.f), 3.f);
        EXPECT_FLOAT_EQ(levelOf(2.f, 1.f, infinity), 0.f);
    }

    TEST(PuddleLevel, height_survives_its_sixteen_bit_round_trip_and_clamps_outside_the_range)
    {
        const float base = -1500.f;
        for (int q = 0; q < 65536; q += 7)
            ASSERT_EQ(quantiseHeight(dequantiseHeight(static_cast<std::uint16_t>(q), base), base), q);
        Lcg rng(9);
        for (int i = 0; i < 5000; ++i)
        {
            const float h = base + static_cast<float>(rng.below(8000000)) / 1000.f; // within the 8191.875 range
            ASSERT_TRUE(heightRepresentable(h, base));
            ASSERT_LE(std::abs(dequantiseHeight(quantiseHeight(h, base), base) - h), 1.f / 16.f + 1e-3f) << h;
        }
        EXPECT_EQ(quantiseHeight(base - 10.f, base), 0);
        EXPECT_EQ(quantiseHeight(base + 9000.f, base), 65535);
        EXPECT_EQ(quantiseHeight(nan, base), 0);
        EXPECT_EQ(quantiseHeight(infinity, base), 0);
        EXPECT_EQ(quantiseHeight(0.f, nan), 0);
        EXPECT_FALSE(heightRepresentable(base - 0.5f, base));
        EXPECT_FALSE(heightRepresentable(base + 8192.f, base));
        EXPECT_TRUE(heightRepresentable(base + 8191.875f, base));
        EXPECT_FALSE(heightRepresentable(nan, base));
        EXPECT_FLOAT_EQ(heightRange, 8191.875f);
    }
}
