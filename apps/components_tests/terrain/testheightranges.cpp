// The height ranges the terrain builder keeps for culling (2026-11-10): a tile of level 3 and up has 33 vertices over
// 64 or more grid cells, and every cell must still see the heights around it, not the south-west vertex only. Pure
// code.
#include <gtest/gtest.h>

#include <components/terrain/heightranges.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace
{
    // The code before the fix (Builder::getBatchData), with numVerts(1/8 cell, lod) as it computes it.
    std::vector<std::pair<float, float>> oldRanges(
        const std::vector<float>& heights, std::uint32_t width, std::uint32_t gridSize, std::uint32_t verts0)
    {
        std::vector<std::pair<float, float>> ranges(static_cast<std::size_t>(gridSize) * gridSize);
        for (std::uint32_t gx = 0; gx < gridSize; ++gx)
        {
            for (std::uint32_t gy = 0; gy < gridSize; ++gy)
            {
                float low = 1e30f, high = -1e30f;
                for (std::uint32_t x = gx * (verts0 - 1); x < gx * (verts0 - 1) + verts0; ++x)
                {
                    for (std::uint32_t y = gy * (verts0 - 1); y < gy * (verts0 - 1) + verts0; ++y)
                    {
                        const float h = heights[x + y * width];
                        low = std::min(low, h);
                        high = std::max(high, h);
                    }
                }
                ranges[gx + std::size_t{ gy } * gridSize] = { low, high };
            }
        }
        return ranges;
    }

    // A 33 x 33 tile that is flat at 0 with a 5000 high mountain in the middle.
    std::vector<float> mountainTile()
    {
        std::vector<float> heights(33 * 33, 0.f);
        for (int y = 12; y <= 20; ++y)
        {
            for (int x = 12; x <= 20; ++x)
                heights[x + y * 33] = 5000.f - 400.f * static_cast<float>(std::abs(x - 16) + std::abs(y - 16));
        }
        return heights;
    }

    TEST(TerrainHeightRanges, a_coarse_tile_sees_its_mountain_not_only_its_south_west_corner)
    {
        const std::vector<float> heights = mountainTile();
        // Level 3: 8 cells of 8 grid cells each = 64 x 64; numVerts(0.125, lod 4) truncates to 1 vertex.
        const auto old = oldRanges(heights, 33, 64, 1);
        float oldHigh = -1e30f;
        for (const auto& r : old)
            oldHigh = std::max(oldHigh, r.second);
        EXPECT_FLOAT_EQ(oldHigh, 0.f) << "the old code saw only vertex (0, 0)";

        const auto ranges = Terrain::computeHeightRanges(heights.data(), 33, 64);
        ASSERT_EQ(ranges.size(), 64u * 64u);
        float high = -1e30f, low = 1e30f;
        for (const auto& r : ranges)
        {
            high = std::max(high, r.second);
            low = std::min(low, r.first);
        }
        EXPECT_FLOAT_EQ(high, 5000.f);
        EXPECT_FLOAT_EQ(low, 0.f);
    }

    TEST(TerrainHeightRanges, every_vertex_is_inside_the_range_of_the_cell_that_holds_it)
    {
        const std::vector<float> heights = mountainTile();
        for (const std::uint32_t grid : { 16u, 32u, 64u, 512u })
        {
            const auto ranges = Terrain::computeHeightRanges(heights.data(), 33, grid);
            for (std::uint32_t y = 0; y < 33; ++y)
            {
                for (std::uint32_t x = 0; x < 33; ++x)
                {
                    const std::uint32_t gx = std::min<std::uint32_t>(x * grid / 32, grid - 1);
                    const std::uint32_t gy = std::min<std::uint32_t>(y * grid / 32, grid - 1);
                    const auto r = ranges[gx + std::size_t{ gy } * grid];
                    EXPECT_LE(r.first, heights[x + y * 33]) << grid << " " << x << "," << y;
                    EXPECT_GE(r.second, heights[x + y * 33]) << grid << " " << x << "," << y;
                }
            }
        }
    }

    TEST(TerrainHeightRanges, a_grid_that_divides_the_vertices_evenly_matches_the_old_code)
    {
        std::vector<float> heights(33 * 33);
        for (std::size_t i = 0; i < heights.size(); ++i)
            heights[i] = static_cast<float>((i * 7919) % 1000);
        // Level 1: 16 grid cells over 32 intervals: 3 vertices a cell, as numVerts gives.
        EXPECT_EQ(Terrain::computeHeightRanges(heights.data(), 33, 16), oldRanges(heights, 33, 16, 3));
        // Level 2: 32 grid cells over 32 intervals: 2 vertices a cell.
        EXPECT_EQ(Terrain::computeHeightRanges(heights.data(), 33, 32), oldRanges(heights, 33, 32, 2));
    }

    // How much the old box mattered: the cull sphere is the box's bounding sphere, so a box that is flat at one corner
    // height only moves the sphere's centre by half the height range and shrinks its radius by almost nothing, since
    // the horizontal half diagonal (46 km for a level 3 tile) dwarfs the mountain: a vertex sticks out of the old
    // sphere only near the tile's corners, by about height squared over twice the radius.
    TEST(TerrainHeightRanges, the_old_flat_box_missed_a_mountain_only_at_the_tile_corners_and_by_hundreds_of_units)
    {
        const double side = 65536.0; // a level 3 tile
        const double radius = std::sqrt(2.0) * side / 2.0; // flat box: half the horizontal diagonal
        const double peak = 5400.0; // Red Mountain's order of height, with the corner at 0
        // The worst vertex: a tile corner (on the sphere's rim) at the peak height.
        const double out = std::sqrt(radius * radius + peak * peak) - radius;
        EXPECT_GT(out, 100.0);
        EXPECT_LT(out, 400.0);
        // A vertex at the middle of the tile is far inside.
        EXPECT_LT(std::sqrt(peak * peak), radius);
    }

    TEST(TerrainHeightRanges, bad_input_gives_no_ranges)
    {
        EXPECT_TRUE(Terrain::computeHeightRanges(nullptr, 33, 16).empty());
        const std::vector<float> one(1, 0.f);
        EXPECT_TRUE(Terrain::computeHeightRanges(one.data(), 1, 16).empty());
        EXPECT_TRUE(Terrain::computeHeightRanges(one.data(), 33, 0).empty());
    }
}
