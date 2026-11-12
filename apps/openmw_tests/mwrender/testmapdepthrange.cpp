#include "apps/openmw/mwrender/mapdepthrange.hpp"

#include <cmath>
#include <limits>

#include <components/esm3/loadcell.hpp>

#include <gtest/gtest.h>

namespace
{
    using MWRender::mapDepthRange;

    vsg::dbox box(double zmin, double zmax)
    {
        return vsg::dbox(vsg::dvec3(0, 0, zmin), vsg::dvec3(100, 100, zmax));
    }

    // Task skyext: the exterior local map was black in a Skyrim exterior because it placed every map cell at the
    // Morrowind cell size (8192) while the neutral worldspace's cells are 4096 units.
    TEST(MapWorldSize, a_worldspace_uses_its_own_cell_size)
    {
        EXPECT_EQ(MWRender::mapWorldSizeFor(ESM::Cell::sDefaultWorldspaceId), 8192.f);
        EXPECT_EQ(MWRender::mapWorldSizeFor(ESM::RefId::stringRefId("skyrimse:w4")), 4096.f);
    }

    TEST(MapDepthRange, nothing_known_gives_no_range)
    {
        EXPECT_FALSE(mapDepthRange(vsg::dbox(), std::nullopt, std::nullopt).has_value());
    }

    TEST(MapDepthRange, objects_alone_give_their_heights)
    {
        const auto range = mapDepthRange(box(-20, 300), std::nullopt, std::nullopt);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, -20.f);
        EXPECT_FLOAT_EQ(range->mMax, 300.f);
    }

    TEST(MapDepthRange, terrain_above_the_objects_raises_the_top)
    {
        const auto range = mapDepthRange(box(0, 300), vsg::vec2(-50.f, 2500.f), std::nullopt);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, -50.f);
        EXPECT_FLOAT_EQ(range->mMax, 2500.f);
    }

    TEST(MapDepthRange, sea_floor_below_the_objects_lowers_the_bottom)
    {
        const auto range = mapDepthRange(box(10, 300), vsg::vec2(-1200.f, 40.f), 0.f);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, -1200.f);
        EXPECT_FLOAT_EQ(range->mMax, 300.f);
    }

    TEST(MapDepthRange, an_empty_object_grid_uses_terrain_and_water)
    {
        const auto range = mapDepthRange(vsg::dbox(), vsg::vec2(-900.f, -100.f), 0.f);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, -900.f);
        EXPECT_FLOAT_EQ(range->mMax, 0.f);
        EXPECT_TRUE(std::isfinite(range->mMin));
        EXPECT_TRUE(std::isfinite(range->mMax));
    }

    TEST(MapDepthRange, water_alone_gives_a_flat_range)
    {
        const auto range = mapDepthRange(vsg::dbox(), std::nullopt, 0.f);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, 0.f);
        EXPECT_FLOAT_EQ(range->mMax, 0.f);
    }

    TEST(MapDepthRange, objects_above_the_water_keep_the_water_in_range)
    {
        const auto range = mapDepthRange(box(50, 300), std::nullopt, 0.f);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, 0.f);
        EXPECT_FLOAT_EQ(range->mMax, 300.f);
    }

    TEST(MapDepthRange, non_finite_inputs_are_ignored)
    {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto range = mapDepthRange(box(0, 100), vsg::vec2(-inf, nan), inf);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, 0.f);
        EXPECT_FLOAT_EQ(range->mMax, 100.f);
        EXPECT_FALSE(mapDepthRange(vsg::dbox(), vsg::vec2(nan, nan), nan).has_value());
    }

    TEST(MapDepthRange, a_terrain_range_given_upside_down_is_ordered)
    {
        const auto range = mapDepthRange(vsg::dbox(), vsg::vec2(500.f, -500.f), std::nullopt);
        ASSERT_TRUE(range.has_value());
        EXPECT_FLOAT_EQ(range->mMin, -500.f);
        EXPECT_FLOAT_EQ(range->mMax, 500.f);
    }
}
