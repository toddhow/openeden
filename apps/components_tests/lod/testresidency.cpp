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

    TEST(LodResidency, one_call_looks_at_a_bounded_number_of_blocked_candidates)
    {
        ResidencyParams params;
        params.mBudgetBytes = 10 * mib;
        params.mMaxBlockedPerCall = 5;
        ResidencyItem fixed = held(9 * mib, 1, true);
        fixed.mReplaceable = false;
        std::vector<ResidencyItem> items = { fixed };
        for (int i = 0; i < 30; ++i)
            items.push_back(wantedAbsent(4 * mib, 10.f + static_cast<float>(i)));
        EXPECT_EQ(plan(items, params).mBlocked, 5u);
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
