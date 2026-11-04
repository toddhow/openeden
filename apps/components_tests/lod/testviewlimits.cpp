// Task 0066 (stage b): the three limits (tile range, far plane, fog end) taken apart, the `view distance = auto`
// setting and the land extent it comes from. Pure code; no GPU, window or game data. Written from the spec before
// the implementation.
#include <gtest/gtest.h>

#include <components/lod/tileselection.hpp>
#include <components/lod/viewlimits.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    using Cell = std::pair<std::int32_t, std::int32_t>;

    constexpr double cellSize = 8192.0;

    ViewLimitsInput input(float camera, bool lodActive, bool mapView, ViewDistanceSetting setting,
        float maxViewDistance, double landDiagonal)
    {
        ViewLimitsInput in;
        in.mCameraViewingDistance = camera;
        in.mLodActive = lodActive;
        in.mMapView = mapView;
        in.mSetting = setting;
        in.mMaxViewDistance = maxViewDistance;
        in.mLandDiagonal = landDiagonal;
        return in;
    }

    ViewDistanceSetting number(float value)
    {
        ViewDistanceSetting setting;
        setting.mAuto = false;
        setting.mValue = value;
        return setting;
    }

    ViewDistanceSetting automatic()
    {
        ViewDistanceSetting setting;
        setting.mAuto = true;
        return setting;
    }

    void expectAll(const ViewLimits& limits, float expected)
    {
        EXPECT_FLOAT_EQ(limits.mTileRange, expected);
        EXPECT_FLOAT_EQ(limits.mPagingRange, expected);
        EXPECT_FLOAT_EQ(limits.mFarPlane, expected);
        EXPECT_FLOAT_EQ(limits.mFogRange, expected);
    }

    // ---- parsing -------------------------------------------------------------------------------------------

    TEST(LodViewLimits, auto_is_recognised_in_any_case_with_spaces)
    {
        for (const char* text : { "auto", "AUTO", "Auto", "  auto", "auto  ", "\tAuto\t" })
        {
            const auto parsed = parseViewDistanceSetting(text);
            ASSERT_TRUE(parsed.has_value()) << text;
            EXPECT_TRUE(parsed->mAuto) << text;
        }
    }

    TEST(LodViewLimits, a_positive_number_is_a_number)
    {
        const auto whole = parseViewDistanceSetting("40000");
        ASSERT_TRUE(whole.has_value());
        EXPECT_FALSE(whole->mAuto);
        EXPECT_FLOAT_EQ(whole->mValue, 40000.f);

        const auto fraction = parseViewDistanceSetting(" 7168.5 ");
        ASSERT_TRUE(fraction.has_value());
        EXPECT_FLOAT_EQ(fraction->mValue, 7168.5f);

        const auto exponent = parseViewDistanceSetting("1e5");
        ASSERT_TRUE(exponent.has_value());
        EXPECT_FLOAT_EQ(exponent->mValue, 100000.f);
    }

    TEST(LodViewLimits, anything_else_is_refused)
    {
        for (const char* text : { "", " ", "0", "-5", "-0.5", "autox", "au to", "12abc", "abc", "nan", "inf", "-inf",
                 "1e99", "0x10", "40000 40000" })
        {
            EXPECT_FALSE(parseViewDistanceSetting(text).has_value()) << "'" << text << "'";
        }
    }

    // ---- the land extent ---------------------------------------------------------------------------------

    TEST(LodViewLimits, the_extent_of_no_cells_is_empty)
    {
        const std::vector<Cell> none;
        const LandExtent extent = landExtent(none);
        EXPECT_TRUE(extent.empty());
        EXPECT_EQ(landDiagonal(extent, cellSize), 0.0);
    }

    TEST(LodViewLimits, one_cell_is_one_cell_wide)
    {
        const std::vector<Cell> cells = { { 3, -4 } };
        const LandExtent extent = landExtent(cells);
        EXPECT_FALSE(extent.empty());
        EXPECT_EQ(extent.mMinX, 3);
        EXPECT_EQ(extent.mMaxX, 3);
        EXPECT_EQ(extent.mMinY, -4);
        EXPECT_EQ(extent.mMaxY, -4);
        EXPECT_NEAR(landDiagonal(extent, cellSize), cellSize * std::numbers::sqrt2, 1e-6);
    }

    TEST(LodViewLimits, the_extent_covers_negative_and_positive_cells_in_any_order)
    {
        // Morrowind's land runs from x -28 to 24 and y -20 to 30 (task 0066, research note 0029).
        const std::vector<Cell> cells = { { 24, 3 }, { -28, 30 }, { 0, -20 }, { 5, 5 } };
        const LandExtent extent = landExtent(cells);
        EXPECT_EQ(extent.mMinX, -28);
        EXPECT_EQ(extent.mMaxX, 24);
        EXPECT_EQ(extent.mMinY, -20);
        EXPECT_EQ(extent.mMaxY, 30);
        const double width = 53.0 * cellSize; // -28 to 24 inclusive
        const double height = 51.0 * cellSize; // -20 to 30 inclusive
        EXPECT_NEAR(landDiagonal(extent, cellSize), std::hypot(width, height), 1e-3);
    }

    TEST(LodViewLimits, the_extent_does_not_depend_on_the_order_of_the_cells)
    {
        std::vector<Cell> cells = { { 1, 2 }, { -7, 9 }, { 12, -3 }, { 0, 0 } };
        const LandExtent a = landExtent(cells);
        std::vector<Cell> reversed(cells.rbegin(), cells.rend());
        const LandExtent b = landExtent(reversed);
        EXPECT_EQ(a.mMinX, b.mMinX);
        EXPECT_EQ(a.mMaxX, b.mMaxX);
        EXPECT_EQ(a.mMinY, b.mMinY);
        EXPECT_EQ(a.mMaxY, b.mMaxY);
    }

    // ---- the limits --------------------------------------------------------------------------------------

    TEST(LodViewLimits, with_the_world_lod_off_all_four_limits_are_the_camera_viewing_distance)
    {
        // Whatever the other settings say: no new behaviour unless the world LOD is active.
        for (const bool mapView : { false, true })
        {
            expectAll(computeViewLimits(input(7168.f, false, mapView, number(100000.f), 600000.f, 590000.0)), 7168.f);
            expectAll(computeViewLimits(input(7168.f, false, mapView, automatic(), 600000.f, 590000.0)), 7168.f);
        }
    }

    TEST(LodViewLimits, with_map_view_off_the_limits_are_one_number_as_before)
    {
        // The rule of commit ed53100fd3: the larger of [Camera] viewing distance and [WorldLod] view distance.
        expectAll(computeViewLimits(input(7168.f, true, false, number(40000.f), 600000.f, 590000.0)), 40000.f);
        expectAll(computeViewLimits(input(7168.f, true, false, number(5000.f), 600000.f, 590000.0)), 7168.f);
        expectAll(computeViewLimits(input(50000.f, true, false, number(40000.f), 600000.f, 590000.0)), 50000.f);
    }

    TEST(LodViewLimits, auto_without_map_view_is_the_old_default_number)
    {
        expectAll(computeViewLimits(input(7168.f, true, false, automatic(), 600000.f, 590000.0)), legacyViewDistance);
        expectAll(computeViewLimits(input(7168.f, true, false, automatic(), 600000.f, 0.0)), legacyViewDistance);
    }

    TEST(LodViewLimits, map_view_with_auto_reaches_the_land_diagonal_but_paging_stays_put)
    {
        const ViewLimits limits = computeViewLimits(input(7168.f, true, true, automatic(), 700000.f, 590000.0));
        EXPECT_FLOAT_EQ(limits.mTileRange, 590000.f);
        EXPECT_FLOAT_EQ(limits.mFarPlane, 590000.f);
        EXPECT_FLOAT_EQ(limits.mFogRange, 590000.f); // until task 0066 stage c gives the fog its own model
        EXPECT_FLOAT_EQ(limits.mPagingRange, legacyViewDistance); // the live chunks do not follow the map
    }

    TEST(LodViewLimits, auto_is_capped_by_the_maximum)
    {
        const ViewLimits limits = computeViewLimits(input(7168.f, true, true, automatic(), 300000.f, 590000.0));
        EXPECT_FLOAT_EQ(limits.mTileRange, 300000.f);
        EXPECT_FLOAT_EQ(limits.mFarPlane, 300000.f);
    }

    TEST(LodViewLimits, the_cap_never_lowers_the_reach_below_the_old_one)
    {
        const ViewLimits limits = computeViewLimits(input(7168.f, true, true, automatic(), 1000.f, 590000.0));
        EXPECT_FLOAT_EQ(limits.mTileRange, legacyViewDistance);
        EXPECT_GE(limits.mTileRange, limits.mPagingRange);
    }

    TEST(LodViewLimits, auto_with_an_unknown_world_falls_back_to_the_old_default)
    {
        // The store is not loaded yet (or has no land): the diagonal is 0.
        const ViewLimits limits = computeViewLimits(input(7168.f, true, true, automatic(), 600000.f, 0.0));
        expectAll(limits, legacyViewDistance);
    }

    TEST(LodViewLimits, a_number_overrides_auto_and_is_not_capped)
    {
        const ViewLimits limits = computeViewLimits(input(7168.f, true, true, number(900000.f), 600000.f, 590000.0));
        EXPECT_FLOAT_EQ(limits.mTileRange, 900000.f);
        EXPECT_FLOAT_EQ(limits.mFarPlane, 900000.f);
    }

    TEST(LodViewLimits, the_tile_range_is_never_below_the_paging_range_or_the_camera)
    {
        for (const float camera : { 1000.f, 7168.f, 60000.f })
        {
            for (const float max : { 1000.f, 50000.f, 600000.f })
            {
                for (const double diagonal : { 0.0, 20000.0, 590000.0 })
                {
                    const ViewLimits limits = computeViewLimits(input(camera, true, true, automatic(), max, diagonal));
                    EXPECT_GE(limits.mTileRange, limits.mPagingRange);
                    EXPECT_GE(limits.mTileRange, camera);
                    EXPECT_GE(limits.mFarPlane, limits.mTileRange);
                    EXPECT_GE(limits.mFogRange, 0.f);
                }
            }
        }
    }

    TEST(LodViewLimits, the_limits_are_a_pure_function_of_the_input)
    {
        const ViewLimitsInput in = input(7168.f, true, true, automatic(), 600000.f, 590000.0);
        const ViewLimits a = computeViewLimits(in);
        const ViewLimits b = computeViewLimits(in);
        EXPECT_EQ(a.mTileRange, b.mTileRange);
        EXPECT_EQ(a.mPagingRange, b.mPagingRange);
        EXPECT_EQ(a.mFarPlane, b.mFarPlane);
        EXPECT_EQ(a.mFogRange, b.mFogRange);
    }

    // ---- the tile selection out to 600,000 units -------------------------------------------------------------

    // A camera looking along +X with a 60 degree vertical field of view and a 720 pixel viewport (the stage a pose).
    SelectionCamera farCamera(double viewDistance)
    {
        SelectionCamera camera;
        camera.mX = 0;
        camera.mY = 0;
        camera.mZ = 25000;
        camera.mForwardX = 1;
        camera.mForwardY = 0;
        camera.mForwardZ = 0;
        camera.mViewportHeight = 720;
        camera.mFovY = std::numbers::pi / 3.0;
        camera.mAspect = 16.0 / 9.0;
        camera.mViewDistance = viewDistance;
        return camera;
    }

    // One tile per level 3 to 6 at the same place, straight ahead at `distance`; the errors grow 2.9 times a level
    // (task 0066 design, the calibrated starting values of the baker: about 960, 2,900, 8,700 for the clusters; the
    // terrain levels grow the same way from level 3's measured error).
    TileBounds levelTile(int level, double distance)
    {
        TileBounds bounds;
        const float side = 8192.f * static_cast<float>(1 << level);
        bounds.mSide = side;
        bounds.mOriginX = static_cast<float>(distance);
        bounds.mOriginY = -side / 2;
        bounds.mMinHeight = 0;
        bounds.mMaxHeight = 3000;
        float error = 600.f; // level 3
        for (int l = 3; l < level; ++l)
        {
            error *= 2.9f;
        }
        bounds.mGeometricError = error;
        return bounds;
    }

    // The highest level whose tile is accepted at `distance` with the threshold, or 0 when none is.
    int coarsestAccepted(double distance, float threshold)
    {
        const SelectionCamera camera = farCamera(700000);
        int best = 0;
        for (int level = 3; level <= 6; ++level)
        {
            const TileBounds bounds = levelTile(level, distance);
            if (pixelError(camera, bounds) <= threshold)
            {
                best = level;
            }
        }
        return best;
    }

    TEST(LodViewLimits, the_accepted_level_never_gets_finer_with_distance_out_to_600000)
    {
        int previous = 0;
        for (double distance = 20000; distance <= 600000; distance += 10000)
        {
            const int level = coarsestAccepted(distance, 8.f);
            EXPECT_GE(level, previous) << "at " << distance;
            previous = level;
        }
        EXPECT_GE(previous, 4); // the far corner is drawn with a level coarser than 3
    }

    TEST(LodViewLimits, a_tile_is_skipped_beyond_the_tile_range_and_chosen_inside_it)
    {
        // The tile range is the only distance cut of the selection: there is no far plane in the camera at all.
        TileState state;
        state.mBounds = levelTile(5, 500000);
        std::vector<TileState> states = { state };

        const SelectionResult near = selectTiles(states, farCamera(40000), 8.f);
        EXPECT_EQ(near.mSkipped, 1u);
        EXPECT_EQ(near.mTiles + near.mLive, 0u);

        states[0] = state;
        const SelectionResult far = selectTiles(states, farCamera(600000), 8.f);
        EXPECT_EQ(far.mSkipped, 0u);
        EXPECT_EQ(far.mTiles + far.mLive, 1u);
        EXPECT_TRUE(states[0].mVisible);
    }

    TEST(LodViewLimits, a_far_tile_keeps_its_hysteresis_at_600000)
    {
        // Draw it while it is accepted; once the camera moves so that the error is slightly above the threshold the
        // tile goes live and does not return until the error is under 0.7 of the threshold.
        TileState state;
        state.mBounds = levelTile(4, 150000);
        std::vector<TileState> states = { state };
        const float threshold = 8.f;

        // Start close enough that the error is well below the threshold: Tile.
        SelectionCamera camera = farCamera(600000);
        states[0].mBounds = levelTile(4, 400000);
        selectTiles(states, camera, threshold);
        ASSERT_EQ(states[0].mChoice, TileChoice::Tile);

        // Bring the tile close: the error rises above the threshold: Live.
        states[0].mBounds = levelTile(4, 60000);
        selectTiles(states, camera, threshold);
        ASSERT_EQ(states[0].mChoice, TileChoice::Live);

        // Between 0.7 and 1.0 of the threshold: still Live.
        double distance = 60000;
        while (distance < 600000)
        {
            states[0].mBounds = levelTile(4, distance);
            const double pixels = pixelError(camera, states[0].mBounds);
            if (pixels < threshold && pixels > hysteresisShare * threshold)
            {
                break;
            }
            distance += 500;
        }
        selectTiles(states, camera, threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Live);

        // Far enough that the error is under 0.7 of the threshold: Tile again.
        states[0].mBounds = levelTile(4, 560000);
        selectTiles(states, camera, threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
    }

    TEST(LodViewLimits, the_selection_does_not_depend_on_anything_but_the_camera_and_the_tiles)
    {
        std::vector<TileState> a(4), b(4);
        for (int i = 0; i < 4; ++i)
        {
            a[i].mBounds = levelTile(3 + i, 100000.0 * (i + 1));
            b[i].mBounds = a[i].mBounds;
        }
        const SelectionResult first = selectTiles(a, farCamera(600000), 8.f);
        const SelectionResult second = selectTiles(b, farCamera(600000), 8.f);
        EXPECT_EQ(first.mTiles, second.mTiles);
        EXPECT_EQ(first.mLive, second.mLive);
        EXPECT_EQ(first.mSkipped, second.mSkipped);
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(a[i].mChoice, b[i].mChoice);
        }
    }
}
