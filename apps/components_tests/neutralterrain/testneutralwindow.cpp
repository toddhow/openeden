// Task 0125, stage E2: the pure helpers of the neutral terrain in the engine. Written from the spec before the code;
// the first run was against stubs.
#include <components/neutralterrain/window.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
    using NeutralTerrain::CellWindow;
    using NeutralTerrain::terrainEnsureWindow;
    using NeutralTerrain::terrainHeightRange;
    using openeden::sdk::game::TerrainCell;

    TEST(NeutralTerrainWindow, a_default_view_gives_a_small_window_around_the_camera_cell)
    {
        // View 7168 = 1.75 cells: radius ceil(1.75) + batch 1 + 2 = 5, plus one cell east and north.
        const CellWindow w = terrainEnsureWindow(4096.0 * 3 + 100.0, -4096.0 * 2 + 5.0, 7168.f, 4096.f);
        EXPECT_EQ(w.mX0, 3 - 5);
        EXPECT_EQ(w.mY0, -2 - 5);
        EXPECT_EQ(w.mX1, 3 + 5 + 1);
        EXPECT_EQ(w.mY1, -2 + 5 + 1);
        EXPECT_EQ(w.count(), 12 * 12); // -5 .. +5 is 11 cells, plus one east and north
    }

    TEST(NeutralTerrainWindow, a_negative_position_floors_to_the_cell_below)
    {
        const CellWindow w = terrainEnsureWindow(-1.0, -4097.0, 4096.f, 4096.f);
        // Cell (-1, -2); view 1 cell: radius 1 + 1 + 2 = 4.
        EXPECT_EQ(w.mX0, -1 - 4);
        EXPECT_EQ(w.mY0, -2 - 4);
        EXPECT_EQ(w.mX1, -1 + 4 + 1);
        EXPECT_EQ(w.mY1, -2 + 4 + 1);
    }

    TEST(NeutralTerrainWindow, a_long_view_widens_the_margin_by_the_batch_size)
    {
        // 40,960 units = 10 cells: the batch is nextPowerOfTwo(ceil(10 * 2 / 4)) = 8, so radius 10 + 8 + 2 = 20.
        const CellWindow w = terrainEnsureWindow(0.0, 0.0, 40960.f, 4096.f);
        EXPECT_EQ(w.mX0, -20);
        EXPECT_EQ(w.mX1, 21);
        EXPECT_EQ(w.count(), 42 * 42);
    }

    TEST(NeutralTerrainWindow, the_batch_is_capped_at_the_pagings_largest)
    {
        // 1,000,000 units: the batch would be 128, Paging caps it at 16.
        const CellWindow w = terrainEnsureWindow(0.0, 0.0, 1.0e6f, 4096.f);
        const int viewCells = static_cast<int>(std::ceil(1.0e6 / 4096.0));
        EXPECT_EQ(w.mX1, viewCells + 16 + 2 + 1);
    }

    TEST(NeutralTerrainWindow, the_window_always_contains_the_camera_cell_and_grows_with_the_view)
    {
        for (float view : { 1.f, 4096.f, 7168.f, 20000.f, 60000.f })
        {
            const CellWindow w = terrainEnsureWindow(5000.0, 9000.0, view, 4096.f);
            EXPECT_LE(w.mX0, 1);
            EXPECT_GE(w.mX1, 1);
            EXPECT_LE(w.mY0, 2);
            EXPECT_GE(w.mY1, 2);
        }
        EXPECT_GT(
            terrainEnsureWindow(0, 0, 40000.f, 4096.f).count(), terrainEnsureWindow(0, 0, 8000.f, 4096.f).count());
    }

    TEST(NeutralTerrainWindow, bad_numbers_give_an_empty_window)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        EXPECT_TRUE(terrainEnsureWindow(nan, 0.0, 7168.f, 4096.f).empty());
        EXPECT_TRUE(terrainEnsureWindow(0.0, 0.0, 7168.f, 0.f).empty());
        EXPECT_TRUE(terrainEnsureWindow(0.0, 0.0, -1.f, 4096.f).empty());
        EXPECT_TRUE(terrainEnsureWindow(1.0e30, 0.0, 7168.f, 4096.f).empty());
        EXPECT_EQ(terrainEnsureWindow(nan, 0.0, 1.f, 1.f).count(), 0);
    }

    TEST(NeutralTerrainHeightRange, finds_the_lowest_and_highest_height)
    {
        TerrainCell cell;
        cell.vertsPerSide = 3;
        cell.hasHeights = true;
        cell.heights = { 5, 6, 7, -3, 0, 9, 100, 2, 1 };
        const auto range = terrainHeightRange(cell, 42.f);
        EXPECT_FLOAT_EQ(range.first, -3.f);
        EXPECT_FLOAT_EQ(range.second, 100.f);
    }

    TEST(NeutralTerrainHeightRange, a_cell_without_heights_is_the_default_height)
    {
        TerrainCell cell;
        cell.vertsPerSide = 33;
        cell.hasHeights = false;
        const auto range = terrainHeightRange(cell, -27000.f);
        EXPECT_FLOAT_EQ(range.first, -27000.f);
        EXPECT_FLOAT_EQ(range.second, -27000.f);
    }

    TEST(NeutralTerrainHeightRange, a_flat_cell_has_equal_bounds_and_a_short_array_is_the_default)
    {
        TerrainCell flat;
        flat.vertsPerSide = 2;
        flat.hasHeights = true;
        flat.heights = { 8, 8, 8, 8 };
        EXPECT_EQ(terrainHeightRange(flat, 0.f), std::make_pair(8.f, 8.f));
        TerrainCell broken;
        broken.vertsPerSide = 33;
        broken.hasHeights = true;
        broken.heights = { 1, 2, 3 };
        EXPECT_EQ(terrainHeightRange(broken, 7.f), std::make_pair(7.f, 7.f));
    }
}
