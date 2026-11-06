// Task 0092 stages b and c: compiling and running ObScript on the stub world.
//
// In plain words: ObScript is the script language of Oblivion, Fallout 3 and Fallout New Vegas. These tests write
// small scripts of their own (no game text), compile them through the runtime of the common compatibility pack and run
// them against an invented world, then look at the local variables, the calls the scripts made and the messages the
// runtime printed. They are written from the task spec (docs/tasks/0092-...), not from the implementation.

#include <gtest/gtest.h>

#include <string>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::Budget;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::ScriptKind;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    const char* const sBasic = R"(ScriptName BasicScript
short count
long total
int big
float ratio
ref target

Begin GameMode
    Set count to count + 1
    Set total to count * 10 + 5
    Set ratio to total / 4.0
    If count == 1
        Set big to 100
    ElseIf count == 2
        Set big to 200
    Else
        Set big to 300
    EndIf
    If count > 2
        Return
    EndIf
    Set total to total + 1000
End

Begin OnActivate
    Set count to 42
End
)";

    struct ObScriptRun : public ::testing::Test
    {
        RuntimeFixture mFixture;
        openeden::script::ObScriptContract& runtime() { return *mFixture.mRuntime; }
    };

    TEST_F(ObScriptRun, a_script_with_blocks_and_every_local_type_compiles)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        EXPECT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
    }

    TEST_F(ObScriptRun, set_arithmetic_and_if_elseif_else_run_and_keep_their_locals_between_runs)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        ASSERT_NE(script, 0u);
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_NE(instance, 0u);

        auto result = runtime().run(instance, "GameMode");
        EXPECT_TRUE(result.ok) << result.error.format();
        EXPECT_TRUE(result.blockFound);
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 1);
        EXPECT_EQ(runtime().getLocal(instance, "total").integer, 1015);
        EXPECT_EQ(runtime().getLocal(instance, "big").integer, 100);
        EXPECT_FLOAT_EQ(runtime().getLocal(instance, "ratio").number, 3.75f);

        result = runtime().run(instance, "gamemode");
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 2);
        EXPECT_EQ(runtime().getLocal(instance, "total").integer, 1025);
        EXPECT_EQ(runtime().getLocal(instance, "big").integer, 200);

        // The third run takes the Return before the last Set.
        result = runtime().run(instance, "GAMEMODE");
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 3);
        EXPECT_EQ(runtime().getLocal(instance, "total").integer, 35);
        EXPECT_EQ(runtime().getLocal(instance, "big").integer, 300);
    }

    TEST_F(ObScriptRun, another_block_of_the_same_script_runs_by_its_event_name)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        const InstanceId instance = runtime().createInstance(script, sCrate);
        EXPECT_TRUE(runtime().run(instance, "OnActivate").ok);
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 42);
    }

    TEST_F(ObScriptRun, an_event_the_script_has_no_block_for_runs_nothing_and_is_not_an_error)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        const InstanceId instance = runtime().createInstance(script, sCrate);
        const auto result = runtime().run(instance, "OnDeath");
        EXPECT_TRUE(result.ok);
        EXPECT_FALSE(result.blockFound);
        EXPECT_EQ(result.instructionsExecuted, 0u);
    }

    TEST_F(ObScriptRun, two_instances_of_one_script_have_separate_locals)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        const InstanceId first = runtime().createInstance(script, sCrate);
        const InstanceId second = runtime().createInstance(script, sGold);
        EXPECT_NE(first, second);
        runtime().run(first, "GameMode");
        runtime().run(first, "GameMode");
        runtime().run(second, "GameMode");
        EXPECT_EQ(runtime().getLocal(first, "count").integer, 2);
        EXPECT_EQ(runtime().getLocal(second, "count").integer, 1);
    }

    TEST_F(ObScriptRun, an_unknown_script_or_instance_is_refused_without_a_crash)
    {
        EXPECT_EQ(runtime().createInstance(9999, sCrate), 0u);
        const auto result = runtime().run(12345, "GameMode");
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(runtime().getLocal(12345, "count").integer, 0);
        EXPECT_FALSE(runtime().setLocal(12345, "count", Value::makeInteger(1)));
    }

    TEST_F(ObScriptRun, a_local_can_be_set_from_outside_and_is_converted_to_its_type)
    {
        const ScriptId script = mFixture.compileOk("BasicScript", sBasic);
        const InstanceId instance = runtime().createInstance(script, sCrate);
        EXPECT_TRUE(runtime().setLocal(instance, "count", Value::makeInteger(9)));
        EXPECT_TRUE(runtime().setLocal(instance, "ratio", Value::makeInteger(2)));
        EXPECT_TRUE(runtime().setLocal(instance, "target", Value::makeForm(sPlayer)));
        EXPECT_FALSE(runtime().setLocal(instance, "nothing", Value::makeInteger(2)));
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 9);
        EXPECT_FLOAT_EQ(runtime().getLocal(instance, "ratio").number, 2.0f);
        EXPECT_EQ(static_cast<openeden::script::FormId>(runtime().getLocal(instance, "target").integer), sPlayer);
    }

    TEST_F(ObScriptRun, an_explicit_reference_call_reaches_the_registered_function_with_typed_arguments)
    {
        const ScriptId script = mFixture.compileOk("RefScript", R"(ScriptName RefScript
ref target
short result

Begin GameMode
    target.AddItem gold001 5
    player.ADDITEM gold001 2
    Set result to target.GetItemCount gold001
    Set result to result + player.GetItemCount gold001
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().setLocal(instance, "target", Value::makeForm(sCrate)));
        const auto result = runtime().run(instance, "GameMode");
        ASSERT_TRUE(result.ok) << result.error.format();

        ASSERT_EQ(mFixture.mCalls.size(), 4u);
        EXPECT_EQ(mFixture.mCalls[0].mFunction, "AddItem");
        EXPECT_EQ(mFixture.mCalls[0].mSelf, sCrate);
        EXPECT_EQ(mFixture.mCalls[0].mReference, sCrate);
        ASSERT_EQ(mFixture.mCalls[0].mArguments.size(), 2u);
        EXPECT_EQ(mFixture.mCalls[0].mArguments[0], "form" + std::to_string(sGold));
        EXPECT_EQ(mFixture.mCalls[0].mArguments[1], "i5");
        EXPECT_EQ(mFixture.mCalls[1].mReference, sPlayer);
        EXPECT_EQ(mFixture.mCalls[1].mArguments[1], "i2");
        EXPECT_EQ(runtime().getLocal(instance, "result").integer, 7);
    }

    TEST_F(ObScriptRun, a_function_returns_a_float_and_takes_float_and_integer_arguments)
    {
        const ScriptId script = mFixture.compileOk("ScaleScript", R"(ScriptName ScaleScript
float value
Begin GameMode
    Set value to ScaleBy 2.5 4
    Say "hello there"
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        EXPECT_FLOAT_EQ(runtime().getLocal(instance, "value").number, 10.0f);
        ASSERT_EQ(mFixture.mCalls.size(), 2u);
        EXPECT_EQ(mFixture.mCalls[1].mFunction, "Say");
        EXPECT_EQ(mFixture.mCalls[1].mArguments[0], "shello there");
    }

    TEST_F(ObScriptRun, optional_arguments_arrive_only_when_given)
    {
        const ScriptId script = mFixture.compileOk("OptionalScript", R"(ScriptName OptionalScript
Begin GameMode
    Greet 1
    Greet 1 2
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        ASSERT_EQ(mFixture.mCalls.size(), 2u);
        EXPECT_EQ(mFixture.mCalls[0].mArguments.size(), 1u);
        EXPECT_EQ(mFixture.mCalls[1].mArguments.size(), 2u);
    }

    TEST_F(ObScriptRun, a_quest_variable_is_read_and_written_through_the_world)
    {
        const ScriptId script = mFixture.compileOk("QuestScript", R"(ScriptName QuestScript
short count
Begin GameMode
    Set count to MyQuest.stage
    Set MyQuest.stage to count + 10
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        EXPECT_EQ(runtime().getLocal(instance, "count").integer, 3);
        const auto stage = mFixture.mWorld.mVariables[std::make_pair(sQuest, std::string("stage"))];
        EXPECT_EQ(stage.integer, 13);
    }

    TEST_F(ObScriptRun, globals_are_read_and_written_through_the_world)
    {
        const ScriptId script = mFixture.compileOk("GlobalScript", R"(ScriptName GlobalScript
Begin GameMode
    Set TestGlobal to TestGlobal + 5
    Set GameHour to GameHour + 1.5
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        EXPECT_EQ(mFixture.mWorld.mGlobals["testglobal"].integer, 10);
        EXPECT_FLOAT_EQ(mFixture.mWorld.mGlobals["gamehour"].number, 11.5f);
    }

    TEST_F(ObScriptRun, a_message_box_goes_to_the_world_report)
    {
        const ScriptId script = mFixture.compileOk("MessageScript", R"(ScriptName MessageScript
short count
Begin GameMode
    Set count to 4
    MessageBox "Count is %g" count
End
)");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        ASSERT_TRUE(runtime().run(instance, "GameMode").ok);
        ASSERT_FALSE(mFixture.mWorld.mMessages.empty());
        EXPECT_EQ(mFixture.mWorld.mMessages.back(), "Count is 4");
    }

    TEST_F(ObScriptRun, a_fragment_without_begin_compiles_and_runs_as_the_empty_event)
    {
        const ScriptId script = mFixture.compileOk(
            "ResultScript", "Set TestGlobal to TestGlobal + 7\nplayer.AddItem gold001 1\n", ScriptKind::Fragment);
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sPlayer);
        ASSERT_TRUE(runtime().run(instance, "").ok);
        ASSERT_TRUE(runtime().run(instance, "").ok);
        EXPECT_EQ(mFixture.mWorld.mGlobals["testglobal"].integer, 14);
        EXPECT_EQ(mFixture.mCalls.size(), 2u);
    }

    TEST_F(ObScriptRun, a_fragment_cannot_declare_locals)
    {
        openeden::script::ScriptSource source;
        source.name = "BadFragment";
        source.text = "short x\nSet x to 1\n";
        source.kind = ScriptKind::Fragment;
        const auto result = runtime().compile(source);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.reports.empty());
    }

    TEST_F(ObScriptRun, two_scripts_get_different_ids)
    {
        const ScriptId a = mFixture.compileOk("BasicScript", sBasic);
        const ScriptId b
            = mFixture.compileOk("RefScript2", "ScriptName RefScript2\nshort x\nBegin GameMode\nSet x to 5\nEnd\n");
        ASSERT_NE(a, 0u);
        ASSERT_NE(b, 0u);
        EXPECT_NE(a, b);
    }

    TEST_F(ObScriptRun, registering_a_function_twice_or_with_a_bad_signature_is_refused)
    {
        EXPECT_FALSE(mFixture.registerStub("addITEM", "rl", ValueKind::Integer));
        EXPECT_FALSE(mFixture.registerStub("Broken", "q", ValueKind::Integer));
        EXPECT_FALSE(mFixture.registerStub("", "l", ValueKind::Integer));
        EXPECT_FALSE(mFixture.registerStub("BadReturn", "l", ValueKind::String));
        EXPECT_TRUE(mFixture.registerStub("Fine", "l", ValueKind::Integer));
    }

    TEST_F(ObScriptRun, a_throwing_function_ends_that_script_with_an_error_and_the_host_continues)
    {
        openeden::script::FunctionDescriptor descriptor;
        descriptor.name = "Explode";
        descriptor.signature = "";
        openeden::script::ErrorReport error;
        ASSERT_TRUE(runtime().registerFunction(
            descriptor, [](const openeden::script::CallArgs&) -> Value { throw std::runtime_error("boom"); }, error));
        const ScriptId script
            = mFixture.compileOk("ExplodeScript", "ScriptName ExplodeScript\nBegin GameMode\nExplode\nEnd\n");
        ASSERT_NE(script, 0u) << (mFixture.mLastReports.empty() ? "" : mFixture.mLastReports.front());
        const InstanceId instance = runtime().createInstance(script, sCrate);
        const auto result = runtime().run(instance, "GameMode");
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("boom"), std::string::npos) << result.error.format();
        EXPECT_EQ(result.error.script, "ExplodeScript");

        const ScriptId other
            = mFixture.compileOk("Other", "ScriptName Other\nshort x\nBegin GameMode\nSet x to 5\nEnd\n");
        const InstanceId second = runtime().createInstance(other, sCrate);
        EXPECT_TRUE(runtime().run(second, "GameMode").ok);
        EXPECT_EQ(runtime().getLocal(second, "x").integer, 5);
    }

    // A handler that runs the script that called it again is unbounded recursion unless the depth cap stops it.
    TEST(ObScriptRunRecursion, nested_runs_beyond_the_depth_cap_end_with_a_depth_error)
    {
        RuntimeFixture fixture;
        InstanceId instance = 0;
        openeden::script::FunctionDescriptor descriptor;
        descriptor.name = "Recurse";
        descriptor.signature = "";
        openeden::script::ErrorReport error;
        openeden::script::RunResult inner;
        ASSERT_TRUE(fixture.mRuntime->registerFunction(
            descriptor,
            [&](const openeden::script::CallArgs&) {
                inner = fixture.mRuntime->run(instance, "GameMode");
                return Value::makeInteger(0);
            },
            error));
        const ScriptId script = fixture.compileOk("Recursive", "ScriptName Recursive\nBegin GameMode\nRecurse\nEnd\n");
        ASSERT_NE(script, 0u);
        instance = fixture.mRuntime->createInstance(script, sCrate);
        Budget budget;
        budget.maxCallDepth = 5;
        const auto result = fixture.mRuntime->run(instance, "GameMode", budget);
        // The innermost run is stopped by the cap; its error is reported by the run that was stopped.
        EXPECT_FALSE(result.ok && inner.ok);
        std::string text = result.error.message + inner.error.message;
        EXPECT_NE(text.find("call depth"), std::string::npos) << text;
    }
}
