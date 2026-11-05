// Tests for Render::VideoMemory::Scaler, the glue between the memory source, the ledger, the controller and the
// adapters (task 0080, stage c). Written from the spec's adapter table and its log line before the implementation.
// Pure CPU: the source and the adapters are fakes.

#include <components/render/vramscaler.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace Render::VideoMemory;

    constexpr std::int64_t mib = 1024 * 1024;

    // A system whose memory halves with each level; levels are applied to a counter the test can read.
    struct FakeSystem
    {
        std::string name;
        std::int64_t fullBytes = 0;
        int maxLevel = 2;
        int level = 0;
        bool refuse = false;
        int applyCalls = 0;

        std::int64_t bytes() const { return fullBytes >> level; }

        Adapter adapter()
        {
            Adapter a;
            a.name = name;
            a.maxLevel = maxLevel;
            a.account = [this](Ledger& ledger) { ledger.set("lod." + name, bytes()); };
            a.apply = [this](int target) {
                ++applyCalls;
                if (refuse)
                    return false;
                level = target;
                return true;
            };
            a.stepBytes = [this](int from, bool down) {
                const std::int64_t at = fullBytes >> from;
                return down ? at - (at >> 1) : (fullBytes >> (from - 1)) - at;
            };
            return a;
        }
    };

    struct Harness
    {
        FakeSystem a{ "far objects", 400 * mib };
        FakeSystem b{ "far terrain", 200 * mib };
        std::int64_t otherBytes = 400 * mib; // memory nobody accounts for
        std::int64_t driverBudget = 1000 * mib;
        std::vector<std::string> lines;
        std::vector<bool> warnings;
        int sourceCalls = 0;

        ScalerConfig config(const std::string& order)
        {
            ScalerConfig c;
            c.order = order;
            c.budgetFraction = 1.0;
            c.params.dwell = 1.0;
            c.params.restoreDwell = 5.0;
            c.params.settleSeconds = 1.0;
            return c;
        }

        std::unique_ptr<Scaler> make(ScalerConfig c)
        {
            return std::make_unique<Scaler>(
                std::move(c), std::vector<Adapter>{ a.adapter(), b.adapter() },
                [this] {
                    ++sourceCalls;
                    Sample s;
                    s.budgetBytes = driverBudget;
                    s.usageBytes = otherBytes + a.bytes() + b.bytes();
                    return s;
                },
                [this](bool warning, const std::string& text) {
                    lines.push_back(text);
                    warnings.push_back(warning);
                });
        }

        int count(const std::string& needle) const
        {
            int n = 0;
            for (const std::string& line : lines)
                if (line.find(needle) != std::string::npos)
                    ++n;
            return n;
        }
    };

    void run(Scaler& scaler, double seconds, double dt = 0.1)
    {
        for (double t = 0; t < seconds; t += dt)
            scaler.tick(dt);
    }
}

TEST(VideoMemoryScaler, reduces_the_listed_systems_in_the_users_order_and_logs_each_step)
{
    Harness h;
    h.otherBytes = 380 * mib; // 380 + 400 + 200 = 980 of 1000: over high water; one step (100) brings it to 0.88
    auto scaler = h.make(h.config("far terrain, far objects"));
    ASSERT_TRUE(scaler->active());
    run(*scaler, 3.0);
    EXPECT_EQ(h.b.level, 1) << "the first listed system is reduced first";
    EXPECT_EQ(h.a.level, 0);
    ASSERT_GE(h.count("VRAM scale-back: reduced far terrain to step 1 (used"), 1);
    EXPECT_NE(h.lines.back().find("MiB of budget 1000 MiB)"), std::string::npos);
}

TEST(VideoMemoryScaler, goes_on_to_the_next_system_when_the_first_is_at_its_maximum)
{
    Harness h;
    h.otherBytes = 700 * mib;
    h.b.maxLevel = 1;
    auto scaler = h.make(h.config("far terrain, far objects"));
    run(*scaler, 30.0);
    EXPECT_EQ(h.b.level, 1);
    EXPECT_GE(h.a.level, 1);
    const int firstB = [&] {
        for (std::size_t i = 0; i < h.lines.size(); ++i)
            if (h.lines[i].find("reduced far terrain") != std::string::npos)
                return static_cast<int>(i);
        return -1;
    }();
    const int firstA = [&] {
        for (std::size_t i = 0; i < h.lines.size(); ++i)
            if (h.lines[i].find("reduced far objects") != std::string::npos)
                return static_cast<int>(i);
        return -1;
    }();
    EXPECT_GE(firstB, 0);
    EXPECT_GT(firstA, firstB);
}

TEST(VideoMemoryScaler, a_system_not_listed_is_never_touched)
{
    Harness h;
    h.otherBytes = 900 * mib;
    auto scaler = h.make(h.config("far terrain"));
    run(*scaler, 60.0);
    EXPECT_EQ(h.a.applyCalls, 0);
    EXPECT_GE(h.b.level, 1);
}

TEST(VideoMemoryScaler, restores_in_reverse_when_the_pressure_has_stayed_low)
{
    Harness h;
    h.otherBytes = 700 * mib;
    auto scaler = h.make(h.config("far objects, far terrain"));
    run(*scaler, 20.0);
    ASSERT_GE(h.a.level, 1);
    h.otherBytes = 50 * mib;
    run(*scaler, 60.0);
    EXPECT_EQ(h.a.level, 0);
    EXPECT_EQ(h.b.level, 0);
    EXPECT_GE(h.count("VRAM scale-back: restored"), 1);
    EXPECT_EQ(scaler->stepsUp(), scaler->stepsDown());
}

TEST(VideoMemoryScaler, a_refusing_adapter_is_retried_and_does_not_count_as_a_step)
{
    Harness h;
    h.otherBytes = 700 * mib;
    h.a.refuse = true;
    auto scaler = h.make(h.config("far objects"));
    run(*scaler, 30.0);
    EXPECT_EQ(h.a.level, 0);
    EXPECT_EQ(scaler->stepsDown(), 0);
    EXPECT_GE(h.a.applyCalls, 2);
    EXPECT_LE(h.a.applyCalls, 3) << "three failures and it is skipped";
    EXPECT_GE(h.count("could not reduce far objects"), 1);
}

TEST(VideoMemoryScaler, exhausted_is_logged_once_as_a_warning)
{
    Harness h;
    h.otherBytes = 900 * mib;
    h.a.maxLevel = 1;
    h.b.maxLevel = 1;
    auto scaler = h.make(h.config("far objects, far terrain"));
    run(*scaler, 60.0);
    EXPECT_EQ(h.count("nothing is left to reduce"), 1);
}

TEST(VideoMemoryScaler, a_list_with_no_known_system_does_nothing_and_says_so)
{
    Harness h;
    auto scaler = h.make(h.config("texture detail, volumetrics"));
    EXPECT_FALSE(scaler->active());
    EXPECT_GE(h.count("nothing will be scaled back"), 1);
    run(*scaler, 10.0);
    EXPECT_EQ(h.sourceCalls, 0) << "an inactive scaler reads no memory";
}

TEST(VideoMemoryScaler, an_unknown_name_in_the_list_is_warned_about_and_the_rest_is_used)
{
    Harness h;
    auto scaler = h.make(h.config("magic, far objects"));
    EXPECT_TRUE(scaler->active());
    EXPECT_EQ(h.count("unknown system 'magic'"), 1);
}

TEST(VideoMemoryScaler, samples_on_the_interval_not_every_frame)
{
    Harness h;
    ScalerConfig c = h.config("far objects");
    c.sampleInterval = 0.5;
    auto scaler = h.make(c);
    run(*scaler, 10.0, 0.05);
    EXPECT_GE(h.sourceCalls, 18);
    EXPECT_LE(h.sourceCalls, 21);
}

TEST(VideoMemoryScaler, a_paused_game_takes_no_samples)
{
    Harness h;
    auto scaler = h.make(h.config("far objects"));
    scaler->tick(0.0);
    scaler->tick(0.0);
    EXPECT_LE(h.sourceCalls, 1);
}

TEST(VideoMemoryScaler, a_numeric_budget_simulates_a_smaller_card_and_the_fraction_applies)
{
    Harness h;
    h.driverBudget = 16000 * mib; // the real card is big
    ScalerConfig c = h.config("far objects");
    c.budget = parseBudget("1000");
    c.budgetFraction = 0.5; // 500 MiB may be filled
    auto scaler = h.make(c);
    run(*scaler, 3.0);
    EXPECT_NEAR(scaler->budgetMiB(), 500.0, 0.01);
    EXPECT_GE(h.a.level, 1) << "1000 MiB of use against an effective budget of 500";
}

TEST(VideoMemoryScaler, the_fallback_budget_stands_in_when_the_driver_gives_none_and_says_so_once)
{
    Harness h;
    h.driverBudget = -1;
    ScalerConfig c = h.config("far objects");
    c.fallbackBudgetBytes = 1000 * mib;
    auto scaler = h.make(c);
    run(*scaler, 5.0);
    EXPECT_NEAR(scaler->budgetMiB(), 1000.0, 0.01);
    EXPECT_EQ(h.count("driver gives no memory budget"), 1);
}

TEST(VideoMemoryScaler, no_budget_at_all_disables_it_with_one_message)
{
    Harness h;
    h.driverBudget = -1;
    auto scaler = h.make(h.config("far objects"));
    run(*scaler, 20.0);
    EXPECT_EQ(h.count("budget is unknown"), 1);
    EXPECT_EQ(h.a.applyCalls, 0);
}

TEST(VideoMemoryScaler, the_adapters_fill_the_ledger_and_the_counters_follow)
{
    Harness h;
    h.otherBytes = 100 * mib;
    auto scaler = h.make(h.config("far objects, far terrain"));
    run(*scaler, 1.0);
    EXPECT_EQ(scaler->ledger().get("lod.far objects"), 400 * mib);
    EXPECT_EQ(scaler->ledger().get("lod.far terrain"), 200 * mib);
    EXPECT_NEAR(scaler->accountedMiB(), 600.0, 0.01);
    EXPECT_NEAR(scaler->usedMiB(), 700.0, 0.01) << "the larger of the ledger and the driver's usage";
    EXPECT_NEAR(scaler->budgetMiB(), 1000.0, 0.01);
}

TEST(VideoMemoryScaler, the_report_names_the_steps)
{
    Harness h;
    h.otherBytes = 700 * mib;
    auto scaler = h.make(h.config("far objects"));
    run(*scaler, 10.0);
    const std::string text = scaler->reportText();
    EXPECT_NE(text.find("VRAM scale-back report:"), std::string::npos);
    EXPECT_NE(text.find("steps down"), std::string::npos);
}

TEST(VideoMemoryScaler, the_ledger_only_source_ignores_the_drivers_usage)
{
    Harness h;
    h.otherBytes = 5000 * mib; // the driver says 5600 MiB in use; the ledger holds 600 of a 1000 MiB budget
    ScalerConfig c = h.config("far objects, far terrain");
    c.useDriverUsage = false;
    auto scaler = h.make(c);
    run(*scaler, 20.0);
    EXPECT_EQ(h.a.applyCalls, 0) << "600 of 1000 is under high water whatever the driver says";
    EXPECT_NEAR(scaler->usedMiB(), 600.0, 0.01);
}
