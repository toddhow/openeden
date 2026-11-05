// Tests for the pure parts of the video memory scale-back, Render::VideoMemory (task 0080, stages a and b). Written
// from the interfaces and the rules of docs/tasks/0080-vram-scale-back-and-mip-streaming.md ("The ledger", "The
// controller", "Interfaces") before the implementation. Pure CPU: no device, window or game data.

#include <components/render/vramaccount.hpp>
#include <components/render/vramcontroller.hpp>
#include <components/render/vramorder.hpp>
#include <components/render/vramsource.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Render::VideoMemory;

    constexpr std::int64_t budget = 1000; // bytes: the numbers read as thousandths of the budget

    struct Event
    {
        double time;
        Kind kind;
        int system;
        bool operator==(const Event& other) const
        {
            return time == other.time && kind == other.kind && system == other.system;
        }
    };

    // A small world: a ledger with one fixed entry and one entry per system, whose size follows its level.
    struct World
    {
        std::vector<std::int64_t> sizeAtLevel; // per system: bytes at level 0; each level halves it
        std::int64_t fixed = 0;
        std::int64_t extraUsage = 0; // usage the ledger cannot explain
        Ledger ledger;
        Controller controller;
        std::vector<Event> log;

        World(std::vector<SystemInfo> order, const Params& params, std::int64_t fixedBytes,
            std::vector<std::int64_t> sizes)
            : sizeAtLevel(std::move(sizes))
            , fixed(fixedBytes)
            , controller(std::move(order), params)
        {
            refresh();
        }

        std::int64_t bytesAt(int system, int level) const
        {
            return sizeAtLevel[static_cast<std::size_t>(system)] >> level;
        }

        void refresh()
        {
            ledger.set("fixed", fixed);
            for (std::size_t i = 0; i < sizeAtLevel.size(); ++i)
                ledger.set(
                    "system" + std::to_string(i), bytesAt(static_cast<int>(i), controller.level(static_cast<int>(i))));
        }

        // One sample at `now`; the action is carried out at once (the ledger follows) unless `refuse`.
        Action step(double now, bool refuse = false)
        {
            Sample sample;
            sample.budgetBytes = budget;
            sample.usageBytes = ledger.total() + extraUsage;
            const Action action = controller.update(now, sample, ledger, [this](int system, bool down) {
                const int level = controller.level(system);
                return down ? bytesAt(system, level) - bytesAt(system, level + 1)
                            : bytesAt(system, level - 1) - bytesAt(system, level);
            });
            if (action.kind == Kind::Reduce || action.kind == Kind::Restore)
            {
                log.push_back({ now, action.kind, action.system });
                if (refuse)
                    controller.notApplied(action.system);
                else
                {
                    controller.applied(action.system, action.kind);
                    refresh();
                }
            }
            else if (action.kind == Kind::Exhausted)
                log.push_back({ now, action.kind, action.system });
            return action;
        }

        void run(double from, double to, double interval = 0.5)
        {
            for (double t = from; t <= to; t += interval)
                step(t);
        }

        int count(Kind kind) const
        {
            int n = 0;
            for (const Event& e : log)
                if (e.kind == kind)
                    ++n;
            return n;
        }
    };

    std::vector<SystemInfo> threeSystems(int maxLevel = 3)
    {
        return { { "a", maxLevel, 0 }, { "b", maxLevel, 0 }, { "c", maxLevel, 0 } };
    }

    Params quickParams()
    {
        Params p;
        p.dwell = 1.0;
        p.restoreDwell = 5.0;
        p.settleSeconds = 1.0;
        return p;
    }
}

// ---- the ledger ----

TEST(VideoMemoryLedger, set_creates_and_replaces)
{
    Ledger ledger;
    ledger.set("shadow.point", 100);
    EXPECT_EQ(ledger.total(), 100);
    ledger.set("shadow.point", 40);
    EXPECT_EQ(ledger.total(), 40);
    ASSERT_EQ(ledger.entries().size(), 1u);
    EXPECT_EQ(ledger.entries()[0].first, "shadow.point");
    EXPECT_EQ(ledger.entries()[0].second, 40);
}

TEST(VideoMemoryLedger, add_creates_and_the_entry_goes_at_zero)
{
    Ledger ledger;
    ledger.add("texture.object", 30);
    ledger.add("texture.object", 20);
    EXPECT_EQ(ledger.total(), 50);
    ledger.add("texture.object", -50);
    EXPECT_TRUE(ledger.entries().empty());
    EXPECT_EQ(ledger.total(), 0);
}

TEST(VideoMemoryLedger, setting_zero_removes_the_entry)
{
    Ledger ledger;
    ledger.set("a", 5);
    ledger.set("a", 0);
    EXPECT_TRUE(ledger.entries().empty());
}

TEST(VideoMemoryLedger, entries_are_sorted_by_name_whatever_the_insertion_order)
{
    Ledger ledger;
    ledger.set("target.hdr", 1);
    ledger.set("gi", 2);
    ledger.set("shadow.point", 3);
    ledger.set("lod.geometry", 4);
    ASSERT_EQ(ledger.entries().size(), 4u);
    EXPECT_EQ(ledger.entries()[0].first, "gi");
    EXPECT_EQ(ledger.entries()[1].first, "lod.geometry");
    EXPECT_EQ(ledger.entries()[2].first, "shadow.point");
    EXPECT_EQ(ledger.entries()[3].first, "target.hdr");
}

TEST(VideoMemoryLedger, untracked_is_what_the_ledger_cannot_explain)
{
    Ledger ledger;
    ledger.set("a", 600);
    EXPECT_EQ(ledger.untracked(900), 300);
    EXPECT_EQ(ledger.untracked(600), 0);
    EXPECT_EQ(ledger.untracked(400), 0) << "usage below the ledger leaves nothing untracked, never a negative number";
    EXPECT_EQ(ledger.untracked(-1), 0) << "an unknown usage has no untracked part";
}

TEST(VideoMemoryLedger, get_returns_zero_for_a_missing_entry)
{
    Ledger ledger;
    ledger.set("a", 7);
    EXPECT_EQ(ledger.get("a"), 7);
    EXPECT_EQ(ledger.get("b"), 0);
}

TEST(VideoMemoryLedger, bytes_of_target_multiplies_the_dimensions)
{
    EXPECT_EQ(bytesOfTarget(1920, 1080, 8), 1920ll * 1080 * 8);
    EXPECT_EQ(bytesOfTarget(512, 512, 4, 24), 512ll * 512 * 4 * 24);
}

// ---- the fake source ----

TEST(VideoMemorySource, a_fake_source_gives_its_samples_in_order_and_repeats_the_last)
{
    FakeSource source({ { 100, 10 }, { 100, 20 }, { 90, 30 } });
    EXPECT_EQ(source.sample().usageBytes, 10);
    EXPECT_EQ(source.sample().usageBytes, 20);
    EXPECT_EQ(source.sample().usageBytes, 30);
    EXPECT_EQ(source.sample().usageBytes, 30);
    EXPECT_EQ(source.sample().budgetBytes, 90);
}

TEST(VideoMemorySource, an_empty_fake_source_is_unknown)
{
    FakeSource source({});
    EXPECT_EQ(source.sample().budgetBytes, -1);
    EXPECT_EQ(source.sample().usageBytes, -1);
}

// ---- the controller: reducing ----

TEST(VideoMemoryController, low_pressure_does_nothing)
{
    World world(threeSystems(), quickParams(), 300, { 200, 200, 200 });
    world.run(0, 60);
    EXPECT_TRUE(world.log.empty());
    EXPECT_EQ(world.controller.reductions(), 0);
}

TEST(VideoMemoryController, one_high_sample_is_not_enough_two_reduce_the_first_system)
{
    World world(threeSystems(), quickParams(), 400, { 400, 400, 400 }); // 1.2 of the budget: far over high water
    EXPECT_EQ(world.step(0.0).kind, Kind::None) << "the first high sample";
    const Action second = world.step(0.5);
    EXPECT_EQ(second.kind, Kind::Reduce);
    EXPECT_EQ(second.system, 0) << "the first system in the order";
    EXPECT_EQ(world.controller.level(0), 1);
}

TEST(VideoMemoryController, a_single_spike_does_not_reduce)
{
    World world(threeSystems(), quickParams(), 250, { 200, 200, 200 }); // 0.85 of the budget
    world.extraUsage = 0;
    world.step(0.0);
    world.extraUsage = 380; // one high sample (smoothed 0.934, back under 0.92 a second later)
    EXPECT_EQ(world.step(0.5).kind, Kind::None);
    world.extraUsage = 0;
    for (double t = 1.0; t < 20.0; t += 0.5)
        EXPECT_EQ(world.step(t).kind, Kind::None);
}

TEST(VideoMemoryController, the_next_reduction_waits_for_the_dwell)
{
    Params p = quickParams();
    p.dwell = 4.0;
    p.settleSeconds = 1.0;
    // A load the reductions never relieve (a fixed ledger entry the systems do not own, unexplained usage on top).
    World world(threeSystems(), p, 100, { 0, 0, 0 });
    world.extraUsage = 900; // 1.0 of the budget
    world.run(0, 4.0);
    ASSERT_EQ(world.count(Kind::Reduce), 1) << "one reduce at 0.5 and none until the dwell has passed";
    EXPECT_EQ(world.log[0].time, 0.5);
    world.run(4.5, 4.5);
    EXPECT_EQ(world.count(Kind::Reduce), 2) << "0.5 + dwell 4.0 = 4.5";
    EXPECT_EQ(world.log[1].system, 0) << "system 0 is not yet at its maximum level 3";
}

TEST(VideoMemoryController, settling_holds_back_a_second_reduce_when_the_ledger_does_not_show_the_relief)
{
    Params p = quickParams();
    p.dwell = 0.5;
    p.settleSeconds = 3.0;
    // The systems' entries claim to shrink, but a test that does not call refresh() leaves the ledger as it was: the
    // relief is never shown.
    Ledger ledger;
    ledger.set("fixed", 1000);
    Controller controller(threeSystems(), p);
    int reduces = 0;
    double firstSecond = -1;
    for (double t = 0.0; t <= 6.0; t += 0.5)
    {
        const Action a = controller.update(t, { budget, 1000 }, ledger, [](int, bool) { return 200; });
        if (a.kind == Kind::Reduce)
        {
            controller.applied(a.system, a.kind);
            ++reduces;
            if (reduces == 2)
                firstSecond = t;
        }
    }
    EXPECT_EQ(reduces >= 2, true);
    EXPECT_GE(firstSecond, 0.5 + 3.0) << "no second reduce before settleSeconds have passed";
}

TEST(VideoMemoryController, a_system_at_its_minimum_is_skipped)
{
    Params p = quickParams();
    std::vector<SystemInfo> order = { { "a", 3, 1 }, { "b", 3, 0 } }; // a may not go past level 1
    World world(order, p, 100, { 0, 0 });
    world.extraUsage = 900;
    world.run(0, 20);
    ASSERT_GE(world.log.size(), 3u);
    EXPECT_EQ(world.log[0].system, 0);
    EXPECT_EQ(world.log[1].system, 1) << "a is at its minimum after one step";
    EXPECT_EQ(world.log[2].system, 1);
    EXPECT_EQ(world.controller.level(0), 1);
}

TEST(VideoMemoryController, exhausted_is_returned_once_per_episode)
{
    Params p = quickParams();
    World world(threeSystems(1), p, 100, { 0, 0, 0 }); // each system has one step
    world.extraUsage = 900;
    world.run(0, 60);
    EXPECT_EQ(world.count(Kind::Reduce), 3);
    EXPECT_EQ(world.count(Kind::Exhausted), 1);
    // The pressure falls, then rises again: a new episode reports once more.
    world.extraUsage = 0;
    world.run(60.5, 70);
    world.extraUsage = 900;
    world.run(70.5, 120);
    EXPECT_EQ(world.count(Kind::Exhausted), 2) << "once per episode";
}

TEST(VideoMemoryController, an_unknown_budget_disables_it)
{
    Ledger ledger;
    ledger.set("fixed", 5000);
    Controller controller(threeSystems(), quickParams());
    for (double t = 0; t < 30; t += 0.5)
    {
        const Action a = controller.update(t, { -1, 5000 }, ledger, [](int, bool) { return 100; });
        EXPECT_EQ(a.kind, Kind::None);
    }
    EXPECT_TRUE(controller.budgetUnknown());
    controller.update(30.0, { 1000, 100 }, ledger, [](int, bool) { return 100; });
    EXPECT_FALSE(controller.budgetUnknown());
}

TEST(VideoMemoryController, an_unknown_usage_means_the_ledger_alone_over_the_budget)
{
    Params p = quickParams();
    Ledger ledger;
    ledger.set("fixed", 990); // 0.99 of the budget with no usage reading
    Controller controller(threeSystems(), p);
    EXPECT_EQ(controller.update(0.0, { budget, -1 }, ledger, [](int, bool) { return 100; }).kind, Kind::None);
    EXPECT_EQ(controller.update(0.5, { budget, -1 }, ledger, [](int, bool) { return 100; }).kind, Kind::Reduce);
    EXPECT_NEAR(controller.pressure(), 0.99, 1e-9);
}

TEST(VideoMemoryController, the_pressure_includes_the_untracked_usage)
{
    Ledger ledger;
    ledger.set("fixed", 300);
    Controller controller(threeSystems(), quickParams());
    controller.update(0.0, { budget, 950 }, ledger, [](int, bool) { return 0; });
    EXPECT_NEAR(controller.pressure(), 0.95, 1e-9) << "the ledger 0.30 plus 0.65 it cannot explain";
}

TEST(VideoMemoryController, the_pressure_is_smoothed)
{
    Ledger ledger;
    Controller controller(threeSystems(), quickParams());
    controller.update(0.0, { budget, 0 }, ledger, [](int, bool) { return 0; });
    controller.update(0.5, { budget, 1000 }, ledger, [](int, bool) { return 0; });
    EXPECT_GT(controller.pressure(), 0.0);
    EXPECT_LT(controller.pressure(), 0.5) << "a step of 1.0 moves the average only part of the way in half a second";
}

// ---- the controller: restoring ----

TEST(VideoMemoryController, a_restore_waits_for_the_restore_dwell_and_takes_the_last_reduced_system)
{
    Params p = quickParams();
    p.restoreDwell = 20.0;
    World world(threeSystems(1), p, 400, { 300, 300, 300 }); // 1.3 of the budget; each system has one step
    world.run(0, 12); // each reduction frees 150 (the halving): three of them bring it to 0.85
    ASSERT_GE(world.count(Kind::Reduce), 2);
    const int reduced = world.count(Kind::Reduce);
    world.fixed = 100;
    world.sizeAtLevel = { 100, 100, 100 }; // load falls; the ledger now reads low (kept levels still apply)
    world.refresh();
    const double lowStart = 13.0;
    world.run(lowStart, lowStart + 19.5);
    EXPECT_EQ(world.count(Kind::Restore), 0) << "not before restore dwell has passed with the pressure low";
    world.run(lowStart + 20.0, lowStart + 40.0);
    ASSERT_GE(world.count(Kind::Restore), 1);
    const Event* firstRestore = nullptr;
    for (const Event& e : world.log)
        if (e.kind == Kind::Restore)
        {
            firstRestore = &e;
            break;
        }
    ASSERT_NE(firstRestore, nullptr);
    EXPECT_EQ(firstRestore->system, reduced - 1) << "the reverse of the order: the last reduced system first";
}

TEST(VideoMemoryController, a_restore_that_would_not_fit_under_high_water_is_refused)
{
    Params p = quickParams();
    // The ledger sits at 0.5 of the budget; a restore would cost 0.4: 0.9 is above high water 0.92 - margin 0.05.
    Ledger ledger;
    ledger.set("fixed", 500);
    Controller controller({ { "a", 1, 0 } }, p);
    // Put the system at level 1 by a high-pressure episode first.
    Ledger high;
    high.set("fixed", 1000);
    controller.update(0.0, { budget, 1000 }, high, [](int, bool) { return 100; });
    const Action reduce = controller.update(0.5, { budget, 1000 }, high, [](int, bool) { return 100; });
    ASSERT_EQ(reduce.kind, Kind::Reduce);
    controller.applied(reduce.system, reduce.kind);
    int restores = 0;
    for (double t = 1.0; t < 200.0; t += 0.5)
        if (controller.update(t, { budget, 500 }, ledger, [](int, bool down) { return down ? 100 : 400; }).kind
            == Kind::Restore)
            ++restores;
    EXPECT_EQ(restores, 0);
    // A cheaper restore (0.3: 0.8 <= 0.87) goes through.
    Controller again({ { "a", 1, 0 } }, p);
    again.update(0.0, { budget, 1000 }, high, [](int, bool) { return 100; });
    const Action r2 = again.update(0.5, { budget, 1000 }, high, [](int, bool) { return 100; });
    ASSERT_EQ(r2.kind, Kind::Reduce);
    again.applied(r2.system, r2.kind);
    int restores2 = 0;
    for (double t = 1.0; t < 200.0; t += 0.5)
    {
        const Action a = again.update(t, { budget, 500 }, ledger, [](int, bool down) { return down ? 100 : 300; });
        if (a.kind == Kind::Restore)
        {
            ++restores2;
            again.applied(a.system, a.kind);
        }
    }
    EXPECT_EQ(restores2, 1);
}

TEST(VideoMemoryController, manual_restore_never_restores)
{
    Params p = quickParams();
    p.automaticRestore = false;
    World world(threeSystems(1), p, 400, { 300, 300, 300 });
    world.run(0, 12);
    ASSERT_GE(world.count(Kind::Reduce), 1);
    world.fixed = 0;
    world.sizeAtLevel = { 10, 10, 10 };
    world.refresh();
    world.run(13, 400);
    EXPECT_EQ(world.count(Kind::Restore), 0);
}

// ---- the controller: failures, configuration, determinism ----

TEST(VideoMemoryController, a_refused_action_is_retried_after_the_dwell_and_skipped_after_three_failures)
{
    Params p = quickParams();
    p.dwell = 2.0;
    World world({ { "a", 3, 0 }, { "b", 3, 0 } }, p, 100, { 0, 0 });
    world.extraUsage = 900;
    int tries = 0;
    double last = -100;
    for (double t = 0; t < 40; t += 0.5)
    {
        const Action a = world.step(t, true); // every action is refused
        if (a.kind == Kind::Reduce && a.system == 0)
        {
            ++tries;
            if (tries > 1)
            {
                EXPECT_GE(t - last, 2.0) << "retried only after the dwell";
            }
            last = t;
        }
    }
    EXPECT_EQ(tries, 3);
    EXPECT_TRUE(world.controller.skipped(0));
    // After the three failures the controller goes on to the next system.
    bool reachedB = false;
    for (const Event& e : world.log)
        if (e.system == 1)
            reachedB = true;
    EXPECT_TRUE(reachedB);
}

TEST(VideoMemoryController, only_listed_systems_are_ever_touched)
{
    World world({ { "far objects", 2, 0 } }, quickParams(), 100, { 0 });
    world.extraUsage = 900;
    world.run(0, 100);
    for (const Event& e : world.log)
        EXPECT_TRUE(e.kind == Kind::Exhausted || e.system == 0);
    EXPECT_EQ(world.controller.systemCount(), 1u);
}

TEST(VideoMemoryController, the_same_series_gives_the_same_actions_twice)
{
    auto run = [] {
        Params p = quickParams();
        World world(threeSystems(), p, 100, { 300, 250, 200 });
        for (int i = 0; i < 400; ++i)
        {
            world.extraUsage = (i % 80 < 40) ? 300 : 0;
            world.step(i * 0.5);
        }
        return world.log;
    };
    const std::vector<Event> first = run();
    const std::vector<Event> second = run();
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(first == second);
}

// A deterministic generator for the property test (no std::random: its distributions differ between standard
// libraries).
namespace
{
    struct Lcg
    {
        std::uint64_t state;
        std::uint32_t next()
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::int64_t range(std::int64_t lo, std::int64_t hi)
        {
            return lo + static_cast<std::int64_t>(next() % (hi - lo + 1));
        }
    };
}

TEST(VideoMemoryController, a_restore_is_never_followed_by_a_reduce_within_the_restore_dwell_on_a_stable_load)
{
    int violations = 0;
    int restoresSeen = 0;
    for (std::uint64_t seed = 1; seed <= 10000; ++seed)
    {
        Lcg rng{ seed * 2654435761ull };
        Params p;
        p.dwell = 2.0;
        p.restoreDwell = 10.0;
        p.settleSeconds = 1.5;
        const int systems = static_cast<int>(rng.range(1, 4));
        std::vector<SystemInfo> order;
        std::vector<std::int64_t> sizes;
        for (int i = 0; i < systems; ++i)
        {
            order.push_back({ "s" + std::to_string(i), static_cast<int>(rng.range(1, 3)), 0 });
            sizes.push_back(rng.range(50, 600));
        }
        World world(order, p, rng.range(0, 500), sizes);
        // A stable load: a fixed unexplained part. A phase of heavy load then a phase of lighter, to make restores.
        const std::int64_t heavy = rng.range(0, 500);
        world.extraUsage = heavy;
        double lastRestore = -1e9;
        for (int i = 0; i < 1200; ++i)
        {
            if (i == 300)
                world.extraUsage = heavy / 4;
            const double t = i * 0.5;
            const Action a = world.step(t);
            if (a.kind == Kind::Restore)
            {
                lastRestore = t;
                ++restoresSeen;
            }
            else if (a.kind == Kind::Reduce && t - lastRestore < p.restoreDwell)
                ++violations;
        }
    }
    EXPECT_EQ(violations, 0);
    EXPECT_GT(restoresSeen, 100) << "the property test must exercise restores";
}

// ---- clamping the thresholds, parsing the settings ----

TEST(VideoMemoryParams, good_thresholds_are_left_alone)
{
    Params p;
    EXPECT_TRUE(clampParams(p).empty());
    EXPECT_DOUBLE_EQ(p.highWater, 0.92);
    EXPECT_DOUBLE_EQ(p.lowWater, 0.75);
}

TEST(VideoMemoryParams, a_low_water_too_far_below_high_water_is_clamped_with_a_message)
{
    Params p;
    p.lowWater = 0.2; // high 0.92: at least 0.62
    const auto messages = clampParams(p);
    EXPECT_EQ(messages.size(), 1u);
    EXPECT_NEAR(p.lowWater, 0.62, 1e-9);
}

TEST(VideoMemoryParams, a_low_water_above_high_water_is_clamped_under_it_by_the_margin)
{
    Params p;
    p.lowWater = 0.95;
    EXPECT_EQ(clampParams(p).size(), 1u);
    EXPECT_NEAR(p.lowWater, 0.87, 1e-9);
}

TEST(VideoMemoryParams, a_high_water_out_of_range_is_clamped)
{
    Params p;
    p.highWater = 1.4;
    EXPECT_GE(clampParams(p).size(), 1u);
    EXPECT_LE(p.highWater, 1.0);
    EXPECT_GE(p.lowWater, p.highWater - 0.30 - 1e-9);
    EXPECT_LE(p.lowWater, p.highWater - 0.05 + 1e-9);
}

TEST(VideoMemoryOrder, a_list_is_split_trimmed_and_lowercased)
{
    const std::vector<std::string> known = { "far objects", "far terrain", "point light shadows" };
    const ParsedOrder parsed = parseOrder("  Far Terrain ,far objects", known);
    ASSERT_EQ(parsed.names.size(), 2u);
    EXPECT_EQ(parsed.names[0], "far terrain");
    EXPECT_EQ(parsed.names[1], "far objects");
    EXPECT_TRUE(parsed.problems.empty());
}

TEST(VideoMemoryOrder, unknown_and_repeated_names_are_dropped_with_a_message_each)
{
    const std::vector<std::string> known = { "far objects", "far terrain" };
    const ParsedOrder parsed = parseOrder("far objects, magic, far objects, , far terrain", known);
    ASSERT_EQ(parsed.names.size(), 2u);
    EXPECT_EQ(parsed.names[0], "far objects");
    EXPECT_EQ(parsed.names[1], "far terrain");
    EXPECT_EQ(parsed.problems.size(), 2u);
}

TEST(VideoMemoryOrder, an_empty_list_names_nothing)
{
    const ParsedOrder parsed = parseOrder("", { "far objects" });
    EXPECT_TRUE(parsed.names.empty());
    EXPECT_TRUE(parsed.problems.empty());
}

TEST(VideoMemoryBudget, auto_and_numbers_parse_and_nonsense_is_invalid)
{
    EXPECT_TRUE(parseBudget("auto").autoBudget);
    EXPECT_TRUE(parseBudget(" AUTO ").autoBudget);
    EXPECT_TRUE(parseBudget("").autoBudget);
    const BudgetSetting n = parseBudget("1024");
    EXPECT_TRUE(n.valid);
    EXPECT_FALSE(n.autoBudget);
    EXPECT_EQ(n.mib, 1024);
    EXPECT_FALSE(parseBudget("lots").valid);
    EXPECT_FALSE(parseBudget("0").valid);
    EXPECT_FALSE(parseBudget("-5").valid);
}
