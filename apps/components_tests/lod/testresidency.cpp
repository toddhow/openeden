#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <components/lod/residency.hpp>

namespace
{
    using namespace Lod;

    constexpr std::uint64_t mib = 1024ull * 1024;

    // An item that is wanted and not held.
    ResidencyItem wantedAbsent(std::uint64_t bytes, float priority)
    {
        ResidencyItem item;
        item.mBytes = bytes;
        item.mPriority = priority;
        item.mWanted = true;
        return item;
    }

    ResidencyItem held(std::uint64_t bytes, float priority, bool wanted)
    {
        ResidencyItem item;
        item.mBytes = bytes;
        item.mPriority = priority;
        item.mHeld = true;
        item.mWanted = wanted;
        item.mIdleFrames = wanted ? 0 : 1000;
        return item;
    }

    ResidencyPlan plan(const std::vector<ResidencyItem>& items, const ResidencyParams& params)
    {
        ResidencyPlan result;
        planResidency(items, params, result);
        return result;
    }

    bool contains(const std::vector<std::uint32_t>& list, std::uint32_t index)
    {
        return std::find(list.begin(), list.end(), index) != list.end();
    }

    TEST(LodResidency, nothing_to_do_gives_an_empty_plan)
    {
        const ResidencyPlan result = plan({}, ResidencyParams{});
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mHeldAfter, 0u);
        EXPECT_EQ(result.mBlocked, 0u);
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidency, with_no_budget_the_nearest_wanted_items_load_first_up_to_the_loads_in_flight)
    {
        ResidencyParams params;
        params.mMaxLoadsInFlight = 2;
        params.mMaxLoadsPerCall = 2;
        const std::vector<ResidencyItem> items
            = { wantedAbsent(mib, 900), wantedAbsent(mib, 100), wantedAbsent(mib, 500), wantedAbsent(mib, 300) };
        const ResidencyPlan result = plan(items, params);
        ASSERT_EQ(result.mLoad.size(), 2u);
        EXPECT_EQ(result.mLoad[0], 1u);
        EXPECT_EQ(result.mLoad[1], 3u);
        EXPECT_EQ(result.mHeldAfter, 2 * mib);
    }

    TEST(LodResidency, equal_priorities_load_in_index_order)
    {
        ResidencyParams params;
        params.mMaxLoadsInFlight = 3;
        params.mMaxLoadsPerCall = 3;
        const std::vector<ResidencyItem> items
            = { wantedAbsent(mib, 7), wantedAbsent(mib, 7), wantedAbsent(mib, 7), wantedAbsent(mib, 7) };
        const ResidencyPlan result = plan(items, params);
        ASSERT_EQ(result.mLoad.size(), 3u);
        EXPECT_EQ(result.mLoad[0], 0u);
        EXPECT_EQ(result.mLoad[1], 1u);
        EXPECT_EQ(result.mLoad[2], 2u);
    }

    TEST(LodResidency, loads_already_running_count_against_the_loads_in_flight_and_the_held_bytes)
    {
        ResidencyParams params;
        params.mMaxLoadsInFlight = 2;
        params.mMaxLoadsPerCall = 2;
        ResidencyItem running = wantedAbsent(3 * mib, 10);
        running.mLoading = true;
        const std::vector<ResidencyItem> items = { running, wantedAbsent(mib, 20), wantedAbsent(mib, 30) };
        const ResidencyPlan result = plan(items, params);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
        EXPECT_EQ(result.mHeldAfter, 4 * mib);
    }

    TEST(LodResidency, the_plan_starts_at_most_loads_per_call)
    {
        ResidencyParams params;
        params.mMaxLoadsInFlight = 8;
        params.mMaxLoadsPerCall = 1;
        const std::vector<ResidencyItem> items = { wantedAbsent(mib, 1), wantedAbsent(mib, 2), wantedAbsent(mib, 3) };
        EXPECT_EQ(plan(items, params).mLoad.size(), 1u);
    }

    TEST(LodResidency, an_item_in_cooldown_is_neither_loaded_nor_counted_as_blocked)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        ResidencyItem cooling = wantedAbsent(mib, 1);
        cooling.mCooldown = 5;
        const std::vector<ResidencyItem> items = { cooling, wantedAbsent(mib, 2) };
        const ResidencyPlan result = plan(items, params);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
        EXPECT_EQ(result.mBlocked, 0u);
    }

    TEST(LodResidency, an_unwanted_absent_item_is_never_loaded)
    {
        ResidencyItem idle;
        idle.mBytes = mib;
        EXPECT_TRUE(plan({ idle }, ResidencyParams{}).mLoad.empty());
    }

    TEST(LodResidency, a_candidate_that_fits_the_budget_is_admitted_and_one_that_does_not_is_blocked)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mMaxLoadsInFlight = 4;
        params.mMaxLoadsPerCall = 4;
        const std::vector<ResidencyItem> items
            = { held(6 * mib, 1, true), wantedAbsent(3 * mib, 2), wantedAbsent(3 * mib, 3) };
        const ResidencyPlan result = plan(items, params);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
        EXPECT_EQ(result.mBlocked, 1u);
        EXPECT_EQ(result.mHeldAfter, 9 * mib);
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidency, the_first_item_is_admitted_when_nothing_is_held_even_if_it_is_larger_than_the_budget)
    {
        ResidencyParams params;
        params.mBudgetBytes = mib;
        const ResidencyPlan result = plan({ wantedAbsent(5 * mib, 1), wantedAbsent(mib, 2) }, params);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 0u);
        EXPECT_TRUE(result.mOverBudget);
    }

    TEST(LodResidency, an_idle_item_is_freed_after_the_keep_frames_and_not_before)
    {
        ResidencyParams params;
        params.mKeepFrames = 100;
        ResidencyItem young = held(mib, 1, false);
        young.mIdleFrames = 99;
        ResidencyItem old = held(mib, 1, false);
        old.mIdleFrames = 100;
        const ResidencyPlan result = plan({ young, old }, params);
        ASSERT_EQ(result.mFree.size(), 1u);
        EXPECT_EQ(result.mFree[0], 1u);
        EXPECT_EQ(result.mHeldAfter, mib);
    }

    TEST(LodResidency, with_no_keep_frames_every_unwanted_unpinned_item_is_freed_at_once)
    {
        ResidencyItem fading = held(mib, 1, false);
        fading.mPinned = true;
        const ResidencyPlan result = plan({ held(mib, 1, false), fading, held(mib, 1, true) }, ResidencyParams{});
        ASSERT_EQ(result.mFree.size(), 1u);
        EXPECT_EQ(result.mFree[0], 0u);
        EXPECT_EQ(result.mHeldAfter, 2 * mib);
    }

    TEST(LodResidency, a_wanted_item_is_never_freed_by_idleness)
    {
        ResidencyItem odd = held(mib, 1, true);
        odd.mIdleFrames = 100000;
        EXPECT_TRUE(plan({ odd }, ResidencyParams{}).mFree.empty());
    }

    TEST(LodResidency, a_closer_candidate_frees_an_unwanted_item_to_make_room_farthest_first)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mKeepFrames = 1000; // idle items stay unless pressure needs the room
        ResidencyItem near = held(4 * mib, 100, false);
        ResidencyItem far = held(4 * mib, 900, false);
        near.mIdleFrames = 1;
        far.mIdleFrames = 1;
        const ResidencyPlan result = plan({ near, far, wantedAbsent(4 * mib, 50) }, params);
        ASSERT_EQ(result.mFree.size(), 1u);
        EXPECT_EQ(result.mFree[0], 1u);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 2u);
        EXPECT_EQ(result.mHeldAfter, 8 * mib);
    }

    TEST(LodResidency, a_wanted_replaceable_item_is_freed_for_a_clearly_closer_candidate)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib;
        params.mSwapMargin = 1.25f;
        const ResidencyPlan result = plan({ held(4 * mib, 1000, true), wantedAbsent(4 * mib, 100) }, params);
        ASSERT_EQ(result.mFree.size(), 1u);
        EXPECT_EQ(result.mFree[0], 0u);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
    }

    TEST(LodResidency, a_wanted_item_of_about_the_same_urgency_is_not_swapped_out)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib;
        params.mSwapMargin = 1.25f;
        const ResidencyPlan result = plan({ held(4 * mib, 120, true), wantedAbsent(4 * mib, 100) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mBlocked, 1u);
    }

    TEST(LodResidency, a_wanted_item_that_is_not_replaceable_is_never_freed_for_room)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib;
        ResidencyItem fixed = held(4 * mib, 5000, true);
        fixed.mReplaceable = false;
        const ResidencyPlan result = plan({ fixed, wantedAbsent(4 * mib, 1) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mBlocked, 1u);
    }

    TEST(LodResidency, a_pinned_item_is_never_freed_for_room_or_pressure)
    {
        ResidencyParams params;
        params.mBudgetBytes = 2 * mib;
        ResidencyItem pinned = held(4 * mib, 5000, false);
        pinned.mPinned = true;
        const ResidencyPlan result = plan({ pinned, wantedAbsent(mib, 1) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_TRUE(result.mOverBudget);
    }

    TEST(LodResidency, freeing_is_all_or_nothing_when_the_victims_cannot_make_room)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mKeepFrames = 1000;
        ResidencyItem small = held(mib, 900, false);
        small.mIdleFrames = 1;
        ResidencyItem wall = held(8 * mib, 1, true);
        wall.mReplaceable = false;
        // 9 MiB held, the candidate needs 5: freeing the small item (1 MiB) cannot make room.
        const ResidencyPlan result = plan({ small, wall, wantedAbsent(5 * mib, 2) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mBlocked, 1u);
    }

    TEST(LodResidency, pressure_after_the_budget_shrinks_frees_the_farthest_down_to_the_low_water_mark)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mLowWater = 0.5f;
        const std::vector<ResidencyItem> items
            = { held(4 * mib, 100, true), held(4 * mib, 300, true), held(4 * mib, 200, true) };
        const ResidencyPlan result = plan(items, params);
        // 12 MiB held, budget 10, low water 5: free the farthest (1), then the next farthest (2): 4 MiB held.
        ASSERT_EQ(result.mFree.size(), 2u);
        EXPECT_EQ(result.mFree[0], 1u);
        EXPECT_EQ(result.mFree[1], 2u);
        EXPECT_EQ(result.mHeldAfter, 4 * mib);
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidency, pressure_frees_unwanted_items_before_wanted_ones)
    {
        ResidencyParams params;
        params.mBudgetBytes = 8 * mib;
        params.mKeepFrames = 1000;
        ResidencyItem unwanted = held(4 * mib, 10, false);
        unwanted.mIdleFrames = 1;
        const ResidencyPlan result = plan({ held(4 * mib, 9000, true), unwanted, held(4 * mib, 50, true) }, params);
        ASSERT_FALSE(result.mFree.empty());
        EXPECT_EQ(result.mFree[0], 1u);
    }

    TEST(LodResidency, an_unfixable_overrun_is_reported_not_forced)
    {
        ResidencyParams params;
        params.mBudgetBytes = mib;
        ResidencyItem fixed = held(4 * mib, 1, true);
        fixed.mReplaceable = false;
        const ResidencyPlan result = plan({ fixed }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mOverBudget);
        EXPECT_EQ(result.mHeldAfter, 4 * mib);
    }

    TEST(LodResidency, a_nan_priority_is_the_least_urgent_and_the_plan_stays_deterministic)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        ResidencyParams params;
        params.mMaxLoadsInFlight = 1;
        params.mMaxLoadsPerCall = 1;
        const std::vector<ResidencyItem> items = { wantedAbsent(mib, nan), wantedAbsent(mib, 5e6f) };
        const ResidencyPlan a = plan(items, params);
        const ResidencyPlan b = plan(items, params);
        ASSERT_EQ(a.mLoad.size(), 1u);
        EXPECT_EQ(a.mLoad[0], 1u);
        EXPECT_EQ(a.mLoad, b.mLoad);
        EXPECT_EQ(a.mFree, b.mFree);
    }

    TEST(LodResidency, byte_sums_saturate_instead_of_wrapping)
    {
        ResidencyParams params;
        params.mBudgetBytes = 100 * mib;
        const std::uint64_t huge = std::numeric_limits<std::uint64_t>::max() - 5;
        ResidencyItem a = held(huge, 1, true);
        ResidencyItem b = held(100, 1, true);
        a.mReplaceable = false;
        b.mReplaceable = false;
        const ResidencyPlan result = plan({ a, b, wantedAbsent(100, 2) }, params);
        // The two held items cannot be freed and their sum would wrap past 2^64; no wrap makes the sum look small
        // enough to admit the candidate.
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_TRUE(result.mOverBudget);
        EXPECT_EQ(result.mHeldAfter, std::numeric_limits<std::uint64_t>::max());
    }

    TEST(LodResidency, the_plan_reuses_its_vectors_without_growing_after_the_first_call)
    {
        ResidencyParams params;
        params.mBudgetBytes = 50 * mib;
        const std::vector<ResidencyItem> items
            = { held(mib, 5, false), wantedAbsent(mib, 3), wantedAbsent(mib, 4), held(mib, 9, true) };
        ResidencyPlan result;
        planResidency(items, params, result);
        const std::size_t freeCapacity = result.mFree.capacity();
        const std::size_t loadCapacity = result.mLoad.capacity();
        for (int i = 0; i < 100; ++i)
            planResidency(items, params, result);
        EXPECT_EQ(result.mFree.capacity(), freeCapacity);
        EXPECT_EQ(result.mLoad.capacity(), loadCapacity);
    }

    // Applies a plan to a toy world, the way a client does: freed items go absent with a cooldown, loaded items
    // become held at once.
    struct World
    {
        std::vector<ResidencyItem> mItems;
        std::uint32_t mSwaps = 0;

        void apply(const ResidencyPlan& result)
        {
            for (std::uint32_t index : result.mFree)
            {
                mItems[index].mHeld = false;
                mItems[index].mCooldown = 3;
                ++mSwaps;
            }
            for (std::uint32_t index : result.mLoad)
                mItems[index].mHeld = true;
            for (ResidencyItem& item : mItems)
            {
                if (item.mCooldown > 0)
                    --item.mCooldown;
                item.mIdleFrames = item.mWanted ? 0 : item.mIdleFrames + 1;
            }
        }

        std::uint64_t heldBytes() const
        {
            std::uint64_t sum = 0;
            for (const ResidencyItem& item : mItems)
                if (item.mHeld)
                    sum += item.mBytes;
            return sum;
        }
    };

    TEST(LodResidency, two_items_of_about_equal_urgency_do_not_swap_back_and_forth)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib; // room for one of the two
        params.mSwapMargin = 1.25f;
        World world;
        world.mItems = { wantedAbsent(4 * mib, 1000), wantedAbsent(4 * mib, 1001) };
        ResidencyPlan result;
        for (int frame = 0; frame < 200; ++frame)
        {
            // The camera drifts a little: the priorities wobble by a few percent.
            world.mItems[0].mPriority = 1000.f + 20.f * std::sin(frame * 0.3f);
            world.mItems[1].mPriority = 1000.f - 20.f * std::sin(frame * 0.3f);
            planResidency(world.mItems, params, result);
            world.apply(result);
        }
        EXPECT_EQ(world.mSwaps, 0u);
        EXPECT_EQ(world.heldBytes(), 4 * mib);
    }

    TEST(LodResidency, when_the_camera_really_moves_the_held_item_follows_it_once)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib;
        World world;
        world.mItems = { wantedAbsent(4 * mib, 100), wantedAbsent(4 * mib, 5000) };
        ResidencyPlan result;
        for (int frame = 0; frame < 200; ++frame)
        {
            // A flight: item 0 goes from near to far while item 1 comes from far to near.
            const float t = static_cast<float>(frame) / 199.f;
            world.mItems[0].mPriority = 100.f + 5000.f * t;
            world.mItems[1].mPriority = 5000.f - 4900.f * t;
            planResidency(world.mItems, params, result);
            world.apply(result);
        }
        EXPECT_EQ(world.mSwaps, 1u);
        EXPECT_TRUE(world.mItems[1].mHeld);
        EXPECT_FALSE(world.mItems[0].mHeld);
    }

    TEST(LodResidency, held_bytes_return_to_zero_when_nothing_is_wanted_any_more)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * mib;
        params.mKeepFrames = 30;
        World world;
        for (int i = 0; i < 6; ++i)
            world.mItems.push_back(wantedAbsent(2 * mib, 100.f * static_cast<float>(i + 1)));
        ResidencyPlan result;
        for (int frame = 0; frame < 20; ++frame)
        {
            planResidency(world.mItems, params, result);
            world.apply(result);
        }
        EXPECT_EQ(world.heldBytes(), 12 * mib);
        for (ResidencyItem& item : world.mItems)
            item.mWanted = false;
        for (int frame = 0; frame < 100; ++frame)
        {
            planResidency(world.mItems, params, result);
            world.apply(result);
        }
        EXPECT_EQ(world.heldBytes(), 0u);
    }

    TEST(LodResidency, the_plan_depends_only_on_its_arguments)
    {
        ResidencyParams params;
        params.mBudgetBytes = 9 * mib;
        params.mKeepFrames = 10;
        std::vector<ResidencyItem> items;
        for (int i = 0; i < 12; ++i)
        {
            ResidencyItem item = (i % 3 == 0) ? held(2 * mib, static_cast<float>(i * 37 % 11), i % 2 == 0)
                                              : wantedAbsent(mib, static_cast<float>(i * 53 % 17));
            item.mIdleFrames = i;
            items.push_back(item);
        }
        const ResidencyPlan a = plan(items, params);
        ResidencyPlan b;
        planResidency(items, params, b);
        planResidency(items, params, b); // reused
        EXPECT_EQ(a.mFree, b.mFree);
        EXPECT_EQ(a.mLoad, b.mLoad);
        EXPECT_EQ(a.mHeldAfter, b.mHeldAfter);
        EXPECT_EQ(a.mBlocked, b.mBlocked);
        EXPECT_EQ(a.mOverBudget, b.mOverBudget);
        // No index is both freed and loaded, and none appears twice.
        for (std::uint32_t index : a.mFree)
            EXPECT_FALSE(contains(a.mLoad, index));
    }
}
