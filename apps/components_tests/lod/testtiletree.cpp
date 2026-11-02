// Task 0055 (albedo bake, tile hierarchy): ordering tiles for a tree, their nearest baked ancestors, and the selection
// over several levels. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tileselection.hpp>

#include <algorithm>
#include <cstdint>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float cellSize = 8192.f;
    constexpr float threshold = 4.f;

    TileId tileAt(std::uint32_t level, std::int32_t cellX, std::int32_t cellY)
    {
        return tileContainingCell(0, level, cellX, cellY).value();
    }

    std::int32_t indexOf(const std::vector<TileId>& ids, const TileId& id)
    {
        const auto found = std::find(ids.begin(), ids.end(), id);
        return found == ids.end() ? -1 : static_cast<std::int32_t>(found - ids.begin());
    }

    void addUnique(std::vector<TileId>& ids, const TileId& id)
    {
        if (indexOf(ids, id) < 0)
        {
            ids.push_back(id);
        }
    }

    // The rule written out: the biggest level first, then the smaller key.
    std::vector<TileId> expectedOrder(std::vector<TileId> ids)
    {
        std::sort(ids.begin(), ids.end(), [](const TileId& a, const TileId& b) {
            if (a.mLevel != b.mLevel)
            {
                return a.mLevel > b.mLevel;
            }
            return packTileKey(a).value() < packTileKey(b).value();
        });
        return ids;
    }

    // All tiles of levels 1 to 3 containing the cells -3..2 in both directions (negative cells included).
    std::vector<TileId> blockOfTiles()
    {
        std::vector<TileId> ids;
        for (std::int32_t cellY = -3; cellY <= 2; ++cellY)
        {
            for (std::int32_t cellX = -3; cellX <= 2; ++cellX)
            {
                for (std::uint32_t level = 1; level <= 3; ++level)
                {
                    addUnique(ids, tileAt(level, cellX, cellY));
                }
            }
        }
        return ids;
    }

    // ---- orderTilesForTree -----------------------------------------------------------------------------------------

    TEST(LodTileTree, order_is_level_descending_then_key_ascending)
    {
        const std::vector<TileId> ids = blockOfTiles();
        ASSERT_GT(ids.size(), 20u);
        const std::vector<TileId> expected = expectedOrder(ids);

        std::mt19937 engine(2024);
        std::vector<TileId> shuffled = ids;
        for (int round = 0; round < 4; ++round)
        {
            std::shuffle(shuffled.begin(), shuffled.end(), engine);
            EXPECT_EQ(orderTilesForTree(shuffled), expected) << "round " << round;
        }
        EXPECT_EQ(orderTilesForTree(expected), expected);
    }

    TEST(LodTileTree, a_parent_is_always_before_its_children)
    {
        const std::vector<TileId> ordered = orderTilesForTree(blockOfTiles());
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            const std::optional<TileId> parent = parentTile(ordered[i]);
            if (!parent)
            {
                continue;
            }
            const std::int32_t at = indexOf(ordered, *parent);
            if (at >= 0)
            {
                EXPECT_LT(static_cast<std::size_t>(at), i);
            }
        }
    }

    TEST(LodTileTree, ordering_nothing_gives_nothing)
    {
        EXPECT_TRUE(orderTilesForTree({}).empty());
        EXPECT_TRUE(nearestAncestors({}).empty());
    }

    // ---- nearestAncestors ------------------------------------------------------------------------------------------

    void expectSmallerThanOwnIndex(const std::vector<std::int32_t>& ancestors)
    {
        for (std::size_t i = 0; i < ancestors.size(); ++i)
        {
            EXPECT_LT(ancestors[i], static_cast<std::int32_t>(i)) << i;
            EXPECT_GE(ancestors[i], -1) << i;
        }
    }

    TEST(LodTileTree, the_parent_is_the_nearest_ancestor_when_it_is_present)
    {
        const TileId parent = tileAt(2, -4, 0);
        TileId children[4];
        ASSERT_TRUE(childTiles(parent, children));
        std::vector<TileId> ids = { children[3], parent, children[0], children[2], children[1] };

        const std::vector<TileId> ordered = orderTilesForTree(ids);
        ASSERT_EQ(ordered.size(), 5u);
        EXPECT_EQ(ordered[0], parent);
        const std::vector<std::int32_t> ancestors = nearestAncestors(ordered);
        EXPECT_EQ(ancestors, (std::vector<std::int32_t>{ -1, 0, 0, 0, 0 }));
    }

    TEST(LodTileTree, a_missing_parent_gives_the_grandparent_and_none_gives_minus_one)
    {
        // Level 3 tile G holds cells 0..7; there is no level 2 tile in the list.
        const TileId grandparent = tileAt(3, 0, 0);
        const TileId inside = tileAt(1, 5, 6);
        const TileId outside = tileAt(1, -1, -1);
        const std::vector<TileId> ordered = orderTilesForTree({ outside, inside, grandparent });
        ASSERT_EQ(ordered.size(), 3u);
        const std::vector<std::int32_t> ancestors = nearestAncestors(ordered);
        ASSERT_EQ(ancestors.size(), 3u);
        EXPECT_EQ(ancestors[static_cast<std::size_t>(indexOf(ordered, grandparent))], -1);
        EXPECT_EQ(ancestors[static_cast<std::size_t>(indexOf(ordered, inside))], indexOf(ordered, grandparent));
        EXPECT_EQ(ancestors[static_cast<std::size_t>(indexOf(ordered, outside))], -1);
        expectSmallerThanOwnIndex(ancestors);
    }

    TEST(LodTileTree, tiles_with_no_baked_ancestor_have_minus_one)
    {
        const std::vector<TileId> ordered
            = orderTilesForTree({ tileAt(1, 10, 10), tileAt(2, -8, 4), tileAt(1, -3, 7) });
        EXPECT_EQ(nearestAncestors(ordered), (std::vector<std::int32_t>{ -1, -1, -1 }));
    }

    TEST(LodTileTree, a_mixed_hierarchy_picks_the_nearest_present_ancestor_for_each_tile)
    {
        const TileId top = tileAt(3, 0, 0);
        const TileId middle = tileAt(2, 0, 0); // present
        const TileId underMiddle = tileAt(1, 0, 0); // parent: middle
        const TileId underAbsentEast = tileAt(1, 4, 0); // parent tileAt(2, 4, 0) is not in the list: top
        const TileId underAbsentNorth = tileAt(1, 0, 4); // parent tileAt(2, 0, 4) is not in the list: top
        const std::vector<TileId> ordered
            = orderTilesForTree({ underAbsentNorth, underMiddle, top, underAbsentEast, middle });
        ASSERT_EQ(ordered.size(), 5u);

        const std::vector<std::int32_t> ancestors = nearestAncestors(ordered);
        ASSERT_EQ(ancestors.size(), 5u);
        const auto ancestorOf
            = [&](const TileId& id) { return ancestors[static_cast<std::size_t>(indexOf(ordered, id))]; };
        EXPECT_EQ(ancestorOf(top), -1);
        EXPECT_EQ(ancestorOf(middle), indexOf(ordered, top));
        EXPECT_EQ(ancestorOf(underMiddle), indexOf(ordered, middle));
        EXPECT_EQ(ancestorOf(underAbsentEast), indexOf(ordered, top));
        EXPECT_EQ(ancestorOf(underAbsentNorth), indexOf(ordered, top));
        expectSmallerThanOwnIndex(ancestors);
    }

    TEST(LodTileTree, ancestors_over_a_block_of_cells_with_negative_coordinates)
    {
        const std::vector<TileId> ordered = orderTilesForTree(blockOfTiles());
        const std::vector<std::int32_t> ancestors = nearestAncestors(ordered);
        ASSERT_EQ(ancestors.size(), ordered.size());
        expectSmallerThanOwnIndex(ancestors);
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            if (ordered[i].mLevel == 3)
            {
                EXPECT_EQ(ancestors[i], -1) << i; // no level 4 tile in the list
                continue;
            }
            const std::optional<TileId> parent = parentTile(ordered[i]);
            ASSERT_TRUE(parent.has_value());
            ASSERT_GE(ancestors[i], 0) << i;
            EXPECT_EQ(ordered[static_cast<std::size_t>(ancestors[i])], *parent) << i;
        }
    }

    // ---- selectTileTree --------------------------------------------------------------------------------------------

    // One level 3 tile (cells 0..7), its four level 2 children, and the four level 1 children of the south-west one.
    // Sides are 2^level cells, errors 100 * 2^level, heights 0 to 1000.
    struct Tree
    {
        std::vector<TileId> mIds;
        std::vector<std::int32_t> mParents;
        std::vector<TileState> mStates;
        std::size_t mTop = 0; // index of the level 3 tile
        std::size_t mWestChild = 0; // index of the level 2 tile at cell (0, 0)
        std::vector<std::size_t> mLevelOne; // indices of the level 1 tiles, which lie under mWestChild
    };

    TileState stateOfTile(const TileId& id)
    {
        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
        tileOriginCell(id, cellX, cellY);
        const float sideInCells = static_cast<float>(1u << id.mLevel);
        TileState state;
        state.mBounds.mOriginX = static_cast<float>(cellX) * cellSize;
        state.mBounds.mOriginY = static_cast<float>(cellY) * cellSize;
        state.mBounds.mSide = sideInCells * cellSize;
        state.mBounds.mMinHeight = 0.f;
        state.mBounds.mMaxHeight = 1000.f;
        state.mBounds.mGeometricError = 100.f * sideInCells;
        return state;
    }

    Tree makeTree()
    {
        const TileId top = tileAt(3, 0, 0);
        TileId levelTwo[4];
        EXPECT_TRUE(childTiles(top, levelTwo));
        const TileId westChild = tileAt(2, 0, 0);
        TileId levelOne[4];
        EXPECT_TRUE(childTiles(westChild, levelOne));

        std::vector<TileId> ids = { levelOne[2], levelTwo[3], top, levelOne[0], levelTwo[1], levelOne[3], levelTwo[0],
            levelTwo[2], levelOne[1] };
        Tree tree;
        tree.mIds = orderTilesForTree(ids);
        tree.mParents = nearestAncestors(tree.mIds);
        for (const TileId& id : tree.mIds)
        {
            tree.mStates.push_back(stateOfTile(id));
        }
        tree.mTop = static_cast<std::size_t>(indexOf(tree.mIds, top));
        tree.mWestChild = static_cast<std::size_t>(indexOf(tree.mIds, westChild));
        for (const TileId& id : levelOne)
        {
            tree.mLevelOne.push_back(static_cast<std::size_t>(indexOf(tree.mIds, id)));
        }
        return tree;
    }

    // The camera is `distance` west of the hierarchy (x = -distance), looking east, level with the middle of the
    // south-west level 2 tile in y and inside every tile's height range.
    SelectionCamera cameraAtDistance(double distance)
    {
        SelectionCamera camera;
        camera.mX = -distance;
        camera.mY = 16384;
        camera.mZ = 500;
        camera.mForwardX = 1;
        camera.mForwardY = 0;
        camera.mForwardZ = 0;
        camera.mViewportHeight = 1000;
        camera.mFovY = std::numbers::pi / 2.0; // tan(fov / 2) = 1: pixels = 500 * error / distance
        camera.mAspect = 4.0;
        camera.mViewDistance = 1e9;
        return camera;
    }

    SelectionResult run(Tree& tree, double distance, float limit = threshold)
    {
        return selectTileTree(tree.mStates, tree.mParents, cameraAtDistance(distance), limit);
    }

    std::size_t countOf(const Tree& tree, TileChoice choice)
    {
        return static_cast<std::size_t>(std::count_if(tree.mStates.begin(), tree.mStates.end(),
            [choice](const TileState& state) { return state.mChoice == choice; }));
    }

    TEST(LodTileTree, the_test_tree_has_the_shape_the_cases_below_rely_on)
    {
        const Tree tree = makeTree();
        ASSERT_EQ(tree.mIds.size(), 9u);
        EXPECT_EQ(tree.mIds[0].mLevel, 3u);
        EXPECT_EQ(tree.mTop, 0u);
        EXPECT_EQ(tree.mParents[tree.mTop], -1);
        EXPECT_EQ(tree.mParents[tree.mWestChild], static_cast<std::int32_t>(tree.mTop));
        ASSERT_EQ(tree.mLevelOne.size(), 4u);
        for (const std::size_t at : tree.mLevelOne)
        {
            EXPECT_EQ(tree.mParents[at], static_cast<std::int32_t>(tree.mWestChild));
        }
        EXPECT_NEAR(tree.mStates[tree.mTop].mBounds.mSide, 65536.0, 1e-3);
        EXPECT_NEAR(tree.mStates[tree.mTop].mBounds.mGeometricError, 800.0, 1e-3);
    }

    TEST(LodTileTree, far_away_the_biggest_tile_is_drawn_and_everything_below_it_is_covered)
    {
        Tree tree = makeTree();
        const SelectionResult result = run(tree, 200000); // top: 500 * 800 / 200000 = 2 pixels
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Tile);
        EXPECT_TRUE(tree.mStates[tree.mTop].mVisible);
        EXPECT_NEAR(tree.mStates[tree.mTop].mPixels, 2.0, 1e-4);
        for (std::size_t i = 0; i < tree.mStates.size(); ++i)
        {
            if (i == tree.mTop)
            {
                continue;
            }
            EXPECT_EQ(tree.mStates[i].mChoice, TileChoice::Covered) << i;
            EXPECT_FALSE(tree.mStates[i].mVisible) << i;
            EXPECT_EQ(tree.mStates[i].mPixels, 0.f) << i;
        }
        EXPECT_EQ(result.mTiles, 1u);
        EXPECT_EQ(result.mLive, 0u);
        EXPECT_EQ(result.mSkipped, 0u);
        EXPECT_EQ(result.mCovered, 8u);
        EXPECT_EQ(result.mChanged, 0u); // a first choice (Unset to Tile) is not a change, as in selectTiles
    }

    TEST(LodTileTree, at_a_middle_distance_the_children_are_drawn_and_the_grandchildren_covered)
    {
        Tree tree = makeTree();
        // top: 5.7 pixels (over 4); level 2: 1.9 to 2.9 pixels; level 1 would be 1.4 to 1.9 but is covered.
        const SelectionResult result = run(tree, 70000);
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Live);
        for (std::size_t i = 0; i < tree.mStates.size(); ++i)
        {
            if (tree.mIds[i].mLevel == 2)
            {
                EXPECT_EQ(tree.mStates[i].mChoice, TileChoice::Tile) << i;
            }
            if (tree.mIds[i].mLevel == 1)
            {
                EXPECT_EQ(tree.mStates[i].mChoice, TileChoice::Covered) << i;
                EXPECT_FALSE(tree.mStates[i].mVisible) << i;
                EXPECT_EQ(tree.mStates[i].mPixels, 0.f) << i;
            }
        }
        EXPECT_EQ(result.mTiles, 4u);
        EXPECT_EQ(result.mLive, 1u);
        EXPECT_EQ(result.mSkipped, 0u);
        EXPECT_EQ(result.mCovered, 4u);
    }

    TEST(LodTileTree, near_the_ground_every_tile_is_live_and_nothing_is_covered)
    {
        Tree tree = makeTree();
        const SelectionResult result = run(tree, 2000);
        for (std::size_t i = 0; i < tree.mStates.size(); ++i)
        {
            EXPECT_EQ(tree.mStates[i].mChoice, TileChoice::Live) << i;
            EXPECT_TRUE(tree.mStates[i].mVisible) << i;
            EXPECT_GT(tree.mStates[i].mPixels, threshold) << i;
        }
        EXPECT_EQ(result.mTiles, 0u);
        EXPECT_EQ(result.mLive, 9u);
        EXPECT_EQ(result.mSkipped, 0u);
        EXPECT_EQ(result.mCovered, 0u);
    }

    TEST(LodTileTree, a_covered_tile_that_is_reached_again_starts_from_nothing)
    {
        Tree tree = makeTree();
        run(tree, 2000);
        for (const std::size_t at : tree.mLevelOne)
        {
            ASSERT_EQ(tree.mStates[at].mChoice, TileChoice::Live);
        }
        run(tree, 200000); // the top tile is drawn: the level 1 tiles are covered and forget they were Live
        for (const std::size_t at : tree.mLevelOne)
        {
            ASSERT_EQ(tree.mStates[at].mChoice, TileChoice::Covered);
        }

        // 30000 away: top 13.3 pixels and level 2 (south-west) 6.7 pixels are over the threshold, so both are Live and
        // the level 1 tiles are chosen again. The two level 1 tiles on the west side are both 3.3 pixels (the camera
        // is level with the border between them): a tile with no memory takes Tile (3.3 <= 4); one that remembered
        // Live would keep it, because 3.3 is not below 0.7 * 4 = 2.8.
        const SelectionResult result = run(tree, 30000);
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Live);
        EXPECT_EQ(tree.mStates[tree.mWestChild].mChoice, TileChoice::Live);
        std::size_t westernTiles = 0;
        for (const std::size_t at : tree.mLevelOne)
        {
            std::int32_t cellX = 0;
            std::int32_t cellY = 0;
            tileOriginCell(tree.mIds[at], cellX, cellY);
            if (cellX == 0)
            {
                EXPECT_EQ(tree.mStates[at].mChoice, TileChoice::Tile) << cellX << ", " << cellY;
                EXPECT_TRUE(tree.mStates[at].mVisible);
                ++westernTiles;
            }
        }
        EXPECT_EQ(westernTiles, 2u);
        EXPECT_EQ(result.mCovered, 0u);
    }

    TEST(LodTileTree, a_tile_that_is_not_covered_keeps_the_hysteresis)
    {
        Tree tree = makeTree();
        run(tree, 2000);
        ASSERT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Live);

        // 114286 away the top tile has 3.5 pixels: over 2.8, so a Live tile stays Live although 3.5 <= 4.
        SelectionResult result = run(tree, 114286);
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Live);
        EXPECT_NEAR(tree.mStates[tree.mTop].mPixels, 3.5, 0.01);
        EXPECT_EQ(result.mLive, 1u);
        EXPECT_EQ(result.mTiles, 4u); // the four level 2 tiles (1.2 to 1.8 pixels)
        EXPECT_EQ(result.mCovered, 4u);

        // 160000 away: 2.5 pixels, below 2.8, so it returns to Tile and covers the rest.
        result = run(tree, 160000);
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Tile);
        EXPECT_EQ(result.mTiles, 1u);
        EXPECT_EQ(result.mCovered, 8u);
    }

    TEST(LodTileTree, a_tile_becomes_live_only_above_the_threshold)
    {
        Tree tree = makeTree();
        run(tree, 200000);
        ASSERT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Tile);

        run(tree, 105000); // 3.8 pixels: still Tile
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Tile);
        EXPECT_EQ(countOf(tree, TileChoice::Covered), 8u);

        run(tree, 90000); // 4.4 pixels: Live; the level 2 tiles (2.2 and less) are chosen as Tile
        EXPECT_EQ(tree.mStates[tree.mTop].mChoice, TileChoice::Live);
        EXPECT_EQ(countOf(tree, TileChoice::Live), 1u);
        EXPECT_EQ(countOf(tree, TileChoice::Tile), 4u);
        EXPECT_EQ(countOf(tree, TileChoice::Covered), 4u);
    }

    TEST(LodTileTree, tiles_out_of_view_distance_are_skipped_and_not_covered)
    {
        Tree tree = makeTree();
        SelectionCamera camera = cameraAtDistance(200000);
        camera.mViewDistance = 1000; // every tile is farther than this
        const SelectionResult result = selectTileTree(tree.mStates, tree.mParents, camera, threshold);
        EXPECT_EQ(result.mSkipped, 9u);
        EXPECT_EQ(result.mTiles, 0u);
        EXPECT_EQ(result.mLive, 0u);
        EXPECT_EQ(result.mCovered, 0u);
        EXPECT_EQ(result.mChanged, 0u);
        for (const TileState& state : tree.mStates)
        {
            EXPECT_EQ(state.mChoice, TileChoice::Unset);
            EXPECT_FALSE(state.mVisible);
        }
    }

    TEST(LodTileTree, the_result_depends_only_on_the_arguments)
    {
        Tree first = makeTree();
        Tree second = makeTree();
        const double distances[] = { 2000, 200000, 30000, 114286, 70000, 160000 };
        for (const double distance : distances)
        {
            const SelectionResult a = run(first, distance);
            const SelectionResult b = run(second, distance);
            EXPECT_EQ(a.mTiles, b.mTiles);
            EXPECT_EQ(a.mLive, b.mLive);
            EXPECT_EQ(a.mSkipped, b.mSkipped);
            EXPECT_EQ(a.mChanged, b.mChanged);
            EXPECT_EQ(a.mCovered, b.mCovered);
            for (std::size_t i = 0; i < first.mStates.size(); ++i)
            {
                EXPECT_EQ(first.mStates[i].mChoice, second.mStates[i].mChoice) << distance << " tile " << i;
                EXPECT_EQ(first.mStates[i].mVisible, second.mStates[i].mVisible);
                EXPECT_EQ(first.mStates[i].mPixels, second.mStates[i].mPixels);
            }
        }
    }
}
