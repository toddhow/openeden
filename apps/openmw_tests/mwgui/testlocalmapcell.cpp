// Task 0201: the cell and the place in it of a world position on the exterior local map, for any cell size.
#include "apps/openmw/mwgui/localmapcell.hpp"

#include <gtest/gtest.h>

namespace
{
    using MWGui::localMapCellOf;

    TEST(LocalMapCell, a_morrowind_cell_is_8192_units_wide)
    {
        const auto c = localMapCellOf(8192.f + 2048.f, 4096.f, 8192.f);
        EXPECT_EQ(c.mX, 1);
        EXPECT_EQ(c.mY, 0);
        EXPECT_FLOAT_EQ(c.mNX, 0.25f);
        EXPECT_FLOAT_EQ(c.mNY, 0.5f);
    }

    TEST(LocalMapCell, a_skyrim_cell_is_4096_units_wide)
    {
        const auto c = localMapCellOf(25670.f, -7633.f, 4096.f);
        EXPECT_EQ(c.mX, 6);
        EXPECT_EQ(c.mY, -2);
        EXPECT_NEAR(c.mNX, (25670.f - 6 * 4096.f) / 4096.f, 1e-6f);
        EXPECT_NEAR(c.mNY, (-7633.f + 2 * 4096.f) / 4096.f, 1e-6f);
    }

    TEST(LocalMapCell, a_negative_position_rounds_down_and_the_place_stays_in_zero_to_one)
    {
        const auto c = localMapCellOf(-1.f, -4096.f, 4096.f);
        EXPECT_EQ(c.mX, -1);
        EXPECT_EQ(c.mY, -1);
        EXPECT_GE(c.mNX, 0.f);
        EXPECT_LT(c.mNX, 1.f);
        EXPECT_FLOAT_EQ(c.mNY, 0.f);
    }

    TEST(LocalMapCell, the_edge_belongs_to_the_cell_it_starts)
    {
        const auto c = localMapCellOf(4096.f, 0.f, 4096.f);
        EXPECT_EQ(c.mX, 1);
        EXPECT_FLOAT_EQ(c.mNX, 0.f);
    }

    TEST(LocalMapCell, a_bad_cell_size_falls_back_to_the_morrowind_size)
    {
        EXPECT_EQ(localMapCellOf(9000.f, 0.f, 0.f).mX, 1);
        EXPECT_EQ(localMapCellOf(9000.f, 0.f, -5.f).mX, 1);
    }
}
