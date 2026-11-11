#include <gtest/gtest.h>

#include <limits>

#include <components/render/puddlefill.hpp>
#include <components/render/puddlelevel.hpp>

#include "puddletestgrids.hpp"

// Task 0095, stage W-b (decision 5 of toddhow, 2026-11-10): the catchment ratio with the contributing ground bounded by
// the length of its water path to the pit. The reference below walks every texel's chain to the first pit one texel at
// a time, which is not how the code under test works (one sweep over the flood order).

namespace
{
    using namespace Render::Puddle;
    constexpr float inf = std::numeric_limits<float>::infinity();

    struct Case
    {
        PuddleTest::Grid grid;
        FillResult fill;
        Basins basins;
        FloorCapacity floor;
    };

    Case make(PuddleTest::Grid grid, float cap = 12.f)
    {
        Case c{ std::move(grid), {}, {}, {} };
        c.fill = fillDepressions(c.grid.heights, c.grid.sinks, c.grid.width, c.grid.height, inf);
        c.basins = labelBasins(c.fill, c.grid.heights);
        c.floor = capacityFromFloor(c.fill, c.basins, cap);
        return c;
    }

    // A plane falling east (30 at x = 0, 0 at x = 30) with a 3 x 3 pit sunk by 3 units at (14..16, 14..16).
    PuddleTest::Grid slopeWithPit()
    {
        PuddleTest::Grid g(31, 31);
        for (int y = 0; y < 31; ++y)
            for (int x = 0; x < 31; ++x)
                g.at(x, y) = 30.f - static_cast<float>(x);
        for (int y = 14; y <= 16; ++y)
            for (int x = 14; x <= 16; ++x)
                g.at(x, y) -= 3.f;
        return g;
    }

    // The inflow of every pit by walking each texel's chain (independent of the code under test).
    std::vector<double> referenceInflow(const Case& c, std::int64_t maxTenths)
    {
        std::vector<double> inflow(c.basins.basins.size(), 0.0);
        const int w = c.grid.width;
        for (std::size_t start = 0; start < c.fill.parent.size(); ++start)
        {
            if (c.basins.id[start] >= 0)
                continue;
            std::int64_t length = 0;
            std::size_t at = start;
            while (true)
            {
                const std::int32_t parent = c.fill.parent[at];
                if (parent < 0)
                    break;
                const int dx = static_cast<int>(parent % w) - static_cast<int>(at % w);
                const int dy = static_cast<int>(parent / w) - static_cast<int>(at / w);
                length += (dx != 0 && dy != 0) ? 14 : 10;
                at = static_cast<std::size_t>(parent);
                if (c.basins.id[at] >= 0)
                {
                    if (length <= maxTenths)
                        inflow[static_cast<std::size_t>(c.basins.id[at])] += 1.0;
                    break;
                }
            }
        }
        return inflow;
    }

    std::vector<float> pondOf(const Case& c)
    {
        return c.floor.capacity;
    }

    TEST(PuddleCatchmentBounded, the_slope_with_a_pit_has_one_pit_that_the_upslope_ground_drains_into)
    {
        const Case c = make(slopeWithPit());
        ASSERT_TRUE(c.floor.ok());
        ASSERT_EQ(c.basins.basins.size(), 1u);
        const Catchment all = catchmentRatioBounded(c.basins, c.fill, pondOf(c), 1000000);
        ASSERT_TRUE(all.ok());
        // Some ground west of the pit drains into it: the ratio is above the pit's own area over its pond.
        EXPECT_GT(all.ratio[0], 1.5f);
    }

    TEST(PuddleCatchmentBounded, a_bound_of_zero_counts_only_the_pit_itself)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> pond = pondOf(c);
        const Catchment none = catchmentRatioBounded(c.basins, c.fill, pond, 0);
        ASSERT_TRUE(none.ok());
        double footprint = 0;
        for (const float v : pond)
            footprint += v > 0.f ? 1.0 : 0.0;
        EXPECT_FLOAT_EQ(none.ratio[0], static_cast<float>(c.basins.basins[0].area / footprint));
    }

    TEST(PuddleCatchmentBounded, the_inflow_matches_a_texel_by_texel_walk_for_several_bounds)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> pond = pondOf(c);
        double footprint = 0;
        for (const float v : pond)
            footprint += v > 0.f ? 1.0 : 0.0;
        for (const std::int64_t bound : { 0, 10, 14, 20, 30, 50, 100, 150, 1000000 })
        {
            const Catchment got = catchmentRatioBounded(c.basins, c.fill, pond, bound);
            ASSERT_TRUE(got.ok());
            const double inflow = referenceInflow(c, bound)[0];
            const double expected
                = std::min<double>(c.basins.basins[0].area + inflow, static_cast<double>(c.grid.heights.size()))
                / footprint;
            EXPECT_FLOAT_EQ(got.ratio[0], static_cast<float>(expected)) << "bound " << bound;
        }
    }

    TEST(PuddleCatchmentBounded, a_larger_bound_never_gives_a_smaller_ratio)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> pond = pondOf(c);
        float previous = 0.f;
        for (std::int64_t bound = 0; bound <= 400; bound += 7)
        {
            const Catchment got = catchmentRatioBounded(c.basins, c.fill, pond, bound);
            ASSERT_TRUE(got.ok());
            EXPECT_GE(got.ratio[0], previous) << "bound " << bound;
            previous = got.ratio[0];
        }
    }

    TEST(PuddleCatchmentBounded, a_small_bound_gives_a_ratio_below_the_unbounded_one_on_a_long_slope)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> pond = pondOf(c);
        const Catchment bounded = catchmentRatioBounded(c.basins, c.fill, pond, 30);
        const Catchment all = catchmentRatioBounded(c.basins, c.fill, pond, 1000000);
        ASSERT_TRUE(bounded.ok() && all.ok());
        EXPECT_LT(bounded.ratio[0], all.ratio[0]);
    }

    TEST(PuddleCatchmentBounded, random_grids_agree_with_the_walk_at_every_bound)
    {
        for (std::uint64_t seed = 1; seed <= 25; ++seed)
        {
            const Case c = make(PuddleTest::randomGrid(24, 24, seed, 12, 10));
            ASSERT_TRUE(c.floor.ok());
            const std::vector<float> pond = pondOf(c);
            std::vector<double> footprint(c.basins.basins.size(), 0.0);
            for (std::size_t t = 0; t < pond.size(); ++t)
                if (c.basins.id[t] >= 0 && pond[t] > 0.f)
                    footprint[static_cast<std::size_t>(c.basins.id[t])] += 1.0;
            for (const std::int64_t bound : { 0, 10, 24, 60, 1000000 })
            {
                const Catchment got = catchmentRatioBounded(c.basins, c.fill, pond, bound);
                ASSERT_TRUE(got.ok());
                const std::vector<double> inflow = referenceInflow(c, bound);
                for (std::size_t b = 0; b < inflow.size(); ++b)
                {
                    if (!(footprint[b] > 0.0))
                    {
                        EXPECT_FLOAT_EQ(got.ratio[b], 1.f);
                        continue;
                    }
                    const double expected = std::min<double>(c.basins.basins[b].area + inflow[b],
                                                static_cast<double>(c.grid.heights.size()))
                        / footprint[b];
                    EXPECT_FLOAT_EQ(got.ratio[b], static_cast<float>(expected))
                        << "seed " << seed << " basin " << b << " bound " << bound;
                }
            }
        }
    }

    TEST(PuddleCatchmentBounded, a_pit_with_no_pond_keeps_the_ratio_one)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> noPond(c.grid.heights.size(), 0.f);
        const Catchment got = catchmentRatioBounded(c.basins, c.fill, noPond, 1000);
        ASSERT_TRUE(got.ok());
        EXPECT_FLOAT_EQ(got.ratio[0], 1.f);
    }

    TEST(PuddleCatchmentBounded, misuse_is_refused)
    {
        const Case c = make(slopeWithPit());
        const std::vector<float> pond = pondOf(c);
        EXPECT_FALSE(catchmentRatioBounded(c.basins, c.fill, pond, -1).ok());
        const std::vector<float> wrong(5, 0.f);
        EXPECT_FALSE(catchmentRatioBounded(c.basins, c.fill, wrong, 10).ok());
        const Case other = make(PuddleTest::randomGrid(31, 31, 3, 12, 10));
        EXPECT_FALSE(catchmentRatioBounded(other.basins, c.fill, pond, 10).ok());
        FillResult failed;
        failed.error = "x";
        EXPECT_FALSE(catchmentRatioBounded(c.basins, failed, pond, 10).ok());
    }

    TEST(PuddleCatchmentBounded, water_is_counted_in_the_first_pit_it_reaches_and_not_passed_on)
    {
        // Two pits in a row on the slope: the ground above both drains into the upper one first.
        PuddleTest::Grid g(41, 31);
        for (int y = 0; y < 31; ++y)
            for (int x = 0; x < 41; ++x)
                g.at(x, y) = 40.f - static_cast<float>(x);
        for (int y = 14; y <= 16; ++y)
        {
            for (int x = 10; x <= 12; ++x)
                g.at(x, y) -= 3.f;
            for (int x = 24; x <= 26; ++x)
                g.at(x, y) -= 3.f;
        }
        const Case c = make(std::move(g));
        ASSERT_EQ(c.basins.basins.size(), 2u);
        const std::vector<float> pond = pondOf(c);
        const Catchment all = catchmentRatioBounded(c.basins, c.fill, pond, 1000000);
        ASSERT_TRUE(all.ok());
        const std::vector<double> inflow = referenceInflow(c, 1000000);
        // The reference walk stops at the first pit as well; both pits get inflow from the slope above them only.
        EXPECT_GT(inflow[0], 0.0);
        EXPECT_GT(inflow[1], 0.0);
    }
}
