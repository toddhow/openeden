// Task 0092, gap D: the result of a command used as the reference of the next one ("GetTarget.Probe 5").
//
// In plain words: a command that returns an object may be written right before a dot, and what follows the dot is
// then called on that object, as it would be after a variable or an editor id. These tests use generated scripts and
// functions with invented names against the stub world.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::CallArgs;
    using openeden::script::ErrorReport;
    using openeden::script::FunctionDescriptor;
    using openeden::script::InstanceId;
    using openeden::script::ScriptSource;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    struct CallRefFixture
    {
        explicit CallRefFixture(UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant)
            : mFixture(policy)
        {
            mFixture.mItems[sCrate] = 4;
            mFixture.mItems[sQuest] = 9;
            mFixture.registerStub("Probe", "l", ValueKind::Integer);
            registerForm("GetTarget", sCrate);
            registerForm("GetOther", sQuest);
        }

        void registerForm(const char* name, openeden::script::FormId form)
        {
            FunctionDescriptor descriptor;
            descriptor.name = name;
            descriptor.signature = "";
            descriptor.returns = ValueKind::Integer;
            ErrorReport error;
            const std::string text = name;
            const bool registered = mFixture.mRuntime->registerFunction(
                descriptor,
                [this, text, form](const CallArgs&) {
                    Call call;
                    call.mFunction = text;
                    mFixture.mCalls.push_back(call);
                    return Value::makeInteger(static_cast<std::int32_t>(form));
                },
                error);
            EXPECT_TRUE(registered) << error.format();
        }

        bool compile(const std::string& body)
        {
            ScriptSource source;
            source.name = "CallRef";
            source.text = "ScriptName CallRef\nlong number\nshort flag\nBegin GameMode\n" + body + "\nEnd\n";
            const auto result = mFixture.mRuntime->compile(source);
            mOk = result.ok;
            mScript = result.script;
            for (const auto& report : result.reports)
            {
                mReports += report.format() + "; ";
            }
            return mOk;
        }

        bool run()
        {
            mInstance = mFixture.mRuntime->createInstance(mScript, sPlayer);
            return mFixture.mRuntime->run(mInstance, "GameMode").ok;
        }

        std::int32_t integer(const char* name) const { return mFixture.mRuntime->getLocal(mInstance, name).integer; }

        std::vector<std::string> trace() const
        {
            std::vector<std::string> result;
            for (const Call& call : mFixture.mCalls)
            {
                std::string line = call.mFunction + "@" + std::to_string(call.mReference);
                for (const std::string& argument : call.mArguments)
                {
                    line += " " + argument;
                }
                result.push_back(line);
            }
            return result;
        }

        RuntimeFixture mFixture;
        bool mOk = false;
        openeden::script::ScriptId mScript = 0;
        InstanceId mInstance = 0;
        std::string mReports;
    };

    std::string at(openeden::script::FormId form)
    {
        return std::to_string(form);
    }

    TEST(ObScriptCallRef, a_command_result_is_the_reference_of_a_statement)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("GetTarget.Probe 5")) << f.mReports;
        ASSERT_TRUE(f.run());
        const std::vector<std::string> expected = { "GetTarget@0", "Probe@" + at(sCrate) + " i5" };
        EXPECT_EQ(f.trace(), expected);
    }

    TEST(ObScriptCallRef, the_arrow_works_like_the_dot)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("GetTarget->Probe 6")) << f.mReports;
        ASSERT_TRUE(f.run());
        const std::vector<std::string> expected = { "GetTarget@0", "Probe@" + at(sCrate) + " i6" };
        EXPECT_EQ(f.trace(), expected);
    }

    TEST(ObScriptCallRef, a_command_result_is_the_reference_inside_an_expression)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(
            f.compile("Set number to GetTarget.GetItemCount Gold001\nIf ( GetOther.GetItemCount Gold001 == 9 )\n Set "
                      "flag to 1\nEndIf"))
            << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 4);
        EXPECT_EQ(f.integer("flag"), 1);
    }

    TEST(ObScriptCallRef, a_reference_call_in_the_arguments_of_a_reference_call_keeps_both_references)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("GetTarget.Probe GetOther.GetItemCount Gold001")) << f.mReports;
        ASSERT_TRUE(f.run());
        // The first call, then the arguments (which make their own call on the other object), then the member.
        const std::vector<std::string> expected
            = { "GetTarget@0", "GetOther@0", "GetItemCount@" + at(sQuest) + " form512", "Probe@" + at(sCrate) + " i9" };
        EXPECT_EQ(f.trace(), expected);
    }

    TEST(ObScriptCallRef, two_in_one_expression_each_use_their_own_object)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("Set number to ( GetTarget.GetItemCount Gold001 ) + ( GetOther.GetItemCount Gold001 )"))
            << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 13);
    }

    TEST(ObScriptCallRef, a_command_without_a_dot_is_unchanged)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("Set number to GetTarget\nPlayer.Probe 2")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), static_cast<std::int32_t>(sCrate));
        const std::vector<std::string> expected = { "GetTarget@0", "Probe@" + at(sPlayer) + " i2" };
        EXPECT_EQ(f.trace(), expected);
    }

    TEST(ObScriptCallRef, a_command_nobody_knows_still_works_as_a_reference_in_tolerant_mode)
    {
        CallRefFixture f;
        ASSERT_TRUE(f.compile("Frobnicate.Probe 5\nSet number to 3")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 3);
        // The unknown command returns zero (no object), so the member runs on the script's own object.
        ASSERT_EQ(f.trace().size(), 1u);
        EXPECT_EQ(f.trace()[0], "Probe@0 i5");
    }

    TEST(ObScriptCallRef, an_unknown_command_may_be_followed_by_a_member_in_a_condition)
    {
        CallRefFixture f;
        ASSERT_TRUE(f.compile("Frobnicate.Probe 1\nIf ( Frobnicate.GetItemCount Gold001 == 0 )\n Set flag to 1\nEndIf"))
            << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("flag"), 1);
    }

    TEST(ObScriptCallRef, several_uses_in_one_script_run_again_with_the_same_result)
    {
        CallRefFixture f;
        ASSERT_TRUE(f.compile("GetTarget.Probe 1\nGetOther.Probe 2\nGetTarget.Probe 3")) << f.mReports;
        ASSERT_TRUE(f.run());
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.trace().size(), 12u);
        EXPECT_EQ(f.trace()[5], "Probe@" + at(sCrate) + " i3");
        EXPECT_EQ(f.trace()[8], "GetOther@0");
    }

    TEST(ObScriptCallRef, a_saved_script_carries_on_with_the_same_results)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("GetTarget.Probe 5")) << f.mReports;
        ASSERT_TRUE(f.run());
        const auto blob = f.mFixture.mRuntime->serialise();
        const auto restored = f.mFixture.mRuntime->restore(blob);
        ASSERT_TRUE(restored.ok);
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.trace().back(), "Probe@" + at(sCrate) + " i5");
    }
}
