// Task 0055 (stage S1a): the selection of tiles by pixel error. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tileselection.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr double halfPi = std::numbers::pi / 2.0;

    // Camera looking along +X with a 90 degree vertical field of view (tan(fov / 2) = 1) and a 1000 pixel viewport,
    // so that pixels = 2 * error / ... = error * 1000 / (2 * d) = 500 * error / d.
    SelectionCamera cameraAt(double x, double y, double z)
    {
        SelectionCamera camera;
        camera.mX = x;
        camera.mY = y;
        camera.mZ = z;
        camera.mForwardX = 1;
        camera.mForwardY = 0;
        camera.mForwardZ = 0;
        camera.mViewportHeight = 1000;
        camera.mFovY = halfPi;
        camera.mAspect = 1.6;
        camera.mViewDistance = 30000;
        return camera;
    }

    // A tile 100 units wide at the origin, from height 0 to 50.
    TileBounds unitTile(float error)
    {
        TileBounds bounds;
        bounds.mOriginX = 0;
        bounds.mOriginY = 0;
        bounds.mSide = 100;
        bounds.mMinHeight = 0;
        bounds.mMaxHeight = 50;
        bounds.mGeometricError = error;
        return bounds;
    }

    TileState stateOf(const TileBounds& bounds)
    {
        TileState state;
        state.mBounds = bounds;
        return state;
    }

    // The camera is `distance` units in front of the tile's west face, in line with the tile's middle.
    SelectionCamera cameraAtDistance(double distance)
    {
        return cameraAt(-distance, 50, 25);
    }

    SelectionResult step(TileState& state, const SelectionCamera& camera, float threshold)
    {
        return selectTiles(std::span<TileState>(&state, 1), camera, threshold);
    }

    // ---- the pixel error formula --------------------------------------------------------------------------

    TEST(LodTileSelection, pixel_error_matches_the_hand_computed_value)
    {
        // error 4, viewport 1000, tan(fov / 2) = 1, d = 100: 4 * 1000 / (2 * 100) = 20.
        EXPECT_NEAR(pixelError(cameraAtDistance(100), unitTile(4)), 20.0, 1e-9);
        // Twice as far, half the pixels.
        EXPECT_NEAR(pixelError(cameraAtDistance(200), unitTile(4)), 10.0, 1e-9);
        // Twice the error, twice the pixels.
        EXPECT_NEAR(pixelError(cameraAtDistance(100), unitTile(8)), 40.0, 1e-9);
        EXPECT_NEAR(pixelError(cameraAtDistance(100), unitTile(0)), 0.0, 1e-12);
    }

    TEST(LodTileSelection, pixel_error_scales_with_the_viewport_and_the_field_of_view)
    {
        SelectionCamera camera = cameraAtDistance(100);
        camera.mViewportHeight = 2000;
        EXPECT_NEAR(pixelError(camera, unitTile(4)), 40.0, 1e-9);

        camera = cameraAtDistance(100);
        camera.mFovY = std::numbers::pi / 3.0; // tan(pi / 6)
        EXPECT_NEAR(
            pixelError(camera, unitTile(4)), 4.0 * 1000.0 / (2.0 * 100.0 * std::tan(std::numbers::pi / 6.0)), 1e-9);
        // A wider field of view puts fewer pixels on the same error.
        SelectionCamera wide = cameraAtDistance(100);
        wide.mFovY = 2.0;
        EXPECT_LT(pixelError(wide, unitTile(4)), 20.0);
    }

    TEST(LodTileSelection, distance_is_clamped_to_one)
    {
        // Inside the box: d = 0, treated as 1: 4 * 1000 / 2 = 2000.
        EXPECT_NEAR(pixelError(cameraAt(50, 50, 25), unitTile(4)), 2000.0, 1e-6);
        // Half a unit away: still clamped.
        EXPECT_NEAR(pixelError(cameraAt(-0.5, 50, 25), unitTile(4)), 2000.0, 1e-6);
        // Exactly one unit away.
        EXPECT_NEAR(pixelError(cameraAt(-1, 50, 25), unitTile(4)), 2000.0, 1e-6);
        // Two units away.
        EXPECT_NEAR(pixelError(cameraAt(-2, 50, 25), unitTile(4)), 1000.0, 1e-6);
    }

    // ---- the distance to the box --------------------------------------------------------------------------

    TEST(LodTileSelection, distance_inside_the_box_is_zero)
    {
        EXPECT_DOUBLE_EQ(distanceToTile(cameraAt(50, 50, 25), unitTile(4)), 0.0);
        EXPECT_DOUBLE_EQ(distanceToTile(cameraAt(0, 0, 0), unitTile(4)), 0.0); // on a corner
        EXPECT_DOUBLE_EQ(distanceToTile(cameraAt(100, 100, 50), unitTile(4)), 0.0);
    }

    TEST(LodTileSelection, distance_to_the_nearest_point_of_the_box)
    {
        const TileBounds bounds = unitTile(4);
        // Straight in front of a face.
        EXPECT_NEAR(distanceToTile(cameraAt(-100, 50, 25), bounds), 100.0, 1e-9);
        EXPECT_NEAR(distanceToTile(cameraAt(200, 50, 25), bounds), 100.0, 1e-9);
        EXPECT_NEAR(distanceToTile(cameraAt(50, -70, 25), bounds), 70.0, 1e-9);
        EXPECT_NEAR(distanceToTile(cameraAt(50, 130, 25), bounds), 30.0, 1e-9);
        // Above the tile: the nearest point is the top at height 50.
        EXPECT_NEAR(distanceToTile(cameraAt(50, 50, 150), bounds), 100.0, 1e-9);
        // Below the tile: the nearest point is the bottom at height 0.
        EXPECT_NEAR(distanceToTile(cameraAt(50, 50, -30), bounds), 30.0, 1e-9);
        // Beside a corner in the plane: a 3-4-5 triangle.
        EXPECT_NEAR(distanceToTile(cameraAt(-30, -40, 25), bounds), 50.0, 1e-9);
        // Beside and above: dx 60, dz 80.
        EXPECT_NEAR(distanceToTile(cameraAt(160, 50, 130), bounds), 100.0, 1e-9);
        // Beside an edge, above a corner: dx 30, dy 40, dz 120 -> sqrt(900 + 1600 + 14400) = sqrt(16900) = 130.
        EXPECT_NEAR(distanceToTile(cameraAt(130, 140, 170), bounds), 130.0, 1e-9);
    }

    TEST(LodTileSelection, distance_uses_the_tiles_height_range)
    {
        TileBounds bounds = unitTile(4);
        bounds.mMinHeight = 200;
        bounds.mMaxHeight = 300;
        EXPECT_NEAR(distanceToTile(cameraAt(50, 50, 0), bounds), 200.0, 1e-9);
        EXPECT_NEAR(distanceToTile(cameraAt(50, 50, 250), bounds), 0.0, 1e-9);
        EXPECT_NEAR(distanceToTile(cameraAt(50, 50, 400), bounds), 100.0, 1e-9);
    }

    // ---- the choice ---------------------------------------------------------------------------------------

    TEST(LodTileSelection, first_choice_is_tile_when_the_error_is_small_enough_and_live_otherwise)
    {
        // pixels = 2000 / d for error 4.
        {
            TileState state = stateOf(unitTile(4));
            EXPECT_EQ(state.mChoice, TileChoice::Unset);
            const SelectionResult result = step(state, cameraAtDistance(1000), 4.f); // 2 pixels
            EXPECT_EQ(state.mChoice, TileChoice::Tile);
            EXPECT_TRUE(state.mVisible);
            EXPECT_NEAR(state.mPixels, 2.0, 1e-4);
            EXPECT_EQ(result.mTiles, 1u);
            EXPECT_EQ(result.mLive, 0u);
            EXPECT_EQ(result.mSkipped, 0u);
        }
        {
            TileState state = stateOf(unitTile(4));
            const SelectionResult result = step(state, cameraAtDistance(250), 4.f); // 8 pixels
            EXPECT_EQ(state.mChoice, TileChoice::Live);
            EXPECT_TRUE(state.mVisible);
            EXPECT_NEAR(state.mPixels, 8.0, 1e-4);
            EXPECT_EQ(result.mTiles, 0u);
            EXPECT_EQ(result.mLive, 1u);
        }
    }

    TEST(LodTileSelection, the_boundary_is_inclusive_for_the_tile)
    {
        // Equal to the threshold is exact only with zero error: 0 <= 0 chooses the tile.
        {
            TileState state = stateOf(unitTile(0));
            step(state, cameraAtDistance(100), 0.f);
            EXPECT_EQ(state.mChoice, TileChoice::Tile);
        }
        // Any error above a zero threshold is live.
        {
            TileState state = stateOf(unitTile(1));
            step(state, cameraAtDistance(100), 0.f);
            EXPECT_EQ(state.mChoice, TileChoice::Live);
        }
        // Just either side of the boundary at 4 pixels (d = 500 for error 4).
        {
            TileState state = stateOf(unitTile(4));
            step(state, cameraAtDistance(500.0 * 1.0001), 4.f); // just under 4 pixels
            EXPECT_EQ(state.mChoice, TileChoice::Tile);
        }
        {
            TileState state = stateOf(unitTile(4));
            step(state, cameraAtDistance(500.0 * 0.9999), 4.f); // just over 4 pixels
            EXPECT_EQ(state.mChoice, TileChoice::Live);
        }
    }

    TEST(LodTileSelection, pixels_reported_equal_the_pixel_error_function)
    {
        const SelectionCamera camera = cameraAtDistance(321);
        TileState state = stateOf(unitTile(5));
        step(state, camera, 4.f);
        EXPECT_NEAR(state.mPixels, pixelError(camera, state.mBounds), 1e-3);
    }

    TEST(LodTileSelection, the_standard_thresholds_are_the_documented_starting_values)
    {
        EXPECT_FLOAT_EQ(thresholdHigh, 2.f);
        EXPECT_FLOAT_EQ(thresholdMedium, 4.f);
        EXPECT_FLOAT_EQ(thresholdLow, 8.f);
        EXPECT_FLOAT_EQ(hysteresisShare, 0.7f);
    }

    // ---- hysteresis ---------------------------------------------------------------------------------------

    TEST(LodTileSelection, a_camera_moving_back_and_forth_across_the_boundary_changes_the_choice_once)
    {
        TileState state = stateOf(unitTile(4));
        // d = 520 is 3.85 pixels and d = 480 is 4.17 pixels: the threshold 4 lies between them.
        step(state, cameraAtDistance(520), 4.f);
        ASSERT_EQ(state.mChoice, TileChoice::Tile);

        std::size_t changes = 0;
        for (unsigned frame = 0; frame < 40; ++frame)
        {
            const SelectionResult result = step(state, cameraAtDistance(frame % 2 == 0 ? 480 : 520), 4.f);
            changes += result.mChanged;
        }
        EXPECT_EQ(changes, 1u);
        EXPECT_EQ(state.mChoice, TileChoice::Live);
    }

    TEST(LodTileSelection, a_live_tile_returns_only_below_seventy_percent_of_the_threshold)
    {
        TileState state = stateOf(unitTile(4));
        step(state, cameraAtDistance(600), 4.f); // 3.33 pixels
        ASSERT_EQ(state.mChoice, TileChoice::Tile);

        SelectionResult result = step(state, cameraAtDistance(480), 4.f); // 4.17: over the threshold
        EXPECT_EQ(state.mChoice, TileChoice::Live);
        EXPECT_EQ(result.mChanged, 1u);

        // Between 0.7 * 4 = 2.8 and 4: stays live.
        for (const double distance : { 520.0, 600.0, 700.0 }) // 3.85, 3.33, 2.86
        {
            result = step(state, cameraAtDistance(distance), 4.f);
            EXPECT_EQ(state.mChoice, TileChoice::Live) << distance;
            EXPECT_EQ(result.mChanged, 0u) << distance;
        }
        // Below 2.8: back to the tile.
        result = step(state, cameraAtDistance(720), 4.f); // 2.78
        EXPECT_EQ(state.mChoice, TileChoice::Tile);
        EXPECT_EQ(result.mChanged, 1u);
        // Just over the threshold again: live again.
        result = step(state, cameraAtDistance(480), 4.f);
        EXPECT_EQ(state.mChoice, TileChoice::Live);
        EXPECT_EQ(result.mChanged, 1u);
    }

    TEST(LodTileSelection, a_tile_stays_a_tile_while_it_is_at_or_under_the_threshold)
    {
        TileState state = stateOf(unitTile(4));
        step(state, cameraAtDistance(1000), 4.f);
        for (const double distance : { 900.0, 700.0, 520.0, 1200.0, 3000.0 })
        {
            const SelectionResult result = step(state, cameraAtDistance(distance), 4.f);
            EXPECT_EQ(state.mChoice, TileChoice::Tile) << distance;
            EXPECT_EQ(result.mChanged, 0u) << distance;
        }
    }

    // ---- what is not visible ------------------------------------------------------------------------------

    TEST(LodTileSelection, tiles_beyond_the_view_distance_are_skipped_and_counted)
    {
        SelectionCamera camera = cameraAtDistance(5000);
        camera.mViewDistance = 1000;
        TileState state = stateOf(unitTile(4));
        const SelectionResult result = step(state, camera, 4.f);
        EXPECT_FALSE(state.mVisible);
        EXPECT_EQ(state.mChoice, TileChoice::Unset);
        EXPECT_FLOAT_EQ(state.mPixels, 0.f);
        EXPECT_EQ(result.mSkipped, 1u);
        EXPECT_EQ(result.mTiles, 0u);
        EXPECT_EQ(result.mLive, 0u);
        EXPECT_EQ(result.mChanged, 0u);
    }

    TEST(LodTileSelection, a_tile_just_inside_the_view_distance_is_visible)
    {
        SelectionCamera camera = cameraAtDistance(900);
        camera.mViewDistance = 1000;
        TileState state = stateOf(unitTile(4));
        const SelectionResult result = step(state, camera, 4.f);
        EXPECT_TRUE(state.mVisible);
        EXPECT_EQ(result.mSkipped, 0u);
    }

    TEST(LodTileSelection, tiles_behind_the_camera_are_skipped)
    {
        // Camera at the origin looking along +X; the tile is 3000 units behind it.
        TileBounds bounds = unitTile(4);
        bounds.mOriginX = -3100;
        bounds.mOriginY = -50;
        TileState state = stateOf(bounds);
        const SelectionResult result = step(state, cameraAt(0, 0, 25), 4.f);
        EXPECT_FALSE(state.mVisible);
        EXPECT_EQ(state.mChoice, TileChoice::Unset);
        EXPECT_EQ(result.mSkipped, 1u);
    }

    TEST(LodTileSelection, a_tile_straight_ahead_and_a_tile_around_the_camera_are_visible)
    {
        TileBounds ahead = unitTile(4);
        ahead.mOriginX = 2000;
        ahead.mOriginY = -50;
        TileState aheadState = stateOf(ahead);
        step(aheadState, cameraAt(0, 0, 25), 4.f);
        EXPECT_TRUE(aheadState.mVisible);

        TileState inside = stateOf(unitTile(4));
        step(inside, cameraAt(50, 50, 25), 4.f);
        EXPECT_TRUE(inside.mVisible);
        EXPECT_EQ(inside.mChoice, TileChoice::Live); // 2000 pixels
    }

    TEST(LodTileSelection, a_tile_that_is_not_visible_keeps_its_choice_so_hysteresis_survives_looking_away)
    {
        TileState state = stateOf(unitTile(4));
        step(state, cameraAtDistance(600), 4.f);
        step(state, cameraAtDistance(480), 4.f);
        ASSERT_EQ(state.mChoice, TileChoice::Live);

        // Look the other way.
        SelectionCamera away = cameraAtDistance(480);
        away.mForwardX = -1;
        const SelectionResult skipped = step(state, away, 4.f);
        EXPECT_FALSE(state.mVisible);
        EXPECT_EQ(state.mChoice, TileChoice::Live);
        EXPECT_FLOAT_EQ(state.mPixels, 0.f);
        EXPECT_EQ(skipped.mSkipped, 1u);
        EXPECT_EQ(skipped.mChanged, 0u);

        // Look back at 3.33 pixels: between 2.8 and 4, so the live choice is still held.
        const SelectionResult back = step(state, cameraAtDistance(600), 4.f);
        EXPECT_TRUE(state.mVisible);
        EXPECT_EQ(state.mChoice, TileChoice::Live);
        EXPECT_EQ(back.mChanged, 0u);
    }

    TEST(LodTileSelection, a_tile_that_is_not_visible_keeps_the_tile_choice_too)
    {
        TileState state = stateOf(unitTile(4));
        step(state, cameraAtDistance(1000), 4.f);
        ASSERT_EQ(state.mChoice, TileChoice::Tile);
        SelectionCamera tooFar = cameraAtDistance(1000);
        tooFar.mViewDistance = 100;
        step(state, tooFar, 4.f);
        EXPECT_EQ(state.mChoice, TileChoice::Tile);
        EXPECT_FALSE(state.mVisible);
    }

    // ---- counts -------------------------------------------------------------------------------------------

    TEST(LodTileSelection, the_result_counts_tiles_live_and_skipped)
    {
        // The camera is at x = -1000 on the middle line of the row, looking along +X.
        const SelectionCamera camera = cameraAt(-1000, 50, 25);

        TileBounds nearTile = unitTile(4); // d = 1000: 2 pixels, a tile
        TileBounds farther = unitTile(4); // d = 1600: 1.25 pixels, a tile
        farther.mOriginX = 600;
        TileBounds around = unitTile(4); // the camera is on its east edge: 2000 pixels, live
        around.mOriginX = -1100;
        TileBounds behind = unitTile(4); // 4000 units behind
        behind.mOriginX = -5000;

        std::vector<TileState> states = { stateOf(nearTile), stateOf(farther), stateOf(around), stateOf(behind) };
        const SelectionResult result = selectTiles(states, camera, 4.f);
        EXPECT_EQ(result.mTiles, 2u);
        EXPECT_EQ(result.mLive, 1u);
        EXPECT_EQ(result.mSkipped, 1u);
        EXPECT_EQ(result.mTiles + result.mLive + result.mSkipped, states.size());
        EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
        EXPECT_EQ(states[1].mChoice, TileChoice::Tile);
        EXPECT_EQ(states[2].mChoice, TileChoice::Live);
        EXPECT_FALSE(states[3].mVisible);
    }

    TEST(LodTileSelection, mchanged_counts_the_visible_tiles_that_flipped)
    {
        const TileBounds bounds = unitTile(4);
        std::vector<TileState> states = { stateOf(bounds), stateOf(bounds), stateOf(bounds) };
        states[2].mBounds.mOriginY = 100000; // never visible with the default view distance
        selectTiles(states, cameraAtDistance(600), 4.f); // both visible tiles become tiles
        ASSERT_EQ(states[0].mChoice, TileChoice::Tile);
        ASSERT_EQ(states[1].mChoice, TileChoice::Tile);

        // Make the second one hold on: it is live already, the first flips alone.
        states[1].mChoice = TileChoice::Live;
        states[1].mBounds.mGeometricError = 100.f; // far over the threshold, stays live
        states[0].mBounds.mGeometricError = 100.f; // flips from tile to live
        const SelectionResult result = selectTiles(states, cameraAtDistance(600), 4.f);
        EXPECT_EQ(result.mChanged, 1u);
        EXPECT_EQ(result.mLive, 2u);
        EXPECT_EQ(result.mSkipped, 1u);
    }

    TEST(LodTileSelection, no_tiles_gives_an_empty_result)
    {
        std::vector<TileState> states;
        const SelectionResult result = selectTiles(states, cameraAtDistance(100), 4.f);
        EXPECT_EQ(result.mTiles, 0u);
        EXPECT_EQ(result.mLive, 0u);
        EXPECT_EQ(result.mSkipped, 0u);
        EXPECT_EQ(result.mChanged, 0u);
    }

    // ---- determinism --------------------------------------------------------------------------------------

    void expectSameStates(const std::vector<TileState>& a, const std::vector<TileState>& b)
    {
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].mChoice, b[i].mChoice) << i;
            EXPECT_EQ(a[i].mVisible, b[i].mVisible) << i;
            EXPECT_EQ(a[i].mPixels, b[i].mPixels) << i;
        }
    }

    std::vector<TileState> makeRow()
    {
        std::vector<TileState> states;
        for (unsigned i = 0; i < 24; ++i)
        {
            TileBounds bounds;
            bounds.mOriginX = static_cast<float>((i % 6) * 2000);
            bounds.mOriginY = static_cast<float>((i / 6) * 2000) - 4000.f;
            bounds.mSide = 1600;
            bounds.mMinHeight = -100.f;
            bounds.mMaxHeight = 100.f + static_cast<float>(i);
            bounds.mGeometricError = 2.f + static_cast<float>(i % 5);
            states.push_back(stateOf(bounds));
        }
        return states;
    }

    SelectionCamera pathCamera(unsigned frame)
    {
        SelectionCamera camera;
        camera.mX = -3000.0 + 400.0 * frame;
        camera.mY = 500.0 * std::sin(0.3 * frame);
        camera.mZ = 300.0;
        camera.mForwardX = std::cos(0.1 * frame);
        camera.mForwardY = std::sin(0.1 * frame);
        camera.mForwardZ = 0;
        camera.mViewportHeight = 1080;
        camera.mFovY = 1.0;
        camera.mAspect = 16.0 / 9.0;
        camera.mViewDistance = 9000;
        return camera;
    }

    TEST(LodTileSelection, the_same_inputs_give_the_same_states)
    {
        std::vector<TileState> first = makeRow();
        std::vector<TileState> second = makeRow();
        for (unsigned frame = 0; frame < 60; ++frame)
        {
            const SelectionResult a = selectTiles(first, pathCamera(frame), 4.f);
            const SelectionResult b = selectTiles(second, pathCamera(frame), 4.f);
            EXPECT_EQ(a.mTiles, b.mTiles);
            EXPECT_EQ(a.mLive, b.mLive);
            EXPECT_EQ(a.mSkipped, b.mSkipped);
            EXPECT_EQ(a.mChanged, b.mChanged);
            expectSameStates(first, second);
        }
    }

    TEST(LodTileSelection, each_tile_is_chosen_independently_of_the_others)
    {
        std::vector<TileState> together = makeRow();
        std::vector<TileState> alone = makeRow();
        for (unsigned frame = 0; frame < 60; ++frame)
        {
            selectTiles(together, pathCamera(frame), 4.f);
            for (TileState& state : alone)
            {
                step(state, pathCamera(frame), 4.f);
            }
            expectSameStates(together, alone);
        }
        // The path really exercises both outcomes.
        bool sawTile = false, sawLive = false;
        for (const TileState& state : together)
        {
            sawTile = sawTile || state.mChoice == TileChoice::Tile;
            sawLive = sawLive || state.mChoice == TileChoice::Live;
        }
        EXPECT_TRUE(sawTile || sawLive);
    }
}
