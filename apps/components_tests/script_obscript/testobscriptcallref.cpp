// Task 0092, gap D: the result of a command used as the reference of the next one ("GetTarget.Probe 5").
//
// In plain words: a command that returns an object may be written right before a dot, and what follows the dot is
// then called on that object, as it would be after a variable or an editor id. These tests use generated scripts and
// functions with invented names against the stub world.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <components/script_obscript/obscriptlimits.hpp>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::CallArgs;
    using openeden::script::ErrorReport;
    using openeden::script::FunctionDescriptor;
    using openeden::script::InstanceId;
    using openeden::script::RestoreResult;
    using openeden::script::SaveBlob;
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
                [this, text, form](const CallArgs& args) {
                    Call call;
                    call.mFunction = text;
                    call.mReference = args.reference;
                    mFixture.mCalls.push_back(call);
                    return Value::makeInteger(static_cast<std::int32_t>(form));
                },
                error);
            EXPECT_TRUE(registered) << error.format();
        }

        bool compile(const std::string& body, const std::string& extraHeader = "")
        {
            ScriptSource source;
            source.name = "CallRef";
            source.text
                = "ScriptName CallRef\nlong number\nshort flag\n" + extraHeader + "Begin GameMode\n" + body + "\nEnd\n";
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

    TEST(ObScriptCallRef, a_command_nobody_knows_is_a_reference_to_nothing_and_its_member_is_skipped)
    {
        // Review 8, finding 1 (this task's own test, which used to pin "Probe@0 i5"): the unknown command returns
        // zero (no object). A member called on "no object" must not run as if it had no reference, because handlers
        // read that as the script's own object.
        CallRefFixture f;
        ASSERT_TRUE(f.compile("Frobnicate.Probe 5\nFrobnicate.Probe 6\nSet number to 3")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 3);
        EXPECT_TRUE(f.trace().empty());
        int notices = 0;
        for (const std::string& message : f.mFixture.mWorld.mMessages)
        {
            notices += message.find("Probe") != std::string::npos ? 1 : 0;
        }
        EXPECT_EQ(notices, 1) << (f.mFixture.mWorld.mMessages.empty() ? "no message" : f.mFixture.mWorld.mMessages[0])
                              << "; one notice for the function, however often it is skipped";
    }

    TEST(ObScriptCallRef, a_command_that_returns_none_does_not_run_the_member_on_the_script_itself)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        f.mFixture.mItems[sPlayer] = 50;
        f.registerForm("GetNone", 0);
        ASSERT_TRUE(f.compile("GetNone.Probe 5\nSet number to GetNone.GetItemCount Gold001 + 1")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 1);
        const std::vector<std::string> expected = { "GetNone@0", "GetNone@0" };
        EXPECT_EQ(f.trace(), expected);
    }

    TEST(ObScriptCallRef, an_explicit_reference_variable_that_was_never_set_does_not_run_the_member_on_self)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("target.Probe 5", "ref target\n")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_TRUE(f.trace().empty());
    }

    TEST(ObScriptCallRef, a_result_may_be_the_reference_of_a_second_call_in_a_row)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("GetTarget.GetOther.Probe 1\nSet number to GetTarget.GetOther.GetItemCount Gold001"))
            << f.mReports;
        ASSERT_TRUE(f.run());
        const std::vector<std::string> expected
            = { "GetTarget@0", "GetOther@" + at(sCrate), "Probe@" + at(sQuest) + " i1", "GetTarget@0",
                  "GetOther@" + at(sCrate), "GetItemCount@" + at(sQuest) + " form512" };
        EXPECT_EQ(f.trace(), expected);
        EXPECT_EQ(f.integer("number"), 9);
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

    // ---- review 8: the blob does not depend on hidden locals

    std::string keepScript(const std::string& declarations, const std::string& body)
    {
        return "ScriptName Keep\n" + declarations + "Begin GameMode\n" + body + "\nEnd\n";
    }

    void putU16(std::vector<std::uint8_t>& bytes, std::uint32_t v)
    {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    }

    void putU32(std::vector<std::uint8_t>& bytes, std::uint32_t v)
    {
        putU16(bytes, v & 0xffff);
        putU16(bytes, v >> 16);
    }

    TEST(ObScriptCallRef, a_save_does_not_carry_the_hidden_locals_so_a_script_with_other_ones_restores_it)
    {
        // Same variables a and b; the first version uses a call result (a hidden local), the second does not.
        RuntimeFixture first;
        first.registerStub("Probe", "l", ValueKind::Integer);
        first.registerStub("GetThing", "", ValueKind::Integer);
        const auto idA
            = first.compileOk("Keep", keepScript("long a\nlong b\n", "Set a to 4\nSet b to 8\nGetThing.Probe 1"));
        ASSERT_NE(idA, 0u) << (first.mLastReports.empty() ? "" : first.mLastReports[0]);
        const auto instance = first.mRuntime->createInstance(idA, sPlayer);
        ASSERT_TRUE(first.mRuntime->run(instance, "GameMode").ok);
        const SaveBlob blob = first.mRuntime->serialise();

        RuntimeFixture second;
        const auto idB = second.compileOk("Keep", keepScript("long a\nlong b\n", "Set a to 1"));
        ASSERT_NE(idB, 0u);
        const RestoreResult restored = second.mRuntime->restore(blob);
        ASSERT_TRUE(restored.ok) << restored.error.format();
        EXPECT_EQ(second.mRuntime->getLocal(1, "a").integer, 4);
        EXPECT_EQ(second.mRuntime->getLocal(1, "b").integer, 8);
    }

    TEST(ObScriptCallRef, an_edit_that_keeps_the_variable_counts_cannot_permute_the_restored_values)
    {
        // Version 1: a and a hidden local (two longs). Version 2: a and b (two longs). The positional blob put the
        // hidden local's stale value into b.
        RuntimeFixture first;
        const auto idA = first.compileOk("Keep", keepScript("long a\n", "Set a to 4\nGetThing.GetItemCount Gold001"));
        ASSERT_NE(idA, 0u) << (first.mLastReports.empty() ? "" : first.mLastReports[0]);
        const auto instance = first.mRuntime->createInstance(idA, sPlayer);
        ASSERT_TRUE(first.mRuntime->run(instance, "GameMode").ok);
        const SaveBlob blob = first.mRuntime->serialise();

        RuntimeFixture second;
        ASSERT_NE(second.compileOk("Keep", keepScript("long a\nlong b\n", "Set a to 1")), 0u);
        const RestoreResult restored = second.mRuntime->restore(blob);
        EXPECT_FALSE(restored.ok) << "b is not in the save: this is another script";
    }

    TEST(ObScriptCallRef, restored_values_follow_the_names_when_the_declarations_are_reordered)
    {
        RuntimeFixture first;
        const auto idA = first.compileOk("Keep", keepScript("long a\nlong b\n", "Set a to 4\nSet b to 8"));
        ASSERT_NE(idA, 0u);
        const auto instance = first.mRuntime->createInstance(idA, sPlayer);
        ASSERT_TRUE(first.mRuntime->run(instance, "GameMode").ok);
        const SaveBlob blob = first.mRuntime->serialise();

        RuntimeFixture second;
        ASSERT_NE(second.compileOk("Keep", keepScript("long b\nlong a\n", "Set a to 1")), 0u);
        const RestoreResult restored = second.mRuntime->restore(blob);
        ASSERT_TRUE(restored.ok) << restored.error.format();
        EXPECT_EQ(second.mRuntime->getLocal(1, "a").integer, 4);
        EXPECT_EQ(second.mRuntime->getLocal(1, "b").integer, 8);
    }

    TEST(ObScriptCallRef, a_version_1_save_with_positional_variables_is_still_restored)
    {
        // The layout this runtime wrote before: counts, then the values by position.
        RuntimeFixture f;
        ASSERT_NE(f.compileOk("Keep", keepScript("long a\nlong b\nshort c\nfloat d\n", "Set a to 1")), 0u);
        SaveBlob blob;
        blob.formatVersion = 1;
        putU32(blob.bytes, 1); // instances
        putU32(blob.bytes, 2); // next id
        putU32(blob.bytes, 1); // id
        putU32(blob.bytes, sCrate); // self
        putU16(blob.bytes, 4);
        for (const char c : std::string("keep"))
        {
            blob.bytes.push_back(static_cast<std::uint8_t>(c));
        }
        putU16(blob.bytes, 1); // shorts
        putU16(blob.bytes, 2); // longs
        putU16(blob.bytes, 1); // floats
        putU16(blob.bytes, 7);
        putU32(blob.bytes, 11);
        putU32(blob.bytes, 22);
        const float real = 2.5f;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &real, sizeof(bits));
        putU32(blob.bytes, bits);
        const RestoreResult restored = f.mRuntime->restore(blob);
        ASSERT_TRUE(restored.ok) << restored.error.format();
        EXPECT_EQ(f.mRuntime->getLocal(1, "a").integer, 11);
        EXPECT_EQ(f.mRuntime->getLocal(1, "b").integer, 22);
        EXPECT_EQ(f.mRuntime->getLocal(1, "c").integer, 7);
    }

    // ---- review 8: limits

    TEST(ObScriptCallRef, a_hidden_local_respects_the_limit_on_variables)
    {
        RuntimeFixture f;
        f.registerStub("Probe", "l", ValueKind::Integer);
        f.registerStub("GetThing", "", ValueKind::Integer);
        std::string declarations;
        for (std::size_t i = 0; i < ScriptObscript::Limits::kMaxLocals; ++i)
        {
            declarations += "long v" + std::to_string(i) + "\n";
        }
        EXPECT_EQ(f.compileOk("Full", keepScript(declarations, "GetThing.Probe 1")), 0u);
        ASSERT_FALSE(f.mLastReports.empty());
        EXPECT_NE(f.mLastReports[0].find("too many variables"), std::string::npos) << f.mLastReports[0];

        // One fewer user variable leaves room for the hidden one.
        RuntimeFixture g;
        g.registerStub("Probe", "l", ValueKind::Integer);
        g.registerStub("GetThing", "", ValueKind::Integer);
        declarations.erase(declarations.rfind("long v"));
        EXPECT_NE(g.compileOk("Room", keepScript(declarations, "GetThing.Probe 1")), 0u);
    }

    TEST(ObScriptCallRef, a_deep_chain_of_call_results_stops_at_the_depth_cap_and_does_not_overflow)
    {
        for (const std::size_t terms : { std::size_t(70), std::size_t(20000) })
        {
            RuntimeFixture f;
            f.registerStub("Probe", "l", ValueKind::Integer);
            f.registerStub("GetThing", "", ValueKind::Integer);
            std::string body;
            for (std::size_t i = 0; i < terms; ++i)
            {
                body += "GetThing.Probe ";
            }
            body += "1";
            EXPECT_EQ(f.compileOk("Deep", keepScript("long a\n", body)), 0u) << terms;
            ASSERT_FALSE(f.mLastReports.empty());
            EXPECT_NE(f.mLastReports[0].find("nesting"), std::string::npos) << f.mLastReports[0];
        }
    }

    TEST(ObScriptCallRef, the_deepest_allowed_chain_compiles)
    {
        RuntimeFixture f;
        f.registerStub("Probe", "l", ValueKind::Integer);
        f.registerStub("GetThing", "", ValueKind::Integer);
        std::string body;
        for (std::size_t i = 0; i + 1 < ScriptObscript::Limits::kMaxCallDepth; ++i)
        {
            body += "GetThing.Probe ";
        }
        body += "1";
        EXPECT_NE(f.compileOk("Deep", keepScript("long a\n", body)), 0u)
            << (f.mLastReports.empty() ? "" : f.mLastReports[0]);
    }

    // ---- review 8: names before a dot

    TEST(ObScriptCallRef, in_strict_mode_a_missing_form_before_a_dot_is_not_called_an_unknown_function)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(f.compile("Nobody.Probe 1"));
        EXPECT_EQ(f.mReports.find("unknown function"), std::string::npos) << f.mReports;
        EXPECT_NE(f.mReports.find("Nobody"), std::string::npos) << f.mReports;
        EXPECT_NE(f.mReports.find("form"), std::string::npos) << f.mReports;
    }

    TEST(ObScriptCallRef, a_word_before_a_dot_is_a_command_only_with_a_known_member_or_arguments)
    {
        CallRefFixture f;
        EXPECT_TRUE(f.compile("Zork.Frob 5")) << f.mReports; // both unknown, arguments follow: a call
        CallRefFixture g;
        EXPECT_FALSE(g.compile("Gone.Whatever")); // a bare "Name.Word" is a form or variable that is missing
        EXPECT_EQ(g.mReports.find("too many"), std::string::npos);
    }

    TEST(ObScriptCallRef, names_before_a_dot_have_a_bucket_of_their_own_per_script)
    {
        CallRefFixture f;
        std::string sixtyFour;
        for (int i = 0; i < 64; ++i)
        {
            sixtyFour += "Zed" + std::to_string(i) + ".Probe 1\n";
        }
        EXPECT_TRUE(f.compile(sixtyFour)) << f.mReports;

        CallRefFixture g;
        std::string many;
        for (int i = 0; i < 100; ++i)
        {
            many += "Zed" + std::to_string(i) + ".Probe 1\n";
        }
        EXPECT_FALSE(g.compile(many));
        EXPECT_EQ(g.mReports.find("too many different unknown functions"), std::string::npos) << g.mReports;
    }

    TEST(ObScriptCallRef, names_before_a_dot_do_not_use_up_the_room_for_unknown_functions)
    {
        CallRefFixture f;
        for (int script = 0; script < 80; ++script)
        {
            std::string names;
            for (int i = 0; i < 60; ++i)
            {
                names += "N" + std::to_string(script) + "x" + std::to_string(i) + ".Probe 1\n";
            }
            f.compile(names); // the later ones are refused by the bucket: that is the point
        }
        std::string genuine;
        for (int i = 0; i < 100; ++i)
        {
            genuine += "Zork" + std::to_string(i) + " 1\n";
        }
        EXPECT_TRUE(f.compile(genuine)) << f.mReports;
    }

    // ---- review 8: what follows a call

    TEST(ObScriptCallRef, a_comment_or_the_end_of_the_text_after_a_call_is_not_a_member)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(f.compile("Set number to GetTarget ; the target\nGetTarget ; again\nSet flag to GetOther"))
            << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), static_cast<std::int32_t>(sCrate));
        EXPECT_EQ(f.integer("flag"), static_cast<std::int32_t>(sQuest));

        ScriptSource source;
        source.name = "Eof";
        source.text = "GetTarget";
        source.kind = openeden::script::ScriptKind::Fragment;
        const auto result = f.mFixture.mRuntime->compile(source);
        EXPECT_TRUE(result.ok) << (result.reports.empty() ? "" : result.reports[0].format());
    }

    TEST(ObScriptCallRef, a_stray_dot_after_a_call_is_skipped_in_a_statement)
    {
        CallRefFixture f;
        ASSERT_TRUE(f.compile("GetTarget.\nGetTarget .\nGetTarget ...\nSet number to 3")) << f.mReports;
        ASSERT_TRUE(f.run());
        EXPECT_EQ(f.integer("number"), 3);
        EXPECT_EQ(f.trace().size(), 3u);
    }

    TEST(ObScriptCallRef, a_dot_with_no_member_after_it_in_an_expression_is_an_error_and_never_a_crash)
    {
        for (const char* body : { "Set number to GetTarget.", "Set number to GetTarget.Unknownmember",
                 "If ( GetTarget. )\n Set flag to 1\nEndIf", "Set number to GetTarget.\nSet flag to 1" })
        {
            CallRefFixture f(UnknownFunctionPolicy::Strict);
            EXPECT_FALSE(f.compile(body)) << body;
            EXPECT_FALSE(f.mReports.empty()) << body;
        }
    }

    TEST(ObScriptCallRef, a_script_cannot_declare_a_variable_that_collides_with_a_hidden_local)
    {
        CallRefFixture f(UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(f.compile("GetTarget.Probe 5", "long $ref0\n"));
        EXPECT_FALSE(f.mReports.empty());
        CallRefFixture g(UnknownFunctionPolicy::Strict);
        EXPECT_FALSE(g.compile("GetTarget.Probe 5", "ref $ref1\n"));
    }
}
