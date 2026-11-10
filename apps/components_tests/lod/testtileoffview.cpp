// The tile selection of tiles that are not in view (the far-terrain turn gap, 2026-11-10): a tile that nothing else
// draws, past the live terrain's reach, is built only when it is seen, so turning the camera shows the ground
// appearing from nothing. With SelectionCamera::mOffViewScale above 0 a tile out of the view cone but inside the view
// distance is chosen too, with the threshold scaled up (a coarser, cheaper tile), so that a coarse tile is already
// drawn when it comes into view and the finer ones then replace it. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tileselection.hpp>

#include <numbers>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float threshold = 8.f;

    TileState tileAt(float originX, float originY, float error, bool hasChildren)
    {
        TileState state;
        state.mBounds.mOriginX = originX;
        state.mBounds.mOriginY = originY;
        state.mBounds.mSide = 8192.f;
        state.mBounds.mMinHeight = 0.f;
        state.mBounds.mMaxHeight = 1000.f;
        state.mBounds.mGeometricError = error;
        state.mHasChildren = hasChildren;
        return state;
    }

    // At (0, 4096), tan(fov / 2) = 1, so pixels = 500 * error / distance. Looking east or west.
    SelectionCamera camera(double forwardX, double offViewScale)
    {
        SelectionCamera result;
        result.mX = 0;
        result.mY = 4096;
        result.mZ = 500;
        result.mForwardX = forwardX;
        result.mForwardY = 0;
        result.mForwardZ = 0;
        result.mViewportHeight = 1000;
        result.mFovY = std::numbers::pi / 2.0;
        result.mAspect = 1.6;
        result.mViewDistance = 1e9;
        result.mLiveRange = 10000;
        result.mOffViewScale = offViewScale;
        return result;
    }

    // A tile 30000 units east, error 400: 500 * 400 / 30000 = 6.7 pixels (Tile at threshold 8) ...
    // ... and one with error 1000: 16.7 pixels, over the threshold of 8 and under the scaled 64.
    TEST(LodTileOffView, a_tile_out_of_view_is_not_chosen_without_the_scale)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 400.f, true) };
        const std::vector<std::int32_t> parents{ -1 };
        const SelectionResult result = selectTileTree(states, parents, camera(-1.0, 0.0), threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Unset);
        EXPECT_FALSE(states[0].mVisible);
        EXPECT_EQ(result.mSkipped, 1u);
    }

    TEST(LodTileOffView, a_tile_out_of_view_is_chosen_with_the_scale_and_stays_not_visible)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, true) };
        const std::vector<std::int32_t> parents{ -1 };
        selectTileTree(states, parents, camera(-1.0, 8.0), threshold);
        // 16.7 pixels against 8 * 8 = 64: a Tile, though in view it would be Live.
        EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
        EXPECT_FALSE(states[0].mVisible);
    }

    TEST(LodTileOffView, the_same_tile_in_view_uses_the_plain_threshold)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, true) };
        const std::vector<std::int32_t> parents{ -1 };
        selectTileTree(states, parents, camera(1.0, 8.0), threshold);
        EXPECT_TRUE(states[0].mVisible);
        EXPECT_EQ(states[0].mChoice, TileChoice::Live);
    }

    TEST(LodTileOffView, a_leaf_past_the_live_range_out_of_view_is_a_tile)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, false) };
        const std::vector<std::int32_t> parents{ -1 };
        selectTileTree(states, parents, camera(-1.0, 8.0), threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
    }

    TEST(LodTileOffView, a_choice_made_in_view_stays_when_the_camera_looks_away)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, true) };
        const std::vector<std::int32_t> parents{ -1 };
        selectTileTree(states, parents, camera(1.0, 8.0), threshold);
        ASSERT_EQ(states[0].mChoice, TileChoice::Live); // 16.7 pixels in view: the plain rule
        for (int i = 0; i < 3; ++i)
        {
            selectTileTree(states, parents, camera(-1.0, 8.0), threshold);
            // Not re-chosen coarse (the scaled threshold would make it a Tile): that would build a coarse tile over
            // the fine ones the camera just looked at.
            EXPECT_EQ(states[0].mChoice, TileChoice::Live);
        }
    }

    TEST(LodTileOffView, a_tile_beyond_the_view_distance_is_still_skipped)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, true) };
        const std::vector<std::int32_t> parents{ -1 };
        SelectionCamera far = camera(-1.0, 8.0);
        far.mViewDistance = 20000;
        const SelectionResult result = selectTileTree(states, parents, far, threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Unset);
        EXPECT_EQ(result.mSkipped, 1u);
    }

    // Turning: the coarse tile chosen while looking away becomes Live when it comes into view and its (forced) leaf
    // child takes over; until then the parent is the choice that kept the square covered.
    TEST(LodTileOffView, turning_toward_a_tile_hands_it_to_the_finer_tile)
    {
        std::vector<TileState> states{ tileAt(30000.f, 0.f, 1000.f, true), tileAt(30000.f, 0.f, 200.f, false) };
        const std::vector<std::int32_t> parents{ -1, 0 };
        selectTileTree(states, parents, camera(-1.0, 8.0), threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
        EXPECT_EQ(states[1].mChoice, TileChoice::Covered);
        selectTileTree(states, parents, camera(1.0, 8.0), threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Live);
        EXPECT_EQ(states[1].mChoice, TileChoice::Tile);
        EXPECT_TRUE(states[1].mVisible);
    }

    // The property: with the scale on, whichever way the camera looks, every leaf past the live range has a Tile in
    // its chain (itself or an ancestor), so nothing has to appear from nothing.
    TEST(LodTileOffView, every_leaf_past_the_live_range_is_covered_whichever_way_the_camera_looks)
    {
        for (const double forward : { 1.0, -1.0 })
        {
            // A line of four leaves east, each with a parent pair above them.
            std::vector<TileState> states;
            std::vector<std::int32_t> parents;
            states.push_back(tileAt(30000.f, 0.f, 1500.f, true)); // 0: parent of 2, 3
            states.push_back(tileAt(46384.f, 0.f, 1500.f, true)); // 1: parent of 4, 5
            parents = { -1, -1 };
            for (const float x : { 30000.f, 38192.f, 46384.f, 54576.f })
            {
                states.push_back(tileAt(x, 0.f, 900.f, false));
                parents.push_back(x < 46000.f ? 0 : 1);
            }

            for (int round = 0; round < 3; ++round)
                selectTileTree(states, parents, camera(forward, 8.0), threshold);
            for (std::size_t leaf = 2; leaf < states.size(); ++leaf)
            {
                bool covered = false;
                for (std::int32_t i = static_cast<std::int32_t>(leaf); i >= 0; i = parents[static_cast<std::size_t>(i)])
                    covered = covered || states[static_cast<std::size_t>(i)].mChoice == TileChoice::Tile;
                EXPECT_TRUE(covered) << "leaf " << leaf << " looking " << forward;
            }
        }
    }
}
