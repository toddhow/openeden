// Tests for Render::VideoMemory::PoolReleasePlanner (task 0080): when the scale-back asks VSG to hand its unused
// device-memory pools back to the driver. Written from the rule "a few delayed attempts after a reduction, never more
// often than a minimum interval, nothing without a reduction". Pure CPU.

#include <components/render/vrampoolrelease.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace Render::VideoMemory;

    TEST(VramPoolRelease, nothing_is_due_without_a_reduction)
    {
        PoolReleasePlanner planner;
        EXPECT_FALSE(planner.pending());
        for (double t = 0.0; t < 100.0; t += 0.5)
            EXPECT_FALSE(planner.due(t));
    }

    TEST(VramPoolRelease, the_first_attempt_waits_for_the_first_delay)
    {
        PoolReleasePlanner planner;
        planner.reductionApplied(10.0);
        EXPECT_TRUE(planner.pending());
        EXPECT_FALSE(planner.due(10.0));
        EXPECT_FALSE(planner.due(10.9));
        EXPECT_TRUE(planner.due(11.0));
        EXPECT_FALSE(planner.due(11.0)); // once only
    }

    TEST(VramPoolRelease, three_attempts_follow_one_reduction_and_then_it_stops)
    {
        PoolReleasePlanner planner;
        planner.reductionApplied(0.0);
        int count = 0;
        for (double t = 0.0; t <= 60.0; t += 0.5)
            if (planner.due(t))
                ++count;
        EXPECT_EQ(count, 3);
        EXPECT_FALSE(planner.pending());
    }

    TEST(VramPoolRelease, the_attempts_come_at_the_configured_delays)
    {
        PoolReleaseParams params;
        params.delays[0] = 1.0;
        params.delays[1] = 3.0;
        params.delays[2] = 8.0;
        PoolReleasePlanner planner(params);
        planner.reductionApplied(100.0);
        EXPECT_TRUE(planner.due(101.0));
        EXPECT_FALSE(planner.due(102.9));
        EXPECT_TRUE(planner.due(103.0));
        EXPECT_FALSE(planner.due(107.9));
        EXPECT_TRUE(planner.due(108.0));
        EXPECT_FALSE(planner.pending());
    }

    TEST(VramPoolRelease, a_late_tick_makes_one_attempt_not_three)
    {
        PoolReleasePlanner planner;
        planner.reductionApplied(0.0);
        EXPECT_TRUE(planner.due(30.0));
        EXPECT_FALSE(planner.due(30.5));
        EXPECT_FALSE(planner.pending());
    }

    TEST(VramPoolRelease, a_new_reduction_starts_the_schedule_again)
    {
        PoolReleasePlanner planner;
        planner.reductionApplied(0.0);
        planner.reductionApplied(0.5);
        EXPECT_FALSE(planner.due(1.0)); // the first schedule is gone
        EXPECT_TRUE(planner.due(1.5));
    }

    TEST(VramPoolRelease, attempts_are_never_closer_than_the_minimum_interval)
    {
        PoolReleaseParams params;
        params.delays[0] = 1.0;
        params.delays[1] = 2.0;
        params.delays[2] = 3.0;
        params.minInterval = 5.0;
        PoolReleasePlanner planner(params);
        planner.reductionApplied(0.0);
        EXPECT_TRUE(planner.due(1.0));
        EXPECT_FALSE(planner.due(2.0)); // due by its delay but too soon after the last
        EXPECT_FALSE(planner.due(3.0));
        EXPECT_TRUE(planner.due(6.0));
        EXPECT_FALSE(planner.pending());
    }

    TEST(VramPoolRelease, the_minimum_interval_also_holds_across_reductions)
    {
        PoolReleaseParams params;
        params.minInterval = 4.0;
        PoolReleasePlanner planner(params);
        planner.reductionApplied(0.0);
        EXPECT_TRUE(planner.due(1.0));
        planner.reductionApplied(1.5);
        EXPECT_FALSE(planner.due(2.5)); // 1 s after the last attempt, interval 4 s
        EXPECT_TRUE(planner.due(5.0));
    }

    TEST(VramPoolRelease, a_hold_interval_repeats_the_attempt_while_reduced)
    {
        PoolReleaseParams params;
        params.holdInterval = 10.0;
        PoolReleasePlanner planner(params);
        EXPECT_FALSE(planner.due(0.0)); // not reduced
        planner.setReduced(true);
        EXPECT_TRUE(planner.due(1.0));
        EXPECT_FALSE(planner.due(10.9));
        EXPECT_TRUE(planner.due(11.0));
        planner.setReduced(false);
        EXPECT_FALSE(planner.due(100.0));
    }

    TEST(VramPoolRelease, a_zero_hold_interval_never_repeats)
    {
        PoolReleaseParams params;
        params.holdInterval = 0.0;
        PoolReleasePlanner planner(params);
        planner.setReduced(true);
        for (double t = 0.0; t < 100.0; t += 1.0)
            EXPECT_FALSE(planner.due(t));
    }

    TEST(VramPoolRelease, a_negative_delay_counts_as_zero)
    {
        PoolReleaseParams params;
        params.delays[0] = -3.0;
        PoolReleasePlanner planner(params);
        planner.reductionApplied(5.0);
        EXPECT_TRUE(planner.due(5.0));
    }
}

#include <components/render/vramscaler.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // One system that can always be reduced, under a driver whose usage is above the high water mark until the
    // system has been reduced; counts the calls to the pool release.
    struct ReleaseHarness
    {
        static constexpr std::int64_t mib = 1024 * 1024;
        int level = 0;
        int releaseCalls = 0;
        std::int64_t bytesPerRelease = 40 * mib;
        double holdInterval = 0.0; // the periodic attempts are tested on their own
        std::vector<std::string> lines;

        std::unique_ptr<Scaler> make(bool withRelease)
        {
            ScalerConfig c;
            c.order = "far objects";
            c.budgetFraction = 1.0;
            c.params.dwell = 1.0;
            c.params.settleSeconds = 1.0;
            c.releaseParams.holdInterval = holdInterval;
            if (withRelease)
                c.releasePools = [this] {
                    ++releaseCalls;
                    ReleaseResult r;
                    r.bytes = bytesPerRelease;
                    r.milliseconds = 0.5;
                    return r;
                };
            Adapter a;
            a.name = "far objects";
            a.maxLevel = 1;
            a.account = [this](Ledger& ledger) { ledger.set("lod.x", (400 * mib) >> level); };
            a.apply = [this](int target) {
                level = target;
                return true;
            };
            a.stepBytes = [](int, bool) { return std::int64_t{ 200 * mib }; };
            return std::make_unique<Scaler>(
                std::move(c), std::vector<Adapter>{ a },
                [this] {
                    Sample s;
                    s.budgetBytes = 1000 * mib;
                    s.usageBytes = level == 0 ? 980 * mib : 700 * mib;
                    return s;
                },
                [this](bool, const std::string& text) { lines.push_back(text); });
        }
    };

    void runFor(Scaler& scaler, double seconds)
    {
        for (double t = 0; t < seconds; t += 0.1)
            scaler.tick(0.1);
    }

    TEST(VramScalerPoolRelease, nothing_is_released_without_a_reduction)
    {
        ReleaseHarness h;
        h.level = 1; // already reduced before the scaler started: no step will happen
        auto scaler = h.make(true);
        runFor(*scaler, 30.0);
        EXPECT_EQ(h.releaseCalls, 0);
    }

    TEST(VramScalerPoolRelease, a_reduction_is_followed_by_three_attempts_and_logged)
    {
        ReleaseHarness h;
        auto scaler = h.make(true);
        runFor(*scaler, 40.0);
        ASSERT_EQ(h.level, 1);
        EXPECT_EQ(h.releaseCalls, 3);
        EXPECT_EQ(scaler->poolReleases(), 3);
        int logged = 0;
        for (const std::string& line : h.lines)
            if (line.find("released 40 MiB of unused video memory pools") != std::string::npos)
                ++logged;
        EXPECT_EQ(logged, 3);
    }

    TEST(VramScalerPoolRelease, no_callback_means_no_release_and_no_failure)
    {
        ReleaseHarness h;
        auto scaler = h.make(false);
        runFor(*scaler, 40.0);
        EXPECT_EQ(h.level, 1);
        EXPECT_EQ(h.releaseCalls, 0);
    }

    TEST(VramScalerPoolRelease, an_attempt_comes_after_the_step_not_in_the_same_sample)
    {
        ReleaseHarness h;
        auto scaler = h.make(true);
        for (double t = 0; t < 40.0 && h.level == 0; t += 0.1)
            scaler->tick(0.1);
        ASSERT_EQ(h.level, 1);
        EXPECT_EQ(h.releaseCalls, 0) << "the objects a step drops leave the deletion queue frames later";
    }

    TEST(VramScalerPoolRelease, while_reduced_an_attempt_is_made_every_hold_interval)
    {
        ReleaseHarness h;
        h.holdInterval = 10.0;
        auto scaler = h.make(true);
        runFor(*scaler, 100.0);
        ASSERT_EQ(h.level, 1);
        // three scheduled attempts in the first 10 s, then about one every 10 s
        EXPECT_GE(h.releaseCalls, 10);
        EXPECT_LE(h.releaseCalls, 14);
    }

    TEST(VramScalerPoolRelease, nothing_periodic_happens_while_nothing_is_reduced)
    {
        ReleaseHarness h;
        h.holdInterval = 10.0;
        h.level = 1;
        auto scaler = h.make(true);
        // the harness reports the level the adapter is at, but the controller's own count is 0: no reduction active
        runFor(*scaler, 100.0);
        EXPECT_EQ(h.releaseCalls, 0);
    }
}

#include <components/render/vrambudgetscript.hpp>

namespace
{
    TEST(VramBudgetScript, parses_steps_and_looks_up_the_budget_by_time)
    {
        const auto steps = parseBudgetScript("0:900,40:380,100.5:900");
        ASSERT_EQ(steps.size(), 3u);
        EXPECT_EQ(budgetAt(steps, 0.0), 900);
        EXPECT_EQ(budgetAt(steps, 39.9), 900);
        EXPECT_EQ(budgetAt(steps, 40.0), 380);
        EXPECT_EQ(budgetAt(steps, 100.4), 380);
        EXPECT_EQ(budgetAt(steps, 100.5), 900);
        EXPECT_EQ(budgetAt(steps, 1e6), 900);
    }

    TEST(VramBudgetScript, before_the_first_step_there_is_no_budget)
    {
        const auto steps = parseBudgetScript("5:300");
        EXPECT_EQ(budgetAt(steps, 4.9), -1);
        EXPECT_EQ(budgetAt({}, 10.0), -1);
    }

    TEST(VramBudgetScript, anything_malformed_gives_an_empty_script)
    {
        for (const char* text :
            { "", "abc", "5", "5:", ":5", "5:0", "5:-3", "5:300,5:400", "9:300,5:400", "5:300,", "5:30x", "-1:300" })
            EXPECT_TRUE(parseBudgetScript(text).empty()) << text;
    }
}
