#include <components/misc/buildqueue.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <tuple>
#include <vector>

namespace
{
    // Task 0060: tests for the work queue that spreads building a new cell's objects over several frames.
    // The queue knows nothing about the game: groups, keys and payloads are plain pointers and ints here.
    using Queue = Misc::BuildQueue<int>;

    constexpr Misc::BuildPhase sObject = Misc::BuildPhase::Object;
    constexpr Misc::BuildPhase sActor = Misc::BuildPhase::Actor;
    constexpr Misc::BuildPhase sNavigator = Misc::BuildPhase::Navigator;

    // A budget so large that a run never stops because of time.
    constexpr double sHugeBudget = 1.0e9;

    Queue::Item makeItem(
        const void* group, const void* key, int rank, Misc::BuildPhase phase, float distance, int payload)
    {
        Queue::Item result;
        result.mGroup = group;
        result.mKey = key;
        result.mGroupRank = rank;
        result.mPhase = phase;
        result.mDistance = distance;
        result.mPayload = payload;
        return result;
    }

    // Builds everything that is queued in one run and returns the payloads in the order they were built.
    std::vector<int> buildAll(Queue& queue)
    {
        std::vector<int> result;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; }, [&result](const Queue::Item& entry) { result.push_back(entry.mPayload); });
        return result;
    }

    TEST(MiscBuildQueueTest, EmptyQueueBuildsNothing)
    {
        Queue queue;
        int key = 0;
        int group = 0;
        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.size(), 0u);
        EXPECT_FALSE(queue.contains(&key));
        EXPECT_EQ(queue.pendingInGroup(&group), 0u);

        std::size_t clockCalls = 0;
        std::size_t buildCalls = 0;
        const std::size_t built = queue.run(
            5.0, 0,
            [&clockCalls]() {
                ++clockCalls;
                return 0.0;
            },
            [&buildCalls](const Queue::Item&) { ++buildCalls; });
        EXPECT_EQ(built, 0u);
        EXPECT_EQ(buildCalls, 0u);
        EXPECT_LE(clockCalls, 2u);
    }

    TEST(MiscBuildQueueTest, OrderIsRankThenPhaseThenDistanceThenInsertion)
    {
        Queue queue;
        int group = 0;
        int keys[7] = {};
        queue.add(makeItem(&group, &keys[0], 1, sObject, 0.f, 1));
        queue.add(makeItem(&group, &keys[1], 0, sNavigator, 0.f, 2));
        queue.add(makeItem(&group, &keys[2], 0, sObject, 5.f, 3));
        queue.add(makeItem(&group, &keys[3], 0, sObject, 1.f, 4));
        queue.add(makeItem(&group, &keys[4], 0, sActor, 0.f, 5));
        queue.add(makeItem(&group, &keys[5], 0, sObject, 1.f, 6)); // exact tie with payload 4, added later
        queue.add(makeItem(&group, &keys[6], 2, sObject, 0.f, 7));

        // Rank 0 comes first (Object by distance, then Actor, then Navigator), then rank 1, then rank 2.
        const std::vector<int> expected = { 4, 6, 3, 5, 2, 1, 7 };
        EXPECT_EQ(buildAll(queue), expected);
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, WholeNearCellIsBuiltBeforeFarCellObjects)
    {
        Queue queue;
        int nearGroup = 0;
        int farGroup = 0;
        int keys[3] = {};
        queue.add(makeItem(&farGroup, &keys[0], 1, sObject, 0.f, 1));
        queue.add(makeItem(&nearGroup, &keys[1], 0, sNavigator, 100.f, 2));
        queue.add(makeItem(&nearGroup, &keys[2], 0, sObject, 100.f, 3));

        // Even the near cell's Navigator item goes before the far cell's Object item.
        const std::vector<int> expected = { 3, 2, 1 };
        EXPECT_EQ(buildAll(queue), expected);
    }

    TEST(MiscBuildQueueTest, EqualItemsKeepInsertionOrderAndRepeatExactly)
    {
        int group = 0;
        int keys[6] = {};
        std::vector<std::vector<int>> sequences;
        for (int pass = 0; pass < 2; ++pass)
        {
            Queue queue;
            for (int i = 0; i < 6; ++i)
                queue.add(makeItem(&group, &keys[i], 0, sObject, 3.f, 10 + i));
            sequences.push_back(buildAll(queue));
        }
        const std::vector<int> expected = { 10, 11, 12, 13, 14, 15 };
        EXPECT_EQ(sequences[0], expected);
        EXPECT_EQ(sequences[1], sequences[0]);
    }

    TEST(MiscBuildQueueTest, ItemsAddedAfterPartialRunMergeIntoRemainingOrder)
    {
        Queue queue;
        int group = 0;
        int keys[5] = {};
        queue.add(makeItem(&group, &keys[0], 0, sObject, 1.f, 1));
        queue.add(makeItem(&group, &keys[1], 0, sObject, 2.f, 2));
        queue.add(makeItem(&group, &keys[2], 0, sObject, 3.f, 3));

        std::vector<int> built;
        const auto record = [&built](const Queue::Item& entry) { built.push_back(entry.mPayload); };

        // Zero budget: exactly one item (payload 1) is built.
        EXPECT_EQ(queue.run(0.0, 0, []() { return 0.0; }, record), 1u);

        // Two new items: one nearer than everything left, one between payload 2 and 3.
        queue.add(makeItem(&group, &keys[3], 0, sObject, 0.f, 4));
        queue.add(makeItem(&group, &keys[4], 0, sObject, 2.5f, 5));
        EXPECT_EQ(queue.size(), 4u);

        queue.run(sHugeBudget, 0, []() { return 0.0; }, record);
        const std::vector<int> expected = { 1, 4, 2, 5, 3 };
        EXPECT_EQ(built, expected);
    }

    TEST(MiscBuildQueueTest, BudgetStopsRunWhenElapsedTimeReachesIt)
    {
        Queue queue;
        int group = 0;
        int keys[10] = {};
        for (int i = 0; i < 10; ++i)
            queue.add(makeItem(&group, &keys[i], 0, sObject, static_cast<float>(i), i));

        // Every built item costs 1 ms. The clock is read before the 2nd, 3rd, 4th and 5th item:
        // it shows 1, 2, 3 (below 3.5, so those items are built) and 4 (not below 3.5, so the run stops).
        double now = 0.0;
        std::vector<int> built;
        const std::size_t count = queue.run(
            3.5, 0, [&now]() { return now; },
            [&now, &built](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                now += 1.0;
            });
        EXPECT_EQ(count, 4u);
        EXPECT_EQ(built.size(), 4u);
        EXPECT_EQ(queue.size(), 6u);
    }

    TEST(MiscBuildQueueTest, ZeroBudgetStillBuildsExactlyOneItemPerRun)
    {
        Queue queue;
        int group = 0;
        int keys[4] = {};
        for (int i = 0; i < 4; ++i)
            queue.add(makeItem(&group, &keys[i], 0, sObject, static_cast<float>(i), i));

        double now = 0.0;
        std::vector<int> built;
        const auto clock = [&now]() { return now; };
        const auto build = [&now, &built](const Queue::Item& entry) {
            built.push_back(entry.mPayload);
            now += 1.0;
        };

        // Four runs with no time at all: each one makes progress by one item, and nothing is built twice.
        for (std::size_t i = 0; i < 4; ++i)
        {
            EXPECT_EQ(queue.run(0.0, 0, clock, build), 1u);
        }
        EXPECT_TRUE(queue.empty());
        const std::vector<int> expected = { 0, 1, 2, 3 };
        EXPECT_EQ(built, expected);
        EXPECT_EQ(queue.run(0.0, 0, clock, build), 0u);
    }

    TEST(MiscBuildQueueTest, MinItemsAreBuiltEvenWithZeroBudget)
    {
        Queue queue;
        int group = 0;
        int keys[8] = {};
        for (int i = 0; i < 8; ++i)
            queue.add(makeItem(&group, &keys[i], 0, sObject, static_cast<float>(i), i));

        double now = 0.0;
        const auto clock = [&now]() { return now; };
        const auto build = [&now](const Queue::Item&) { now += 1.0; };

        EXPECT_EQ(queue.run(0.0, 5, clock, build), 5u);
        EXPECT_EQ(queue.size(), 3u);

        // Only three are left, so only three can be built even though five were asked for.
        EXPECT_EQ(queue.run(0.0, 5, clock, build), 3u);
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, RunInSeveralFramesBuildsEveryItemExactlyOnce)
    {
        Queue queue;
        int group = 0;
        int keys[25] = {};
        for (int i = 0; i < 25; ++i)
            queue.add(makeItem(&group, &keys[i], i % 3, sObject, static_cast<float>(i % 5), i));

        double now = 0.0;
        std::vector<int> built;
        const auto clock = [&now]() { return now; };
        const auto build = [&now, &built](const Queue::Item& entry) {
            built.push_back(entry.mPayload);
            now += 1.0;
        };

        std::size_t runs = 0;
        while (!queue.empty() && runs < 100)
        {
            // The clock keeps running between runs; every run measures its own start.
            queue.run(2.5, 1, clock, build);
            ++runs;
        }
        EXPECT_TRUE(queue.empty());
        EXPECT_GT(runs, 1u);
        ASSERT_EQ(built.size(), 25u);

        std::vector<int> sorted = built;
        std::sort(sorted.begin(), sorted.end());
        for (int i = 0; i < 25; ++i)
        {
            EXPECT_EQ(sorted[static_cast<std::size_t>(i)], i);
        }
    }

    TEST(MiscBuildQueueTest, LargeBudgetBuildsEverythingInOneRun)
    {
        Queue queue;
        int group = 0;
        int keys[50] = {};
        for (int i = 0; i < 50; ++i)
            queue.add(makeItem(&group, &keys[i], 0, sObject, static_cast<float>(i), i));

        double now = 0.0;
        const std::size_t count = queue.run(
            sHugeBudget, 0, [&now]() { return now; }, [&now](const Queue::Item&) { now += 1.0; });
        EXPECT_EQ(count, 50u);
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, ItemIsRemovedBeforeBuildIsCalled)
    {
        Queue queue;
        int group = 0;
        int keyA = 0;
        int keyB = 0;
        queue.add(makeItem(&group, &keyA, 0, sObject, 0.f, 1));
        queue.add(makeItem(&group, &keyB, 0, sObject, 1.f, 2));

        std::vector<std::size_t> sizesSeen;
        std::vector<bool> containsSeen;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; },
            [&queue, &sizesSeen, &containsSeen](const Queue::Item& entry) {
                sizesSeen.push_back(queue.size());
                containsSeen.push_back(queue.contains(entry.mKey));
            });

        // While payload 1 is built, only payload 2 is left; while payload 2 is built, nothing is left.
        ASSERT_EQ(sizesSeen.size(), 2u);
        EXPECT_EQ(sizesSeen[0], 1u);
        EXPECT_EQ(sizesSeen[1], 0u);
        EXPECT_FALSE(containsSeen[0]);
        EXPECT_FALSE(containsSeen[1]);
    }

    TEST(MiscBuildQueueTest, SameKeyInTwoPhasesIsTwoItems)
    {
        Queue queue;
        int group = 0;
        int key = 0;
        queue.add(makeItem(&group, &key, 0, sObject, 0.f, 1));
        queue.add(makeItem(&group, &key, 0, sActor, 0.f, 2));
        EXPECT_EQ(queue.size(), 2u);
        EXPECT_TRUE(queue.contains(&key));

        std::vector<bool> containsSeen;
        std::vector<int> built;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; },
            [&queue, &containsSeen, &built](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                containsSeen.push_back(queue.contains(entry.mKey));
            });

        // The key stays queued while its Actor item is waiting, and is gone after the last one.
        const std::vector<int> expected = { 1, 2 };
        EXPECT_EQ(built, expected);
        ASSERT_EQ(containsSeen.size(), 2u);
        EXPECT_TRUE(containsSeen[0]);
        EXPECT_FALSE(containsSeen[1]);
        EXPECT_FALSE(queue.contains(&key));
    }

    TEST(MiscBuildQueueTest, CancelKeyRemovesEveryPhaseOfTheKey)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[3] = {};
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&groupA, &keys[0], 0, sActor, 0.f, 2));
        queue.add(makeItem(&groupA, &keys[0], 0, sNavigator, 0.f, 3));
        queue.add(makeItem(&groupA, &keys[1], 0, sObject, 1.f, 4));
        queue.add(makeItem(&groupB, &keys[2], 1, sObject, 0.f, 5));
        EXPECT_EQ(queue.pendingInGroup(&groupA), 4u);

        EXPECT_EQ(queue.cancelKey(&keys[0]), 3u);
        EXPECT_FALSE(queue.contains(&keys[0]));
        EXPECT_TRUE(queue.contains(&keys[1]));
        EXPECT_TRUE(queue.contains(&keys[2]));
        EXPECT_EQ(queue.size(), 2u);
        EXPECT_EQ(queue.pendingInGroup(&groupA), 1u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 1u);

        const std::vector<int> expected = { 4, 5 };
        EXPECT_EQ(buildAll(queue), expected);
    }

    TEST(MiscBuildQueueTest, CancelOfUnknownKeyOrGroupReturnsZero)
    {
        Queue queue;
        int group = 0;
        int key = 0;
        int otherKey = 0;
        int otherGroup = 0;
        queue.add(makeItem(&group, &key, 0, sObject, 0.f, 1));

        EXPECT_EQ(queue.cancelKey(&otherKey), 0u);
        EXPECT_EQ(queue.cancelGroup(&otherGroup), 0u);
        EXPECT_EQ(queue.size(), 1u);
    }

    TEST(MiscBuildQueueTest, CancelGroupRemovesOnlyThatGroup)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[4] = {};
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&groupB, &keys[1], 0, sObject, 1.f, 2));
        queue.add(makeItem(&groupA, &keys[2], 0, sActor, 0.f, 3));
        queue.add(makeItem(&groupB, &keys[3], 0, sNavigator, 0.f, 4));

        EXPECT_EQ(queue.cancelGroup(&groupA), 2u);
        EXPECT_EQ(queue.pendingInGroup(&groupA), 0u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 2u);
        EXPECT_FALSE(queue.contains(&keys[0]));
        EXPECT_FALSE(queue.contains(&keys[2]));
        EXPECT_TRUE(queue.contains(&keys[1]));

        const std::vector<int> expected = { 2, 4 };
        EXPECT_EQ(buildAll(queue), expected);
    }

    TEST(MiscBuildQueueTest, ItemCancelledByTheCallbackIsNeverBuilt)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[4] = {};
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&groupA, &keys[1], 0, sObject, 1.f, 2));
        queue.add(makeItem(&groupB, &keys[2], 1, sObject, 0.f, 3));
        queue.add(makeItem(&groupB, &keys[3], 1, sObject, 1.f, 4));

        // Building payload 1 cancels the item of keys[1] and the whole of group B.
        std::vector<int> built;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; },
            [&queue, &built, &keys, &groupB](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                if (entry.mPayload == 1)
                {
                    static_cast<void>(queue.cancelKey(&keys[1]));
                    static_cast<void>(queue.cancelGroup(&groupB));
                }
            });

        const std::vector<int> expected = { 1 };
        EXPECT_EQ(built, expected);
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, PendingInGroupReachesZeroWhenLastItemIsBuilt)
    {
        Queue queue;
        int group = 0;
        int keys[3] = {};
        queue.add(makeItem(&group, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&group, &keys[1], 0, sObject, 1.f, 2));
        queue.add(makeItem(&group, &keys[2], 0, sActor, 0.f, 3));
        EXPECT_EQ(queue.pendingInGroup(&group), 3u);

        std::vector<std::size_t> pendingSeen;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; },
            [&queue, &pendingSeen, &group](const Queue::Item&) { pendingSeen.push_back(queue.pendingInGroup(&group)); });

        // After each build the number still pending drops by one; the last build sees zero.
        const std::vector<std::size_t> expected = { 2u, 1u, 0u };
        EXPECT_EQ(pendingSeen, expected);
        EXPECT_EQ(queue.pendingInGroup(&group), 0u);
    }

    TEST(MiscBuildQueueTest, ReplaceKeyMovesItemsToNewKeyAndPayload)
    {
        Queue queue;
        int group = 0;
        int oldKey = 0;
        int newKey = 0;
        int otherKey = 0;
        int unknownKey = 0;
        queue.add(makeItem(&group, &otherKey, 0, sObject, 1.f, 1));
        queue.add(makeItem(&group, &oldKey, 0, sObject, 2.f, 10));
        queue.add(makeItem(&group, &oldKey, 0, sActor, 0.f, 11));
        queue.add(makeItem(&group, &otherKey, 0, sActor, 5.f, 2));

        EXPECT_EQ(queue.replaceKey(&unknownKey, &newKey, 77), 0u);
        EXPECT_EQ(queue.replaceKey(&oldKey, &newKey, 99), 2u);
        EXPECT_FALSE(queue.contains(&oldKey));
        EXPECT_TRUE(queue.contains(&newKey));
        EXPECT_EQ(queue.size(), 4u);
        EXPECT_EQ(queue.pendingInGroup(&group), 4u);

        std::vector<Queue::Item> built;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; }, [&built](const Queue::Item& entry) { built.push_back(entry); });

        // The two changed items keep their place: Object by distance (1, 2), then Actor by distance (0, 5).
        ASSERT_EQ(built.size(), 4u);
        EXPECT_EQ(built[0].mPayload, 1);
        EXPECT_EQ(built[1].mPayload, 99);
        EXPECT_EQ(built[1].mKey, static_cast<const void*>(&newKey));
        EXPECT_EQ(built[1].mPhase, sObject);
        EXPECT_EQ(built[1].mDistance, 2.f);
        EXPECT_EQ(built[1].mGroup, static_cast<const void*>(&group));
        EXPECT_EQ(built[2].mPayload, 99);
        EXPECT_EQ(built[2].mKey, static_cast<const void*>(&newKey));
        EXPECT_EQ(built[2].mPhase, sActor);
        EXPECT_EQ(built[2].mDistance, 0.f);
        EXPECT_EQ(built[3].mPayload, 2);
    }

    TEST(MiscBuildQueueTest, CancelKeyAfterReplaceKeyRemovesTheMovedItems)
    {
        Queue queue;
        int group = 0;
        int oldKey = 0;
        int newKey = 0;
        queue.add(makeItem(&group, &oldKey, 0, sObject, 0.f, 1));
        queue.add(makeItem(&group, &oldKey, 0, sNavigator, 0.f, 2));

        EXPECT_EQ(queue.replaceKey(&oldKey, &newKey, 5), 2u);
        EXPECT_EQ(queue.cancelKey(&oldKey), 0u);
        EXPECT_EQ(queue.cancelKey(&newKey), 2u);
        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.pendingInGroup(&group), 0u);
    }

    TEST(MiscBuildQueueTest, ItemAddedByTheCallbackIsBuiltInTheSameRun)
    {
        Queue queue;
        int group = 0;
        int keys[3] = {};
        queue.add(makeItem(&group, &keys[0], 1, sObject, 0.f, 1));
        queue.add(makeItem(&group, &keys[1], 1, sObject, 1.f, 2));

        // Building payload 1 adds a nearer (rank 0) item. It goes to the front of what is left.
        bool added = false;
        std::vector<int> built;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; },
            [&queue, &built, &added, &group, &keys](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                if (entry.mPayload == 1 && !added)
                {
                    added = true;
                    queue.add(makeItem(&group, &keys[2], 0, sObject, 50.f, 100));
                }
            });

        const std::vector<int> expected = { 1, 100, 2 };
        EXPECT_EQ(built, expected);
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, RunGroupBuildsOnlyThatGroupInOrder)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[5] = {};
        queue.add(makeItem(&groupA, &keys[0], 1, sActor, 0.f, 1));
        queue.add(makeItem(&groupB, &keys[1], 0, sObject, 0.f, 2));
        queue.add(makeItem(&groupA, &keys[2], 1, sObject, 3.f, 3));
        queue.add(makeItem(&groupA, &keys[3], 1, sObject, 1.f, 4));
        queue.add(makeItem(&groupB, &keys[4], 0, sNavigator, 0.f, 5));

        std::vector<int> built;
        const std::size_t count
            = queue.runGroup(&groupA, [&built](const Queue::Item& entry) { built.push_back(entry.mPayload); });

        const std::vector<int> expected = { 4, 3, 1 };
        EXPECT_EQ(count, 3u);
        EXPECT_EQ(built, expected);
        EXPECT_EQ(queue.pendingInGroup(&groupA), 0u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 2u);
        EXPECT_EQ(queue.size(), 2u);

        const std::vector<int> rest = { 2, 5 };
        EXPECT_EQ(buildAll(queue), rest);
    }

    TEST(MiscBuildQueueTest, RunGroupOfUnknownGroupBuildsNothing)
    {
        Queue queue;
        int group = 0;
        int otherGroup = 0;
        int key = 0;
        queue.add(makeItem(&group, &key, 0, sObject, 0.f, 1));

        std::size_t buildCalls = 0;
        EXPECT_EQ(queue.runGroup(&otherGroup, [&buildCalls](const Queue::Item&) { ++buildCalls; }), 0u);
        EXPECT_EQ(buildCalls, 0u);
        EXPECT_EQ(queue.size(), 1u);
    }

    TEST(MiscBuildQueueTest, RunGroupIncludesItemsAddedToTheGroupDuringTheRun)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[4] = {};
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&groupA, &keys[1], 0, sObject, 1.f, 2));
        queue.add(makeItem(&groupB, &keys[2], 0, sObject, 0.f, 9));

        bool added = false;
        std::vector<int> built;
        const std::size_t count = queue.runGroup(
            &groupA, [&queue, &built, &added, &groupA, &keys](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                if (entry.mPayload == 1 && !added)
                {
                    added = true;
                    queue.add(makeItem(&groupA, &keys[3], 0, sObject, 5.f, 3));
                }
            });

        const std::vector<int> expected = { 1, 2, 3 };
        EXPECT_EQ(count, 3u);
        EXPECT_EQ(built, expected);
        EXPECT_EQ(queue.pendingInGroup(&groupA), 0u);
        EXPECT_EQ(queue.size(), 1u);
    }

    TEST(MiscBuildQueueTest, ClearDropsEverythingAndQueueCanBeReused)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[3] = {};
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 1));
        queue.add(makeItem(&groupA, &keys[1], 0, sActor, 0.f, 2));
        queue.add(makeItem(&groupB, &keys[2], 1, sObject, 0.f, 3));

        queue.clear();
        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.size(), 0u);
        EXPECT_EQ(queue.pendingInGroup(&groupA), 0u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 0u);
        EXPECT_FALSE(queue.contains(&keys[0]));
        EXPECT_TRUE(buildAll(queue).empty());

        // Add again after clear, build, and add again after the queue was drained.
        queue.add(makeItem(&groupA, &keys[0], 0, sObject, 0.f, 4));
        EXPECT_EQ(queue.pendingInGroup(&groupA), 1u);
        EXPECT_EQ(buildAll(queue), std::vector<int>{ 4 });
        EXPECT_TRUE(queue.empty());

        queue.add(makeItem(&groupB, &keys[2], 0, sObject, 0.f, 5));
        EXPECT_EQ(queue.size(), 1u);
        EXPECT_EQ(buildAll(queue), std::vector<int>{ 5 });
    }

    TEST(MiscBuildQueueTest, ManyItemsAreBuiltInFullySortedOrder)
    {
        constexpr int itemCount = 2000;
        constexpr int groupCount = 9;

        Queue queue;
        int groups[groupCount] = {};
        std::vector<int> keys(itemCount, 0);
        std::vector<Queue::Item> expectedItems;
        for (int i = 0; i < itemCount; ++i)
        {
            const int groupIndex = (i * 5) % groupCount;
            // Groups 0..2 are rank 0, 3..5 are rank 1, 6..8 are rank 2, so ranks are shared between groups.
            const int rank = groupIndex / 3;
            const Misc::BuildPhase phase = static_cast<Misc::BuildPhase>((i * 7) % 3);
            const float distance = static_cast<float>((i * 13) % 17);
            const Queue::Item entry
                = makeItem(&groups[groupIndex], &keys[static_cast<std::size_t>(i)], rank, phase, distance, i);
            queue.add(entry);
            expectedItems.push_back(entry);
        }
        EXPECT_EQ(queue.size(), static_cast<std::size_t>(itemCount));

        // The expected order is worked out independently: a stable sort keeps insertion order for ties.
        std::stable_sort(expectedItems.begin(), expectedItems.end(), [](const Queue::Item& lhs, const Queue::Item& rhs) {
            return std::make_tuple(lhs.mGroupRank, static_cast<int>(lhs.mPhase), lhs.mDistance)
                < std::make_tuple(rhs.mGroupRank, static_cast<int>(rhs.mPhase), rhs.mDistance);
        });

        const std::vector<int> built = buildAll(queue);
        ASSERT_EQ(built.size(), expectedItems.size());
        for (std::size_t i = 0; i < built.size(); ++i)
        {
            ASSERT_EQ(built[i], expectedItems[i].mPayload) << "position " << i;
        }
        EXPECT_TRUE(queue.empty());
    }

    // The order used by the queue, worked out independently of it.
    bool buildsBefore(const Queue::Item& lhs, const Queue::Item& rhs)
    {
        return std::make_tuple(lhs.mGroupRank, static_cast<int>(lhs.mPhase), lhs.mDistance)
            < std::make_tuple(rhs.mGroupRank, static_cast<int>(rhs.mPhase), rhs.mDistance);
    }

    // Payloads of the items that belong (or do not belong) to the group, in the order the queue must build them.
    std::vector<int> expectedOrder(std::vector<Queue::Item> items, const void* group, bool inGroup)
    {
        std::vector<Queue::Item> chosen;
        for (const Queue::Item& entry : items)
        {
            if ((entry.mGroup == group) == inGroup)
                chosen.push_back(entry);
        }
        std::stable_sort(chosen.begin(), chosen.end(), buildsBefore);
        std::vector<int> result;
        for (const Queue::Item& entry : chosen)
            result.push_back(entry.mPayload);
        return result;
    }

    TEST(MiscBuildQueueTest, ReplaceKeyAndGroupMovesItemsToNewKeyAndGroup)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int oldKey = 0;
        int newKey = 0;
        int otherKey = 0;
        int unknownKey = 0;
        queue.add(makeItem(&groupA, &otherKey, 0, sObject, 1.f, 1));
        queue.add(makeItem(&groupA, &oldKey, 0, sObject, 2.f, 10));
        queue.add(makeItem(&groupA, &oldKey, 0, sActor, 0.f, 11));
        EXPECT_EQ(queue.pendingInGroup(&groupA), 3u);

        EXPECT_EQ(queue.replaceKeyAndGroup(&unknownKey, &newKey, &groupB, 77), 0u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 0u);

        EXPECT_EQ(queue.replaceKeyAndGroup(&oldKey, &newKey, &groupB, 99), 2u);
        EXPECT_FALSE(queue.contains(&oldKey));
        EXPECT_TRUE(queue.contains(&newKey));
        EXPECT_EQ(queue.pendingInGroup(&groupA), 1u);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 2u);
        EXPECT_EQ(queue.size(), 3u);

        std::vector<Queue::Item> built;
        queue.run(
            sHugeBudget, 0, []() { return 0.0; }, [&built](const Queue::Item& entry) { built.push_back(entry); });

        // The order does not depend on the group: Object 1, Object 2 (moved), Actor 0 (moved).
        ASSERT_EQ(built.size(), 3u);
        EXPECT_EQ(built[0].mPayload, 1);
        EXPECT_EQ(built[0].mGroup, static_cast<const void*>(&groupA));
        EXPECT_EQ(built[1].mPayload, 99);
        EXPECT_EQ(built[1].mGroup, static_cast<const void*>(&groupB));
        EXPECT_EQ(built[1].mKey, static_cast<const void*>(&newKey));
        EXPECT_EQ(built[2].mPayload, 99);
        EXPECT_EQ(built[2].mGroup, static_cast<const void*>(&groupB));
    }

    TEST(MiscBuildQueueTest, CancelGroupFollowsItemsMovedByReplaceKeyAndGroup)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int oldKey = 0;
        int newKey = 0;
        int otherKey = 0;
        queue.add(makeItem(&groupA, &otherKey, 0, sObject, 1.f, 1));
        queue.add(makeItem(&groupA, &oldKey, 0, sObject, 2.f, 10));
        queue.add(makeItem(&groupA, &oldKey, 0, sNavigator, 0.f, 11));
        EXPECT_EQ(queue.replaceKeyAndGroup(&oldKey, &newKey, &groupB, 5), 2u);

        // Cancelling the old group only removes what is still in it.
        EXPECT_EQ(queue.cancelGroup(&groupA), 1u);
        EXPECT_TRUE(queue.contains(&newKey));
        EXPECT_EQ(queue.pendingInGroup(&groupB), 2u);

        EXPECT_EQ(queue.cancelGroup(&groupB), 2u);
        EXPECT_FALSE(queue.contains(&newKey));
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, ContainsStaysCorrectAfterBuildsCancelsAndReplaceKey)
    {
        Queue queue;
        int group = 0;
        int keyA = 0;
        int keyB = 0;
        int keyC = 0;
        int keyD = 0;
        queue.add(makeItem(&group, &keyA, 0, sObject, 0.f, 1));
        queue.add(makeItem(&group, &keyA, 0, sActor, 0.f, 2));
        queue.add(makeItem(&group, &keyB, 1, sObject, 0.f, 3));
        queue.add(makeItem(&group, &keyB, 1, sActor, 0.f, 4));
        queue.add(makeItem(&group, &keyC, 2, sObject, 0.f, 5));

        // Build exactly one item: keyA's Object item. Its Actor item is still queued.
        EXPECT_EQ(queue.run(0.0, 0, []() { return 0.0; }, [](const Queue::Item&) {}), 1u);
        EXPECT_TRUE(queue.contains(&keyA));

        // Move keyB (two phases) to keyD: keyB is gone, keyD answers for both.
        EXPECT_EQ(queue.replaceKey(&keyB, &keyD, 30), 2u);
        EXPECT_FALSE(queue.contains(&keyB));
        EXPECT_TRUE(queue.contains(&keyD));

        EXPECT_EQ(queue.cancelKey(&keyC), 1u);
        EXPECT_FALSE(queue.contains(&keyC));

        // Build keyA's Actor item: keyA is gone, keyD is still queued.
        EXPECT_EQ(queue.run(0.0, 0, []() { return 0.0; }, [](const Queue::Item&) {}), 1u);
        EXPECT_FALSE(queue.contains(&keyA));
        EXPECT_TRUE(queue.contains(&keyD));

        EXPECT_EQ(queue.cancelKey(&keyD), 2u);
        EXPECT_FALSE(queue.contains(&keyD));
        EXPECT_TRUE(queue.empty());
    }

    TEST(MiscBuildQueueTest, NanDistanceIsTreatedAsLargest)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Queue queue;
        int group = 0;
        int keys[6] = {};
        queue.add(makeItem(&group, &keys[0], 0, sObject, nan, 1));
        queue.add(makeItem(&group, &keys[1], 0, sObject, 5.f, 2));
        queue.add(makeItem(&group, &keys[2], 0, sObject, 1.f, 3));
        queue.add(makeItem(&group, &keys[3], 0, sObject, nan, 4));
        queue.add(makeItem(&group, &keys[4], 0, sActor, nan, 5));
        queue.add(makeItem(&group, &keys[5], 1, sObject, 0.f, 6));

        // NaN is last within its rank and phase only; rank and phase still decide first.
        const std::vector<int> expected = { 3, 2, 1, 4, 5, 6 };
        EXPECT_EQ(buildAll(queue), expected);
    }

    TEST(MiscBuildQueueTest, RunGroupOfScatteredGroupLeavesOthersInOrder)
    {
        int groups[3] = {};
        const int ranks[3] = { 1, 0, 2 };
        int keys[18] = {};
        Queue queue;
        std::vector<Queue::Item> all;
        for (int i = 0; i < 18; ++i)
        {
            const int groupIndex = i % 3;
            const Queue::Item entry = makeItem(&groups[groupIndex], &keys[i], ranks[groupIndex],
                static_cast<Misc::BuildPhase>((i / 3) % 3), static_cast<float>((i * 5) % 4), i);
            queue.add(entry);
            all.push_back(entry);
        }

        std::vector<int> built;
        const std::size_t count
            = queue.runGroup(&groups[0], [&built](const Queue::Item& entry) { built.push_back(entry.mPayload); });
        EXPECT_EQ(count, 6u);
        EXPECT_EQ(built, expectedOrder(all, &groups[0], true));
        EXPECT_EQ(queue.pendingInGroup(&groups[0]), 0u);
        EXPECT_EQ(queue.size(), 12u);

        EXPECT_EQ(buildAll(queue), expectedOrder(all, &groups[0], false));
    }

    TEST(MiscBuildQueueTest, RunGroupWithNearerItemOfAnotherGroupAddedKeepsRestSorted)
    {
        Queue queue;
        int groupA = 0;
        int groupB = 0;
        int keys[5] = {};
        queue.add(makeItem(&groupA, &keys[0], 1, sObject, 0.f, 1));
        queue.add(makeItem(&groupA, &keys[1], 1, sObject, 1.f, 2));
        queue.add(makeItem(&groupB, &keys[2], 2, sObject, 0.f, 5));
        queue.add(makeItem(&groupB, &keys[3], 3, sObject, 0.f, 6));

        // Building payload 1 adds an item of group B with a lower rank than everything queued.
        bool added = false;
        std::vector<int> built;
        const std::size_t count = queue.runGroup(
            &groupA, [&queue, &built, &added, &groupB, &keys](const Queue::Item& entry) {
                built.push_back(entry.mPayload);
                if (entry.mPayload == 1 && !added)
                {
                    added = true;
                    queue.add(makeItem(&groupB, &keys[4], 0, sObject, 0.f, 100));
                }
            });

        const std::vector<int> expectedGroup = { 1, 2 };
        EXPECT_EQ(count, 2u);
        EXPECT_EQ(built, expectedGroup);
        EXPECT_EQ(queue.pendingInGroup(&groupB), 3u);

        const std::vector<int> expectedRest = { 100, 5, 6 };
        EXPECT_EQ(buildAll(queue), expectedRest);
    }

    TEST(MiscBuildQueueTest, LargeRunGroupLeavesTheRestInCorrectOrder)
    {
        constexpr int itemCount = 3000;
        constexpr int groupCount = 5;
        int groups[groupCount] = {};
        const int ranks[groupCount] = { 2, 0, 1, 0, 2 };
        std::vector<int> keys(itemCount, 0);
        Queue queue;
        std::vector<Queue::Item> all;
        for (int i = 0; i < itemCount; ++i)
        {
            const int groupIndex = (i * 7) % groupCount;
            const Queue::Item entry = makeItem(&groups[groupIndex], &keys[static_cast<std::size_t>(i)],
                ranks[groupIndex], static_cast<Misc::BuildPhase>((i * 11) % 3), static_cast<float>((i * 13) % 19), i);
            queue.add(entry);
            all.push_back(entry);
        }

        std::vector<int> built;
        const std::size_t count
            = queue.runGroup(&groups[2], [&built](const Queue::Item& entry) { built.push_back(entry.mPayload); });
        const std::vector<int> expectedGroup = expectedOrder(all, &groups[2], true);
        EXPECT_EQ(count, expectedGroup.size());
        EXPECT_EQ(built, expectedGroup);
        EXPECT_EQ(queue.pendingInGroup(&groups[2]), 0u);
        EXPECT_EQ(queue.size(), static_cast<std::size_t>(itemCount) - expectedGroup.size());

        EXPECT_EQ(buildAll(queue), expectedOrder(all, &groups[2], false));
    }
}
