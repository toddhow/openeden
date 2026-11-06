// Task 0092: "&&" and "||" in ObScript conditions, run on the stub world.
//
// In plain words: each expression is run for every combination of three variables (0 to 3) and its answer is compared
// with the answer worked out here in C++ from the rules: a number is true when it is not zero, "&&" binds tighter than
// "||", the comparisons bind tighter than both, brackets group, and BOTH sides of an operator are always evaluated
// (the data of the New Vegas masters has no condition that needs the right side skipped: the only divisions on a line
// with && or || are by the constant 2; recorded in the 0092 stage log).

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::Budget;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;

    struct Case
    {
        const char* mText;
        std::function<bool(int a, int b, int c)> mExpected;
    };

    std::vector<Case> cases()
    {
        return {
            { "a && b", [](int a, int b, int) { return a != 0 && b != 0; } },
            { "a || b", [](int a, int b, int) { return a != 0 || b != 0; } },
            { "a || b && c", [](int a, int b, int c) { return a != 0 || (b != 0 && c != 0); } },
            { "a && b || c", [](int a, int b, int c) { return (a != 0 && b != 0) || c != 0; } },
            { "( a || b ) && c", [](int a, int b, int c) { return (a != 0 || b != 0) && c != 0; } },
            { "a == 1 && b != 2", [](int a, int b, int) { return a == 1 && b != 2; } },
            { "a < 2 || b >= 3 && c", [](int a, int b, int c) { return a < 2 || (b >= 3 && c != 0); } },
            { "( a == 1 ) && ( b == 2 ) && ( c == 3 )",
                [](int a, int b, int c) { return a == 1 && b == 2 && c == 3; } },
            { "( ( a && b ) || ( c && ( a || b ) ) )",
                [](int a, int b, int c) { return (a != 0 && b != 0) || (c != 0 && (a != 0 || b != 0)); } },
            { "a + b > 2 && c - 1 == 0", [](int a, int b, int c) { return a + b > 2 && c - 1 == 0; } },
            { "a == 2 || a == 3 || b == 2 || b == 3", [](int a, int b, int) { return a >= 2 || b >= 2; } },
            { "a != 0 && b != 0 && c != 0 && a != b", [](int a, int b, int c) { return a && b && c && a != b; } },
            { "a * 2 && b", [](int a, int b, int) { return a * 2 != 0 && b != 0; } },
        };
    }

    std::string script(const std::string& condition)
    {
        return "ScriptName Logic\nshort a\nshort b\nshort c\nshort r\nBegin GameMode\n    If ( " + condition
            + " )\n        Set r to 1\n    Else\n        Set r to 0\n    EndIf\nEnd\n";
    }

    TEST(ObScriptLogic, every_expression_gives_the_answer_the_rules_give_for_every_input)
    {
        for (const Case& item : cases())
        {
            SCOPED_TRACE(item.mText);
            RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
            const ScriptId id = fixture.compileOk("Logic", script(item.mText));
            ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
            const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
            for (int a = 0; a < 4; ++a)
            {
                for (int b = 0; b < 4; ++b)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        fixture.mRuntime->setLocal(instance, "a", Value::makeInteger(a));
                        fixture.mRuntime->setLocal(instance, "b", Value::makeInteger(b));
                        fixture.mRuntime->setLocal(instance, "c", Value::makeInteger(c));
                        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
                        ASSERT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, item.mExpected(a, b, c) ? 1 : 0)
                            << "a=" << a << " b=" << b << " c=" << c;
                    }
                }
            }
        }
    }

    TEST(ObScriptLogic, a_float_operand_counts_as_true_when_it_is_not_zero_after_truncation_to_an_integer)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id = fixture.compileOk("F",
            "ScriptName F\nfloat f\nshort r\nBegin GameMode\n    If ( f && 1 )\n        Set r to 1\n    Else\n        "
            "Set r to 0\n"
            "    EndIf\nEnd\n");
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        fixture.mRuntime->setLocal(instance, "f", Value::makeFloat(2.5f));
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, 1);
        fixture.mRuntime->setLocal(instance, "f", Value::makeFloat(0.0f));
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, 0);
    }

    TEST(ObScriptLogic, elseif_and_while_take_the_operators)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id = fixture.compileOk("W", R"(ScriptName W
short a
short i
short j
short r
Begin GameMode
    If ( a == 5 )
        Set r to 1
    ElseIf ( a == 1 && i == 0 || a == 2 )
        Set r to 2
    Else
        Set r to 3
    EndIf
    While ( i < 5 && j == 0 )
        Set i to i + 1
    EndWhile
End
)");
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        fixture.mRuntime->setLocal(instance, "a", Value::makeInteger(1));
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, 2);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "i").integer, 5);
    }

    TEST(ObScriptLogic, both_sides_are_always_evaluated)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id = fixture.compileOk("E",
            "ScriptName E\nshort a\nshort r\nBegin GameMode\n    If ( a == 0 || Greet 7 == 1 )\n        Set r to 1\n   "
            " EndIf\n"
            "    If ( a == 1 && Greet 8 == 1 )\n        Set r to 2\n    EndIf\nEnd\n");
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        // a is 0: the right side of the || is not needed, and the left side of the && is false, but both functions ran.
        ASSERT_EQ(fixture.mCalls.size(), 2u);
        EXPECT_EQ(fixture.mCalls[0].mArguments[0], "i7");
        EXPECT_EQ(fixture.mCalls[1].mArguments[0], "i8");
    }

    TEST(ObScriptLogic, a_function_call_may_stand_on_either_side)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        fixture.mItems[sPlayer] = 3;
        const ScriptId id = fixture.compileOk("G",
            "ScriptName G\nshort a\nshort r\nBegin GameMode\n    If ( player.GetItemCount gold001 > 2 && a == 0 )\n"
            "        Set r to 1\n    EndIf\nEnd\n");
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, 1);
    }

    TEST(ObScriptLogic, malformed_forms_give_located_errors_and_the_next_script_compiles)
    {
        struct Bad
        {
            const char* mCondition;
            std::uint32_t mLine;
        };
        const std::vector<Bad> table = { { "a &&", 7 }, { "&& a", 7 }, { "a || || b", 7 }, { "a & b", 7 },
            { "a | b", 7 }, { "a &&& b", 7 }, { "( a && b", 7 } };
        for (const Bad& item : table)
        {
            SCOPED_TRACE(item.mCondition);
            RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
            openeden::script::ScriptSource source;
            source.name = "Bad";
            source.text = script(item.mCondition);
            const auto result = fixture.mRuntime->compile(source);
            EXPECT_FALSE(result.ok);
            ASSERT_FALSE(result.reports.empty());
            std::string error;
            for (const auto& report : result.reports)
            {
                if (report.severity == openeden::script::Severity::Error)
                {
                    error = report.format();
                    break;
                }
            }
            EXPECT_EQ(error.rfind("Bad:", 0), 0u) << error;
            EXPECT_NE(fixture.compileOk("Valid", "ScriptName Valid\nshort x\nBegin GameMode\nSet x to 1\nEnd\n"), 0u);
        }
    }

    TEST(ObScriptLogic, the_budget_still_stops_a_loop_whose_condition_uses_the_operators)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id = fixture.compileOk("S",
            "ScriptName S\nshort n\nBegin GameMode\n    While ( 1 == 1 && n >= 0 || n < 0 )\n        Set n to n + 1\n"
            "    EndWhile\nEnd\n");
        ASSERT_NE(id, 0u);
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        Budget budget;
        budget.maxInstructions = 20000;
        const auto result = fixture.mRuntime->run(instance, "GameMode", budget);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("instruction budget"), std::string::npos);
    }

    TEST(ObScriptLogic, a_long_chain_compiles_and_runs_and_the_bracket_cap_still_applies)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        std::string chain = "a";
        for (int i = 0; i < 3000; ++i)
        {
            chain += " && a";
        }
        const ScriptId id = fixture.compileOk("Chain", script(chain));
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sCrate);
        fixture.mRuntime->setLocal(instance, "a", Value::makeInteger(1));
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "r").integer, 1);

        openeden::script::ScriptSource deep;
        deep.name = "Deep";
        deep.text = script(std::string(100000, '(') + "a" + std::string(100000, ')') + " && a");
        EXPECT_FALSE(fixture.mRuntime->compile(deep).ok);
    }
}
