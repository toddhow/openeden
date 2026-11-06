// Task 0092 stage d: the instruction budget and determinism of the ObScript runtime.
//
// In plain words: a script with a loop that never ends must not freeze the game. Each run call gets a budget of
// instructions; a script that uses it up is stopped with an error that says which script and why, and the game goes on.
// A script that stays under the budget is not affected. And the same scripts run twice must give the same results and
// the same error text (no clock, no random order).
//
// "While" is the loop construct the shared grammar already has (components/compiler); these tests do not claim that
// vanilla ObScript has it. The budget itself is also tested on a hand-built program with a backward jump in
// apps/components_tests/interpreter/testrunlimits.cpp.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::Budget;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;

    const char* const sSpin = R"(ScriptName Spin
short n
Begin GameMode
    While ( 1 == 1 )
        Set n to n + 1
    EndWhile
End
)";

    const char* const sShort = R"(ScriptName Short
short n
Begin GameMode
    Set n to 1
    Set n to n + 2
    Set n to n * 3
End
)";

    TEST(ObScriptBudget, an_endless_loop_is_stopped_with_a_located_error_and_the_host_continues)
    {
        RuntimeFixture fixture;
        const ScriptId spin = fixture.compileOk("Spin", sSpin);
        const ScriptId shortScript = fixture.compileOk("Short", sShort);
        ASSERT_NE(spin, 0u);
        ASSERT_NE(shortScript, 0u);

        Budget budget;
        budget.maxInstructions = 10000;
        const InstanceId spinning = fixture.mRuntime->createInstance(spin, sCrate);
        const auto stopped = fixture.mRuntime->run(spinning, "GameMode", budget);
        EXPECT_FALSE(stopped.ok);
        EXPECT_TRUE(stopped.blockFound);
        EXPECT_EQ(stopped.error.script, "Spin");
        EXPECT_NE(stopped.error.message.find("instruction budget"), std::string::npos) << stopped.error.format();
        EXPECT_LE(stopped.instructionsExecuted, 10000u);
        EXPECT_GT(fixture.mRuntime->getLocal(spinning, "n").integer, 100);

        // The host goes on: the same instance can run again (and is stopped again), and another script is unaffected.
        EXPECT_FALSE(fixture.mRuntime->run(spinning, "GameMode", budget).ok);
        const InstanceId other = fixture.mRuntime->createInstance(shortScript, sGold);
        const auto fine = fixture.mRuntime->run(other, "GameMode", budget);
        EXPECT_TRUE(fine.ok) << fine.error.format();
        EXPECT_EQ(fixture.mRuntime->getLocal(other, "n").integer, 9);
    }

    TEST(ObScriptBudget, the_default_budget_also_stops_an_endless_loop)
    {
        RuntimeFixture fixture;
        const ScriptId spin = fixture.compileOk("Spin", sSpin);
        ASSERT_NE(spin, 0u);
        const InstanceId instance = fixture.mRuntime->createInstance(spin, sCrate);
        const auto stopped = fixture.mRuntime->run(instance, "GameMode");
        EXPECT_FALSE(stopped.ok);
        EXPECT_NE(stopped.error.message.find("instruction budget"), std::string::npos);
    }

    TEST(ObScriptBudget, a_script_under_the_budget_runs_the_same_with_or_without_a_small_budget)
    {
        RuntimeFixture fixture;
        const ScriptId script = fixture.compileOk("Short", sShort);
        ASSERT_NE(script, 0u);
        const InstanceId free = fixture.mRuntime->createInstance(script, sCrate);
        const InstanceId limited = fixture.mRuntime->createInstance(script, sCrate);

        const auto first = fixture.mRuntime->run(free, "GameMode");
        Budget budget;
        budget.maxInstructions = first.instructionsExecuted; // exactly what it needs
        const auto second = fixture.mRuntime->run(limited, "GameMode", budget);
        EXPECT_TRUE(first.ok);
        EXPECT_TRUE(second.ok) << second.error.format();
        EXPECT_EQ(first.instructionsExecuted, second.instructionsExecuted);
        EXPECT_GT(first.instructionsExecuted, 0u);
        EXPECT_EQ(fixture.mRuntime->getLocal(free, "n").integer, fixture.mRuntime->getLocal(limited, "n").integer);

        // One instruction less is not enough.
        budget.maxInstructions = first.instructionsExecuted - 1;
        const InstanceId tight = fixture.mRuntime->createInstance(script, sCrate);
        EXPECT_FALSE(fixture.mRuntime->run(tight, "GameMode", budget).ok);
    }

    TEST(ObScriptDeterminism, two_runtimes_given_the_same_work_give_the_same_results_and_error_text)
    {
        auto scenario = [](std::vector<std::string>& log) {
            RuntimeFixture fixture;
            const ScriptId basic = fixture.compileOk("Spin", sSpin);
            const ScriptId shortScript = fixture.compileOk("Short", sShort);
            const ScriptId tolerant = fixture.compileOk(
                "Tolerant", "ScriptName Tolerant\nshort c\nBegin GameMode\nFrobnicate 1\nSet c to Wobble 2\nEnd\n");
            Budget budget;
            budget.maxInstructions = 5000;
            const InstanceId a = fixture.mRuntime->createInstance(basic, sCrate);
            const InstanceId b = fixture.mRuntime->createInstance(shortScript, sGold);
            const InstanceId c = fixture.mRuntime->createInstance(tolerant, sPlayer);
            for (int i = 0; i < 3; ++i)
            {
                const auto ra = fixture.mRuntime->run(a, "GameMode", budget);
                log.push_back(ra.error.format() + " " + std::to_string(ra.instructionsExecuted));
                const auto rb = fixture.mRuntime->run(b, "GameMode", budget);
                log.push_back(std::to_string(rb.instructionsExecuted));
                fixture.mRuntime->run(c, "GameMode", budget);
            }
            log.insert(log.end(), fixture.mWorld.mMessages.begin(), fixture.mWorld.mMessages.end());
            const auto blob = fixture.mRuntime->serialise();
            log.push_back(std::to_string(blob.formatVersion));
            log.emplace_back(blob.bytes.begin(), blob.bytes.end());
        };
        std::vector<std::string> first;
        std::vector<std::string> second;
        scenario(first);
        scenario(second);
        EXPECT_EQ(first, second);
        EXPECT_GT(first.size(), 8u);
    }

    TEST(ObScriptDeterminism, compiling_in_a_different_order_gives_the_same_run_results)
    {
        auto runBoth = [](bool shortFirst) {
            RuntimeFixture fixture;
            ScriptId shortScript = 0;
            ScriptId spin = 0;
            if (shortFirst)
            {
                shortScript = fixture.compileOk("Short", sShort);
                spin = fixture.compileOk("Spin", sSpin);
            }
            else
            {
                spin = fixture.compileOk("Spin", sSpin);
                shortScript = fixture.compileOk("Short", sShort);
            }
            const InstanceId instance = fixture.mRuntime->createInstance(shortScript, sCrate);
            Budget budget;
            budget.maxInstructions = 5000;
            const auto result = fixture.mRuntime->run(instance, "GameMode", budget);
            (void)spin;
            return std::to_string(result.instructionsExecuted) + ":"
                + std::to_string(fixture.mRuntime->getLocal(instance, "n").integer);
        };
        EXPECT_EQ(runBoth(true), runBoth(false));
    }
}
