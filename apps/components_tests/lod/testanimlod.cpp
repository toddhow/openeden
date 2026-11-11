// Task 0096 stage d: the pure part of the animation LOD of the far (object paging) chunks. Which update rate an object
// gets from its distance (with hysteresis), when it runs, what it is given (the time that passed since it last ran, so
// its animation phase never changes), the per-frame budget of controllers and the fairness of the rotation.
#include <gtest/gtest.h>

#include <components/lod/animlod.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace
{
    using namespace Lod;

    constexpr float kStop = 1000.f;
    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();

    AnimItem item(float distance, unsigned controllers = 1, std::uint8_t phase = 0, AnimTier tier = AnimTier::Full)
    {
        AnimItem result;
        result.distance = distance;
        result.controllers = static_cast<std::uint16_t>(controllers);
        result.phase = phase;
        result.tier = tier;
        return result;
    }

    // Runs the planner for one frame and returns the runs.
    std::vector<AnimRun> runFrame(std::vector<AnimItem>& items, double dt, unsigned frameNumber,
        unsigned budget = 1000000, float stop = kStop)
    {
        std::vector<AnimRun> runs;
        runs.reserve(items.size());
        AnimBudget b{ budget, budget };
        planAnimFrame(items, dt, stop, frameNumber, b, runs);
        return runs;
    }
}

TEST(LodAnimTier, the_periods_are_one_four_sixteen_and_none_when_stopped)
{
    EXPECT_EQ(animTierPeriod(AnimTier::Full), 1u);
    EXPECT_EQ(animTierPeriod(AnimTier::Quarter), 4u);
    EXPECT_EQ(animTierPeriod(AnimTier::Sixteenth), 16u);
    EXPECT_EQ(animTierPeriod(AnimTier::Stopped), 0u);
}

TEST(LodAnimTier, distances_pick_the_tier_at_a_quarter_sixty_percent_and_the_stop_distance)
{
    EXPECT_EQ(chooseAnimTier(0.f, AnimTier::Full, kStop), AnimTier::Full);
    EXPECT_EQ(chooseAnimTier(100.f, AnimTier::Full, kStop), AnimTier::Full);
    EXPECT_EQ(chooseAnimTier(400.f, AnimTier::Quarter, kStop), AnimTier::Quarter);
    EXPECT_EQ(chooseAnimTier(800.f, AnimTier::Sixteenth, kStop), AnimTier::Sixteenth);
    EXPECT_EQ(chooseAnimTier(1500.f, AnimTier::Stopped, kStop), AnimTier::Stopped);
}

TEST(LodAnimTier, a_far_jump_crosses_several_tiers_at_once)
{
    EXPECT_EQ(chooseAnimTier(5000.f, AnimTier::Full, kStop), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(10.f, AnimTier::Stopped, kStop), AnimTier::Full);
    EXPECT_EQ(chooseAnimTier(500.f, AnimTier::Stopped, kStop), AnimTier::Quarter);
}

TEST(LodAnimTier, hysteresis_keeps_the_tier_near_a_boundary)
{
    // The Full/Quarter boundary is 250: a Full object stays Full until 10 percent past it, a Quarter object stays
    // Quarter until 10 percent before it.
    EXPECT_EQ(chooseAnimTier(260.f, AnimTier::Full, kStop), AnimTier::Full);
    EXPECT_EQ(chooseAnimTier(280.f, AnimTier::Full, kStop), AnimTier::Quarter);
    EXPECT_EQ(chooseAnimTier(240.f, AnimTier::Quarter, kStop), AnimTier::Quarter);
    EXPECT_EQ(chooseAnimTier(220.f, AnimTier::Quarter, kStop), AnimTier::Full);
    // The stop boundary too.
    EXPECT_EQ(chooseAnimTier(1050.f, AnimTier::Sixteenth, kStop), AnimTier::Sixteenth);
    EXPECT_EQ(chooseAnimTier(1150.f, AnimTier::Sixteenth, kStop), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(950.f, AnimTier::Stopped, kStop), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(850.f, AnimTier::Stopped, kStop), AnimTier::Sixteenth);
}

TEST(LodAnimTier, a_distance_wobbling_across_a_boundary_never_flips_the_tier)
{
    for (const AnimTier start : { AnimTier::Full, AnimTier::Quarter })
    {
        AnimTier tier = start;
        for (int i = 0; i < 100; ++i)
        {
            tier = chooseAnimTier(i % 2 == 0 ? 245.f : 255.f, tier, kStop);
            EXPECT_EQ(tier, start);
        }
    }
}

TEST(LodAnimTier, bad_input_stops_the_animation)
{
    EXPECT_EQ(chooseAnimTier(kNan, AnimTier::Full, kStop), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(kInf, AnimTier::Full, kStop), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(10.f, AnimTier::Full, 0.f), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(10.f, AnimTier::Full, -5.f), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(10.f, AnimTier::Full, kNan), AnimTier::Stopped);
    EXPECT_EQ(chooseAnimTier(-10.f, AnimTier::Full, kStop), AnimTier::Full);
}

TEST(LodAnimPhase, is_deterministic_in_range_and_spread)
{
    std::set<unsigned> seen;
    for (std::uint64_t id = 0; id < 2000; ++id)
    {
        EXPECT_EQ(animPhaseOf(id), animPhaseOf(id));
        EXPECT_LT(animPhaseOf(id), 16u);
        seen.insert(animPhaseOf(id));
    }
    EXPECT_EQ(seen.size(), 16u);
}

TEST(LodAnimPhase, the_start_time_is_zero_for_group_zero_and_stable_in_range_for_the_others)
{
    EXPECT_EQ(animStartTime(12345, 0), 0.f);
    std::set<float> seen;
    for (std::uint64_t id = 0; id < 500; ++id)
        for (int group = 1; group < 4; ++group)
        {
            const float t = animStartTime(id, group);
            EXPECT_EQ(t, animStartTime(id, group));
            EXPECT_GE(t, 0.f);
            EXPECT_LT(t, 3600.f);
            seen.insert(t);
        }
    EXPECT_GT(seen.size(), 1400u);
}

TEST(LodAnimPlan, a_full_rate_object_runs_every_frame_with_that_frames_time)
{
    std::vector<AnimItem> items{ item(100.f) };
    for (unsigned f = 0; f < 10; ++f)
    {
        const auto runs = runFrame(items, 0.016, f);
        ASSERT_EQ(runs.size(), 1u);
        EXPECT_EQ(runs[0].index, 0u);
        EXPECT_FLOAT_EQ(runs[0].dt, 0.016f);
    }
}

TEST(LodAnimPlan, a_quarter_rate_object_runs_once_in_four_frames_with_the_four_frames_time)
{
    std::vector<AnimItem> items{ item(400.f, 1, 0, AnimTier::Quarter) };
    unsigned runCount = 0;
    double delivered = 0;
    for (unsigned f = 0; f < 16; ++f)
    {
        const auto runs = runFrame(items, 0.01, f);
        if (!runs.empty())
        {
            ++runCount;
            delivered += runs[0].dt;
        }
    }
    EXPECT_EQ(runCount, 4u);
    EXPECT_NEAR(delivered + items[0].pendingDt, 0.16, 1e-6);
    EXPECT_GE(delivered, 0.12 - 1e-6);
}

TEST(LodAnimPlan, a_sixteenth_rate_object_runs_once_in_sixteen_frames)
{
    std::vector<AnimItem> items{ item(900.f, 1, 3, AnimTier::Sixteenth) };
    unsigned runCount = 0;
    for (unsigned f = 0; f < 160; ++f)
        runCount += static_cast<unsigned>(runFrame(items, 0.01, f).size());
    EXPECT_EQ(runCount, 10u);
}

TEST(LodAnimPlan, objects_of_different_phases_do_not_all_run_on_the_same_frame)
{
    std::vector<AnimItem> items;
    for (std::uint8_t phase = 0; phase < 4; ++phase)
        items.push_back(item(400.f, 1, phase, AnimTier::Quarter));
    for (unsigned f = 0; f < 40; ++f)
        EXPECT_EQ(runFrame(items, 0.01, f).size(), 1u) << "frame " << f;
}

TEST(LodAnimPlan, a_stopped_object_never_runs_but_keeps_its_time_for_when_it_comes_back)
{
    std::vector<AnimItem> items{ item(5000.f, 1, 0, AnimTier::Stopped) };
    for (unsigned f = 0; f < 100; ++f)
        EXPECT_TRUE(runFrame(items, 0.01, f).empty());
    EXPECT_NEAR(items[0].pendingDt, 1.0, 1e-9);
    items[0].distance = 100.f;
    const auto runs = runFrame(items, 0.01, 100);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].dt, 1.01f, 1e-5f);
    EXPECT_EQ(items[0].pendingDt, 0.0);
}

TEST(LodAnimPlan, the_phase_of_an_animation_never_changes_whatever_the_rate)
{
    // The time given to the animation plus the time still waiting equals the time that passed, always: this is what keeps
    // an object in step with a near copy of itself when its rate changes or it stops and starts.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> distance(0.f, 2500.f);
    std::vector<AnimItem> items;
    for (int i = 0; i < 20; ++i)
        items.push_back(item(distance(rng), 1 + i % 3, static_cast<std::uint8_t>(i % 16)));
    std::vector<double> given(items.size(), 0.0);
    double elapsed = 0;
    for (unsigned f = 0; f < 600; ++f)
    {
        const double dt = 0.005 + 0.0001 * (f % 7);
        elapsed += dt;
        if (f % 5 == 0)
            for (auto& i : items)
                i.distance = distance(rng);
        for (const AnimRun& run : runFrame(items, dt, f, 12))
            given[run.index] += run.dt;
        for (std::size_t i = 0; i < items.size(); ++i)
            ASSERT_NEAR(given[i] + items[i].pendingDt, elapsed, 1e-3) << "item " << i << " frame " << f;
    }
}

TEST(LodAnimPlan, the_budget_caps_the_controllers_run_in_a_frame_and_nothing_starves)
{
    std::vector<AnimItem> items;
    for (int i = 0; i < 10; ++i)
        items.push_back(item(10.f, 10, 0));
    std::vector<unsigned> timesRun(items.size(), 0);
    for (unsigned f = 0; f < 50; ++f)
    {
        const auto runs = runFrame(items, 0.01, f, 25);
        EXPECT_LE(runs.size(), 2u);
        for (const auto& run : runs)
            ++timesRun[run.index];
    }
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        EXPECT_GE(timesRun[i], 8u) << "item " << i;
        EXPECT_LE(timesRun[i], 12u) << "item " << i;
    }
}

TEST(LodAnimPlan, an_object_with_more_controllers_than_the_whole_budget_still_runs_alone)
{
    std::vector<AnimItem> items{ item(10.f, 50) };
    const auto runs = runFrame(items, 0.01, 0, 10);
    EXPECT_EQ(runs.size(), 1u);
}

TEST(LodAnimPlan, the_budget_is_shared_between_calls_for_several_chunks)
{
    std::vector<AnimItem> a{ item(10.f, 10), item(10.f, 10) };
    std::vector<AnimItem> b{ item(10.f, 10), item(10.f, 10) };
    std::vector<AnimRun> runs;
    runs.reserve(4);
    AnimBudget budget{ 30, 30 };
    planAnimFrame(a, 0.01, kStop, 0, budget, runs);
    EXPECT_EQ(runs.size(), 2u);
    EXPECT_EQ(budget.left, 10u);
    runs.clear();
    planAnimFrame(b, 0.01, kStop, 0, budget, runs);
    EXPECT_EQ(runs.size(), 1u);
    EXPECT_EQ(budget.left, 0u);
}

TEST(LodAnimPlan, a_deferred_object_is_not_lost_it_runs_with_all_its_waiting_time_later)
{
    std::vector<AnimItem> items{ item(10.f, 10), item(10.f, 10) };
    double given[2] = { 0, 0 };
    for (unsigned f = 0; f < 20; ++f)
        for (const auto& run : runFrame(items, 0.01, f, 10))
            given[run.index] += run.dt;
    EXPECT_NEAR(given[0] + items[0].pendingDt, 0.2, 1e-4);
    EXPECT_NEAR(given[1] + items[1].pendingDt, 0.2, 1e-4);
    EXPECT_GT(given[0], 0.0);
    EXPECT_GT(given[1], 0.0);
}

TEST(LodAnimPlan, the_plan_does_not_allocate_once_the_run_list_has_room)
{
    std::vector<AnimItem> items;
    for (int i = 0; i < 64; ++i)
        items.push_back(item(static_cast<float>(i * 20), 2, static_cast<std::uint8_t>(i % 16)));
    std::vector<AnimRun> runs;
    runs.reserve(items.size());
    const auto* data = runs.data();
    const auto capacity = runs.capacity();
    for (unsigned f = 0; f < 200; ++f)
    {
        runs.clear();
        AnimBudget budget{ 40, 40 };
        planAnimFrame(items, 0.01, kStop, f, budget, runs);
        ASSERT_EQ(runs.capacity(), capacity);
        ASSERT_EQ(runs.data(), data);
    }
}

TEST(LodAnimPlan, bad_time_and_an_empty_list_are_harmless)
{
    std::vector<AnimItem> none;
    EXPECT_TRUE(runFrame(none, 0.01, 0).empty());
    std::vector<AnimItem> items{ item(10.f) };
    runFrame(items, -1.0, 0);
    EXPECT_GE(items[0].pendingDt, 0.0);
    runFrame(items, std::numeric_limits<double>::quiet_NaN(), 1);
    EXPECT_FALSE(std::isnan(items[0].pendingDt));
    runFrame(items, std::numeric_limits<double>::infinity(), 2);
    EXPECT_TRUE(std::isfinite(items[0].pendingDt));
}

TEST(LodAnimPlan, the_same_inputs_give_the_same_plan)
{
    auto run = [] {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> distance(0.f, 1500.f);
        std::vector<AnimItem> items;
        for (int i = 0; i < 30; ++i)
            items.push_back(item(distance(rng), 1 + i % 4, static_cast<std::uint8_t>(i * 5 % 16)));
        std::vector<std::uint32_t> log;
        for (unsigned f = 0; f < 100; ++f)
        {
            if (f % 9 == 0)
                for (auto& i : items)
                    i.distance = distance(rng);
            for (const auto& r : runFrame(items, 0.016, f, 20))
                log.push_back(r.index + 1000 * f);
        }
        return log;
    };
    EXPECT_EQ(run(), run());
}
