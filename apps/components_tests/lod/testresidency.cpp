#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
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

    // Task 0060 review fixes: the toy World above frees and loads at once with exact bytes, which hid the churn the
    // game showed. This one models the real clients: a load completes some frames later and holds its true size
    // (which the estimate may miss); a freed item stays in memory while it is handed over (leaving), is not asked for
    // again until it is gone (suppressed) and then waits a cooldown.
    struct Sim
    {
        enum class Phase
        {
            Absent,
            Loading,
            Held,
            Leaving,
        };
        struct Entry
        {
            std::uint64_t mTrueBytes = 0;
            std::uint64_t mEstimate = 0;
            bool mKnown = false; // read once: the estimate is the true size from then on
            float mPriority = 0;
            bool mWantedBySelection = true;
            bool mReplaceable = true;
            Phase mPhase = Phase::Absent;
            int mTimer = 0;
            int mCooldown = 0;
            bool mEvicted = false;
        };

        std::vector<Entry> mEntries;
        std::vector<ResidencyItem> mItems;
        int mLoadFrames = 3;
        int mHandOverFrames = 25;
        int mCooldownFrames = 60;
        std::uint32_t mLoads = 0;
        std::uint32_t mFrees = 0;

        void step(const ResidencyParams& params, ResidencyPlan& result)
        {
            mItems.resize(mEntries.size());
            for (std::size_t i = 0; i < mEntries.size(); ++i)
            {
                const Entry& entry = mEntries[i];
                ResidencyItem& item = mItems[i];
                item = ResidencyItem{};
                item.mHeld = entry.mPhase == Phase::Held || entry.mPhase == Phase::Leaving;
                item.mLeaving = entry.mPhase == Phase::Leaving;
                item.mLoading = entry.mPhase == Phase::Loading;
                item.mWanted = entry.mWantedBySelection && !entry.mEvicted;
                item.mReplaceable = entry.mReplaceable;
                item.mPriority = entry.mPriority;
                item.mCooldown = entry.mCooldown;
                item.mIdleFrames = item.mWanted ? 0 : 1000;
                item.mBytes = item.mHeld ? entry.mTrueBytes : (entry.mKnown ? entry.mTrueBytes : entry.mEstimate);
            }
            planResidency(mItems, params, result);
            for (const std::uint32_t index : result.mFree)
            {
                Entry& entry = mEntries[index];
                if (entry.mPhase != Phase::Held)
                    continue;
                entry.mPhase = Phase::Leaving;
                entry.mTimer = mHandOverFrames;
                entry.mEvicted = entry.mWantedBySelection;
                ++mFrees;
            }
            for (const std::uint32_t index : result.mLoad)
            {
                Entry& entry = mEntries[index];
                entry.mPhase = Phase::Loading;
                entry.mTimer = mLoadFrames;
                ++mLoads;
            }
            for (Entry& entry : mEntries)
            {
                if (entry.mCooldown > 0)
                    --entry.mCooldown;
                if (entry.mPhase == Phase::Loading && --entry.mTimer <= 0)
                {
                    entry.mKnown = true;
                    entry.mPhase = entry.mWantedBySelection && !entry.mEvicted ? Phase::Held : Phase::Absent;
                }
                else if (entry.mPhase == Phase::Leaving && --entry.mTimer <= 0)
                {
                    entry.mPhase = Phase::Absent;
                    entry.mEvicted = false;
                    entry.mCooldown = mCooldownFrames;
                }
            }
        }

        std::uint64_t residentBytes() const
        {
            std::uint64_t sum = 0;
            for (const Entry& entry : mEntries)
            {
                if (entry.mPhase == Phase::Held || entry.mPhase == Phase::Leaving)
                    sum += entry.mTrueBytes;
            }
            return sum;
        }

        std::size_t heldCount() const
        {
            return static_cast<std::size_t>(std::count_if(
                mEntries.begin(), mEntries.end(), [](const Entry& entry) { return entry.mPhase == Phase::Held; }));
        }
    };

    Sim::Entry simEntry(std::uint64_t trueBytes, std::uint64_t estimate, float priority)
    {
        Sim::Entry entry;
        entry.mTrueBytes = trueBytes;
        entry.mEstimate = estimate;
        entry.mPriority = priority;
        return entry;
    }

    TEST(LodResidencySim, a_budget_smaller_than_one_item_keeps_the_first_item_and_does_not_churn)
    {
        // The review's `far memory budget = 1` run: every cluster is larger than the budget.
        ResidencyParams params;
        params.mBudgetBytes = mib;
        Sim sim;
        for (int i = 0; i < 10; ++i)
            sim.mEntries.push_back(simEntry(3 * mib, 3 * mib, 1000.f + 100.f * static_cast<float>(i)));
        ResidencyPlan result;
        for (int frame = 0; frame < 600; ++frame)
            sim.step(params, result);
        EXPECT_LE(sim.mLoads, 2u);
        EXPECT_EQ(sim.mFrees, 0u);
        EXPECT_EQ(sim.heldCount(), 1u);
        EXPECT_TRUE(result.mOverBudget);
    }

    TEST(LodResidencySim, an_estimate_that_undershoots_settles_instead_of_cycling)
    {
        // The review's 24 MiB run: the first estimate is 2 MiB a cluster, the clusters hold 3.
        ResidencyParams params;
        params.mBudgetBytes = 24 * mib;
        Sim sim;
        for (int i = 0; i < 16; ++i)
            sim.mEntries.push_back(simEntry(3 * mib, 2 * mib, 500.f + 250.f * static_cast<float>(i)));
        ResidencyPlan result;
        for (int frame = 0; frame < 600; ++frame)
            sim.step(params, result);
        const std::uint32_t loadsBefore = sim.mLoads;
        const std::uint32_t freesBefore = sim.mFrees;
        for (int frame = 0; frame < 600; ++frame)
            sim.step(params, result);
        EXPECT_EQ(sim.mLoads, loadsBefore); // nothing moves once settled
        EXPECT_EQ(sim.mFrees, freesBefore);
        EXPECT_LE(sim.residentBytes(), 24 * mib);
        EXPECT_GE(sim.heldCount(), 6u);
        EXPECT_LE(loadsBefore, 16u + 4u);
    }

    TEST(LodResidencySim, a_slow_flight_loads_each_item_a_bounded_number_of_times)
    {
        ResidencyParams params;
        params.mBudgetBytes = 12 * mib;
        Sim sim;
        for (int i = 0; i < 20; ++i)
            sim.mEntries.push_back(simEntry(2 * mib, mib, 0));
        ResidencyPlan result;
        for (int frame = 0; frame < 2000; ++frame)
        {
            // The camera moves along the row of items.
            const float camera = 10.f * static_cast<float>(frame);
            for (std::size_t i = 0; i < sim.mEntries.size(); ++i)
                sim.mEntries[i].mPriority = std::abs(1000.f * static_cast<float>(i) - camera) + 1.f;
            sim.step(params, result);
        }
        EXPECT_LE(sim.mLoads, 2u * 20u);
    }

    TEST(LodResidency, a_leaving_item_is_not_committed_and_causes_no_pressure)
    {
        ResidencyParams params;
        params.mBudgetBytes = 12 * mib;
        ResidencyItem going = held(10 * mib, 50, false);
        going.mLeaving = true;
        const ResidencyPlan result = plan({ going, held(10 * mib, 10, true) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_EQ(result.mHeldAfter, 10 * mib);
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidency, a_leaving_item_is_never_freed_again)
    {
        ResidencyParams params;
        ResidencyItem going = held(mib, 50, false);
        going.mLeaving = true;
        EXPECT_TRUE(plan({ going }, params).mFree.empty());
    }

    TEST(LodResidency, pressure_never_frees_the_last_committed_item)
    {
        ResidencyParams params;
        params.mBudgetBytes = mib;
        const ResidencyPlan result = plan({ held(3 * mib, 100, true) }, params);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_TRUE(result.mOverBudget);
    }

    TEST(LodResidency, admission_stops_at_the_low_water_mark)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mLowWater = 0.9f;
        params.mKeepFrames = 1000;
        ResidencyItem fixed = held(5 * mib, 1, true);
        fixed.mReplaceable = false;
        // 5 + 4.5 = 9.5 MiB is under the budget but over 9: blocked.
        EXPECT_TRUE(plan({ fixed, wantedAbsent(9 * mib / 2, 2) }, params).mLoad.empty());
        // 5 + 4 = 9 MiB: admitted.
        EXPECT_EQ(plan({ fixed, wantedAbsent(4 * mib, 2) }, params).mLoad.size(), 1u);
    }

    TEST(LodResidency, a_candidate_with_no_fallback_takes_the_room_of_a_nearer_replaceable_item)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mib;
        ResidencyItem far = wantedAbsent(4 * mib, 50000);
        far.mReplaceable = false;
        const ResidencyPlan result = plan({ held(4 * mib, 100, true), far }, params);
        ASSERT_EQ(result.mFree.size(), 1u);
        EXPECT_EQ(result.mFree[0], 0u);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
    }

    TEST(LodResidency, candidates_with_no_fallback_come_first)
    {
        ResidencyParams params;
        params.mMaxLoadsInFlight = 1;
        params.mMaxLoadsPerCall = 1;
        ResidencyItem far = wantedAbsent(mib, 50000);
        far.mReplaceable = false;
        const ResidencyPlan result = plan({ wantedAbsent(mib, 10), far }, params);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
    }

    TEST(LodResidency, a_replacement_that_releases_pinned_items_is_not_blocked_by_them)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        std::vector<ResidencyItem> items;
        for (int i = 0; i < 4; ++i)
        {
            ResidencyItem child = held(2 * mib, 100, false);
            child.mPinned = true;
            items.push_back(child);
        }
        items.push_back(wantedAbsent(4 * mib, 120));
        // 8 MiB pinned + 4 = 12: blocked.
        EXPECT_TRUE(plan(items, params).mLoad.empty());
        // Drawing the parent lets the 8 MiB of children go: 4 MiB.
        items.back().mReleasesBytes = 8 * mib;
        EXPECT_EQ(plan(items, params).mLoad.size(), 1u);
    }

    TEST(LodResidency, without_nearest_first_candidates_load_in_index_order)
    {
        ResidencyParams params;
        params.mNearestFirst = false;
        const ResidencyPlan result = plan(
            { wantedAbsent(mib, 40), wantedAbsent(mib, 30), wantedAbsent(mib, 20), wantedAbsent(mib, 10) }, params);
        ASSERT_EQ(result.mLoad.size(), 2u);
        EXPECT_EQ(result.mLoad[0], 0u);
        EXPECT_EQ(result.mLoad[1], 1u);
    }

    // Changed by the second review of task 0060 (finding 7): this test used to pin mBlocked at the search limit (5),
    // which hid the other 25 candidates the budget left out. The limit now bounds the victim searches only; mBlocked
    // is the true count. Awaiting toddhow's approval (stage log).
    TEST(LodResidency, one_call_counts_every_blocked_candidate_while_it_searches_victims_for_a_bounded_number)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mMaxBlockedPerCall = 5;
        ResidencyItem fixed = held(9 * mib, 1, true);
        fixed.mReplaceable = false;
        std::vector<ResidencyItem> items = { fixed };
        for (int i = 0; i < 30; ++i)
            items.push_back(wantedAbsent(4 * mib, 10.f + static_cast<float>(i)));
        EXPECT_EQ(plan(items, params).mBlocked, 30u);
    }

    TEST(LodResidencyRange, a_square_counts_as_within_range_only_when_all_of_it_is)
    {
        // A square of side 10 from (100, 0); the camera at the origin.
        EXPECT_TRUE(squareWithinRange(0, 0, 100, 0, 10, 200));
        // Its far corner (110, 10) is about 110.45 away.
        EXPECT_TRUE(squareWithinRange(0, 0, 100, 0, 10, 110.5));
        EXPECT_FALSE(squareWithinRange(0, 0, 100, 0, 10, 110.4));
        // Partly inside: not within.
        EXPECT_FALSE(squareWithinRange(0, 0, 100, 0, 10, 105));
        // The camera inside the square: the farthest corner counts (about 7.07 away).
        EXPECT_TRUE(squareWithinRange(105, 5, 100, 0, 10, 7.1));
        EXPECT_FALSE(squareWithinRange(105, 5, 100, 0, 10, 7.0));
    }

    TEST(LodResidencyRange, a_range_that_is_not_positive_or_finite_holds_nothing)
    {
        EXPECT_FALSE(squareWithinRange(0, 0, 0, 0, 1, 0));
        EXPECT_FALSE(squareWithinRange(0, 0, 0, 0, 1, -5));
        EXPECT_FALSE(squareWithinRange(0, 0, 0, 0, 1, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_FALSE(squareWithinRange(0, 0, 0, 0, 1, std::numeric_limits<double>::infinity()));
    }
}

namespace
{
    TEST(LodResidencyDescribe, a_budget_under_one_mebibyte_is_not_shown_as_zero)
    {
        EXPECT_EQ(Lod::describeBytes(256 * 1024), "256 KiB");
        EXPECT_EQ(Lod::describeBytes(24ull * 1024 * 1024), "24 MiB");
        EXPECT_EQ(Lod::describeBytes(3ull * 1024 * 1024 / 2), "1.5 MiB");
        EXPECT_EQ(Lod::describeBytes(512), "512 bytes");
        EXPECT_EQ(Lod::describeBytes(0), "0 bytes");
    }
}

namespace
{
    using namespace Lod;

    constexpr std::uint64_t mebi = 1024ull * 1024;

    ResidencyItem absentItem(std::uint64_t bytes, float priority, bool replaceable = true)
    {
        ResidencyItem item;
        item.mBytes = bytes;
        item.mPriority = priority;
        item.mWanted = true;
        item.mReplaceable = replaceable;
        return item;
    }

    ResidencyItem heldItem(std::uint64_t bytes, float priority, bool wanted, bool replaceable = true)
    {
        ResidencyItem item = absentItem(bytes, priority, replaceable);
        item.mHeld = true;
        item.mWanted = wanted;
        item.mIdleFrames = wanted ? 0 : 1000;
        return item;
    }

    // The governing rule of the second review: an item with no fallback is never blocked, never freed.
    TEST(LodResidencyNoFallback, a_candidate_with_no_fallback_is_admitted_when_nothing_can_make_room)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mebi;
        const std::vector<ResidencyItem> items
            = { heldItem(4 * mebi, 100, true, false), absentItem(4 * mebi, 50000, false) };
        ResidencyPlan result;
        planResidency(items, params, result);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 1u);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_EQ(result.mBlocked, 0u);
        EXPECT_TRUE(result.mOverBudget); // reported, not fixed with a hole
    }

    TEST(LodResidencyNoFallback, a_no_fallback_candidate_that_failed_before_is_still_admitted)
    {
        ResidencyParams params;
        params.mBudgetBytes = 5 * mebi;
        ResidencyItem candidate = absentItem(4 * mebi, 50000, false);
        candidate.mFailures = 3;
        const std::vector<ResidencyItem> items = { heldItem(4 * mebi, 100, true, false), candidate };
        ResidencyPlan result;
        planResidency(items, params, result);
        EXPECT_EQ(result.mLoad.size(), 1u);
        EXPECT_TRUE(result.mFree.empty());
    }

    // The review's example of finding 4: four pinned 2 MiB children, a wanted replaceable X of 6 MiB, a parent of
    // 8 MiB that releases the children.
    std::vector<ResidencyItem> coarseningItems()
    {
        std::vector<ResidencyItem> items;
        for (int i = 0; i < 4; ++i)
        {
            ResidencyItem child = heldItem(2 * mebi, 300, false);
            child.mPinned = true;
            items.push_back(child);
        }
        items.push_back(heldItem(6 * mebi, 900, true)); // X, index 4
        ResidencyItem parent = absentItem(8 * mebi, 300);
        parent.mReleasesBytes = 8 * mebi;
        items.push_back(parent); // index 5
        return items;
    }

    TEST(LodResidencyReleases, pressure_credits_what_an_admitted_replacement_releases)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * mebi;
        ResidencyPlan result;
        planResidency(coarseningItems(), params, result);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 5u);
        EXPECT_FALSE(contains(result.mFree, 4)); // X stays
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_FALSE(result.mOverBudget);
        EXPECT_EQ(result.mHeldAfter, 14 * mebi); // 8 + 6 + 8 - 8 released
    }

    TEST(LodResidencyReleases, pressure_credits_what_a_loading_replacement_releases)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * mebi;
        std::vector<ResidencyItem> items = coarseningItems();
        items[5].mLoading = true;
        ResidencyPlan result;
        planResidency(items, params, result);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidencyFailures, a_candidate_that_failed_before_frees_nothing_to_make_room)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        ResidencyItem candidate = absentItem(4 * mebi, 10);
        candidate.mFailures = 1;
        const std::vector<ResidencyItem> items = { heldItem(8 * mebi, 1000, true), candidate };
        ResidencyPlan result;
        planResidency(items, params, result);
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_EQ(result.mBlocked, 1u);
        // The same candidate without the failure takes the farther item's room.
        std::vector<ResidencyItem> fresh = items;
        fresh[1].mFailures = 0;
        planResidency(fresh, params, result);
        EXPECT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mFree.size(), 1u);
    }

    TEST(LodResidencyFailures, a_candidate_that_failed_before_is_admitted_when_it_fits)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        ResidencyItem candidate = absentItem(2 * mebi, 10);
        candidate.mFailures = 2;
        const std::vector<ResidencyItem> items = { heldItem(4 * mebi, 1000, true), candidate };
        ResidencyPlan result;
        planResidency(items, params, result);
        EXPECT_EQ(result.mLoad.size(), 1u);
        EXPECT_TRUE(result.mFree.empty());
    }

    TEST(LodResidencyFailures, the_cooldown_doubles_with_each_failure_up_to_sixteen_times_the_base)
    {
        EXPECT_EQ(failureCooldownFrames(600, 0), 0);
        EXPECT_EQ(failureCooldownFrames(600, 1), 600);
        EXPECT_EQ(failureCooldownFrames(600, 2), 1200);
        EXPECT_EQ(failureCooldownFrames(600, 3), 2400);
        EXPECT_EQ(failureCooldownFrames(600, 5), 9600);
        EXPECT_EQ(failureCooldownFrames(600, 6), 9600);
        EXPECT_EQ(failureCooldownFrames(600, 4000000000u), 9600);
        EXPECT_EQ(failureCooldownFrames(0, 3), 0);
    }

    TEST(LodResidencyCap, a_candidate_past_the_search_limit_that_fits_without_victims_is_admitted)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        params.mMaxBlockedPerCall = 5;
        std::vector<ResidencyItem> items = { heldItem(6 * mebi, 1, true, false) };
        for (int i = 0; i < 10; ++i)
            items.push_back(absentItem(5 * mebi, 10.f + static_cast<float>(i)));
        items.push_back(absentItem(2 * mebi, 100)); // 6 + 2 = 8 fits under the low water mark (9)
        ResidencyPlan result;
        planResidency(items, params, result);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 11u);
        EXPECT_EQ(result.mBlocked, 10u);
    }

    TEST(LodResidencyRange, replaceable_has_hysteresis_inside_the_range_only)
    {
        const double range = 40000;
        const double margin = replaceableMargin(range);
        ASSERT_GT(margin, 0.0);
        // A square of side 1000 on the x axis whose far corner is about `far` from the origin.
        const auto square = [](bool previous, double far, double r) {
            return squareReplaceable(previous, 0, 0, far - 1000, 0, 1000, r);
        };
        // Not replaceable before: only inside the range less the margin.
        EXPECT_FALSE(square(false, range - margin / 2, range));
        EXPECT_TRUE(square(false, range - margin * 2, range));
        // Replaceable before: stays so while inside the range, never outside it.
        EXPECT_TRUE(square(true, range - margin / 2, range));
        EXPECT_FALSE(square(true, range + 1, range));
        EXPECT_FALSE(square(false, range + 1, range));
    }

    TEST(LodResidencyRange, the_margin_is_five_percent_of_the_range_and_at_least_a_quarter_cell)
    {
        EXPECT_DOUBLE_EQ(replaceableMargin(100000), 5000);
        EXPECT_DOUBLE_EQ(replaceableMargin(10000), 2048);
        EXPECT_DOUBLE_EQ(replaceableMargin(0), 2048);
    }

    // A Sim that models the cluster client (second review of task 0060): hand-over with a delay, loads that arrive
    // late with their true size, loads that fail and are retried after failureCooldownFrames, and replaceability from
    // each item's square through squareReplaceable as the camera moves. The client's rule that a leaving item which
    // lost its fallback is kept (the free cancelled) is mirrored here. Terrain is the separate test further down.
    // The cluster client (MWRender::LodClusters::update) as the planner sees it, in the client's order (third review,
    // finding 5): cooldowns count down first; then the loads that have arrived become draws, at most one a frame (the
    // client's build cap); then the plan; then the hand-over: a cluster freed while wanted waits until the live objects
    // draw its cells (only while it is replaceable) and then fades out. An entry starts not replaceable, as the
    // client's tile does, and its square (default: near) decides it each frame.
    struct ClientSim
    {
        enum Phase
        {
            Absent,
            Loading,
            Held,
            Leaving,
        };
        struct Entry
        {
            std::uint64_t mTrueBytes = 0;
            std::uint64_t mEstimate = 0;
            bool mKnown = false;
            float mPriority = 0;
            bool mWantedBySelection = true;
            bool mFailing = false; // every load fails
            std::uint32_t mFailures = 0;
            double mFarX = 1000; // the square [mFarX - 1000, mFarX] x [0, 1000]
            bool mReplaceable = false;
            Phase mPhase = Absent;
            int mTimer = 0;
            int mLiveWait = 0; // frames of the hand-over before the live objects draw the cells
            int mCooldown = 0; // eviction cooldown
            int mFailureCooldown = 0;
            bool mEvicted = false;
            bool mLoadDone = false;
        };
        std::vector<Entry> mEntries;
        std::vector<ResidencyItem> mItems;
        double mEyeX = 0;
        double mRange = 40000;
        int mLoadFrames = 3;
        int mLiveFrames = 5;
        int mHandOverFrames = 25;
        int mEvictedFrames = 60;
        int mRetryFrames = 600;
        std::size_t mBuildsPerFrame = 1;
        std::uint32_t mLoads = 0;
        std::vector<std::uint32_t> mFreesOf;

        void step(const ResidencyParams& params, ResidencyPlan& result)
        {
            mFreesOf.resize(mEntries.size(), 0);
            mItems.resize(mEntries.size());
            // Cooldowns, the fallback, a cancelled free, and the loads in flight.
            for (Entry& entry : mEntries)
            {
                if (entry.mCooldown > 0)
                    --entry.mCooldown;
                if (entry.mFailureCooldown > 0)
                    --entry.mFailureCooldown;
                entry.mReplaceable
                    = clusterReplaceable(entry.mReplaceable, true, mEyeX, 0, entry.mFarX - 1000, 0, 1000, mRange);
                if (cancelEviction(entry.mEvicted, entry.mPhase == Leaving, entry.mReplaceable))
                {
                    entry.mPhase = Held;
                    entry.mEvicted = false;
                }
                if (entry.mPhase == Loading && --entry.mTimer <= 0)
                    entry.mLoadDone = true;
            }
            // Arrivals become draws before the plan, at most mBuildsPerFrame of them.
            std::size_t builds = 0;
            for (Entry& entry : mEntries)
            {
                if (entry.mPhase != Loading || !entry.mLoadDone || builds >= mBuildsPerFrame)
                    continue;
                entry.mLoadDone = false;
                if (entry.mFailing)
                {
                    ++entry.mFailures;
                    entry.mFailureCooldown = failureCooldownFrames(mRetryFrames, entry.mFailures);
                    entry.mPhase = Absent;
                    continue;
                }
                entry.mKnown = true;
                if (!entry.mWantedBySelection || entry.mEvicted)
                {
                    entry.mPhase = Absent;
                    continue;
                }
                entry.mFailures = 0;
                entry.mPhase = Held;
                ++builds;
            }
            for (std::size_t i = 0; i < mEntries.size(); ++i)
            {
                Entry& entry = mEntries[i];
                ResidencyItem& item = mItems[i];
                item = ResidencyItem{};
                item.mHeld = entry.mPhase == Held || entry.mPhase == Leaving;
                item.mLeaving = entry.mPhase == Leaving;
                item.mLoading = entry.mPhase == Loading;
                item.mWanted = entry.mWantedBySelection && !entry.mEvicted;
                item.mReplaceable = entry.mReplaceable;
                item.mPriority = entry.mPriority;
                item.mCooldown = entry.mCooldown;
                item.mFailureCooldown = entry.mFailureCooldown;
                item.mFailures = entry.mFailures;
                item.mIdleFrames = item.mWanted ? 0 : 1000;
                item.mBytes = item.mHeld || entry.mKnown ? entry.mTrueBytes : entry.mEstimate;
            }
            planResidency(mItems, params, result);
            for (const std::uint32_t index : result.mFree)
            {
                Entry& entry = mEntries[index];
                if (entry.mPhase != Held)
                    continue;
                entry.mPhase = Leaving;
                entry.mLiveWait = mLiveFrames;
                entry.mTimer = mHandOverFrames;
                entry.mEvicted = entry.mWantedBySelection;
                ++mFreesOf[index];
            }
            for (const std::uint32_t index : result.mLoad)
            {
                mEntries[index].mPhase = Loading;
                mEntries[index].mTimer = mLoadFrames;
                mEntries[index].mLoadDone = false;
                ++mLoads;
            }
            // The hand-over: the live objects take the cells back (only while they reach them), then the fade.
            for (Entry& entry : mEntries)
            {
                if (entry.mPhase != Leaving)
                    continue;
                if (entry.mLiveWait > 0)
                {
                    if (entry.mReplaceable)
                        --entry.mLiveWait;
                    continue;
                }
                if (--entry.mTimer <= 0)
                {
                    entry.mPhase = Absent;
                    entry.mEvicted = false;
                    entry.mCooldown = mEvictedFrames;
                }
            }
        }
    };

    ClientSim::Entry clientEntry(std::uint64_t bytes, float priority)
    {
        ClientSim::Entry entry;
        entry.mTrueBytes = bytes;
        entry.mEstimate = bytes;
        entry.mPriority = priority;
        return entry;
    }

    TEST(LodResidencySim, a_failing_load_never_costs_a_good_item_its_place)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        ClientSim sim;
        sim.mEntries.push_back(clientEntry(4 * mebi, 1000));
        sim.mEntries.push_back(clientEntry(4 * mebi, 2000));
        ResidencyPlan result;
        for (int frame = 0; frame < 30; ++frame)
            sim.step(params, result);
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Held);
        ASSERT_EQ(sim.mEntries[1].mPhase, ClientSim::Held);
        ClientSim::Entry failing = clientEntry(4 * mebi, 10); // nearer, but every load of it fails
        failing.mFailing = true;
        sim.mEntries.push_back(failing);
        const std::uint32_t loadsBefore = sim.mLoads;
        for (int frame = 0; frame < 20000; ++frame)
            sim.step(params, result);
        // Its first try may take the room of the farther item (it has not failed yet); after that it never does.
        EXPECT_LE(sim.mFreesOf[0] + sim.mFreesOf[1], 1u);
        // Retries back off (600, 1200, 2400, 4800, 9600 frames), and the farther item comes back after the first.
        EXPECT_LE(sim.mLoads - loadsBefore, 8u);
    }

    TEST(LodResidencySim, a_camera_swaying_at_the_paging_edge_does_not_churn)
    {
        // Y is near and replaceable; X's far corner sits at the paging range and the camera sways by less than the
        // hysteresis margin, so X's replaceability must not flip back and forth.
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        ClientSim sim;
        ClientSim::Entry y = clientEntry(6 * mebi, 100);
        y.mFarX = 5000;
        ClientSim::Entry x = clientEntry(6 * mebi, 500);
        x.mFarX = sim.mRange;
        sim.mEntries = { y, x };
        ResidencyPlan result;
        for (int frame = 0; frame < 6000; ++frame)
        {
            sim.mEyeX = 300.0 * std::sin(static_cast<double>(frame) * 0.05);
            sim.step(params, result);
        }
        EXPECT_LE(sim.mLoads, 6u);
        EXPECT_LE(sim.mFreesOf[0] + sim.mFreesOf[1], 3u);
    }

    TEST(LodResidencySim, a_leaving_item_that_loses_its_fallback_is_kept_and_never_freed_again)
    {
        // Y is freed for a nearer item while replaceable; the camera then moves so Y's square leaves the range during
        // the hand-over: the client cancels the free, and the planner never frees it while it has no fallback.
        ResidencyParams params;
        params.mBudgetBytes = 10 * mebi;
        ClientSim sim;
        ClientSim::Entry y = clientEntry(6 * mebi, 5000);
        y.mFarX = 20000;
        sim.mEntries = { y };
        ResidencyPlan result;
        for (int frame = 0; frame < 10; ++frame)
            sim.step(params, result);
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Held);
        sim.mEntries.push_back(clientEntry(6 * mebi, 10));
        sim.step(params, result); // the near one takes Y's room: Y is leaving
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Leaving);
        sim.mEyeX = -30000; // Y's far corner is now 50000 away: past the range
        for (int frame = 0; frame < 600; ++frame)
            sim.step(params, result);
        EXPECT_EQ(sim.mEntries[0].mPhase, ClientSim::Held);
        EXPECT_EQ(sim.mFreesOf[0], 1u);
    }

    // Terrain as the client draws it: a build is drawn in the frame it is planned, and the removal loop takes the
    // replaced tiles away in that frame. One parent P (8 MiB) over four children (2 MiB each), and a far tile X (6 MiB,
    // wanted, replaceable). The camera alternates: coarse (P chosen, the children no longer) and fine (the children
    // chosen, P no longer). The client's releases: what P replaces (the drawn children), and P split among the children
    // not yet drawn.
    TEST(LodResidencySim, terrain_coarsening_and_refinement_never_free_a_far_tile_needlessly)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * mebi;
        params.mMaxLoadsInFlight = 4;
        params.mMaxLoadsPerCall = 4;
        const std::uint64_t bytes[6] = { 8 * mebi, 2 * mebi, 2 * mebi, 2 * mebi, 2 * mebi, 6 * mebi };
        bool drawn[6] = { false, true, true, true, true, true };
        std::uint32_t freesOfX = 0;
        std::uint32_t overBudget = 0;
        std::vector<ResidencyItem> items(6);
        ResidencyPlan result;
        for (int frame = 0; frame < 400; ++frame)
        {
            const bool coarse = (frame / 50) % 2 == 0;
            const bool chosen[6] = { coarse, !coarse, !coarse, !coarse, !coarse, true };
            std::uint64_t childrenMissing = 0;
            std::uint64_t childrenDrawnBytes = 0;
            for (int c = 1; c <= 4; ++c)
            {
                childrenMissing += chosen[c] && !drawn[c] ? 1 : 0;
                childrenDrawnBytes += drawn[c] ? bytes[c] : 0;
            }
            for (int i = 0; i < 6; ++i)
            {
                ResidencyItem& item = items[static_cast<std::size_t>(i)];
                item = ResidencyItem{};
                item.mBytes = bytes[i];
                item.mHeld = drawn[i];
                item.mWanted = chosen[i];
                item.mPinned = drawn[i] && !chosen[i];
                item.mPriority = i == 5 ? 900.f : 300.f;
                item.mIdleFrames = chosen[i] ? 0 : 1000;
                if (i == 0 && !drawn[0] && chosen[0])
                    item.mReleasesBytes = childrenDrawnBytes;
                if (i >= 1 && i <= 4 && !drawn[i] && chosen[i] && drawn[0] && childrenMissing > 0)
                    item.mReleasesBytes = bytes[0] / childrenMissing;
            }
            planResidency(items, params, result);
            for (const std::uint32_t index : result.mFree)
            {
                if (index == 5)
                    ++freesOfX;
                drawn[index] = false;
            }
            for (const std::uint32_t index : result.mLoad)
                drawn[index] = true;
            overBudget += result.mOverBudget ? 1 : 0;
            // The removal loop: the children go once P is drawn and chosen; P goes once every chosen child is drawn.
            if (drawn[0] && chosen[0])
            {
                for (int c = 1; c <= 4; ++c)
                    drawn[c] = false;
            }
            if (!chosen[0] && drawn[1] && drawn[2] && drawn[3] && drawn[4])
                drawn[0] = false;
        }
        EXPECT_EQ(freesOfX, 0u);
        EXPECT_EQ(overBudget, 0u);
        EXPECT_TRUE(drawn[5]);
    }
}

// Task 0060, third review: the eviction cooldown never holds back an item with no fallback (finding 1), a failing item
// with no fallback starts no pressure (finding 3), and the client rules extracted as pure functions (findings 2 and 5).
namespace
{
    using namespace Lod;

    constexpr std::uint64_t mbyte = 1024ull * 1024;

    ResidencyItem absent3(std::uint64_t bytes, float priority, bool replaceable)
    {
        ResidencyItem item;
        item.mBytes = bytes;
        item.mPriority = priority;
        item.mWanted = true;
        item.mReplaceable = replaceable;
        return item;
    }

    ResidencyItem held3(std::uint64_t bytes, float priority, bool replaceable)
    {
        ResidencyItem item = absent3(bytes, priority, replaceable);
        item.mHeld = true;
        return item;
    }

    TEST(LodResidencyCooldown, an_item_with_no_fallback_ignores_the_eviction_cooldown)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ResidencyItem item = absent3(4 * mbyte, 5000, false);
        item.mCooldown = 60;
        ResidencyPlan result;
        planResidency(std::vector<ResidencyItem>{ item }, params, result);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mCooling, 0u);
    }

    TEST(LodResidencyCooldown, a_replaceable_item_in_eviction_cooldown_waits_and_is_counted)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ResidencyItem item = absent3(4 * mbyte, 5000, true);
        item.mCooldown = 60;
        ResidencyPlan result;
        planResidency(std::vector<ResidencyItem>{ item }, params, result);
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mCooling, 1u);
        EXPECT_EQ(result.mBlocked, 0u);
    }

    TEST(LodResidencyCooldown, the_failure_back_off_holds_back_an_item_with_no_fallback_too)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ResidencyItem item = absent3(4 * mbyte, 5000, false);
        item.mFailures = 2;
        item.mFailureCooldown = 1200;
        ResidencyPlan result;
        planResidency(std::vector<ResidencyItem>{ item }, params, result);
        EXPECT_TRUE(result.mLoad.empty());
        EXPECT_EQ(result.mCooling, 1u);
    }

    TEST(LodResidencyCooldown, with_no_budget_a_cooling_item_is_held_back_as_before)
    {
        ResidencyItem item = absent3(4 * mbyte, 5000, true);
        item.mFailureCooldown = 10;
        ResidencyPlan result;
        planResidency(std::vector<ResidencyItem>{ item }, ResidencyParams{}, result);
        EXPECT_TRUE(result.mLoad.empty());
    }

    // Two replaceable held items (6 and 3 MiB) under a 10 MiB budget, and a far item with no fallback whose loads
    // failed before. Its estimate must not push the others out: the load may fail again.
    TEST(LodResidencyFailures, a_failing_candidate_with_no_fallback_starts_no_pressure)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ResidencyItem failing = absent3(4 * mbyte, 50000, false);
        failing.mFailures = 2;
        const std::vector<ResidencyItem> items = { held3(6 * mbyte, 100, true), held3(3 * mbyte, 200, true), failing };
        ResidencyPlan result;
        planResidency(items, params, result);
        ASSERT_EQ(result.mLoad.size(), 1u);
        EXPECT_EQ(result.mLoad[0], 2u);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidencyFailures, a_loading_failing_item_with_no_fallback_starts_no_pressure)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ResidencyItem failing = absent3(4 * mbyte, 50000, false);
        failing.mFailures = 1;
        failing.mLoading = true;
        const std::vector<ResidencyItem> items = { held3(6 * mbyte, 100, true), held3(3 * mbyte, 200, true), failing };
        ResidencyPlan result;
        planResidency(items, params, result);
        EXPECT_TRUE(result.mFree.empty());
        EXPECT_FALSE(result.mOverBudget);
    }

    TEST(LodResidencyFailures, once_held_its_bytes_count_again)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        const std::vector<ResidencyItem> items
            = { held3(6 * mbyte, 100, true), held3(3 * mbyte, 200, true), held3(4 * mbyte, 50000, false) };
        ResidencyPlan result;
        planResidency(items, params, result);
        // 13 MiB: pressure frees down to the low water mark (9 MiB), so both replaceable ones go (the farther first);
        // the one with no fallback stays (4 MiB held after the plan).
        ASSERT_EQ(result.mFree.size(), 2u);
        EXPECT_EQ(result.mFree[0], 1u);
        EXPECT_EQ(result.mFree[1], 0u);
        EXPECT_EQ(result.mHeldAfter, 4 * mbyte);
    }

    TEST(LodClusterFallback, nothing_is_replaceable_without_live_objects)
    {
        EXPECT_FALSE(clusterReplaceable(true, false, 0, 0, 0, 0, 1000, 40000));
        EXPECT_TRUE(clusterReplaceable(true, true, 0, 0, 0, 0, 1000, 40000));
        // Not replaceable before: it must lie inside the range less the margin.
        EXPECT_FALSE(clusterReplaceable(false, true, 0, 0, 39000, 0, 900, 40000));
        EXPECT_TRUE(clusterReplaceable(true, true, 0, 0, 39000, 0, 900, 40000));
    }

    TEST(LodClusterFallback, a_free_is_cancelled_only_for_a_drawn_cluster_that_lost_its_fallback)
    {
        EXPECT_TRUE(cancelEviction(true, true, false));
        EXPECT_FALSE(cancelEviction(true, true, true));
        EXPECT_FALSE(cancelEviction(true, false, false));
        EXPECT_FALSE(cancelEviction(false, true, false));
    }

    TEST(LodRefinement, a_child_is_classified_by_what_draws_its_square)
    {
        EXPECT_EQ(classifyRefinedChild(true, false, true, false), RefinedChild::Drawn);
        EXPECT_EQ(classifyRefinedChild(false, true, true, false), RefinedChild::LiveDraws);
        EXPECT_EQ(classifyRefinedChild(false, false, true, true), RefinedChild::Failed);
        EXPECT_EQ(classifyRefinedChild(false, false, false, false), RefinedChild::OutOfView);
        EXPECT_EQ(classifyRefinedChild(false, false, false, true), RefinedChild::Coming);
    }

    TEST(LodRefinement, a_parent_waits_for_coming_children_and_stays_for_failed_ones_only)
    {
        const RefinedChild ready[] = { RefinedChild::Drawn, RefinedChild::LiveDraws, RefinedChild::OutOfView };
        EXPECT_TRUE(refinedParentMayGo(ready));
        const RefinedChild coming[] = { RefinedChild::Drawn, RefinedChild::Coming };
        EXPECT_FALSE(refinedParentMayGo(coming));
        const RefinedChild failed[] = { RefinedChild::Drawn, RefinedChild::Failed };
        EXPECT_FALSE(refinedParentMayGo(failed));
        EXPECT_TRUE(refinedParentMayGo(std::span<const RefinedChild>{}));
    }

    std::vector<RefinementTile> familyOfFive()
    {
        // 0: the parent (8 MiB); 1 to 4: its children (2 MiB each).
        std::vector<RefinementTile> tiles(5);
        tiles[0].mDrawBytes = 8 * mbyte;
        for (std::size_t i = 1; i < 5; ++i)
        {
            tiles[i].mParent = 0;
            tiles[i].mDrawBytes = 2 * mbyte;
        }
        return tiles;
    }

    TEST(LodRefinement, a_coarsening_releases_the_drawn_children)
    {
        std::vector<RefinementTile> tiles = familyOfFive();
        tiles[0].mChosen = true;
        for (std::size_t i = 1; i < 5; ++i)
            tiles[i].mDrawn = true;
        std::vector<std::uint32_t> missing;
        std::vector<std::uint64_t> releases;
        refinementReleases(tiles, missing, releases);
        ASSERT_EQ(releases.size(), 5u);
        EXPECT_EQ(releases[0], 8 * mbyte);
        for (std::size_t i = 1; i < 5; ++i)
            EXPECT_EQ(releases[i], 0u);
    }

    TEST(LodRefinement, a_refinement_splits_the_parent_over_the_missing_children)
    {
        std::vector<RefinementTile> tiles = familyOfFive();
        tiles[0].mDrawn = true;
        for (std::size_t i = 1; i < 5; ++i)
            tiles[i].mChosen = true;
        tiles[1].mDrawn = true;
        tiles[2].mDrawn = true;
        std::vector<std::uint32_t> missing;
        std::vector<std::uint64_t> releases;
        refinementReleases(tiles, missing, releases);
        ASSERT_EQ(missing.size(), 5u);
        EXPECT_EQ(missing[0], 2u);
        EXPECT_EQ(releases[3], 4 * mbyte);
        EXPECT_EQ(releases[4], 4 * mbyte);
        EXPECT_EQ(releases[1], 0u);
    }

    TEST(LodRefinement, a_parent_that_stays_for_good_gives_its_children_no_share)
    {
        std::vector<RefinementTile> tiles = familyOfFive();
        tiles[0].mDrawn = true;
        tiles[0].mStays = true;
        for (std::size_t i = 1; i < 5; ++i)
            tiles[i].mChosen = true;
        tiles[1].mDrawn = true;
        std::vector<std::uint32_t> missing;
        std::vector<std::uint64_t> releases;
        refinementReleases(tiles, missing, releases);
        EXPECT_EQ(missing[0], 3u);
        for (std::size_t i = 0; i < 5; ++i)
            EXPECT_EQ(releases[i], 0u);
    }

    TEST(LodRefinement, a_grandchild_finds_the_drawn_ancestor_above_an_undrawn_parent)
    {
        // 0 drawn and refined; 1 neither drawn nor chosen (refined further); 2 chosen under 1, not drawn.
        std::vector<RefinementTile> tiles(3);
        tiles[0].mDrawn = true;
        tiles[0].mDrawBytes = 8 * mbyte;
        tiles[1].mParent = 0;
        tiles[2].mParent = 1;
        tiles[2].mChosen = true;
        std::vector<std::uint32_t> missing;
        std::vector<std::uint64_t> releases;
        refinementReleases(tiles, missing, releases);
        EXPECT_EQ(missing[0], 1u);
        EXPECT_EQ(releases[2], 8 * mbyte);
    }

    // The review's scenario for finding 1: Y is freed for a nearer item while replaceable and fades out; the camera
    // then moves so Y's square leaves the live range. Y has no fallback now and must come back at once, not after the
    // eviction cooldown.
    TEST(LodResidencySim, an_item_that_loses_its_fallback_after_its_hand_over_comes_back_at_once)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mbyte;
        ClientSim sim;
        ClientSim::Entry y = clientEntry(6 * mbyte, 5000);
        y.mFarX = 20000;
        sim.mEntries = { y };
        ResidencyPlan result;
        for (int frame = 0; frame < 10; ++frame)
            sim.step(params, result);
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Held);
        sim.mEntries.push_back(clientEntry(6 * mbyte, 10));
        sim.step(params, result);
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Leaving);
        for (int frame = 0; frame < 200 && sim.mEntries[0].mPhase != ClientSim::Absent; ++frame)
            sim.step(params, result);
        ASSERT_EQ(sim.mEntries[0].mPhase, ClientSim::Absent);
        ASSERT_GT(sim.mEntries[0].mCooldown, 0);
        sim.mEyeX = -30000; // Y's far corner is now 50000 away: past the range
        int frames = 0;
        for (; frames < 200 && sim.mEntries[0].mPhase != ClientSim::Held; ++frames)
            sim.step(params, result);
        EXPECT_LE(frames, sim.mLoadFrames + 2);
    }
}

// Task 0060, far cluster eviction (toddhow's decision of 2026-11-04, option b): the ground test, the fallback rule, the
// planner's reporting and a Sim of the cluster client with the rule. The Sim here (FarSim) is its own copy of the
// client's order, not a change to ClientSim, so that the earlier tests are untouched.
namespace
{
    using namespace Lod;

    constexpr std::uint64_t farMib = 1024ull * 1024;
    constexpr double cell = 8192;

    // ---- groundDrawnOver ------------------------------------------------------------------------------------------

    TEST(LodGroundDrawn, a_cell_wholly_inside_the_paging_range_is_drawn_by_the_live_terrain)
    {
        // Camera at the origin, range 40000: the cell [0, 8192]^2 has its far corner 11585 away.
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, {}, 0, 0, cell, cell));
    }

    TEST(LodGroundDrawn, a_cell_past_the_range_with_no_drawn_tile_is_not_drawn)
    {
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, {}, 80000, 0, cell, cell));
    }

    TEST(LodGroundDrawn, a_cell_past_the_range_inside_a_drawn_tile_is_drawn)
    {
        const GroundSquare tiles[] = { { 65536, -32768, 65536 } };
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, tiles, 81920, 0, cell, cell));
    }

    TEST(LodGroundDrawn, a_cluster_square_needs_every_cell_covered)
    {
        // A 2 x 2 cell cluster far away; three of its cells are covered by tiles, the fourth is not.
        const double ox = 100000;
        const double oy = 0;
        const GroundSquare threeCells[] = { { ox, oy, cell }, { ox + cell, oy, cell }, { ox, oy + cell, cell } };
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, threeCells, ox, oy, 2 * cell, cell));
        const GroundSquare fourCells[]
            = { { ox, oy, cell }, { ox + cell, oy, cell }, { ox, oy + cell, cell }, { ox + cell, oy + cell, cell } };
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, fourCells, ox, oy, 2 * cell, cell));
        // One tile as big as the cluster covers it, and so do tiles that merely overlap it from the sides.
        const GroundSquare big[] = { { ox, oy, 2 * cell } };
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, big, ox, oy, 2 * cell, cell));
        const GroundSquare wider[] = { { ox - cell, oy - cell, 4 * cell } };
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, wider, ox, oy, 2 * cell, cell));
    }

    TEST(LodGroundDrawn, cells_inside_the_range_and_cells_in_tiles_may_share_a_square)
    {
        // A 2 x 2 cell square with the camera at its south-west corner and a range of 12000: the south-west cell's far
        // corner is 11585 away (the live terrain draws it), the others are farther.
        const double range = 12000;
        EXPECT_FALSE(groundDrawnOver(0, 0, range, {}, 0, 0, 2 * cell, cell));
        const GroundSquare two[] = { { 0, cell, 2 * cell } }; // the north row only
        EXPECT_FALSE(groundDrawnOver(0, 0, range, two, 0, 0, 2 * cell, cell)); // the south-east cell has no ground
        const GroundSquare three[] = { { cell, 0, cell }, { 0, cell, 2 * cell } };
        EXPECT_TRUE(groundDrawnOver(0, 0, range, three, 0, 0, 2 * cell, cell));
    }

    TEST(LodGroundDrawn, a_cell_only_partly_inside_the_range_is_not_live_ground)
    {
        // The near corner is inside, the far corner is not: the live terrain does not draw all of it.
        EXPECT_FALSE(groundDrawnOver(0, 0, 9000, {}, 0, 0, cell, cell));
    }

    TEST(LodGroundDrawn, bad_inputs_are_not_drawn)
    {
        const GroundSquare all[] = { { -1e9, -1e9, 2e9 } };
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, cell, 0)); // no cell size
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, cell, -cell));
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, cell, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, 0, cell)); // no side
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, 1.5 * cell, cell)); // not a whole number of cells
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, 0, 0, 33 * cell, cell)); // more than 32 cells a side
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, all, std::numeric_limits<double>::infinity(), 0, cell, cell));
        EXPECT_FALSE(groundDrawnOver(0, 0, std::numeric_limits<double>::quiet_NaN(), {}, 0, 0, cell, cell));
        EXPECT_FALSE(groundDrawnOver(0, 0, 0, {}, 0, 0, cell, cell)); // no range, no tile
        // The largest square that is examined is still answered.
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, all, 0, 0, 32 * cell, cell));
    }

    TEST(LodGroundDrawn, a_tile_that_does_not_reach_the_cell_centre_does_not_count)
    {
        // The tile ends just before the cell's centre: the cell is not covered.
        const GroundSquare tiles[] = { { 100000, 0, cell * 0.5 - 1 } };
        EXPECT_FALSE(groundDrawnOver(0, 0, 40000, tiles, 100000, 0, cell, cell));
        const GroundSquare enough[] = { { 100000, 0, cell * 0.5 + 1 } };
        EXPECT_TRUE(groundDrawnOver(0, 0, 40000, enough, 100000, 0, cell, cell));
    }

    // ---- stepGroundFrames and clusterGroundFallback ---------------------------------------------------------------

    TEST(LodGroundFrames, counts_up_while_drawn_and_drops_to_zero_at_once)
    {
        std::int32_t frames = 0;
        for (int i = 1; i <= 5; ++i)
            frames = stepGroundFrames(frames, true);
        EXPECT_EQ(frames, 5);
        frames = stepGroundFrames(frames, false);
        EXPECT_EQ(frames, 0);
        EXPECT_EQ(
            stepGroundFrames(std::numeric_limits<std::int32_t>::max(), true), std::numeric_limits<std::int32_t>::max());
    }

    TEST(LodGroundFallback, needs_the_tier_the_live_objects_a_square_past_the_range_and_stable_ground)
    {
        const std::int32_t stable = groundStableFrames;
        EXPECT_TRUE(clusterGroundFallback(true, true, false, stable));
        EXPECT_FALSE(clusterGroundFallback(false, true, false, stable)); // the tier keeps every cluster
        EXPECT_FALSE(clusterGroundFallback(true, false, false, stable)); // object paging off: nothing else is distant
        EXPECT_FALSE(clusterGroundFallback(true, true, true, stable)); // wholly inside: the live objects' business
        EXPECT_FALSE(clusterGroundFallback(true, true, false, stable - 1)); // the ground has not been drawn long enough
        EXPECT_FALSE(clusterGroundFallback(true, true, false, 0));
        EXPECT_TRUE(clusterGroundFallback(true, true, false, stable + 1000));
    }

    TEST(LodGroundWait, a_candidate_waits_for_its_ground_instead_of_being_admitted_as_an_item_with_no_fallback)
    {
        const std::int32_t stable = groundStableFrames;
        const std::int32_t wait = groundWaitFrames;
        // Applies: tier evicts, live objects exist, square past the range; ground not stable and missing for a while.
        EXPECT_TRUE(clusterWaitsForGround(true, true, false, 0, 1));
        EXPECT_TRUE(clusterWaitsForGround(true, true, false, 0, wait - 1));
        EXPECT_TRUE(clusterWaitsForGround(true, true, false, stable - 1, 0)); // drawn, but not yet for long enough
        // Waited long enough: admitted as an item with no fallback (the sea, a tile that failed to build).
        EXPECT_FALSE(clusterWaitsForGround(true, true, false, 0, wait));
        EXPECT_FALSE(clusterWaitsForGround(true, true, false, 0, wait + 1000));
        // Stable ground: no waiting (it is replaceable).
        EXPECT_FALSE(clusterWaitsForGround(true, true, false, stable, 0));
        // The rule does not apply: nothing waits.
        EXPECT_FALSE(clusterWaitsForGround(false, true, false, 0, 1));
        EXPECT_FALSE(clusterWaitsForGround(true, false, false, 0, 1));
        EXPECT_FALSE(clusterWaitsForGround(true, true, true, 0, 1));
    }

    TEST(LodGroundWait, the_wait_is_longer_than_the_stable_count)
    {
        EXPECT_GT(groundWaitFrames, groundStableFrames);
    }

    // ---- stepClusterGround (the one per-cluster step the client and FarSim call) -----------------------------------

    TEST(LodClusterGround, a_square_past_the_range_with_drawn_ground_waits_then_may_be_evicted)
    {
        ClusterGroundState state;
        const ClusterGroundInput input{ true, false, true, true };
        for (std::int32_t frame = 1; frame < groundStableFrames; ++frame)
        {
            const ClusterGroundStep step = stepClusterGround(state, input);
            EXPECT_FALSE(step.mFallback) << "frame " << frame;
            EXPECT_TRUE(step.mWaits) << "frame " << frame;
        }
        const ClusterGroundStep stable = stepClusterGround(state, input);
        EXPECT_TRUE(stable.mFallback);
        EXPECT_FALSE(stable.mWaits);
        EXPECT_EQ(state.mGroundFrames, groundStableFrames);
        EXPECT_EQ(state.mMissingFrames, 0);
    }

    TEST(LodClusterGround, lost_ground_resets_the_count_at_once_and_a_long_loss_stops_the_wait)
    {
        ClusterGroundState state;
        for (int frame = 0; frame < 100; ++frame)
            stepClusterGround(state, ClusterGroundInput{ true, false, true, true });
        ClusterGroundStep step = stepClusterGround(state, ClusterGroundInput{ true, false, false, true });
        EXPECT_FALSE(step.mFallback);
        EXPECT_TRUE(step.mWaits);
        EXPECT_EQ(state.mGroundFrames, 0);
        EXPECT_EQ(state.mMissingFrames, 1);
        for (std::int32_t frame = 1; frame < groundWaitFrames; ++frame)
            step = stepClusterGround(state, ClusterGroundInput{ true, false, false, true });
        EXPECT_FALSE(step.mWaits); // waited long enough: an item with no fallback
        EXPECT_FALSE(step.mFallback);
    }

    TEST(LodClusterGround, a_rule_that_does_not_apply_says_nothing_and_resets)
    {
        ClusterGroundState state{ 50, 7 };
        ClusterGroundStep step = stepClusterGround(state, ClusterGroundInput{ false, false, true, true });
        EXPECT_FALSE(step.mFallback);
        EXPECT_FALSE(step.mWaits);
        EXPECT_EQ(state.mGroundFrames, 0);
        EXPECT_EQ(state.mMissingFrames, 0);
    }

    TEST(LodClusterGround, the_count_goes_on_while_the_square_is_wholly_inside_the_range)
    {
        // Wholly inside, the live terrain draws the ground whatever the cached test says (a square that was not
        // drawn when last tested may be inside now): the frames count, but nothing is said about eviction.
        ClusterGroundState state;
        for (int frame = 0; frame < 100; ++frame)
        {
            const ClusterGroundStep step = stepClusterGround(state, ClusterGroundInput{ true, true, false, true });
            EXPECT_FALSE(step.mFallback);
            EXPECT_FALSE(step.mWaits);
        }
        EXPECT_EQ(state.mGroundFrames, 100);
        EXPECT_EQ(state.mMissingFrames, 0);
        // The frame it leaves the range its ground has been stable for a long time: it can be evicted at once.
        const ClusterGroundStep out = stepClusterGround(state, ClusterGroundInput{ true, false, true, true });
        EXPECT_TRUE(out.mFallback);
        EXPECT_FALSE(out.mWaits);
    }

    TEST(LodClusterGround, nothing_waits_for_its_ground_while_the_budget_is_not_pressing)
    {
        ClusterGroundState state;
        for (int frame = 0; frame < 10; ++frame)
        {
            const ClusterGroundStep step = stepClusterGround(state, ClusterGroundInput{ true, false, false, false });
            EXPECT_FALSE(step.mWaits) << "frame " << frame;
            EXPECT_FALSE(step.mFallback);
        }
        // The counters ran all the same: a cluster that is chosen when the budget starts to press is not new to it.
        EXPECT_EQ(state.mMissingFrames, 10);
        const ClusterGroundStep pressed = stepClusterGround(state, ClusterGroundInput{ true, false, false, true });
        EXPECT_TRUE(pressed.mWaits);
    }

    TEST(LodClusterGround, the_budget_presses_when_the_last_plan_blocked_something_or_the_next_cluster_would_not_fit)
    {
        EXPECT_FALSE(budgetIsPressing(100 * farMib, 0, 6 * farMib, 256 * farMib));
        EXPECT_TRUE(budgetIsPressing(100 * farMib, 1, 6 * farMib, 256 * farMib)); // something was blocked
        EXPECT_TRUE(budgetIsPressing(251 * farMib, 0, 6 * farMib, 256 * farMib)); // the next one would not fit
        EXPECT_FALSE(budgetIsPressing(250 * farMib, 0, 6 * farMib, 256 * farMib)); // exactly fits
        EXPECT_TRUE(budgetIsPressing(300 * farMib, 0, 0, 256 * farMib)); // already over
        EXPECT_FALSE(budgetIsPressing(0, 0, 6 * farMib, 0)); // no budget: the planner is not used, nothing presses
        EXPECT_TRUE(budgetIsPressing(std::numeric_limits<std::uint64_t>::max(), 0, 6 * farMib, 256 * farMib));
    }

    // ---- the kept ground test (finding 4 of the review of the far eviction) -----------------------------------------

    TEST(LodGroundTest, a_test_that_was_never_made_is_stale)
    {
        EXPECT_TRUE(groundTestStale(GroundTestKey{}, 1, 0, 0, 40000));
    }

    TEST(LodGroundTest, the_same_tiles_range_and_a_near_eye_keep_the_test)
    {
        const GroundTestKey key = groundTestKeyFor(7, 1000, -2000, 40000);
        EXPECT_FALSE(groundTestStale(key, 7, 1000, -2000, 40000));
        EXPECT_FALSE(groundTestStale(key, 7, 1000 + (groundRetestDistance - 1), -2000 - (groundRetestDistance - 1),
            40000)); // just under the distance on both axes
    }

    TEST(LodGroundTest, a_change_of_the_tiles_the_range_or_a_quarter_cell_of_eye_movement_makes_it_stale)
    {
        const GroundTestKey key = groundTestKeyFor(7, 1000, -2000, 40000);
        EXPECT_TRUE(groundTestStale(key, 8, 1000, -2000, 40000)); // the drawn tiles changed
        EXPECT_TRUE(groundTestStale(key, 7, 1000, -2000, 40001)); // setViewDistance: the paging range changed
        EXPECT_TRUE(groundTestStale(key, 7, 1000, -2000, 30000));
        EXPECT_TRUE(groundTestStale(key, 7, 1000 + groundRetestDistance, -2000, 40000));
        EXPECT_TRUE(groundTestStale(key, 7, 1000, -2000 - groundRetestDistance, 40000));
        EXPECT_TRUE(groundTestStale(key, 7, std::numeric_limits<double>::quiet_NaN(), -2000, 40000));
    }

    TEST(LodGroundTest, a_cell_that_was_live_ground_when_tested_is_still_inside_the_range_after_the_eye_moves)
    {
        // The test is kept while the eye moves up to groundRetestDistance on each axis. A cell whose far corner is
        // 39,500 from the eye is wholly inside a range of 40,000, but 2,047 units away on both axes (2,895 along the
        // diagonal) it is not: the test must not have counted it as live ground.
        const double range = 40000;
        const double cornerDistance = 39500; // from the eye at the origin: corner (cell + 8192, 0 + 8192)
        const double originX
            = -cell + std::sqrt(cornerDistance * cornerDistance - cell * cell); // far corner x = originX + cell
        EXPECT_TRUE(squareWithinRange(0, 0, originX, 0, cell, range));
        // Exact range: the cell counts as ground; with the range of the kept test it does not.
        EXPECT_TRUE(groundDrawnOver(0, 0, range, {}, originX, 0, cell, cell));
        EXPECT_FALSE(groundDrawnOver(0, 0, groundTestRange(range), {}, originX, 0, cell, cell));
        // The eye moves away by the most the test allows: the cell is outside the range.
        EXPECT_FALSE(
            squareWithinRange(-(groundRetestDistance - 1), -(groundRetestDistance - 1), originX, 0, cell, range));
        // A cell that is inside the range less groundRetestDistance * sqrt(2) is live ground after any such move.
        const double safeCorner = range - groundRetestDistance * std::sqrt(2.0) - 1;
        const double safeX = -cell + std::sqrt(safeCorner * safeCorner - cell * cell);
        EXPECT_TRUE(groundDrawnOver(0, 0, groundTestRange(range), {}, safeX, 0, cell, cell));
        for (const double dx : { -(groundRetestDistance - 1), 0.0, groundRetestDistance - 1 })
        {
            for (const double dy : { -(groundRetestDistance - 1), 0.0, groundRetestDistance - 1 })
                EXPECT_TRUE(squareWithinRange(dx, dy, safeX, 0, cell, range)) << dx << " " << dy;
        }
    }

    TEST(LodGroundTest, the_test_range_is_never_above_the_range_and_a_tiny_range_gives_no_live_ground)
    {
        EXPECT_LT(groundTestRange(40000), 40000);
        EXPECT_NEAR(groundTestRange(40000), 40000 - groundRetestDistance * std::sqrt(2.0), 1e-9);
        EXPECT_FALSE(groundDrawnOver(0, 0, groundTestRange(2000), {}, 0, 0, cell, cell));
        EXPECT_EQ(groundTestRange(std::numeric_limits<double>::infinity()), std::numeric_limits<double>::infinity());
    }

    TEST(LodClusterGround, the_time_without_ground_ends_the_wait_only_while_the_cluster_is_wanted)
    {
        // Start-up: the ground is not drawn yet (the terrain pack is being checked) for far longer than the wait, and
        // the cluster is not wanted. The frames before it is chosen must not count towards the end of its wait.
        ClusterGroundState state;
        for (int frame = 0; frame < 3 * groundWaitFrames; ++frame)
            stepClusterGround(state, ClusterGroundInput{ true, false, false, true, false });
        EXPECT_EQ(state.mMissingFrames, 0);
        ClusterGroundStep step = stepClusterGround(state, ClusterGroundInput{ true, false, false, true, true });
        EXPECT_TRUE(step.mWaits); // just chosen: it waits
        for (std::int32_t frame = 1; frame < groundWaitFrames; ++frame)
            step = stepClusterGround(state, ClusterGroundInput{ true, false, false, true, true });
        EXPECT_FALSE(step.mWaits); // waited its groundWaitFrames as a wanted cluster: an item with no fallback
        // The stable count is not affected: ground drawn all along counts for an unwanted cluster too.
        ClusterGroundState stable;
        for (int frame = 0; frame < 100; ++frame)
            stepClusterGround(stable, ClusterGroundInput{ true, false, true, true, false });
        EXPECT_EQ(stable.mGroundFrames, 100);
        EXPECT_TRUE(stepClusterGround(stable, ClusterGroundInput{ true, false, true, true, true }).mFallback);
    }

    TEST(LodGroundFallback, the_stable_count_is_a_positive_number_of_frames)
    {
        EXPECT_GT(groundStableFrames, 0);
    }

    // ---- the planner's report -------------------------------------------------------------------------------------

    ResidencyItem farHeld(std::uint64_t bytes, float priority, bool replaceable)
    {
        ResidencyItem item;
        item.mBytes = bytes;
        item.mPriority = priority;
        item.mHeld = true;
        item.mWanted = true;
        item.mReplaceable = replaceable;
        return item;
    }

    TEST(LodFarEviction, pressure_frees_the_farthest_replaceable_items_first_and_keeps_the_rest)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * farMib;
        std::vector<ResidencyItem> items;
        for (int i = 0; i < 5; ++i)
            items.push_back(farHeld(6 * farMib, 10000.f + 1000.f * static_cast<float>(i), true)); // 30 MiB, far
        ResidencyPlan plan;
        planResidency(items, params, plan);
        // 30 MiB against a budget of 20: freeing down to 18 takes the two farthest (indices 4 and 3).
        ASSERT_EQ(plan.mFree.size(), 2u);
        EXPECT_EQ(plan.mFree[0], 4u);
        EXPECT_EQ(plan.mFree[1], 3u);
        EXPECT_FALSE(plan.mOverBudget);
        EXPECT_EQ(plan.mHeldAfter, 18 * farMib);
        EXPECT_EQ(plan.mNoFallbackBytes, 0u);
    }

    TEST(LodFarEviction, the_same_items_without_the_fallback_are_kept_and_the_overrun_is_reported)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * farMib;
        std::vector<ResidencyItem> items;
        for (int i = 0; i < 5; ++i)
            items.push_back(farHeld(6 * farMib, 10000.f + 1000.f * static_cast<float>(i), false));
        ResidencyPlan plan;
        planResidency(items, params, plan);
        EXPECT_TRUE(plan.mFree.empty());
        EXPECT_TRUE(plan.mOverBudget);
        EXPECT_EQ(plan.mHeldAfter, 30 * farMib);
        EXPECT_EQ(plan.mNoFallbackBytes, 30 * farMib);
    }

    TEST(LodFarEviction, a_pinned_and_a_near_no_fallback_item_stay_while_far_ones_go)
    {
        ResidencyParams params;
        params.mBudgetBytes = 20 * farMib;
        ResidencyItem pinned = farHeld(6 * farMib, 90000.f, true);
        pinned.mWanted = false;
        pinned.mPinned = true; // a hand-over in progress: never freed
        ResidencyItem noFallback = farHeld(6 * farMib, 100.f, false);
        const std::vector<ResidencyItem> items
            = { pinned, noFallback, farHeld(6 * farMib, 20000.f, true), farHeld(6 * farMib, 30000.f, true) };
        ResidencyPlan plan;
        planResidency(items, params, plan);
        for (const std::uint32_t freed : plan.mFree)
        {
            EXPECT_NE(freed, 0u);
            EXPECT_NE(freed, 1u);
        }
        EXPECT_FALSE(plan.mFree.empty());
        EXPECT_EQ(plan.mNoFallbackBytes, 6 * farMib);
    }

    TEST(LodFarEviction, the_no_fallback_bytes_leave_out_leaving_items_and_count_loading_ones)
    {
        ResidencyParams params;
        params.mBudgetBytes = 100 * farMib;
        ResidencyItem leaving = farHeld(5 * farMib, 100.f, false);
        leaving.mLeaving = true;
        ResidencyItem loading;
        loading.mBytes = 3 * farMib;
        loading.mLoading = true;
        loading.mWanted = true;
        loading.mReplaceable = false;
        const std::vector<ResidencyItem> items
            = { farHeld(4 * farMib, 200.f, false), farHeld(3 * farMib, 300.f, true), leaving, loading };
        ResidencyPlan plan;
        planResidency(items, params, plan);
        EXPECT_EQ(plan.mNoFallbackBytes, 7 * farMib); // 4 held + 3 loading; the leaving 5 are already counted as gone
    }

    // ---- the cluster client as a Sim, with the rule ---------------------------------------------------------------

    // The cluster client's order (MWRender::LodClusters::update, with a budget), for entries that are squares 1000 wide
    // on the x axis: cooldowns, the fallback (live objects, then the ground rule), a cancelled free, arrivals (one a
    // frame), the plan, the hand-over. `mEvictFar` is the tier's flag; `mGround(frame, index)` says whether the ground
    // beneath the entry is drawn this frame.
    struct FarSim
    {
        enum Phase
        {
            Absent,
            Loading,
            Held,
            Leaving,
        };
        struct Entry
        {
            std::uint64_t mTrueBytes = 6 * farMib;
            std::uint64_t mEstimate = farMib; // the first estimate is low, as the client's
            bool mKnown = false;
            double mFarX = 0; // the square [mFarX - 1000, mFarX] x [0, 1000]
            bool mLiveReplaceable = false;
            ClusterGroundState mGround;
            bool mFallback = false;
            bool mWaits = false;
            Phase mPhase = Absent;
            int mTimer = 0;
            int mLiveWait = 0;
            int mCooldown = 0;
            bool mEvicted = false;
            bool mLoadDone = false;
            std::uint32_t mLoads = 0;
            std::uint32_t mFrees = 0;
            std::uint32_t mCancels = 0;
        };
        std::vector<Entry> mEntries;
        std::vector<ResidencyItem> mItems;
        bool mEvictFar = true;
        double mEyeX = 0;
        double mRange = 40000;
        std::function<bool(int, std::size_t)> mGround = [](int, std::size_t) { return true; };
        // Does the selection choose the entry this frame? (The client counts only chosen or non-Idle clusters.)
        std::function<bool(int, std::size_t)> mChosen = [](int, std::size_t) { return true; };
        int mFrame = 0;
        // Counted every frame:
        std::uint32_t mFreedWithoutFallback = 0; // a free whose item had no fallback when the plan was made
        std::uint32_t mHoles = 0; // an evicted item gone with no fallback behind it
        std::uint64_t mWorstHeldBytes = 0; // the most true bytes held (not leaving) at the end of any frame
        std::uint32_t mOverBudgetFrames = 0;
        std::vector<std::vector<std::uint32_t>> mLoadLog; // per frame: the loads the plan started
        std::vector<std::vector<std::uint32_t>> mFreeLog; // per frame: the frees the plan chose
        // What the previous plan said (the client reads them from its ResidencyPlan before planning again).
        std::uint64_t mPrevHeldAfter = 0;
        std::uint32_t mPrevBlocked = 0;
        // What the entries read so far held (all one level): the client's State::estimateBytes is the mean of the
        // level's clusters read so far, and the entry's own first estimate before any was read.
        std::uint64_t mReadBytes = 0;
        std::uint32_t mReadCount = 0;
        std::uint64_t estimateOf(const Entry& entry) const
        {
            if (entry.mKnown)
                return entry.mTrueBytes;
            return mReadCount > 0 ? mReadBytes / mReadCount : entry.mEstimate;
        }

        void step(const ResidencyParams& params, ResidencyPlan& plan)
        {
            mItems.resize(mEntries.size());
            for (std::size_t i = 0; i < mEntries.size(); ++i)
            {
                Entry& entry = mEntries[i];
                const bool held = entry.mPhase == Held || entry.mPhase == Leaving;
                const std::uint64_t bytes = held ? entry.mTrueBytes : estimateOf(entry);
                if (entry.mCooldown > 0)
                    --entry.mCooldown;
                const double originX = entry.mFarX - 1000;
                entry.mLiveReplaceable
                    = clusterReplaceable(entry.mLiveReplaceable, true, mEyeX, 0, originX, 0, 1000, mRange);
                const bool wholly = squareWithinRange(mEyeX, 0, originX, 0, 1000, mRange);
                const bool pressure = budgetIsPressing(mPrevHeldAfter, mPrevBlocked, bytes, params.mBudgetBytes);
                const bool wanted = mChosen(mFrame, i) || entry.mPhase != Absent;
                const ClusterGroundStep ground = stepClusterGround(
                    entry.mGround, ClusterGroundInput{ mEvictFar, wholly, mGround(mFrame, i), pressure, wanted });
                entry.mWaits = ground.mWaits;
                entry.mFallback = entry.mLiveReplaceable || ground.mFallback;
                if (cancelEviction(entry.mEvicted, entry.mPhase == Leaving, entry.mFallback))
                {
                    entry.mPhase = Held;
                    entry.mEvicted = false;
                    ++entry.mCancels;
                }
                if (entry.mPhase == Loading && --entry.mTimer <= 0)
                    entry.mLoadDone = true;
            }
            for (Entry& entry : mEntries)
            {
                if (entry.mPhase != Loading || !entry.mLoadDone)
                    continue;
                entry.mLoadDone = false;
                if (!entry.mKnown)
                {
                    mReadBytes += entry.mTrueBytes;
                    ++mReadCount;
                }
                entry.mKnown = true;
                entry.mPhase = entry.mEvicted ? Absent : Held;
                break; // one build a frame
            }
            for (std::size_t i = 0; i < mEntries.size(); ++i)
            {
                const Entry& entry = mEntries[i];
                ResidencyItem& item = mItems[i];
                item = ResidencyItem{};
                item.mHeld = entry.mPhase == Held || entry.mPhase == Leaving;
                item.mLeaving = entry.mPhase == Leaving;
                item.mLoading = entry.mPhase == Loading;
                item.mWanted = !entry.mEvicted && (mChosen(mFrame, i) || entry.mPhase != Absent);
                // A cluster that is not loaded waits for its ground (the client's rule).
                if (entry.mPhase == Absent && entry.mWaits)
                    item.mWanted = false;
                item.mReplaceable = entry.mFallback;
                item.mPriority = static_cast<float>(std::abs(entry.mFarX - 500 - mEyeX));
                item.mCooldown = entry.mCooldown;
                item.mBytes = item.mHeld ? entry.mTrueBytes : estimateOf(entry);
            }
            planResidency(mItems, params, plan);
            mPrevHeldAfter = plan.mHeldAfter;
            mPrevBlocked = plan.mBlocked;
            mLoadLog.push_back(plan.mLoad);
            mFreeLog.push_back(plan.mFree);
            if (plan.mOverBudget)
                ++mOverBudgetFrames;
            for (const std::uint32_t index : plan.mFree)
            {
                Entry& entry = mEntries[index];
                if (!entry.mFallback)
                    ++mFreedWithoutFallback;
                if (entry.mPhase != Held)
                    continue;
                entry.mPhase = Leaving;
                // The live objects take the cells back only where they reach; beyond the range nothing waits.
                entry.mLiveWait = entry.mLiveReplaceable ? 5 : 0;
                entry.mTimer = 25;
                entry.mEvicted = true;
                ++entry.mFrees;
            }
            for (const std::uint32_t index : plan.mLoad)
            {
                mEntries[index].mPhase = Loading;
                mEntries[index].mTimer = 3;
                mEntries[index].mLoadDone = false;
                ++mEntries[index].mLoads;
            }
            for (Entry& entry : mEntries)
            {
                if (entry.mPhase != Leaving)
                    continue;
                // The live objects take the cells back only where they reach (the client's staticsReady is true for a
                // cell outside their range): a cluster that lost them has nothing to wait for.
                if (!entry.mLiveReplaceable)
                    entry.mLiveWait = 0;
                if (entry.mLiveWait > 0)
                {
                    --entry.mLiveWait;
                    continue;
                }
                if (--entry.mTimer <= 0)
                {
                    if (!entry.mFallback)
                        ++mHoles; // gone with nothing behind it (cancelEviction should have kept it)
                    entry.mPhase = Absent;
                    entry.mEvicted = false;
                    entry.mCooldown = 60;
                }
            }
            std::uint64_t held = 0;
            for (const Entry& entry : mEntries)
                if (entry.mPhase == Held)
                    held += entry.mTrueBytes;
            mWorstHeldBytes = std::max(mWorstHeldBytes, held);
            ++mFrame;
        }

        std::uint32_t totalLoads() const
        {
            std::uint32_t total = 0;
            for (const Entry& entry : mEntries)
                total += entry.mLoads;
            return total;
        }
        std::size_t heldCount() const
        {
            std::size_t count = 0;
            for (const Entry& entry : mEntries)
                count += entry.mPhase == Held ? 1 : 0;
            return count;
        }
    };

    FarSim makeFarSim(std::size_t count, bool evictFar)
    {
        FarSim sim;
        sim.mEvictFar = evictFar;
        sim.mEntries.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            sim.mEntries[i].mFarX = 45000 + 1000.0 * static_cast<double>(i); // all past the 40000 range at eye 0
        return sim;
    }

    ResidencyParams farParams(std::uint64_t budget)
    {
        ResidencyParams params;
        params.mBudgetBytes = budget;
        params.mKeepFrames = 0;
        params.mMaxLoadsInFlight = 2;
        params.mMaxLoadsPerCall = 2;
        params.mNearestFirst = true;
        return params;
    }

    TEST(LodFarEvictionSim, a_still_camera_settles_within_the_budget_with_the_nearest_clusters_held)
    {
        FarSim sim = makeFarSim(40, true);
        const ResidencyParams params = farParams(60 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 3000; ++frame)
            sim.step(params, plan);
        // The low first estimates overshoot by the loads in flight, not more.
        EXPECT_LE(sim.mWorstHeldBytes, 60 * farMib + 12 * farMib);
        // The last frames: nothing moves any more.
        std::size_t actionsLate = 0;
        for (std::size_t f = sim.mLoadLog.size() - 500; f < sim.mLoadLog.size(); ++f)
            actionsLate += sim.mLoadLog[f].size() + sim.mFreeLog[f].size();
        EXPECT_EQ(actionsLate, 0u);
        EXPECT_LE(sim.heldCount() * 6 * farMib, 60 * farMib);
        EXPECT_GE(sim.heldCount(), 8u); // not empty: the budget is used
        // The ones held are the nearest.
        for (std::size_t i = 0; i < sim.heldCount(); ++i)
            EXPECT_EQ(sim.mEntries[i].mPhase, FarSim::Held) << "entry " << i;
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        EXPECT_EQ(sim.mHoles, 0u);
    }

    TEST(LodFarEvictionSim, the_tiers_that_do_not_evict_keep_every_cluster_and_report_the_overrun)
    {
        FarSim sim = makeFarSim(40, false);
        const ResidencyParams params = farParams(60 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 3000; ++frame)
            sim.step(params, plan);
        EXPECT_EQ(sim.heldCount(), 40u);
        EXPECT_EQ(sim.totalLoads(), 40u);
        for (const FarSim::Entry& entry : sim.mEntries)
            EXPECT_EQ(entry.mFrees, 0u);
        EXPECT_TRUE(plan.mOverBudget);
        EXPECT_EQ(plan.mNoFallbackBytes, 40 * 6 * farMib);
    }

    TEST(LodFarEvictionSim, a_climb_that_slides_the_window_loads_each_cluster_a_bounded_number_of_times)
    {
        FarSim sim = makeFarSim(40, true);
        const ResidencyParams params = farParams(60 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 6000; ++frame)
        {
            sim.mEyeX = 10.0 * frame; // 60000 units in 6000 frames: entries come inside the live range one by one
            sim.step(params, plan);
        }
        // The bounds below were 3 loads an entry and 70 in all (EDITED in the review fixes of the far eviction,
        // awaiting toddhow's approval). They were calibrated on a Sim whose unread clusters kept the 1 MiB first
        // estimate for ever; the client takes the mean of the level's clusters read so far (State::estimateBytes),
        // which FarSim now does too, and with true sizes the planner swaps nearer clusters back in for the band
        // clusters that ignore the budget: 97 loads in all, at most 6 an entry, a bounded wave and not a cycle (the
        // old bounds also fail with the old wait rule once the Sim has true sizes). In game the same climb at 24 MiB
        // loads 22 clusters.
        for (std::size_t i = 0; i < sim.mEntries.size(); ++i)
            EXPECT_LE(sim.mEntries[i].mLoads, 7u) << "entry " << i;
        EXPECT_LE(sim.totalLoads(), 110u);
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        EXPECT_EQ(sim.mHoles, 0u);
        EXPECT_LE(sim.mWorstHeldBytes, 60 * farMib + 12 * farMib);
    }

    TEST(LodFarEvictionSim, ground_that_flickers_every_other_frame_never_makes_a_cluster_evictable)
    {
        FarSim sim = makeFarSim(20, true);
        sim.mGround = [](int frame, std::size_t) { return frame % 2 == 0; };
        const ResidencyParams params = farParams(30 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 2000; ++frame)
            sim.step(params, plan);
        for (const FarSim::Entry& entry : sim.mEntries)
        {
            EXPECT_EQ(entry.mFrees, 0u);
            EXPECT_EQ(entry.mCancels, 0u);
            EXPECT_LE(entry.mLoads, 1u);
        }
    }

    TEST(LodFarEvictionSim, ground_lost_for_a_frame_now_and_then_causes_no_evict_and_cancel_cycle)
    {
        FarSim sim = makeFarSim(20, true);
        // Drawn for 100 frames, lost for one, again and again.
        sim.mGround = [](int frame, std::size_t) { return frame % 101 != 100; };
        const ResidencyParams params = farParams(30 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 6000; ++frame)
            sim.step(params, plan);
        for (const FarSim::Entry& entry : sim.mEntries)
        {
            // An eviction is cancelled at most once per loss, and an item is not freed again right after: bounded.
            EXPECT_LE(entry.mLoads, 4u);
            EXPECT_LE(entry.mFrees, 4u);
        }
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        EXPECT_EQ(sim.mHoles, 0u);
    }

    TEST(LodFarEvictionSim, ground_lost_for_good_makes_the_cluster_a_no_fallback_item_that_is_loaded_and_kept)
    {
        FarSim sim = makeFarSim(2, true);
        // Entry 1's ground is lost for good from frame 200 on; until then it is blocked by the budget.
        sim.mGround = [](int frame, std::size_t index) { return index != 1 || frame < 200; };
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        const ResidencyParams params = farParams(7 * farMib); // room for one
        ResidencyPlan plan;
        for (int frame = 0; frame < 199; ++frame)
            sim.step(params, plan);
        ASSERT_EQ(sim.mEntries[0].mPhase, FarSim::Held);
        ASSERT_EQ(sim.mEntries[1].mPhase, FarSim::Absent); // blocked by the budget, its ground is drawn
        for (int frame = 0; frame < 400; ++frame)
            sim.step(params, plan);
        // After waiting groundWaitFrames it is an item with no fallback: loaded whatever the budget says. The budget
        // then costs the other one its place (entry 0 still has its ground, so it is the one evicted).
        EXPECT_EQ(sim.mEntries[1].mPhase, FarSim::Held);
        EXPECT_EQ(sim.mEntries[0].mPhase, FarSim::Absent);
        EXPECT_EQ(sim.mEntries[0].mFrees, 1u);
        EXPECT_EQ(sim.mHoles, 0u);
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        EXPECT_FALSE(plan.mOverBudget);
        EXPECT_EQ(plan.mNoFallbackBytes, 6 * farMib); // entry 1 is the bytes the budget cannot touch
    }

    TEST(LodFarEvictionSim, ground_lost_during_the_hand_over_cancels_the_eviction)
    {
        FarSim sim = makeFarSim(2, true);
        sim.mEntries[1].mFarX = 120000;
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        // Both have ground, room for one. Entry 0 is nearer at first; when the eye moves, entry 1 is much nearer and
        // takes entry 0's room: entry 0 is freed. Its ground is lost while it fades out.
        sim.mGround = [](int frame, std::size_t index) { return index != 0 || frame < 400; };
        const ResidencyParams params = farParams(7 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 300; ++frame)
            sim.step(params, plan);
        ASSERT_EQ(sim.mEntries[0].mPhase, FarSim::Held);
        sim.mEyeX = 100000; // entry 1 is now the nearer (20500 against 55500 units)
        int cancelled = 0;
        for (int frame = 0; frame < 300; ++frame)
        {
            sim.step(params, plan);
            if (sim.mEntries[0].mPhase == FarSim::Leaving && sim.mFrame < 400)
            {
                // Lose the ground of entry 0 while it is leaving.
                sim.mFrame = 400;
            }
            cancelled = static_cast<int>(sim.mEntries[0].mCancels);
        }
        EXPECT_EQ(sim.mEntries[0].mFrees, 1u); // it was freed once ...
        EXPECT_EQ(cancelled, 1); // ... and the free was cancelled when its ground went
        EXPECT_EQ(sim.mHoles, 0u);
        EXPECT_EQ(sim.mEntries[0].mPhase, FarSim::Held);
        // It stays, whatever the budget says; the nearer cluster (which has its ground) is the one the budget blocks.
        EXPECT_EQ(sim.mEntries[1].mPhase, FarSim::Absent);
        EXPECT_FALSE(plan.mOverBudget);
        EXPECT_EQ(plan.mNoFallbackBytes, 6 * farMib);
    }

    // The eye leaves the live objects' range while a far cluster is fading out (finding 2 of the review of the far
    // eviction): its square stops being wholly inside the paging range, so the live objects stop being its fallback,
    // and the ground rule must take over in the same frame. The ground is trivially drawn while the square is inside
    // the range (the live terrain draws it), so the count of stable frames must not restart there.
    TEST(LodFarEvictionSim, leaving_the_live_range_during_a_fade_out_does_not_cancel_the_eviction)
    {
        // Two clusters: pressure never frees the last committed one, so a nearer cluster that stays is needed.
        FarSim sim = makeFarSim(2, true);
        sim.mEntries[0].mFarX = 37000; // far corner 37014 from the eye at 0: inside the range less its margin
        sim.mEntries[1].mFarX = 30000;
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        ResidencyPlan plan;
        for (int frame = 0; frame < 200; ++frame)
            sim.step(farParams(100 * farMib), plan);
        ASSERT_EQ(sim.mEntries[0].mPhase, FarSim::Held);
        ASSERT_EQ(sim.mEntries[1].mPhase, FarSim::Held);
        // The budget shrinks: the live objects can take the farther cluster's cells, so it is freed.
        sim.step(farParams(3 * farMib), plan);
        ASSERT_EQ(sim.mEntries[0].mPhase, FarSim::Leaving);
        sim.mEyeX = -4000; // the far corner is now 41000 away: past the range; the ground is drawn
        for (int frame = 0; frame < 300; ++frame)
            sim.step(farParams(3 * farMib), plan);
        EXPECT_EQ(sim.mEntries[0].mCancels, 0u);
        EXPECT_EQ(sim.mEntries[0].mFrees, 1u);
        EXPECT_EQ(sim.mEntries[0].mPhase, FarSim::Absent);
        EXPECT_EQ(sim.mHoles, 0u);
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
    }

    TEST(LodFarEvictionSim, a_camera_swaying_across_the_edge_of_the_live_range_causes_no_cancelled_fade_outs)
    {
        FarSim sim = makeFarSim(2, true);
        sim.mEntries[0].mFarX = 37000;
        sim.mEntries[1].mFarX = 30000;
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        ResidencyPlan plan;
        for (int frame = 0; frame < 200; ++frame)
            sim.step(farParams(100 * farMib), plan);
        ASSERT_EQ(sim.mEntries[0].mPhase, FarSim::Held);
        // From now on the budget is smaller than the cluster; the eye sways 4000 units (the far corner crosses the
        // range, 40000, and the margin of its hysteresis) with a period of 16 frames, faster than a fade out.
        std::uint32_t outward = 0;
        bool wasOutside = false;
        for (int frame = 0; frame < 3000; ++frame)
        {
            const bool outside = (frame / 8) % 2 == 1;
            sim.mEyeX = outside ? -4000 : 0;
            if (outside && !wasOutside)
                ++outward;
            wasOutside = outside;
            sim.step(farParams(3 * farMib), plan);
        }
        EXPECT_GT(outward, 30u);
        EXPECT_EQ(sim.mHoles, 0u);
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        // The fade out that is running when the camera first goes out is not cancelled (the old gating, which restarted
        // the count of stable frames inside the range, cancelled it: 1 cancel in this run).
        EXPECT_EQ(sim.mEntries[0].mCancels, 0u);
        // And the cluster is not loaded and freed over and over: it has the ground the whole time.
        EXPECT_LE(sim.mEntries[0].mLoads, 3u);
        EXPECT_LE(sim.mEntries[0].mFrees, 3u);
    }

    // Finding 1 of the review of the far eviction: the wait for the ground delayed a newly chosen cluster by 30 frames
    // or more with the budget nowhere near reached, because the counters started only when the cluster was chosen.
    TEST(LodFarEvictionSim, a_cluster_chosen_later_whose_ground_was_drawn_all_along_is_admitted_at_once)
    {
        // Entry 0 is held and farther from the eye than entry 1 by more than the swap margin allows; entry 1 is
        // not chosen until frame 300, but its ground has been drawn since frame 0. The budget fits one cluster, so
        // the budget presses.
        FarSim sim = makeFarSim(2, true);
        sim.mEntries[0].mFarX = 120000;
        sim.mEntries[1].mFarX = 45000;
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        sim.mChosen = [](int frame, std::size_t index) { return index == 0 || frame >= 300; };
        const ResidencyParams params = farParams(7 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 305; ++frame)
            sim.step(params, plan);
        std::size_t firstLoadOfNear = 0;
        for (std::size_t f = 300; f < sim.mLoadLog.size(); ++f)
        {
            for (const std::uint32_t index : sim.mLoadLog[f])
            {
                if (index == 1 && firstLoadOfNear == 0)
                    firstLoadOfNear = f;
            }
        }
        EXPECT_NE(firstLoadOfNear, 0u) << "entry 1 was not loaded within 5 frames of being chosen";
        EXPECT_EQ(sim.mHoles, 0u);
    }

    TEST(LodFarEvictionSim, with_the_budget_nowhere_near_reached_nothing_waits_for_its_ground)
    {
        // Twenty clusters, 6 MiB each, a budget of 1 GiB; the ground is not drawn for the first 400 frames (a terrain
        // tile still loading). Without pressure there is no reason to wait: every cluster is loaded in the first
        // frames, as it was before the ground rule existed.
        FarSim sim = makeFarSim(20, true);
        sim.mGround = [](int frame, std::size_t) { return frame >= 400; };
        const ResidencyParams params = farParams(1024 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 30; ++frame)
            sim.step(params, plan);
        EXPECT_EQ(sim.totalLoads(), 20u);
        for (const FarSim::Entry& entry : sim.mEntries)
            EXPECT_NE(entry.mPhase, FarSim::Absent);
    }

    TEST(LodFarEvictionSim, under_pressure_a_cluster_without_drawn_ground_still_waits_for_it)
    {
        // The same twenty clusters against a budget of 30 MiB: the clusters are not admitted before their ground is
        // drawn (they would be no-fallback items, loaded whatever the budget says and given up again afterwards).
        FarSim sim = makeFarSim(20, true);
        sim.mGround = [](int frame, std::size_t) { return frame >= 100; };
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        const ResidencyParams params = farParams(30 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 3000; ++frame)
            sim.step(params, plan);
        EXPECT_LE(sim.totalLoads(), 8u); // 5 fit; a few loads in the first frames before the pressure shows
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
        EXPECT_EQ(sim.mHoles, 0u);
    }

    TEST(LodFarEvictionSim, a_cluster_chosen_while_the_ground_is_still_missing_waits_for_it_under_pressure)
    {
        // Start-up under a tight budget: the ground is not drawn until frame 350 (the terrain pack is still being
        // checked) and the cluster is chosen at frame 300. Its wait runs from the frame it is chosen, not from the
        // frame the pack opened: it must not be admitted as an item with no fallback (and loaded whatever the budget
        // says) at once.
        FarSim sim = makeFarSim(2, true);
        sim.mEntries[0].mFarX = 120000;
        sim.mEntries[1].mFarX = 45000;
        for (FarSim::Entry& entry : sim.mEntries)
            entry.mEstimate = entry.mTrueBytes;
        sim.mGround = [](int frame, std::size_t) { return frame >= 350; };
        sim.mChosen = [](int frame, std::size_t index) { return index == 0 || frame >= 300; };
        const ResidencyParams params = farParams(7 * farMib);
        ResidencyPlan plan;
        for (int frame = 0; frame < 340; ++frame)
            sim.step(params, plan);
        // Entry 0 (far) is admitted after its own wait; entry 1 was chosen at frame 300 and has waited 40 frames.
        EXPECT_EQ(sim.mEntries[1].mLoads, 0u);
        EXPECT_EQ(sim.mFreedWithoutFallback, 0u);
    }

    TEST(LodFarEvictionSim, two_runs_of_the_same_sequence_make_the_same_plans)
    {
        const auto run = [] {
            FarSim sim = makeFarSim(40, true);
            sim.mGround = [](int frame, std::size_t index) { return (frame / 37 + static_cast<int>(index)) % 11 != 0; };
            const ResidencyParams params = farParams(60 * farMib);
            ResidencyPlan plan;
            for (int frame = 0; frame < 4000; ++frame)
            {
                sim.mEyeX = 12.0 * frame;
                sim.step(params, plan);
            }
            return std::make_pair(sim.mLoadLog, sim.mFreeLog);
        };
        const auto a = run();
        const auto b = run();
        EXPECT_EQ(a.first, b.first);
        EXPECT_EQ(a.second, b.second);
        // And the run did something: not an empty comparison.
        std::size_t actions = 0;
        for (const auto& loads : a.first)
            actions += loads.size();
        EXPECT_GT(actions, 10u);
    }

    TEST(LodGroundRule, a_morrowind_world_needs_a_budget_an_evicting_tier_and_live_objects)
    {
        EXPECT_TRUE(groundRuleApplies(true, true, true, false));
        EXPECT_FALSE(groundRuleApplies(false, true, true, false));
        EXPECT_FALSE(groundRuleApplies(true, false, true, false));
        EXPECT_FALSE(groundRuleApplies(true, true, false, false));
    }

    TEST(LodGroundRule, a_neutral_world_does_not_need_live_objects_but_still_a_budget_and_an_evicting_tier)
    {
        EXPECT_TRUE(groundRuleApplies(true, true, false, true));
        EXPECT_TRUE(groundRuleApplies(true, true, true, true));
        EXPECT_FALSE(groundRuleApplies(false, true, false, true));
        EXPECT_FALSE(groundRuleApplies(true, false, false, true));
    }
}
