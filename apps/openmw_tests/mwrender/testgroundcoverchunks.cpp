// Tests for MWRender::GroundcoverChunkTracker (task 0012, V4g step 3). Written from the spec in
// docs/tasks/0012-vulkan-v4-renderer-parity.md only, without the implementation.
//
// Conventions these tests rely on:
// - GroundcoverChunkTracker<Chunk>::Cell is std::pair<int, int>, and std::pair<int, int> is used directly as a cell.
// - Chunk types used: std::shared_ptr<int> where identity matters (nullptr is the default-constructed, empty chunk) and
//   int (0 is the empty chunk).
// - setWanted applies its steps in the spec's order: shown cells no longer wanted go to the cache, wanted cached cells
//   go back to shown, wanted unknown cells become pending (and are returned), then the cache is trimmed. So a cell
//   leaving the shown set in the same call that overflows the cache is subject to that call's trim.
// - A cell dropped from the cache by a trim is unknown again: a later setWanted that wants it returns it.
// - "Cell becomes shown if the last setWanted wanted it" is read literally: a pending cell's wanted state is whatever
//   the most recent setWanted said, even if earlier calls said otherwise.
// - When several shown cells move to the cache in one setWanted, their order relative to each other is not specified;
//   these tests never depend on it.
// - A capacity of 0 means nothing is ever kept in the cache.

#include "apps/openmw/mwrender/groundcoverchunks.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using Cell = std::pair<int, int>;
    using PtrChunk = std::shared_ptr<int>;
    using Tracker = MWRender::GroundcoverChunkTracker<PtrChunk>;
    using IntTracker = MWRender::GroundcoverChunkTracker<int>;

    static_assert(std::is_same_v<Tracker::Cell, std::pair<int, int>>);

    const Cell sA{ 0, 0 };
    const Cell sB{ 0, 1 };
    const Cell sC{ 1, 0 };
    const Cell sD{ 1, 1 };
    const Cell sE{ 2, 2 };

    PtrChunk makeChunk(int value)
    {
        return std::make_shared<int>(value);
    }

    // Wants `cells` and finishes every returned build, so the cells end up shown.
    void showCells(Tracker& tracker, const std::vector<Cell>& cells)
    {
        int value = 0;
        for (const Cell& cell : tracker.setWanted(cells))
            tracker.finish(cell, makeChunk(++value));
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, newTrackerIsEmpty)
    {
        Tracker tracker(4);
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.pendingCount(), 0u);
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_FALSE(tracker.isPending(sA));
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, setWantedReturnsUnknownCellsInOrderAndMarksThemPending)
    {
        Tracker tracker(4);
        const std::vector<Cell> wanted{ { -1, 3 }, sA, sB, sC };
        const std::vector<Cell> result = tracker.setWanted(wanted);
        EXPECT_EQ(result, wanted);
        for (const Cell& cell : wanted)
            EXPECT_TRUE(tracker.isPending(cell));
        EXPECT_FALSE(tracker.isPending(sD));
        EXPECT_EQ(tracker.pendingCount(), 4u);
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, setWantedWithNothingReturnsNothing)
    {
        Tracker tracker(4);
        EXPECT_TRUE(tracker.setWanted({}).empty());
        EXPECT_EQ(tracker.pendingCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, repeatedSetWantedDoesNotReturnPendingCellsAgain)
    {
        Tracker tracker(4);
        EXPECT_EQ(tracker.setWanted({ sA, sB }), (std::vector<Cell>{ sA, sB }));
        EXPECT_TRUE(tracker.setWanted({ sA, sB }).empty());
        EXPECT_EQ(tracker.setWanted({ sA, sB, sC }), (std::vector<Cell>{ sC }));
        EXPECT_EQ(tracker.pendingCount(), 3u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, setWantedDoesNotReturnShownCells)
    {
        Tracker tracker(4);
        showCells(tracker, { sA, sB });
        EXPECT_EQ(tracker.setWanted({ sA, sB, sC }), (std::vector<Cell>{ sC }));
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, finishOfWantedPendingCellShowsIt)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA, sB });
        const PtrChunk chunk = makeChunk(7);
        tracker.finish(sA, chunk);

        EXPECT_FALSE(tracker.isPending(sA));
        EXPECT_TRUE(tracker.isPending(sB));
        EXPECT_EQ(tracker.pendingCount(), 1u);
        ASSERT_EQ(tracker.shown().size(), 1u);
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), chunk);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, finishOfUnwantedPendingCellCachesIt)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA });
        tracker.setWanted({ sB });
        EXPECT_TRUE(tracker.isPending(sA));

        const PtrChunk chunk = makeChunk(7);
        tracker.finish(sA, chunk);
        EXPECT_FALSE(tracker.isPending(sA));
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 1u);

        // Wanting it again shows the cached chunk without building it again.
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{});
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), chunk);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, pendingCellsStayPendingWhenNoLongerWanted)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA, sB });
        tracker.setWanted({});
        EXPECT_TRUE(tracker.isPending(sA));
        EXPECT_TRUE(tracker.isPending(sB));
        EXPECT_EQ(tracker.pendingCount(), 2u);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, pendingCellUnwantedThenWantedAgainIsNotReturnedAndIsShownOnFinish)
    {
        Tracker tracker(4);
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });
        EXPECT_TRUE(tracker.setWanted({}).empty());
        EXPECT_TRUE(tracker.setWanted({ sA }).empty());
        EXPECT_TRUE(tracker.isPending(sA));
        EXPECT_EQ(tracker.pendingCount(), 1u);

        const PtrChunk chunk = makeChunk(3);
        tracker.finish(sA, chunk);
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), chunk);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, shownCellNoLongerWantedMovesToCacheAndComesBack)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA, sB });
        const PtrChunk chunkA = makeChunk(1);
        const PtrChunk chunkB = makeChunk(2);
        tracker.finish(sA, chunkA);
        tracker.finish(sB, chunkB);

        EXPECT_TRUE(tracker.setWanted({ sB }).empty());
        ASSERT_EQ(tracker.shown().size(), 1u);
        EXPECT_EQ(tracker.shown().at(sB), chunkB);
        EXPECT_EQ(tracker.shown().count(sA), 0u);
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_FALSE(tracker.isPending(sA));

        EXPECT_TRUE(tracker.setWanted({ sA, sB }).empty());
        ASSERT_EQ(tracker.shown().size(), 2u);
        EXPECT_EQ(tracker.shown().at(sA), chunkA);
        EXPECT_EQ(tracker.shown().at(sB), chunkB);
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, cacheTrimsOldestFirstOnSetWanted)
    {
        Tracker tracker(2);
        // Show A, B, C and D one after another, so A, then B, then C enter the cache in that order.
        showCells(tracker, { sA });
        showCells(tracker, { sB });
        EXPECT_EQ(tracker.cachedCount(), 1u);
        showCells(tracker, { sC });
        EXPECT_EQ(tracker.cachedCount(), 2u);
        showCells(tracker, { sD });
        // A was the oldest and is dropped; B and C remain.
        EXPECT_EQ(tracker.cachedCount(), 2u);

        EXPECT_EQ(tracker.setWanted({ sA, sB, sC, sD }), std::vector<Cell>{ sA });
        EXPECT_TRUE(tracker.isPending(sA));
        EXPECT_EQ(tracker.shown().size(), 3u);
        EXPECT_EQ(tracker.shown().count(sB), 1u);
        EXPECT_EQ(tracker.shown().count(sC), 1u);
        EXPECT_EQ(tracker.shown().count(sD), 1u);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, cacheReturnToShownRemovesItFromTrimOrder)
    {
        Tracker tracker(2);
        showCells(tracker, { sA });
        showCells(tracker, { sB }); // cache: A
        showCells(tracker, { sC }); // cache: A, B
        tracker.setWanted({ sA, sC }); // A back to shown; cache: B
        EXPECT_EQ(tracker.cachedCount(), 1u);
        showCells(tracker, { sD }); // A and C to cache; cache: B, then A and C; trimmed to 2, so B is dropped
        EXPECT_EQ(tracker.cachedCount(), 2u);

        EXPECT_EQ(tracker.setWanted({ sA, sB, sC }), std::vector<Cell>{ sB });
        EXPECT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().count(sC), 1u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, cacheTrimsOldestFirstOnFinish)
    {
        Tracker tracker(1);
        showCells(tracker, { sA });
        tracker.setWanted({ sB }); // A cached; B pending
        EXPECT_EQ(tracker.cachedCount(), 1u);
        tracker.setWanted({}); // B unwanted, still pending
        EXPECT_EQ(tracker.cachedCount(), 1u);
        tracker.finish(sB, makeChunk(2)); // B is the newest entry; A is trimmed
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_EQ(tracker.pendingCount(), 0u);

        EXPECT_EQ(tracker.setWanted({ sA, sB }), std::vector<Cell>{ sA });
        EXPECT_EQ(tracker.shown().count(sB), 1u);
        EXPECT_EQ(tracker.cachedCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, cacheTrimDropsCellsLeavingInTheSameCall)
    {
        Tracker tracker(1);
        showCells(tracker, { sA, sB, sC });
        tracker.setWanted({});
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 1u);
        // Two of the three are unknown again, one is cached.
        EXPECT_EQ(tracker.setWanted({ sA, sB, sC }).size(), 2u);
        EXPECT_EQ(tracker.shown().size(), 1u);
        EXPECT_EQ(tracker.pendingCount(), 2u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, zeroCapacityKeepsNothing)
    {
        Tracker tracker(0);
        showCells(tracker, { sA });
        tracker.setWanted({});
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });

        // An unwanted build that finishes is dropped too.
        tracker.setWanted({});
        tracker.finish(sA, makeChunk(1));
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, zeroCapacityStillShowsWantedBuilds)
    {
        Tracker tracker(0);
        tracker.setWanted({ sA });
        const PtrChunk chunk = makeChunk(1);
        tracker.finish(sA, chunk);
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), chunk);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, emptyChunkIsShownAndNotRebuilt)
    {
        IntTracker tracker(4);
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });
        tracker.finish(sA, 0);
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), 0);
        EXPECT_TRUE(tracker.setWanted({ sA }).empty());

        // Through the cache as well.
        EXPECT_TRUE(tracker.setWanted({}).empty());
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_TRUE(tracker.setWanted({ sA }).empty());
        EXPECT_EQ(tracker.shown().count(sA), 1u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, emptyPtrChunkIsCachedAndNotRebuilt)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA });
        tracker.setWanted({});
        tracker.finish(sA, PtrChunk());
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_TRUE(tracker.setWanted({ sA }).empty());
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), nullptr);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, finishOfUnknownCellIsIgnored)
    {
        Tracker tracker(4);
        tracker.finish(sA, makeChunk(1));
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, finishOfShownCellIsIgnored)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA });
        const PtrChunk first = makeChunk(1);
        tracker.finish(sA, first);
        tracker.finish(sA, makeChunk(2));
        ASSERT_EQ(tracker.shown().size(), 1u);
        EXPECT_EQ(tracker.shown().at(sA), first);
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, finishOfCachedCellIsIgnored)
    {
        Tracker tracker(4);
        tracker.setWanted({ sA });
        const PtrChunk first = makeChunk(1);
        tracker.finish(sA, first);
        tracker.setWanted({});
        tracker.finish(sA, makeChunk(2));
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
        tracker.setWanted({ sA });
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), first);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, clearForgetsEveryCell)
    {
        Tracker tracker(4);
        showCells(tracker, { sA, sB });
        tracker.setWanted({ sB, sC }); // A cached, B shown, C pending
        EXPECT_EQ(tracker.cachedCount(), 1u);
        EXPECT_EQ(tracker.shown().size(), 1u);
        EXPECT_EQ(tracker.pendingCount(), 1u);

        tracker.clear();
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 0u);
        EXPECT_EQ(tracker.pendingCount(), 0u);
        EXPECT_FALSE(tracker.isPending(sC));

        // The build of C that was running before clear finishes now and is ignored.
        tracker.finish(sC, makeChunk(9));
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.cachedCount(), 0u);

        // Every cell is unknown again.
        EXPECT_EQ(tracker.setWanted({ sA, sB, sC }), (std::vector<Cell>{ sA, sB, sC }));
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, oldBuildFinishingAfterClearAndNewRequestIsTakenOnce)
    {
        // After clear, the same cell can be pending again; the spec cannot tell an old build from a new one, so the
        // first finish for a pending cell is the one taken and any later one is ignored.
        Tracker tracker(4);
        tracker.setWanted({ sA });
        tracker.clear();
        tracker.finish(sA, makeChunk(1)); // ignored: not pending
        EXPECT_TRUE(tracker.shown().empty());
        EXPECT_EQ(tracker.setWanted({ sA }), std::vector<Cell>{ sA });
        const PtrChunk chunk = makeChunk(2);
        tracker.finish(sA, chunk);
        tracker.finish(sA, makeChunk(3)); // ignored: shown
        ASSERT_EQ(tracker.shown().count(sA), 1u);
        EXPECT_EQ(tracker.shown().at(sA), chunk);
    }

    TEST(MWRenderGroundcoverChunkTrackerTest, countsFollowEachState)
    {
        Tracker tracker(8);
        tracker.setWanted({ sA, sB, sC, sD, sE });
        EXPECT_EQ(tracker.pendingCount(), 5u);
        tracker.finish(sA, makeChunk(1));
        tracker.finish(sB, makeChunk(2));
        EXPECT_EQ(tracker.pendingCount(), 3u);
        EXPECT_EQ(tracker.shown().size(), 2u);

        tracker.setWanted({ sB, sC });
        EXPECT_EQ(tracker.pendingCount(), 3u);
        EXPECT_EQ(tracker.shown().size(), 1u);
        EXPECT_EQ(tracker.cachedCount(), 1u);

        tracker.finish(sC, makeChunk(3)); // wanted: shown
        tracker.finish(sD, makeChunk(4)); // unwanted: cached
        tracker.finish(sE, makeChunk(5)); // unwanted: cached
        EXPECT_EQ(tracker.pendingCount(), 0u);
        EXPECT_EQ(tracker.shown().size(), 2u);
        EXPECT_EQ(tracker.cachedCount(), 3u);
    }
}
