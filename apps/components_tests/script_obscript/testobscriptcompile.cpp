// Task 0092 stage c: the ObScript front end: tolerant and strict handling of unknown functions, located errors, hostile
// input and the compile caps.
//
// In plain words: a script can be wrong in many ways, or hostile. None of them may crash the game, loop for ever or
// stop the next script from compiling, and every problem must say where it is ("script:line:column: message"). The caps
// (how big a script, how many variables, how deep a bracket may nest) are named in
// components/script_obscript/obscriptlimits.hpp and tested here at the limit and one past it.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <components/script_obscript/obscriptlimits.hpp>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::CompileResult;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::ScriptKind;
    using openeden::script::ScriptSource;
    using openeden::script::UnknownFunctionPolicy;

    const char* const sTolerant = R"(ScriptName TolerantScript
short count

Begin GameMode
    Frobnicate 1 2
    Set count to GetWidgetCount player 3
    If SomeUnknownCheck == 0
        Set count to count + 1
    EndIf
    Frobnicate 5
End
)";

    CompileResult compileSource(
        RuntimeFixture& fixture, const std::string& name, const std::string& text, ScriptKind kind = ScriptKind::Script)
    {
        ScriptSource source;
        source.name = name;
        source.text = text;
        source.kind = kind;
        return fixture.mRuntime->compile(source);
    }

    std::string firstError(const CompileResult& result)
    {
        for (const auto& report : result.reports)
        {
            if (report.severity == openeden::script::Severity::Error)
            {
                return report.format();
            }
        }
        return {};
    }

    const char* const sValid = "ScriptName Valid\nshort x\nBegin GameMode\nSet x to 1\nEnd\n";

    TEST(ObScriptTolerant, an_unknown_function_with_arguments_compiles_and_does_nothing)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const ScriptId script = fixture.compileOk("TolerantScript", sTolerant);
        ASSERT_NE(script, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(script, sCrate);
        const auto result = fixture.mRuntime->run(instance, "GameMode");
        EXPECT_TRUE(result.ok) << result.error.format();
        // GetWidgetCount returns zero; SomeUnknownCheck == 0 is true, so the count is incremented once.
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "count").integer, 1);
    }

    TEST(ObScriptTolerant, each_unknown_name_is_logged_once_however_often_it_runs)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const ScriptId script = fixture.compileOk("TolerantScript", sTolerant);
        ASSERT_NE(script, 0u);
        const InstanceId instance = fixture.mRuntime->createInstance(script, sCrate);
        for (int i = 0; i < 3; ++i)
        {
            ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        }
        int frobnicate = 0;
        int widget = 0;
        int check = 0;
        for (const std::string& message : fixture.mWorld.mMessages)
        {
            frobnicate += message.find("frobnicate") != std::string::npos ? 1 : 0;
            widget += message.find("getwidgetcount") != std::string::npos ? 1 : 0;
            check += message.find("someunknowncheck") != std::string::npos ? 1 : 0;
        }
        EXPECT_EQ(frobnicate, 1);
        EXPECT_EQ(widget, 1);
        EXPECT_EQ(check, 1);
        EXPECT_EQ(fixture.mWorld.mMessages.size(), 3u);
    }

    TEST(ObScriptTolerant, the_same_unknown_name_in_a_second_script_is_not_logged_again)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const ScriptId first = fixture.compileOk("TolerantScript", sTolerant);
        const ScriptId second
            = fixture.compileOk("SecondScript", "ScriptName SecondScript\nBegin GameMode\nFrobnicate 9\nEnd\n");
        ASSERT_NE(first, 0u);
        ASSERT_NE(second, 0u);
        fixture.mRuntime->run(fixture.mRuntime->createInstance(first, sCrate), "GameMode");
        fixture.mRuntime->run(fixture.mRuntime->createInstance(second, sCrate), "GameMode");
        EXPECT_EQ(fixture.mWorld.mMessages.size(), 3u);
    }

    TEST(ObScriptTolerant, a_known_function_is_not_treated_as_unknown)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const ScriptId script = fixture.compileOk(
            "KnownScript", "ScriptName KnownScript\nBegin GameMode\nplayer.AddItem gold001 3\nEnd\n");
        ASSERT_NE(script, 0u);
        const InstanceId instance = fixture.mRuntime->createInstance(script, sCrate);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_TRUE(fixture.mWorld.mMessages.empty());
        EXPECT_EQ(fixture.mCalls.size(), 1u);
    }

    TEST(ObScriptStrict, an_unknown_function_is_a_located_error)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const CompileResult result = compileSource(fixture, "TolerantScript", sTolerant);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.script, 0u);
        // "    Frobnicate 1 2" is line 5, and the name starts in column 5.
        EXPECT_EQ(firstError(result), "TolerantScript:5:5: unknown function 'Frobnicate'");
    }

    TEST(ObScriptStrict, every_unknown_function_is_reported_once_at_its_first_use)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const CompileResult result = compileSource(fixture, "TolerantScript", sTolerant);
        std::vector<std::string> errors;
        for (const auto& report : result.reports)
        {
            if (report.severity == openeden::script::Severity::Error)
            {
                errors.push_back(report.format());
            }
        }
        ASSERT_EQ(errors.size(), 3u);
        EXPECT_EQ(errors[1], "TolerantScript:6:18: unknown function 'GetWidgetCount'");
        EXPECT_EQ(errors[2], "TolerantScript:7:8: unknown function 'SomeUnknownCheck'");
    }

    TEST(ObScriptStrict, a_script_that_only_uses_known_names_compiles)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        EXPECT_NE(fixture.compileOk("Valid", sValid), 0u);
        EXPECT_NE(fixture.compileOk("Known", "ScriptName Known\nBegin GameMode\nplayer.AddItem gold001 3\nEnd\n"), 0u);
    }

    TEST(ObScriptStrict, a_warning_of_the_shared_parser_is_an_error)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const CompileResult result
            = compileSource(fixture, "Dup", "ScriptName Dup\nshort a\nshort a\nBegin GameMode\nEnd\n");
        EXPECT_FALSE(result.ok);
        EXPECT_NE(firstError(result).find("re-declaration"), std::string::npos) << firstError(result);
    }

    TEST(ObScriptTolerant, the_same_warning_is_only_a_warning)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const CompileResult result
            = compileSource(fixture, "Dup", "ScriptName Dup\nshort a\nshort a\nBegin GameMode\nEnd\n");
        EXPECT_TRUE(result.ok);
        bool warned = false;
        for (const auto& report : result.reports)
        {
            warned = warned
                || (report.severity == openeden::script::Severity::Warning
                    && report.message.find("re-declaration") != std::string::npos);
        }
        EXPECT_TRUE(warned);
    }

    struct Malformed
    {
        const char* mName;
        std::string mText;
        const char* mExpected; // a part of the message
        std::uint32_t mLine; // 0: no position expected
    };

    std::vector<Malformed> malformedTable()
    {
        std::vector<Malformed> table;
        table.push_back({ "missing End", "ScriptName S\nshort a\nBegin GameMode\nSet a to 1\n", "end of file", 0 });
        table.push_back(
            { "EndIf without If", "ScriptName S\nBegin GameMode\nEndIf\nEnd\n", "endif without matching", 3 });
        table.push_back(
            { "duplicate variable", "ScriptName S\nshort a\nlong a\nBegin GameMode\nEnd\n", "re-declaration", 3 });
        table.push_back({ "type mismatch", "ScriptName S\nshort a\nBegin GameMode\nSet a to \"text\"\nEnd\n",
            "non-variable string", 4 });
        table.push_back({ "overlong identifier",
            "ScriptName S\nshort " + std::string(ScriptObscript::Limits::kMaxIdentifierLength + 1, 'x')
                + "\nBegin GameMode\nEnd\n",
            "identifier", 2 });
        table.push_back({ "100000 nested parentheses",
            "ScriptName S\nshort a\nBegin GameMode\nSet a to " + std::string(100000, '(') + "1"
                + std::string(100000, ')') + "\nEnd\n",
            "nesting", 4 });
        table.push_back(
            { "unclosed parenthesis", "ScriptName S\nshort a\nBegin GameMode\nSet a to ( 1 + 2\nEnd\n", "", 0 });
        table.push_back(
            { "stray number", "ScriptName S\nBegin GameMode\n1 2 3\nEnd\n", "Unexpected numeric value", 3 });
        table.push_back({ "Begin without an event", "ScriptName S\nBegin\nEnd\n", "event", 2 });
        table.push_back({ "no ScriptName", "short a\nBegin GameMode\nEnd\n", "ScriptName", 1 });
        table.push_back(
            { "unknown variable", "ScriptName S\nBegin GameMode\nSet nothing to 1\nEnd\n", "Unknown variable", 3 });
        table.push_back({ "else without if", "ScriptName S\nBegin GameMode\nElse\nEnd\n", "Stray else", 3 });
        table.push_back({ "empty script", "", "", 0 });
        table.push_back(
            { "binary garbage", std::string("ScriptName S\nBegin GameMode\n\x01\x02\xff\xfe\nEnd\n"), "", 0 });
        table.push_back({ "unclosed if", "ScriptName S\nBegin GameMode\nif ( 1 )\nSet TestGlobal to 1\nEnd\n", "", 0 });
        return table;
    }

    TEST(ObScriptMalformed, every_malformed_input_gives_its_error_and_the_next_script_still_compiles)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        for (const Malformed& item : malformedTable())
        {
            SCOPED_TRACE(item.mName);
            const CompileResult result = compileSource(fixture, "Bad", item.mText);
            EXPECT_FALSE(result.ok);
            EXPECT_EQ(result.script, 0u);
            const std::string error = firstError(result);
            ASSERT_FALSE(error.empty()) << "no error was reported";
            EXPECT_EQ(error.rfind("Bad", 0), 0u) << error;
            EXPECT_NE(error.find(item.mExpected), std::string::npos) << error;
            if (item.mLine != 0)
            {
                EXPECT_EQ(error.rfind("Bad:" + std::to_string(item.mLine) + ":", 0), 0u) << error;
            }

            EXPECT_NE(fixture.compileOk("Valid", sValid), 0u) << "the next script no longer compiles";
        }
    }

    TEST(ObScriptMalformed, the_same_inputs_do_not_crash_in_tolerant_mode_either)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        for (const Malformed& item : malformedTable())
        {
            SCOPED_TRACE(item.mName);
            const CompileResult result = compileSource(fixture, "Bad", item.mText);
            if (!result.ok)
            {
                EXPECT_FALSE(firstError(result).empty());
            }
            EXPECT_NE(fixture.compileOk("Valid", sValid), 0u);
        }
    }

    TEST(ObScriptMalformed, compiled_bytecode_is_never_read_so_source_that_looks_like_it_is_just_text)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const CompileResult result
            = compileSource(fixture, "Bytes", std::string("SCDA\x10\x00\x04\x00\x00\x00\x00\x00", 12));
        EXPECT_FALSE(result.ok);
    }

    TEST(ObScriptLimits, a_script_at_the_variable_cap_compiles_and_one_more_does_not)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        auto script = [](std::size_t variables) {
            std::string text = "ScriptName Many\n";
            for (std::size_t i = 0; i < variables; ++i)
            {
                text += "short v" + std::to_string(i) + "\n";
            }
            text += "Begin GameMode\nEnd\n";
            return text;
        };
        EXPECT_NE(fixture.compileOk("Many", script(ScriptObscript::Limits::kMaxLocals)), 0u);
        const CompileResult over = compileSource(fixture, "Many", script(ScriptObscript::Limits::kMaxLocals + 1));
        EXPECT_FALSE(over.ok);
        EXPECT_NE(firstError(over).find("variables"), std::string::npos) << firstError(over);
    }

    TEST(ObScriptLimits, an_identifier_at_the_length_cap_compiles_and_one_more_does_not)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const std::string ok(ScriptObscript::Limits::kMaxIdentifierLength, 'a');
        EXPECT_NE(fixture.compileOk("Long", "ScriptName Long\nshort " + ok + "\nBegin GameMode\nEnd\n"), 0u);
        const CompileResult over
            = compileSource(fixture, "Long", "ScriptName Long\nshort " + ok + "a\nBegin GameMode\nEnd\n");
        EXPECT_FALSE(over.ok);
    }

    TEST(ObScriptLimits, parentheses_at_the_nesting_cap_compile_and_one_more_does_not)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        auto nested = [](std::size_t depth) {
            return "ScriptName Deep\nshort a\nBegin GameMode\nSet a to " + std::string(depth, '(') + "1"
                + std::string(depth, ')') + "\nEnd\n";
        };
        EXPECT_NE(fixture.compileOk("Deep", nested(ScriptObscript::Limits::kMaxNesting)), 0u);
        const CompileResult over = compileSource(fixture, "Deep", nested(ScriptObscript::Limits::kMaxNesting + 1));
        EXPECT_FALSE(over.ok);
        EXPECT_NE(firstError(over).find("nesting"), std::string::npos) << firstError(over);
    }

    TEST(ObScriptLimits, a_source_over_the_size_cap_is_refused_without_being_read)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        const std::string comment(ScriptObscript::Limits::kMaxSourceBytes + 1, ';');
        const CompileResult over = compileSource(fixture, "Huge", comment);
        EXPECT_FALSE(over.ok);
        EXPECT_NE(firstError(over).find("too large"), std::string::npos) << firstError(over);
    }

    TEST(ObScriptLimits, a_source_over_the_line_cap_is_refused)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        std::string text = "ScriptName Lines\nBegin GameMode\n";
        text += std::string(ScriptObscript::Limits::kMaxLines + 1, '\n');
        text += "End\n";
        const CompileResult over = compileSource(fixture, "Lines", text);
        EXPECT_FALSE(over.ok);
        EXPECT_NE(firstError(over).find("lines"), std::string::npos) << firstError(over);
    }

    TEST(ObScriptLimits, too_many_distinct_unknown_functions_are_refused_in_tolerant_mode)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        // A script of that many lines would hit the line cap first, so the names come in many small scripts.
        std::size_t refused = 0;
        bool firstAccepted = false;
        bool firstDone = false;
        std::string part = "ScriptName Part\nBegin GameMode\n";
        std::size_t inPart = 0;
        for (std::size_t i = 0; i < ScriptObscript::Limits::kMaxUnknownFunctions + 50; ++i)
        {
            part += "Unknown" + std::to_string(i) + " 1\n";
            if (++inPart == 100)
            {
                part += "End\n";
                const bool accepted = compileSource(fixture, "Part", part).ok;
                if (!firstDone)
                {
                    firstAccepted = accepted;
                    firstDone = true;
                }
                if (!accepted)
                {
                    ++refused;
                }
                part = "ScriptName Part\nBegin GameMode\n";
                inPart = 0;
            }
        }
        EXPECT_TRUE(firstAccepted);
        EXPECT_GT(refused, 0u);
    }
    // Review A5: compiling a script again under the same name (a mod reload, an editor) replaces it. With no instance
    // of the old one the slot is reused, so reloading does not use up the 65,536 scripts a runtime can hold.
    TEST(ObScriptRecompile, the_same_name_reuses_its_slot_and_never_exhausts_the_script_cap)
    {
        RuntimeFixture f;
        const std::string text = "ScriptName Again\nshort n\nBegin GameMode\n Set n to n + 1\nEnd\n";
        const ScriptId first = f.compileOk("Again", text);
        ASSERT_NE(first, 0u);
        for (std::size_t i = 0; i < ScriptObscript::Limits::kMaxScripts + 100; ++i)
        {
            ASSERT_EQ(f.compileOk("Again", text), first) << "recompile " << i;
        }
        // Another name still gets a script of its own.
        EXPECT_NE(f.compileOk("Other", "ScriptName Other\nBegin GameMode\nEnd\n"), first);
    }

    TEST(ObScriptRecompile, a_script_that_has_instances_keeps_its_old_code_and_the_new_one_gets_a_new_id)
    {
        RuntimeFixture f;
        const ScriptId first = f.compileOk("Again", "ScriptName Again\nshort n\nBegin GameMode\n Set n to n + 1\nEnd\n");
        const InstanceId instance = f.mRuntime->createInstance(first, 0x10);
        ASSERT_NE(instance, 0u);
        const ScriptId second = f.compileOk(
            "Again", "ScriptName Again\nshort n\nshort m\nBegin GameMode\n Set n to n + 2\nEnd\n");
        ASSERT_NE(second, 0u);
        EXPECT_NE(second, first) << "the instance still depends on the old layout";
        EXPECT_TRUE(f.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(f.mRuntime->getLocal(instance, "n").integer, 1) << "the old code ran";
    }
}
