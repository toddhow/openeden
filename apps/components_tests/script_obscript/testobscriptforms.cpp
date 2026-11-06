// Task 0092 item 1: forms and references used as values in ObScript, run on the stub world.
//
// In plain words: "Set target to Crate", "If ( target == Player )" and "If ( GetTarget == Crate )" work. A form is an
// integer (its form id) that the runtime asks the game pack's world for by name at the moment the line runs. The world
// is the stub of stubworld.hpp (invented names: Player 7, MyQuest 0x100, Gold001 0x200, Crate 0x300).

#include <gtest/gtest.h>

#include <string>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    std::string wrap(const std::string& body)
    {
        return "ScriptName Forms\nref target\nref other\nshort flag\nlong number\nfloat real\nBegin GameMode\n" + body
            + "\nEnd\n";
    }

    struct Ran
    {
        RuntimeFixture mFixture;
        InstanceId mInstance = 0;
        bool mOk = false;

        explicit Ran(const std::string& body, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Strict)
            : mFixture(policy)
        {
            const ScriptId id = mFixture.compileOk("Forms", wrap(body));
            if (id == 0)
            {
                return;
            }
            mInstance = mFixture.mRuntime->createInstance(id, sCrate);
            mOk = mFixture.mRuntime->run(mInstance, "GameMode").ok;
        }

        std::int32_t integer(const char* name) const { return mFixture.mRuntime->getLocal(mInstance, name).integer; }
    };

    TEST(ObScriptForms, a_form_name_is_stored_in_a_reference_variable)
    {
        Ran ran("Set target to Crate\nSet other to Player");
        ASSERT_TRUE(ran.mOk) << (ran.mFixture.mLastReports.empty() ? "" : ran.mFixture.mLastReports.front());
        EXPECT_EQ(ran.mFixture.mRuntime->getLocal(ran.mInstance, "target").kind, ValueKind::Form);
        EXPECT_EQ(ran.integer("target"), static_cast<std::int32_t>(sCrate));
        EXPECT_EQ(ran.integer("other"), static_cast<std::int32_t>(sPlayer));
    }

    TEST(ObScriptForms, names_are_matched_without_regard_to_case)
    {
        Ran ran("Set target to cRATE");
        ASSERT_TRUE(ran.mOk);
        EXPECT_EQ(ran.integer("target"), static_cast<std::int32_t>(sCrate));
    }

    TEST(ObScriptForms, equal_and_not_equal_decide_by_form_id)
    {
        Ran ran(
            "Set target to Crate\nSet other to target\n"
            "If ( other == Crate )\n Set flag to 1\nEndIf\n"
            "If ( other != Player )\n Set number to 2\nEndIf\n"
            "If ( target == other )\n Set real to 3\nEndIf\n"
            "If ( Player == Crate )\n Set flag to 9\nEndIf");
        ASSERT_TRUE(ran.mOk) << (ran.mFixture.mLastReports.empty() ? "" : ran.mFixture.mLastReports.front());
        EXPECT_EQ(ran.integer("flag"), 1);
        EXPECT_EQ(ran.integer("number"), 2);
        EXPECT_FLOAT_EQ(ran.mFixture.mRuntime->getLocal(ran.mInstance, "real").number, 3.0f);
    }

    TEST(ObScriptForms, a_form_in_a_condition_is_true_when_it_is_not_none)
    {
        Ran ran(
            "If ( target )\n Set flag to 9\nElse\n Set flag to 1\nEndIf\n"
            "Set target to Crate\nIf ( target )\n Set number to 5\nEndIf");
        ASSERT_TRUE(ran.mOk);
        EXPECT_EQ(ran.integer("flag"), 1);
        EXPECT_EQ(ran.integer("number"), 5);
    }

    TEST(ObScriptForms, a_function_result_can_be_compared_with_a_form)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        openeden::script::FunctionDescriptor descriptor;
        descriptor.name = "GetTarget";
        descriptor.signature = "";
        openeden::script::ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->registerFunction(
            descriptor, [](const openeden::script::CallArgs&) { return Value::makeForm(sCrate); }, error))
            << error.format();
        const ScriptId id = fixture.compileOk("Forms",
            wrap("If ( GetTarget == Crate )\n Set flag to 1\nEndIf\n"
                 "If ( GetTarget == Player )\n Set number to 1\nEndIf\n"
                 "Set target to GetTarget"));
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "flag").integer, 1);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "number").integer, 0);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "target").integer, static_cast<std::int32_t>(sCrate));
    }

    TEST(ObScriptForms, a_form_can_be_stored_in_a_member_of_a_quest)
    {
        Ran ran("Set MyQuest.stage to Crate");
        ASSERT_TRUE(ran.mOk);
        const Value stored = ran.mFixture.mWorld.getVariable(sQuest, "stage");
        EXPECT_EQ(stored.integer, static_cast<std::int32_t>(sCrate));
    }

    TEST(ObScriptForms, an_unknown_name_where_a_value_is_expected_is_a_located_error_in_strict_mode)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        openeden::script::ScriptSource source;
        source.name = "Forms";
        source.text = wrap("Set flag to 1\nSet target to NobodyHere");
        const auto result = fixture.mRuntime->compile(source);
        EXPECT_FALSE(result.ok);
        ASSERT_FALSE(result.reports.empty());
        const auto& report = result.reports.front();
        EXPECT_EQ(report.severity, openeden::script::Severity::Error);
        EXPECT_EQ(report.script, "Forms");
        EXPECT_EQ(report.line, 9u); // six header lines, Begin is line 7, "Set flag" line 8, the bad line 9
        EXPECT_NE(report.message.find("NobodyHere"), std::string::npos);
    }

    TEST(ObScriptForms, an_unknown_name_is_a_warning_and_zero_in_tolerant_mode)
    {
        Ran ran("Set target to Crate\nSet target to NobodyHere", UnknownFunctionPolicy::Tolerant);
        ASSERT_TRUE(ran.mOk);
        EXPECT_EQ(ran.integer("target"), 0);
    }

    TEST(ObScriptForms, a_form_in_arithmetic_is_a_compile_error)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        openeden::script::ScriptSource source;
        source.name = "Forms";
        source.text = wrap("Set number to Crate + 1");
        EXPECT_FALSE(fixture.mRuntime->compile(source).ok);
    }

    TEST(ObScriptForms, a_name_the_world_stops_knowing_reads_as_none_at_run_time)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id = fixture.compileOk("Forms", wrap("Set target to Crate"));
        ASSERT_NE(id, 0u);
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        fixture.mWorld.mForms.erase("crate");
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "target").integer, 0);
    }

    TEST(ObScriptForms, a_fragment_can_use_form_values)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const ScriptId id
            = fixture.compileOk("Frag", "ref who\nSet who to Player\nIf ( who == Player )\n Set who to Crate\nEndIf",
                openeden::script::ScriptKind::Fragment);
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "who").integer, static_cast<std::int32_t>(sCrate));
    }

    TEST(ObScriptForms, the_saved_state_keeps_the_form_in_a_reference_variable)
    {
        Ran ran("Set target to Crate");
        ASSERT_TRUE(ran.mOk);
        const auto blob = ran.mFixture.mRuntime->serialise();
        RuntimeFixture second(UnknownFunctionPolicy::Strict);
        ASSERT_NE(second.compileOk("Forms", wrap("Set target to Crate")), 0u);
        ASSERT_TRUE(second.mRuntime->restore(blob).ok);
        EXPECT_EQ(second.mRuntime->getLocal(1, "target").integer, static_cast<std::int32_t>(sCrate));
    }
}
