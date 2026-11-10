// OpenEden (task 0095, stage W-a): tests of components/render/puddlefill.hpp, written from the spec before the code.
// Known answers on synthetic grids, then properties on seeded random grids against a brute-force reference.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>
#include <vector>

#include <components/render/puddlefill.hpp>

#include "puddletestgrids.hpp"

namespace
{
    using namespace PuddleTest;
    using Render::Puddle::accumulateFlow;
    using Render::Puddle::Basins;
    using Render::Puddle::capacityFromFloor;
    using Render::Puddle::Catchment;
    using Render::Puddle::catchmentRatio;
    using Render::Puddle::fillDepressions;
    using Render::Puddle::FillResult;
    using Render::Puddle::FloorCapacity;
    using Render::Puddle::flowDirection;
    using Render::Puddle::flowNone;
    using Render::Puddle::labelBasins;

    const float infinity = std::numeric_limits<float>::infinity();

    FillResult fill(const Grid& g, float cap = infinity)
    {
        return fillDepressions(g.heights, g.sinks, g.width, g.height, cap);
    }

    // A grid that is a profile along x, constant in y, with walls (height 9 unless given) in the first and last row.
    // `profile` gives the column heights; the first and last columns are the sinks at the sides.
    Grid trench(const std::vector<float>& profile, int rows = 5, float wall = 9.f)
    {
        Grid g(static_cast<int>(profile.size()), rows, wall);
        for (int y = 1; y < rows - 1; ++y)
            for (int x = 0; x < g.width; ++x)
                g.at(x, y) = profile[static_cast<std::size_t>(x)];
        return g;
    }

    TEST(PuddleFill, a_bowl_fills_from_the_bottom_to_its_lowest_rim)
    {
        // A paraboloid with its lowest point on a texel in the middle; the grid's edge is the rim and its lowest
        // point is the middle of a side, 32 texels from the centre.
        const int n = 65;
        const float k = 0.01f;
        Grid g(n, n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                g.at(x, y) = k * static_cast<float>((x - 32) * (x - 32) + (y - 32) * (y - 32));
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok()) << f.error;
        const float spill = k * 1024.f;
        EXPECT_FLOAT_EQ(f.filled[g.index(32, 32)], spill);
        EXPECT_FLOAT_EQ(f.capacity[g.index(32, 32)], spill);
        // Every texel inside the circle of radius 32 holds water up to the spill height; those outside hold none.
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const int r2 = (x - 32) * (x - 32) + (y - 32) * (y - 32);
                if (r2 < 1024)
                {
                    EXPECT_FLOAT_EQ(f.filled[g.index(x, y)], spill) << x << "," << y;
                }
                else
                {
                    EXPECT_EQ(f.capacity[g.index(x, y)], 0.f) << x << "," << y;
                }
            }
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_FLOAT_EQ(b.basins[0].maxCapacity, spill);
        EXPECT_EQ(b.basins[0].deepest, g.index(32, 32));
        EXPECT_FLOAT_EQ(b.basins[0].spillHeight, spill);
    }

    TEST(PuddleFill, a_plane_has_no_capacity_and_drains_to_the_sink)
    {
        Grid g(24, 17);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                g.at(x, y) = 0.5f * static_cast<float>(x) + 0.25f * static_cast<float>(y);
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        for (float c : f.capacity)
            EXPECT_EQ(c, 0.f);
        EXPECT_TRUE(labelBasins(f, g.heights).basins.empty());
        // The whole grid's water leaves at the border: the areas at the roots add up to every texel.
        const auto area = accumulateFlow(f);
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < area.size(); ++i)
            if (f.parent[i] < 0)
                total += area[i];
        EXPECT_EQ(total, g.heights.size());
    }

    TEST(PuddleFill, a_tilted_plane_with_one_hollow_has_exactly_one_basin)
    {
        Grid g(40, 40);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                g.at(x, y) = 0.5f * static_cast<float>(x) + 0.25f * static_cast<float>(y);
        // Dig a 3 x 3 hole into the slope, deeper than the slope's rise across it.
        for (int y = 19; y <= 21; ++y)
            for (int x = 19; x <= 21; ++x)
                g.at(x, y) -= 6.f;
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_GT(b.basins[0].area, 0u);
        EXPECT_GT(b.basins[0].maxCapacity, 0.f);
        // Nothing outside the hole's neighbourhood holds water.
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                if (std::abs(x - 20) > 3 || std::abs(y - 20) > 3)
                {
                    EXPECT_EQ(f.capacity[g.index(x, y)], 0.f);
                }
    }

    TEST(PuddleFill, a_flat_plate_with_a_raised_rim_fills_evenly)
    {
        Grid g(30, 20, 0.f);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                if (x == 0 || y == 0 || x == g.width - 1 || y == g.height - 1)
                    g.at(x, y) = 3.f;
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        for (int y = 1; y < g.height - 1; ++y)
            for (int x = 1; x < g.width - 1; ++x)
                EXPECT_EQ(f.capacity[g.index(x, y)], 3.f);
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].area, 28u * 18u);
    }

    TEST(PuddleFill, a_flat_plate_with_no_rim_has_no_capacity)
    {
        Grid g(30, 20, 2.f);
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        for (float c : f.capacity)
            EXPECT_EQ(c, 0.f);
        // On the flat every texel drains by the shortest way out: a texel next to the border goes to a border texel.
        const std::int32_t next = f.parent[g.index(1, 10)];
        ASSERT_GE(next, 0);
        EXPECT_EQ(next % g.width, 0);
    }

    TEST(PuddleFill, two_bowls_at_a_saddle_the_lower_rim_spills_first)
    {
        // The left bowl (columns 2, 3) can leave over the saddle at 4 or by the wall of 6 and 9; the right bowl
        // (5, 6) leaves over the rim at 3. So the right one fills to 3, and the left one to the saddle, 4.
        const Grid g = trench({ 9, 6, 0, 0, 4, 0, 0, 3, 0 });
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 2u);
        const int left = b.id[g.index(2, 1)];
        const int right = b.id[g.index(5, 1)];
        ASSERT_GE(left, 0);
        ASSERT_GE(right, 0);
        EXPECT_NE(left, right);
        EXPECT_EQ(b.basins[left].spillHeight, 4.f);
        EXPECT_EQ(b.basins[left].maxCapacity, 4.f);
        EXPECT_EQ(b.basins[left].area, 6u);
        EXPECT_EQ(b.basins[left].outlet, static_cast<std::int32_t>(g.index(4, 1)));
        EXPECT_EQ(b.basins[right].spillHeight, 3.f);
        EXPECT_EQ(b.basins[right].maxCapacity, 3.f);
        EXPECT_EQ(b.basins[right].area, 6u);
        EXPECT_EQ(b.basins[right].outlet, static_cast<std::int32_t>(g.index(7, 1)));
        // The saddle itself holds no water.
        EXPECT_EQ(f.capacity[g.index(4, 1)], 0.f);
    }

    TEST(PuddleFill, two_bowls_whose_saddle_is_below_both_rims_are_one_basin)
    {
        const Grid g = trench({ 9, 6, 0, 0, 2, 0, 0, 5, 0 });
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].spillHeight, 5.f);
        EXPECT_EQ(b.basins[0].maxCapacity, 5.f);
        EXPECT_EQ(b.basins[0].area, 15u);
        EXPECT_EQ(f.capacity[g.index(4, 2)], 3.f);
    }

    TEST(PuddleFill, a_nested_pit_is_merged_into_its_basin_in_version_one)
    {
        // An inner pit (column 3, floor 0, shelf of 3 beside it) in a bowl whose rims are 5.
        const Grid g = trench({ 0, 5, 3, 0, 3, 3, 5, 0 });
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].spillHeight, 5.f);
        EXPECT_EQ(f.capacity[g.index(3, 2)], 5.f);
        EXPECT_EQ(f.capacity[g.index(2, 2)], 2.f);
        EXPECT_EQ(f.capacity[g.index(5, 2)], 2.f);
    }

    TEST(PuddleFill, pits_that_touch_at_a_corner_are_one_basin_because_the_flood_is_eight_connected)
    {
        // Two pits on a diagonal with ground of the spill height beside them hold one body of water: the flood is
        // 8-connected and both have the same filled height, so they are one basin (review F1).
        Grid g(7, 7, 5.f);
        for (int i = 0; i < 7; ++i)
        {
            g.at(i, 0) = g.at(i, 6) = g.at(0, i) = g.at(6, i) = 0.f;
        }
        g.at(2, 2) = 0.f;
        g.at(3, 3) = 0.f;
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].area, 2u);
        EXPECT_EQ(b.basins[0].spillHeight, 5.f);
        EXPECT_EQ(b.basins[0].deepest, g.index(2, 2));
        EXPECT_EQ(b.basins[0].outlet, static_cast<std::int32_t>(g.index(1, 1)));
    }

    TEST(PuddleFill, a_one_texel_wide_diagonal_pit_is_one_basin_with_one_catchment)
    {
        // The reviewer's case: a 9 x 9 grid of 10 with (2,2) to (6,6) at 0. It was five basins of one texel with
        // ratios 3, 2, 1, 1, 1; it is one basin of five texels and takes only its own rain.
        Grid g(9, 9, 10.f);
        for (int i = 2; i <= 6; ++i)
        {
            g.at(i, i) = 0.f;
        }
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].area, 5u);
        EXPECT_EQ(b.basins[0].maxCapacity, 10.f);
        EXPECT_EQ(b.basins[0].deepest, g.index(2, 2));
        const auto kappa = catchmentRatio(b, f, accumulateFlow(f)).ratio;
        ASSERT_EQ(kappa.size(), 1u);
        EXPECT_FLOAT_EQ(kappa[0], 1.f);
    }

    TEST(PuddleFill, no_two_neighbouring_wet_texels_are_in_different_basins)
    {
        for (std::uint64_t seed = 900; seed < 925; ++seed)
        {
            const Grid g = randomGrid(31, 29, seed, 10, 10);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            const Basins b = labelBasins(f, g.heights);
            for (int y = 0; y < g.height; ++y)
            {
                for (int x = 0; x < g.width; ++x)
                {
                    ASSERT_EQ(b.id[g.index(x, y)] >= 0, f.capacity[g.index(x, y)] > 0.f);
                    for (const auto& offset : Render::Puddle::neighbours)
                    {
                        const int nx = x + offset[0];
                        const int ny = y + offset[1];
                        if (nx < 0 || ny < 0 || nx >= g.width || ny >= g.height)
                        {
                            continue;
                        }
                        if (b.id[g.index(x, y)] >= 0 && b.id[g.index(nx, ny)] >= 0)
                        {
                            ASSERT_EQ(b.id[g.index(x, y)], b.id[g.index(nx, ny)]) << "seed " << seed;
                        }
                    }
                }
            }
        }
    }

    TEST(PuddleFill, the_outlet_is_a_texel_the_water_really_leaves_by_not_a_dead_end_bump)
    {
        // A 3 x 3 pit at (5..7, 5..7) in ground of 20 with the real pass at (8,6) (height 5, draining down a channel
        // to the border) and a bump at (4,5) (height 5, surrounded by 20) that is reached from the pit (review F3).
        Grid g(13, 13, 20.f);
        for (int y = 5; y <= 7; ++y)
        {
            for (int x = 5; x <= 7; ++x)
            {
                g.at(x, y) = 0.f;
            }
        }
        g.at(8, 6) = 5.f;
        g.at(9, 6) = 2.f;
        g.at(10, 6) = 1.f;
        g.at(11, 6) = 0.f;
        g.at(12, 6) = 0.f;
        g.at(4, 5) = 5.f;
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].spillHeight, 5.f);
        EXPECT_EQ(b.basins[0].outlet, static_cast<std::int32_t>(g.index(8, 6)));
    }

    TEST(PuddleFill, every_outlet_drains_out_of_its_basin)
    {
        for (std::uint64_t seed = 930; seed < 950; ++seed)
        {
            const Grid g = randomGrid(33, 27, seed, 12, 10);
            const FillResult f = fill(g);
            const Basins b = labelBasins(f, g.heights);
            for (std::size_t k = 0; k < b.basins.size(); ++k)
            {
                const std::int32_t o = b.basins[k].outlet;
                if (o < 0)
                {
                    continue;
                }
                ASSERT_EQ(b.id[o], -1);
                ASSERT_EQ(g.heights[o], b.basins[k].spillHeight);
                const std::int32_t parent = f.parent[o];
                ASSERT_TRUE(parent < 0 || b.id[parent] != static_cast<std::int32_t>(k)) << "seed " << seed;
            }
        }
    }

    TEST(PuddleFill, flow_direction_outside_the_grid_is_none)
    {
        const Grid g(4, 4, 1.f);
        const FillResult f = fill(g);
        EXPECT_EQ(flowDirection(f, g.heights.size()), flowNone);
        EXPECT_EQ(flowDirection(f, g.heights.size() + 1000), flowNone);
        EXPECT_EQ(flowDirection(FillResult{}, 0), flowNone);
    }

    TEST(PuddleFill, heights_beyond_a_sane_bound_are_refused)
    {
        Grid g(6, 6, 1.f);
        g.at(2, 3) = 3e38f;
        FillResult f = fill(g);
        EXPECT_FALSE(f.ok());
        EXPECT_NE(f.error.find("(2, 3)"), std::string::npos) << f.error;
        g.at(2, 3) = -2e7f;
        EXPECT_FALSE(fill(g).ok());
        g.at(2, 3) = 9.9e6f;
        EXPECT_TRUE(fill(g).ok());
        g.at(2, 3) = -9.9e6f;
        EXPECT_TRUE(fill(g).ok());
    }

    TEST(PuddleFill, the_catchment_of_the_hand_calculated_pit_is_the_same_mirrored_and_turned)
    {
        const Grid g = trench({ 3, 5, 4, 3, 1, 0, 0, 0, 2, 0 });
        for (int kind = 0; kind < 8; ++kind)
        {
            const Symmetry s = symmetry(g.width, g.height, kind);
            Grid t(s.width, s.height);
            t.heights = permute(g.heights, s);
            t.sinks = permute(g.sinks, s);
            const FillResult f = fill(t);
            ASSERT_TRUE(f.ok());
            const Basins b = labelBasins(f, t.heights);
            ASSERT_EQ(b.basins.size(), 1u) << kind;
            EXPECT_FLOAT_EQ(catchmentRatio(b, f, accumulateFlow(f)).ratio[0], 1.5f) << "kind " << kind;
        }
    }

    TEST(PuddleFill, a_pit_that_receives_the_spill_of_another_counts_it_in_its_catchment)
    {
        // The left pit (columns 2, 3; 6 texels) spills over the saddle at 4 into the right pit (5, 6; 6 texels),
        // which spills over 3. The left one's catchment is its own 6 plus the 3 texels of column 1 that drain into it;
        // the right one's is its own 6 plus the saddle (3) with everything that reaches it: 3 + 6 + 3.
        const Grid g = trench({ 9, 6, 0, 0, 4, 0, 0, 3, 0 });
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 2u);
        const auto kappa = catchmentRatio(b, f, accumulateFlow(f)).ratio;
        EXPECT_FLOAT_EQ(kappa[b.id[g.index(2, 1)]], 1.5f);
        EXPECT_FLOAT_EQ(kappa[b.id[g.index(5, 1)]], 3.f);
    }

    TEST(PuddleFill, a_symmetric_pit_between_two_rims_takes_only_its_own_rain)
    {
        const Grid g = trench({ 0, 5, 0, 0, 0, 5, 0 }, 5, 20.f);
        const FillResult f = fill(g);
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].area, 9u);
        EXPECT_FLOAT_EQ(catchmentRatio(b, f, accumulateFlow(f)).ratio[0], 1.f);
    }

    TEST(PuddleFill, sink_texels_have_no_capacity_and_drain_their_neighbours)
    {
        Grid walled(9, 9, 0.f);
        for (int i = 0; i < 9; ++i)
            walled.at(i, 0) = walled.at(i, 8) = walled.at(0, i) = walled.at(8, i) = 5.f;
        // A sea texel at the plate's own level: the plate drains into it.
        Grid sea = walled;
        sea.sinks[sea.index(4, 4)] = 1;
        const FillResult drained = fill(sea);
        ASSERT_TRUE(drained.ok());
        for (float c : drained.capacity)
            EXPECT_EQ(c, 0.f);
        // Follow the water from a corner of the plate: it reaches the sea.
        std::int32_t at = static_cast<std::int32_t>(sea.index(1, 1));
        for (int steps = 0; steps < 100 && drained.parent[at] >= 0; ++steps)
            at = drained.parent[at];
        EXPECT_EQ(at, static_cast<std::int32_t>(sea.index(4, 4)));
        EXPECT_EQ(flowDirection(drained, sea.index(4, 4)), flowNone);

        // A sea at height 2 only lets the plate fill to 2.
        Grid high = walled;
        high.at(4, 4) = 2.f;
        high.sinks[high.index(4, 4)] = 1;
        const FillResult partly = fill(high);
        ASSERT_TRUE(partly.ok());
        EXPECT_EQ(partly.capacity[high.index(4, 4)], 0.f);
        EXPECT_EQ(partly.capacity[high.index(3, 3)], 2.f);
        EXPECT_EQ(partly.capacity[high.index(1, 7)], 2.f);
    }

    TEST(PuddleFill, the_capacity_is_clamped_to_the_cap)
    {
        const Grid g = trench({ 0, 6, 0, 0, 0, 6, 0 });
        const FillResult f = fill(g, 4.f);
        ASSERT_TRUE(f.ok());
        EXPECT_EQ(f.capacity[g.index(3, 2)], 4.f);
        EXPECT_EQ(f.filled[g.index(3, 2)], 6.f); // the filled height itself is not clamped
        EXPECT_EQ(f.capacity[g.index(1, 2)], 0.f);
        // A shallow texel below the cap is not changed.
        const Grid shallowGrid = trench({ 0, 6, 3, 0, 0, 6, 0 });
        const FillResult shallow = fill(shallowGrid, 4.f);
        EXPECT_EQ(shallow.capacity[shallowGrid.index(2, 2)], 3.f);
    }

    TEST(PuddleFill, the_flow_direction_codes_are_pinned)
    {
        // The code is the table index of the direction from a texel to the one it drains to.
        ASSERT_EQ(Render::Puddle::neighbours[0][0], -1);
        ASSERT_EQ(Render::Puddle::neighbours[0][1], 0);
        ASSERT_EQ(Render::Puddle::neighbours[2][0], 0);
        ASSERT_EQ(Render::Puddle::neighbours[2][1], -1);
        ASSERT_EQ(Render::Puddle::neighbours[4][0], 1);
        ASSERT_EQ(Render::Puddle::neighbours[6][1], 1);
        struct Case
        {
            int fromX, fromY;
            int code;
        };
        // The middle texel (1,1) at height 5 with exactly one lower neighbour (height 0) among the eight; the rest of
        // the ring is at 9, so the middle drains to that neighbour.
        const Case cases[] = { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 2 }, { 2, 0, 3 }, { 2, 1, 4 }, { 2, 2, 5 },
            { 1, 2, 6 }, { 0, 2, 7 } };
        for (const Case& c : cases)
        {
            Grid g(3, 3, 9.f);
            g.at(1, 1) = 5.f;
            g.at(c.fromX, c.fromY) = 0.f;
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            EXPECT_EQ(flowDirection(f, g.index(1, 1)), c.code) << c.fromX << "," << c.fromY;
        }
    }

    TEST(PuddleFill, the_catchment_of_a_pit_counts_the_ground_that_drains_into_it)
    {
        // x:  0  1  2  3  4  5  6  7  8  9
        //     3  5  4  3  1  0  0  0  2  0     rows 1 to 3 (rows 0 and 4 are walls of 9)
        // The pit is columns 4 to 7 (12 texels, spill height 2 over column 8). Columns 2 and 3 (6 texels) are a ramp
        // that drains into it; column 1 drains out of the left border. So kappa = (12 + 6) / 12.
        const Grid g = trench({ 3, 5, 4, 3, 1, 0, 0, 0, 2, 0 });
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].area, 12u);
        EXPECT_EQ(b.basins[0].maxCapacity, 2.f);
        EXPECT_EQ(b.basins[0].deepest, g.index(5, 1));
        EXPECT_EQ(b.basins[0].outlet, static_cast<std::int32_t>(g.index(8, 1)));
        const auto area = accumulateFlow(f);
        const auto kappa = catchmentRatio(b, f, area).ratio;
        ASSERT_EQ(kappa.size(), 1u);
        EXPECT_FLOAT_EQ(kappa[0], 1.5f);
        // A lone bowl in a walled grid takes only its own rain.
        Grid lone(9, 9, 0.f);
        for (int i = 0; i < 9; ++i)
            lone.at(i, 0) = lone.at(i, 8) = lone.at(0, i) = lone.at(8, i) = 4.f;
        const FillResult lf = fill(lone);
        const Basins lb = labelBasins(lf, lone.heights);
        EXPECT_FLOAT_EQ(catchmentRatio(lb, lf, accumulateFlow(lf)).ratio[0], 1.f);
    }

    TEST(PuddleFill, a_deep_pit_keeps_only_its_bottom_when_the_cap_is_measured_from_the_floor)
    {
        // A pit 20 deep with sloping sides: capacities 12, 17, 20, 17, 12 across it. A cap of 8 from the floor keeps
        // the lowest 8 units: 0, 5, 8, 5, 0. (Clamping each texel instead would give 8 everywhere.)
        const Grid g = trench({ 0, 20, 8, 3, 0, 3, 8, 20, 0 }, 5, 40.f);
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_EQ(b.basins[0].maxCapacity, 20.f);
        const std::vector<float> fromFloor = capacityFromFloor(f, b, 8.f).capacity;
        const float expected[] = { 0.f, 0.f, 0.f, 5.f, 8.f, 5.f, 0.f, 0.f, 0.f };
        for (int x = 0; x < 9; ++x)
            EXPECT_EQ(fromFloor[g.index(x, 2)], expected[x]) << x;
        const FillResult clamped = fill(g, 8.f);
        EXPECT_EQ(clamped.capacity[g.index(2, 2)], 8.f);
        // A pit that is not deeper than the cap is unchanged.
        const std::vector<float> roomy = capacityFromFloor(f, b, 30.f).capacity;
        for (std::size_t i = 0; i < roomy.size(); ++i)
            EXPECT_EQ(roomy[i], f.capacity[i]);
        // Bad input is refused with an error, not guessed at.
        EXPECT_FALSE(capacityFromFloor(f, b, 0.f).ok());
        EXPECT_FALSE(capacityFromFloor(f, b, -3.f).ok());
        EXPECT_FALSE(capacityFromFloor(f, b, infinity).ok());
        EXPECT_FALSE(capacityFromFloor(f, b, std::numeric_limits<float>::quiet_NaN()).ok());
        EXPECT_FALSE(capacityFromFloor(FillResult{}, Basins{}, 8.f).ok());
    }

    TEST(PuddleFill, a_cap_measured_from_the_floor_gives_the_depth_of_the_uncapped_pit_while_the_level_is_within_it)
    {
        for (std::uint64_t seed = 800; seed < 812; ++seed)
        {
            const Grid g = randomGrid(37, 31, seed, 24, 8);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            const Basins b = labelBasins(f, g.heights);
            const float cap = 5.f;
            const std::vector<float> fromFloor = capacityFromFloor(f, b, cap).capacity;
            for (std::size_t i = 0; i < fromFloor.size(); ++i)
            {
                ASSERT_GE(fromFloor[i], 0.f);
                ASSERT_LE(fromFloor[i], std::min(f.capacity[i], cap));
                if (b.id[i] < 0)
                    continue;
                const float deepest = b.basins[b.id[i]].maxCapacity;
                const float cappedMax = std::min(deepest, cap);
                for (float level = 0.f; level <= cap; level += 0.5f)
                {
                    const float uncappedLevel = std::min(level, deepest);
                    const float reference = std::clamp(uncappedLevel - (deepest - f.capacity[i]), 0.f, f.capacity[i]);
                    const float viaCap
                        = std::clamp(std::min(level, cappedMax) - (cappedMax - fromFloor[i]), 0.f, fromFloor[i]);
                    ASSERT_EQ(viaCap, reference) << "seed " << seed << " texel " << i << " level " << level;
                }
            }
        }
    }

    TEST(PuddleFill, the_catchment_of_a_capped_pond_is_measured_on_its_footprint_not_the_whole_pit)
    {
        // The deep pit again (capacities 12, 17, 20, 17, 12 over 15 texels). Capped to 8 from the floor the pond is
        // the middle three columns, 9 texels; no ground outside drains in, so the pit's own 15 texels of rain collect
        // in 9 texels of water: 15 / 9. Measured on the whole pit it would be 1 (review F4).
        const Grid g = trench({ 0, 20, 8, 3, 0, 3, 8, 20, 0 }, 5, 40.f);
        const FillResult f = fill(g);
        const Basins b = labelBasins(f, g.heights);
        const auto area = accumulateFlow(f);
        ASSERT_EQ(b.basins.size(), 1u);
        EXPECT_FLOAT_EQ(catchmentRatio(b, f, area).ratio[0], 1.f);
        const FloorCapacity pond = capacityFromFloor(f, b, 8.f);
        ASSERT_TRUE(pond.ok()) << pond.error;
        const Catchment capped = catchmentRatio(b, f, area, pond.capacity);
        ASSERT_TRUE(capped.ok()) << capped.error;
        EXPECT_FLOAT_EQ(capped.ratio[0], 15.f / 9.f);
        // A pit that is not deeper than the cap has its whole area as footprint: nothing changes.
        const Grid hand = trench({ 3, 5, 4, 3, 1, 0, 0, 0, 2, 0 });
        const FillResult hf = fill(hand);
        const Basins hb = labelBasins(hf, hand.heights);
        const FloorCapacity roomy = capacityFromFloor(hf, hb, 12.f);
        ASSERT_TRUE(roomy.ok());
        EXPECT_FLOAT_EQ(catchmentRatio(hb, hf, accumulateFlow(hf), roomy.capacity).ratio[0], 1.5f);
    }

    TEST(PuddleFill, a_fill_with_a_finite_cap_or_basins_of_another_fill_are_refused)
    {
        const Grid g = trench({ 0, 20, 8, 3, 0, 3, 8, 20, 0 }, 5, 40.f);
        const FillResult capped = fill(g, 12.f);
        const Basins cappedBasins = labelBasins(capped, g.heights);
        const FloorCapacity refused = capacityFromFloor(capped, cappedBasins, 8.f);
        EXPECT_FALSE(refused.ok());
        EXPECT_NE(refused.error.find("finite cap"), std::string::npos) << refused.error;
        EXPECT_TRUE(refused.capacity.empty());

        const FillResult f = fill(g);
        const Basins b = labelBasins(f, g.heights);
        Grid other = g;
        other.at(4, 2) = 1.f; // the same size, another surface
        const FillResult otherFill = fill(other);
        const Basins otherBasins = labelBasins(otherFill, other.heights);
        const FloorCapacity mismatched = capacityFromFloor(f, otherBasins, 8.f);
        EXPECT_FALSE(mismatched.ok());
        EXPECT_NE(mismatched.error.find("not made from this fill"), std::string::npos) << mismatched.error;
        EXPECT_FALSE(catchmentRatio(otherBasins, f, accumulateFlow(f)).ok());
        EXPECT_FALSE(catchmentRatio(b, otherFill, accumulateFlow(otherFill)).ok());
        // A flow area of the wrong size, a pond of the wrong size and no pond are refused too.
        EXPECT_FALSE(catchmentRatio(b, f, std::vector<std::uint32_t>(3, 1)).ok());
        const FloorCapacity pond = capacityFromFloor(f, b, 8.f);
        ASSERT_TRUE(pond.ok());
        EXPECT_FALSE(catchmentRatio(b, f, accumulateFlow(f), std::vector<float>(5, 1.f)).ok());
        EXPECT_FALSE(catchmentRatio(b, f, accumulateFlow(f), std::span<const float>{}).ok());
        EXPECT_TRUE(catchmentRatio(b, f, accumulateFlow(f), pond.capacity).ok());
        EXPECT_FALSE(catchmentRatio(Basins{}, FillResult{}, std::vector<std::uint32_t>{}).ok());
    }

    TEST(PuddleFill, the_cap_measured_from_the_floor_stays_finite_for_extreme_heights)
    {
        // Heights at the bounds give a capacity of 2e7 in a pit; nothing overflows or becomes not a number.
        Grid g(9, 9, 9.9e6f);
        for (int y = 3; y <= 5; ++y)
        {
            for (int x = 3; x <= 5; ++x)
            {
                g.at(x, y) = -9.9e6f;
            }
        }
        const FillResult f = fill(g);
        ASSERT_TRUE(f.ok());
        const Basins b = labelBasins(f, g.heights);
        ASSERT_EQ(b.basins.size(), 1u);
        const FloorCapacity pond = capacityFromFloor(f, b, 12.f);
        ASSERT_TRUE(pond.ok()) << pond.error;
        for (const float c : pond.capacity)
        {
            ASSERT_TRUE(std::isfinite(c));
            ASSERT_GE(c, 0.f);
            ASSERT_LE(c, 12.f);
        }
        EXPECT_EQ(pond.capacity[g.index(4, 4)], 12.f);
    }

    TEST(PuddleFill, flow_accumulation_counts_every_texel_once)
    {
        for (std::uint64_t seed = 1; seed <= 10; ++seed)
        {
            const Grid g = randomGrid(31, 23, seed);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            const auto area = accumulateFlow(f);
            std::uint64_t total = 0;
            for (std::size_t i = 0; i < area.size(); ++i)
            {
                EXPECT_GE(area[i], 1u);
                if (f.parent[i] < 0)
                    total += area[i];
            }
            EXPECT_EQ(total, g.heights.size()) << "seed " << seed;
        }
    }

    TEST(PuddleFill, the_filled_height_equals_the_brute_force_minimax_on_random_grids)
    {
        for (std::uint64_t seed = 1; seed <= 40; ++seed)
        {
            const int w = 5 + static_cast<int>(seed % 17);
            const int h = 4 + static_cast<int>(seed % 13);
            const Grid g = randomGrid(w, h, seed, 12, seed % 3 == 0 ? 0 : 40);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            const std::vector<float> reference = referenceFilled(g);
            for (std::size_t i = 0; i < reference.size(); ++i)
                ASSERT_EQ(f.filled[i], reference[i]) << "seed " << seed << " texel " << i;
        }
    }

    TEST(PuddleFill, capacity_is_between_zero_and_the_cap_and_zero_on_sinks)
    {
        for (std::uint64_t seed = 100; seed < 130; ++seed)
        {
            const Grid g = randomGrid(40, 30, seed, 16, 30);
            const FillResult f = fill(g, 5.f);
            ASSERT_TRUE(f.ok());
            for (std::size_t i = 0; i < f.capacity.size(); ++i)
            {
                ASSERT_GE(f.capacity[i], 0.f);
                ASSERT_LE(f.capacity[i], 5.f);
                ASSERT_GE(f.filled[i], g.heights[i]);
                if (g.sinks[i] != 0)
                {
                    ASSERT_EQ(f.capacity[i], 0.f);
                }
            }
        }
    }

    TEST(PuddleFill, filling_a_filled_surface_changes_nothing_and_leaves_no_pit)
    {
        for (std::uint64_t seed = 200; seed < 220; ++seed)
        {
            Grid g = randomGrid(33, 27, seed, 14, 20);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            Grid filled = g;
            filled.heights = f.filled;
            const FillResult again = fill(filled);
            ASSERT_TRUE(again.ok());
            EXPECT_EQ(again.filled, f.filled) << "seed " << seed;
            for (float c : again.capacity)
                ASSERT_EQ(c, 0.f);
        }
    }

    TEST(PuddleFill, water_always_has_a_way_out_along_a_surface_that_never_rises)
    {
        for (std::uint64_t seed = 300; seed < 315; ++seed)
        {
            const Grid g = randomGrid(35, 29, seed, 10, 10);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            ASSERT_EQ(f.order.size(), g.heights.size());
            std::vector<std::size_t> position(f.order.size());
            for (std::size_t i = 0; i < f.order.size(); ++i)
                position[f.order[i]] = i;
            for (std::size_t i = 0; i < f.parent.size(); ++i)
            {
                const std::int32_t p = f.parent[i];
                if (p < 0)
                {
                    const int x = static_cast<int>(i % g.width);
                    const int y = static_cast<int>(i / g.width);
                    const bool edge = x == 0 || y == 0 || x == g.width - 1 || y == g.height - 1;
                    ASSERT_TRUE(edge || g.sinks[i] != 0);
                    continue;
                }
                ASSERT_LE(f.filled[p], f.filled[i]);
                ASSERT_LT(position[p], position[i]);
            }
        }
    }

    TEST(PuddleFill, a_texel_with_a_never_rising_path_to_a_sink_has_no_capacity)
    {
        for (std::uint64_t seed = 400; seed < 420; ++seed)
        {
            const Grid g = randomGrid(30, 30, seed, 10, 15);
            const FillResult f = fill(g);
            ASSERT_TRUE(f.ok());
            // Texels from which a path of non-increasing ground reaches a sink or the edge, found by relaxing.
            std::vector<bool> drains(g.heights.size(), false);
            for (std::size_t i = 0; i < drains.size(); ++i)
            {
                const int x = static_cast<int>(i % g.width);
                const int y = static_cast<int>(i / g.width);
                drains[i] = x == 0 || y == 0 || x == g.width - 1 || y == g.height - 1 || g.sinks[i] != 0;
            }
            for (bool changed = true; changed;)
            {
                changed = false;
                for (int y = 0; y < g.height; ++y)
                    for (int x = 0; x < g.width; ++x)
                    {
                        if (drains[g.index(x, y)])
                            continue;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const int nx = x + dx;
                                const int ny = y + dy;
                                if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= g.width || ny >= g.height)
                                    continue;
                                if (drains[g.index(nx, ny)] && g.at(nx, ny) <= g.at(x, y) && !drains[g.index(x, y)])
                                {
                                    drains[g.index(x, y)] = true;
                                    changed = true;
                                }
                            }
                    }
            }
            for (std::size_t i = 0; i < drains.size(); ++i)
                if (drains[i])
                {
                    ASSERT_EQ(f.capacity[i], 0.f) << "seed " << seed << " texel " << i;
                }
        }
    }

    TEST(PuddleFill, the_basins_of_random_grids_are_consistent)
    {
        for (std::uint64_t seed = 500; seed < 520; ++seed)
        {
            const Grid g = randomGrid(37, 31, seed, 12, 15);
            const FillResult f = fill(g, 6.f);
            ASSERT_TRUE(f.ok());
            const Basins b = labelBasins(f, g.heights);
            const auto kappa = catchmentRatio(b, f, accumulateFlow(f)).ratio;
            ASSERT_EQ(kappa.size(), b.basins.size());
            std::vector<std::uint32_t> counted(b.basins.size(), 0);
            std::uint64_t wet = 0;
            for (std::size_t i = 0; i < f.capacity.size(); ++i)
            {
                if (f.capacity[i] > 0.f)
                {
                    ++wet;
                    ASSERT_GE(b.id[i], 0);
                    ++counted[b.id[i]];
                    // One spill height per basin.
                    ASSERT_EQ(f.filled[i], b.basins[b.id[i]].spillHeight);
                    ASSERT_LE(f.capacity[i], b.basins[b.id[i]].maxCapacity);
                }
                else
                {
                    ASSERT_EQ(b.id[i], -1);
                }
            }
            std::uint64_t sum = 0;
            for (std::size_t k = 0; k < b.basins.size(); ++k)
            {
                ASSERT_EQ(counted[k], b.basins[k].area);
                ASSERT_GE(kappa[k], 1.f);
                sum += b.basins[k].area;
            }
            ASSERT_EQ(sum, wet);
        }
    }

    TEST(PuddleFill, the_same_grid_gives_the_same_bytes_every_time)
    {
        const Grid g = randomGrid(64, 48, 7, 16, 20);
        const FillResult a = fill(g, 12.f);
        const FillResult b = fill(g, 12.f);
        ASSERT_TRUE(a.ok());
        ASSERT_EQ(a.filled.size(), b.filled.size());
        EXPECT_EQ(0, std::memcmp(a.filled.data(), b.filled.data(), a.filled.size() * sizeof(float)));
        EXPECT_EQ(0, std::memcmp(a.capacity.data(), b.capacity.data(), a.capacity.size() * sizeof(float)));
        EXPECT_EQ(a.parent, b.parent);
        EXPECT_EQ(a.order, b.order);
    }

    TEST(PuddleFill, turning_or_mirroring_the_grid_turns_or_mirrors_the_result)
    {
        auto describe = [](const Basins& b) {
            std::multiset<std::tuple<std::uint32_t, float, float>> out;
            for (const auto& k : b.basins)
                out.insert({ k.area, k.maxCapacity, k.spillHeight });
            return out;
        };
        for (std::uint64_t seed = 600; seed < 606; ++seed)
        {
            const Grid g = randomGrid(23, 17, seed, 9, 15);
            const FillResult base = fill(g, 6.f);
            ASSERT_TRUE(base.ok());
            const Basins baseBasins = labelBasins(base, g.heights);
            for (int kind = 1; kind < 8; ++kind)
            {
                const Symmetry s = symmetry(g.width, g.height, kind);
                Grid t(s.width, s.height);
                t.heights = permute(g.heights, s);
                t.sinks = permute(g.sinks, s);
                const FillResult turned = fill(t, 6.f);
                ASSERT_TRUE(turned.ok());
                EXPECT_EQ(turned.filled, permute(base.filled, s)) << "seed " << seed << " kind " << kind;
                EXPECT_EQ(turned.capacity, permute(base.capacity, s)) << "seed " << seed << " kind " << kind;
                EXPECT_EQ(describe(labelBasins(turned, t.heights)), describe(baseBasins)) << "kind " << kind;
            }
        }
    }

    TEST(PuddleFill, a_piece_of_the_grid_has_the_same_filled_height_where_its_water_stays_inside)
    {
        // The whole grid is 44 x 44; the piece is the square from (10,10) to (33,33) with its ring of texels as the
        // piece's edge. Inside the piece the ground is low and random with one sea texel; its ring is high. A piece's
        // edge is a sink, so its filled heights can only be lower than the whole's, and are equal wherever the whole's
        // filled height is below the lowest ground of the ring (water that low cannot have crossed the ring).
        for (std::uint64_t seed = 700; seed < 720; ++seed)
        {
            Lcg rng(seed);
            Grid whole(44, 44);
            for (int y = 0; y < 44; ++y)
                for (int x = 0; x < 44; ++x)
                {
                    const bool inPiece = x >= 10 && x <= 33 && y >= 10 && y <= 33;
                    const bool ring = inPiece && (x == 10 || x == 33 || y == 10 || y == 33);
                    const bool inside = inPiece && !ring;
                    const float low = static_cast<float>(rng.below(20)) / 4.f; // 0 to 4.75
                    const float high = 6.f + static_cast<float>(rng.below(12)) / 4.f; // 6 to 8.75
                    whole.at(x, y) = ring ? high : (inside ? low : static_cast<float>(rng.below(36)) / 4.f);
                }
            whole.at(20, 20) = 0.f;
            whole.sinks[whole.index(20, 20)] = 1;
            const FillResult full = fill(whole, infinity);
            ASSERT_TRUE(full.ok());

            Grid piece(24, 24);
            float ringMin = infinity;
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 24; ++x)
                {
                    piece.at(x, y) = whole.at(10 + x, 10 + y);
                    piece.sinks[piece.index(x, y)] = whole.sinks[whole.index(10 + x, 10 + y)];
                    if (x == 0 || y == 0 || x == 23 || y == 23)
                        ringMin = std::min(ringMin, piece.at(x, y));
                }
            const FillResult cut = fill(piece, infinity);
            ASSERT_TRUE(cut.ok());
            int equalCount = 0;
            for (int y = 1; y < 23; ++y)
                for (int x = 1; x < 23; ++x)
                {
                    const float inWhole = full.filled[whole.index(10 + x, 10 + y)];
                    const float inPiece = cut.filled[piece.index(x, y)];
                    ASSERT_LE(inPiece, inWhole) << "seed " << seed;
                    if (inWhole < ringMin)
                    {
                        ASSERT_EQ(inPiece, inWhole) << "seed " << seed << " at " << x << "," << y;
                        ++equalCount;
                    }
                }
            EXPECT_GT(equalCount, 100) << "the case should exercise the equality";
        }
    }

    TEST(PuddleFill, a_regression_pin_of_the_random_grid_result)
    {
        // A pin of this implementation's own output (not an independent oracle) on a seeded grid: every operation in
        // the fill is a copy, a comparison or one subtraction, so the bytes are the same on every compiler.
        const Grid g = randomGrid(64, 48, 99, 16, 20);
        const FillResult f = fill(g, 12.f);
        ASSERT_TRUE(f.ok());
        std::uint64_t hash = fnv1a(f.filled.data(), f.filled.size() * sizeof(float));
        hash = fnv1a(f.capacity.data(), f.capacity.size() * sizeof(float), hash);
        hash = fnv1a(f.parent.data(), f.parent.size() * sizeof(std::int32_t), hash);
        EXPECT_EQ(hash, 2279726678007969629ull);
    }

    TEST(PuddleFill, odd_sizes_work)
    {
        const Grid one(1, 1, 3.f);
        const FillResult a = fill(one);
        ASSERT_TRUE(a.ok());
        EXPECT_EQ(a.capacity[0], 0.f);
        EXPECT_EQ(a.parent[0], -1);
        const Grid strip(9, 1, 2.f);
        const FillResult b = fill(strip);
        ASSERT_TRUE(b.ok());
        for (float c : b.capacity)
            EXPECT_EQ(c, 0.f);
        const Grid two(2, 2, 1.f);
        EXPECT_TRUE(fill(two).ok());
        const Grid thin(3, 40, 1.f);
        EXPECT_TRUE(fill(thin).ok());
    }

    TEST(PuddleFill, bad_input_is_refused_with_the_place)
    {
        Grid g(8, 6, 1.f);
        g.at(3, 2) = std::numeric_limits<float>::quiet_NaN();
        FillResult f = fill(g);
        EXPECT_FALSE(f.ok());
        EXPECT_NE(f.error.find("(3, 2)"), std::string::npos) << f.error;
        EXPECT_TRUE(f.filled.empty());
        EXPECT_TRUE(f.capacity.empty());

        g.at(3, 2) = infinity;
        f = fill(g);
        EXPECT_FALSE(f.ok());
        EXPECT_NE(f.error.find("(3, 2)"), std::string::npos) << f.error;
        g.at(3, 2) = -infinity;
        EXPECT_FALSE(fill(g).ok());
        g.at(3, 2) = 1.f;
        EXPECT_TRUE(fill(g).ok());

        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 0, 6).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 8, 0).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, -8, -6).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 7, 6).ok()); // wrong size
        const std::vector<std::uint8_t> shortSinks(5, 0);
        EXPECT_FALSE(fillDepressions(g.heights, shortSinks, 8, 6).ok());
        EXPECT_TRUE(fillDepressions(g.heights, {}, 8, 6).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 8, 6, 0.f).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 8, 6, -1.f).ok());
        EXPECT_FALSE(fillDepressions(g.heights, g.sinks, 8, 6, std::numeric_limits<float>::quiet_NaN()).ok());
    }

    TEST(PuddleFill, grids_over_the_caps_are_refused_before_anything_is_allocated)
    {
        const std::vector<float> none;
        EXPECT_FALSE(fillDepressions(none, {}, Render::Puddle::maxGridSide + 1, 2).ok());
        EXPECT_FALSE(fillDepressions(none, {}, 2, Render::Puddle::maxGridSide + 1).ok());
        // Each side is allowed but together they are not.
        const FillResult f = fillDepressions(none, {}, Render::Puddle::maxGridSide, Render::Puddle::maxGridSide);
        EXPECT_FALSE(f.ok());
        EXPECT_NE(f.error.find("texels"), std::string::npos) << f.error;
        EXPECT_FALSE(fillDepressions(none, {}, 2147483647, 2147483647).ok());
    }

    TEST(PuddleFill, a_realistic_grid_takes_a_fraction_of_a_second)
    {
        // 3 x 3 cells of 128 x 128 texels (a cell and its halo at 64 units); the loose bound only catches a blow-up,
        // the figure is recorded in the task's stage log.
        Grid g(384, 384);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
                g.at(x, y) = 8.f * std::sin(0.11f * static_cast<float>(x)) * std::cos(0.07f * static_cast<float>(y))
                    + 0.02f * static_cast<float>((x * 31 + y * 17) % 13);
        const auto start = std::chrono::steady_clock::now();
        const FillResult f = fill(g, 12.f);
        const Basins b = labelBasins(f, g.heights);
        const auto kappa = catchmentRatio(b, f, accumulateFlow(f)).ratio;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(f.ok());
        RecordProperty("milliseconds_384x384", static_cast<int>(ms));
        std::printf(
            "[ perf ] fill + label + flow + catchment on 384 x 384: %.1f ms, %zu basins\n", ms, b.basins.size());
        EXPECT_LT(ms, 5000.0);
        EXPECT_EQ(kappa.size(), b.basins.size());
    }
}
