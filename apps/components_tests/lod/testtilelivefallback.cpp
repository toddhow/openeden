// The tile selection past the live terrain's reach (the far-terrain gap, 2026-11-09): a tile that nothing finer
// replaces and whose square is not wholly inside the live paging range has no fallback, so it is drawn whatever its
// pixel error says. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tileselection.hpp>

#include <numbers>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float threshold = 8.f;

    // A 8192-unit square with the given error, west edge at x = originX.
    TileState tileAtX(float originX, float error, bool hasChildren = false)
    {
        TileState state;
        state.mBounds.mOriginX = originX;
        state.mBounds.mOriginY = 0.f;
        state.mBounds.mSide = 8192.f;
        state.mBounds.mMinHeight = 0.f;
        state.mBounds.mMaxHeight = 1000.f;
        state.mBounds.mGeometricError = error;
        state.mHasChildren = hasChildren;
        return state;
    }

    // At the origin, looking east, tan(fov / 2) = 1: pixels = 500 * error / distance.
    SelectionCamera camera(double liveRange)
    {
        SelectionCamera result;
        result.mX = 0;
        result.mY = 4096;
        result.mZ = 500;
        result.mForwardX = 1;
        result.mForwardY = 0;
        result.mForwardZ = 0;
        result.mViewportHeight = 1000;
        result.mFovY = std::numbers::pi / 2.0;
        result.mAspect = 4.0;
        result.mViewDistance = 1e9;
        result.mLiveRange = liveRange;
        return result;
    }

    TileChoice choose(TileState state, double liveRange)
    {
        std::vector<TileState> states{ state };
        const std::vector<std::int32_t> parents{ -1 };
        selectTileTree(states, parents, camera(liveRange), threshold);
        return states[0].mChoice;
    }

    // 500 * 400 / 20000 = 10 pixels: above the threshold of 8, so the pixel rule says Live.
    TEST(LodTileLiveFallback, the_pixel_rule_alone_leaves_a_far_rough_tile_to_the_live_terrain)
    {
        EXPECT_EQ(choose(tileAtX(20000.f, 400.f), 0.0), TileChoice::Live);
    }

    TEST(LodTileLiveFallback, a_rough_tile_wholly_past_the_live_range_is_drawn)
    {
        EXPECT_EQ(choose(tileAtX(20000.f, 400.f), 12000.0), TileChoice::Tile);
    }

    TEST(LodTileLiveFallback, a_rough_tile_reaching_past_the_live_range_is_drawn)
    {
        // Its far corner is past the range: the live terrain cannot draw all of it.
        EXPECT_EQ(choose(tileAtX(8000.f, 400.f), 12000.0), TileChoice::Tile);
    }

    TEST(LodTileLiveFallback, a_rough_tile_wholly_inside_the_live_range_follows_the_pixel_rule)
    {
        // 500 * 400 / 5000 = 40 pixels, and the live terrain draws the whole square.
        EXPECT_EQ(choose(tileAtX(4000.f, 400.f), 20000.0), TileChoice::Live);
    }

    TEST(LodTileLiveFallback, a_tile_with_finer_tiles_under_it_follows_the_pixel_rule)
    {
        EXPECT_EQ(choose(tileAtX(20000.f, 400.f, true), 12000.0), TileChoice::Live);
    }

    TEST(LodTileLiveFallback, a_forced_tile_stays_drawn_while_nothing_else_draws_its_square)
    {
        std::vector<TileState> states{ tileAtX(20000.f, 400.f) };
        const std::vector<std::int32_t> parents{ -1 };
        for (int i = 0; i < 5; ++i)
        {
            selectTileTree(states, parents, camera(12000.0), threshold);
            EXPECT_EQ(states[0].mChoice, TileChoice::Tile);
        }
        // The camera now reaches over the whole square (range 40000): the pixel rule decides again.
        selectTileTree(states, parents, camera(40000.0), threshold);
        EXPECT_EQ(states[0].mChoice, TileChoice::Live);
    }

    TEST(LodTileLiveFallback, a_range_that_is_not_positive_is_off)
    {
        EXPECT_EQ(choose(tileAtX(20000.f, 400.f), -1.0), TileChoice::Live);
    }
}
