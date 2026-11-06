// Task 0092 item 2: functions declared from a table, in the runtime.
//
// In plain words: a table derived from the player's data gives each command a name and a shape but no behaviour. The
// runtime accepts such a declaration, compiles scripts against it with typed arguments (strict mode refuses a missing
// or extra argument), and when the command runs it logs once per name and returns zero of its return type. The host
// can later register a real handler for the same name and shape. Names are invented; the world is the stub of
// stubworld.hpp.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <components/script_obscript/functiontable.hpp>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::ErrorReport;
    using openeden::script::FunctionDescriptor;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    FunctionDescriptor descriptor(
        const std::string& name, const std::string& signature, ValueKind returns = ValueKind::Integer)
    {
        FunctionDescriptor result;
        result.name = name;
        result.signature = signature;
        result.returns = returns;
        return result;
    }

    std::string wrap(const std::string& body)
    {
        return "ScriptName Decl\nshort n\nfloat f\nref r\nBegin GameMode\n" + body + "\nEnd\n";
    }

    TEST(ObScriptDeclare, the_default_contract_does_not_support_declarations)
    {
        class Bare final : public openeden::script::ObScriptContract
        {
        public:
            bool registerFunction(
                const FunctionDescriptor&, openeden::script::FunctionHandler, ErrorReport&) noexcept override
            {
                return false;
            }
            openeden::script::CompileResult compile(const openeden::script::ScriptSource&) noexcept override
            {
                return {};
            }
            InstanceId createInstance(ScriptId, openeden::script::FormId) noexcept override { return 0; }
            openeden::script::RunResult run(
                InstanceId, std::string_view, const openeden::script::Budget&) noexcept override
            {
                return {};
            }
            Value getLocal(InstanceId, std::string_view) const noexcept override { return {}; }
            bool setLocal(InstanceId, std::string_view, Value) noexcept override { return false; }
            openeden::script::SaveBlob serialise() const noexcept override { return {}; }
            openeden::script::RestoreResult restore(const openeden::script::SaveBlob&) noexcept override { return {}; }
        };
        Bare bare;
        ErrorReport error;
        EXPECT_FALSE(bare.declareFunction(descriptor("Wobble", "r/"), error));
        EXPECT_FALSE(error.message.empty());
    }

    TEST(ObScriptDeclare, a_declared_function_compiles_with_typed_arguments_and_does_nothing_when_run)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Frobnicate", "rl/"), error)) << error.format();
        const ScriptId id = fixture.compileOk("Decl", wrap("Frobnicate Crate 5\nFrobnicate Crate 6"));
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        // one notice for the name, not one per call
        int notices = 0;
        for (const std::string& message : fixture.mWorld.mMessages)
        {
            notices += message.find("frobnicate") != std::string::npos ? 1 : 0;
        }
        EXPECT_EQ(notices, 1);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        notices = 0;
        for (const std::string& message : fixture.mWorld.mMessages)
        {
            notices += message.find("frobnicate") != std::string::npos ? 1 : 0;
        }
        EXPECT_EQ(notices, 1);
    }

    TEST(ObScriptDeclare, strict_mode_checks_the_arguments_against_the_declaration)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Frobnicate", "rl/"), error));
        EXPECT_EQ(fixture.compileOk("Missing", wrap("Frobnicate Crate")), 0u);
        EXPECT_EQ(fixture.compileOk("Extra", wrap("Frobnicate Crate 5 6")), 0u);
        EXPECT_NE(fixture.compileOk("Right", wrap("Frobnicate Crate 5")), 0u);
    }

    TEST(ObScriptDeclare, optional_arguments_are_accepted_or_left_out)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Greeting", "l/l"), error));
        EXPECT_NE(fixture.compileOk("One", wrap("Greeting 5")), 0u);
        EXPECT_NE(fixture.compileOk("Two", wrap("Greeting 5 6")), 0u);
        EXPECT_EQ(fixture.compileOk("Three", wrap("Greeting 5 6 7")), 0u);
    }

    TEST(ObScriptDeclare, a_declared_function_takes_part_in_expressions_and_returns_zero_of_its_type)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("GetThingCount", "r/"), error));
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("GetCloseness", "r/", ValueKind::Float), error));
        const ScriptId id = fixture.compileOk("Decl",
            wrap("Set n to GetThingCount Crate + 2\nSet f to GetCloseness Crate + 1.5\n"
                 "If GetThingCount Crate > 0\n Set n to 99\nEndIf"));
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "n").integer, 2); // the arguments were taken off the stack
        EXPECT_FLOAT_EQ(fixture.mRuntime->getLocal(instance, "f").number, 1.5f);
    }

    TEST(ObScriptDeclare, a_declared_function_can_be_called_on_a_reference)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Frobnicate", "rl/"), error));
        const ScriptId id = fixture.compileOk("Decl", wrap("player.Frobnicate Crate 5\nCrate.Frobnicate Crate 5"));
        ASSERT_NE(id, 0u) << (fixture.mLastReports.empty() ? "" : fixture.mLastReports.front());
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        EXPECT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
    }

    TEST(ObScriptDeclare, the_skip_shape_accepts_any_arguments)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Spread", "*"), error)) << error.format();
        EXPECT_NE(fixture.compileOk("None", wrap("Spread")), 0u);
        EXPECT_NE(fixture.compileOk("Many", wrap("Spread Crate 5 6 7")), 0u);
        const ScriptId id = fixture.compileOk("Run", wrap("Spread Crate 5 6 7"));
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        EXPECT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
    }

    TEST(ObScriptDeclare, a_bad_declaration_is_refused_with_a_reason)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        EXPECT_FALSE(fixture.mRuntime->declareFunction(descriptor("not a name", "r/"), error));
        EXPECT_FALSE(error.message.empty());
        EXPECT_FALSE(fixture.mRuntime->declareFunction(descriptor("Wobble", "q/"), error));
        EXPECT_FALSE(fixture.mRuntime->declareFunction(descriptor("Wobble", "r/", ValueKind::String), error));
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Wobble", "r/"), error));
        EXPECT_FALSE(
            fixture.mRuntime->declareFunction(descriptor("wobble", "r/"), error)); // names are case-insensitive
        EXPECT_NE(error.message.find("already"), std::string::npos);
        // a name the host registered with a handler cannot be declared either
        EXPECT_FALSE(fixture.mRuntime->declareFunction(descriptor("AddItem", "rl/"), error));
    }

    TEST(ObScriptDeclare, a_host_can_give_a_declaration_a_handler_when_the_shape_is_the_same)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Wobble", "r/"), error));
        const ScriptId id = fixture.compileOk("Decl", wrap("Set n to Wobble Crate"));
        ASSERT_NE(id, 0u);
        int calls = 0;
        ASSERT_TRUE(fixture.mRuntime->registerFunction(
            descriptor("Wobble", "r/"),
            [&calls](const openeden::script::CallArgs&) {
                ++calls;
                return Value::makeInteger(42);
            },
            error))
            << error.format();
        const InstanceId instance = fixture.mRuntime->createInstance(id, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(calls, 1); // the script compiled before the handler arrived still reaches it
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "n").integer, 42);
    }

    TEST(ObScriptDeclare, a_different_shape_cannot_replace_a_declaration)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Wobble", "r/"), error));
        EXPECT_FALSE(fixture.mRuntime->registerFunction(
            descriptor("Wobble", "rl/"), [](const openeden::script::CallArgs&) { return Value::makeInteger(0); },
            error));
        EXPECT_NE(error.message.find("declaration"), std::string::npos);
        EXPECT_FALSE(fixture.mRuntime->registerFunction(
            descriptor("Wobble", "r/", ValueKind::Float), [](const openeden::script::CallArgs&) { return Value(); },
            error));
    }

    TEST(ObScriptDeclare, a_second_registration_of_a_registered_name_is_still_refused)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ErrorReport error;
        EXPECT_FALSE(fixture.mRuntime->registerFunction(
            descriptor("AddItem", "rl"), [](const openeden::script::CallArgs&) { return Value(); }, error));
        EXPECT_NE(error.message.find("already"), std::string::npos);
    }

    TEST(ObScriptDeclare, tolerant_mode_falls_back_to_skipping_the_arguments_of_declared_functions)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Frobnicate", "rl/"), error));
        // the shape is wrong for this use (a required argument is missing): strict refuses, tolerant accepts
        openeden::script::ScriptSource source;
        source.name = "Decl";
        source.text = wrap("Frobnicate Crate\nSet n to 4");
        const auto result = fixture.mRuntime->compile(source);
        ASSERT_TRUE(result.ok) << (result.reports.empty() ? "" : result.reports.front().format());
        bool noted = false;
        for (const auto& report : result.reports)
        {
            noted = noted
                || (report.severity == openeden::script::Severity::Warning
                    && report.message.find("skipped") != std::string::npos);
        }
        EXPECT_TRUE(noted);
        const InstanceId instance = fixture.mRuntime->createInstance(result.script, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "n").integer, 4);
    }

    TEST(ObScriptDeclare, tolerant_mode_uses_the_declared_shape_when_it_fits)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("GetThingCount", "r/"), error));
        openeden::script::ScriptSource source;
        source.name = "Decl";
        source.text = wrap("Set n to GetThingCount Crate + 2");
        const auto result = fixture.mRuntime->compile(source);
        ASSERT_TRUE(result.ok);
        for (const auto& report : result.reports)
        {
            EXPECT_EQ(report.message.find("skipped"), std::string::npos); // no fallback was needed
        }
        const InstanceId instance = fixture.mRuntime->createInstance(result.script, sPlayer);
        ASSERT_TRUE(fixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(fixture.mRuntime->getLocal(instance, "n").integer, 2);
    }

    TEST(ObScriptDeclare, a_script_that_fails_for_another_reason_still_fails_in_tolerant_mode)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        ErrorReport error;
        ASSERT_TRUE(fixture.mRuntime->declareFunction(descriptor("Frobnicate", "rl/"), error));
        EXPECT_EQ(fixture.compileOk("Bad", wrap("Frobnicate Crate\nSet n to")), 0u);
    }

    // ---- declaring a whole table ------------------------------------------------------------------------------

    ScriptObscript::TableEntry entry(
        const std::string& name, const std::string& signature, std::uint32_t occurrences, std::uint32_t shapeVotes)
    {
        ScriptObscript::TableEntry result;
        result.mName = name;
        result.mOpcode = 0x2001;
        result.mSignature = signature;
        result.mOccurrences = occurrences;
        result.mOpcodeVotes = occurrences;
        result.mShapeVotes = shapeVotes;
        return result;
    }

    TEST(ObScriptDeclare, declareAll_declares_every_entry_and_skips_arguments_of_the_doubtful_ones)
    {
        ScriptObscript::FunctionTable table;
        table.mEntries.push_back(entry("sure", "rl/", 10, 10));
        table.mEntries.push_back(entry("unsure", "rl/", 10, 5));
        table.mEntries.push_back(entry("bare", "*", 4, 0));
        ScriptObscript::TableEntry floating = entry("measure", "r/", 4, 4);
        floating.mReturns = 'f';
        table.mEntries.push_back(floating);
        table.normalise();

        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        std::vector<ErrorReport> problems;
        EXPECT_EQ(ScriptObscript::declareAll(*fixture.mRuntime, table, 0.8, problems), 4u);
        EXPECT_TRUE(problems.empty());

        EXPECT_NE(fixture.compileOk("A", wrap("Sure Crate 5")), 0u);
        EXPECT_EQ(fixture.compileOk("B", wrap("Sure Crate")), 0u); // typed
        EXPECT_NE(fixture.compileOk("C", wrap("Unsure Crate 5 6 7")), 0u); // doubtful: arguments skipped
        EXPECT_NE(fixture.compileOk("D", wrap("Bare")), 0u);
        EXPECT_NE(fixture.compileOk("E", wrap("Set f to Measure Crate")), 0u);
    }

    TEST(ObScriptDeclare, declareAll_reports_a_name_that_cannot_be_registered_and_goes_on)
    {
        ScriptObscript::FunctionTable table;
        table.mEntries.push_back(entry("fine", "r/", 3, 3));
        table.mEntries.push_back(entry("set", "r/", 3, 3)); // a word of the language
        table.mEntries.push_back(entry("addcount", "r/", 3, 3));
        table.normalise();

        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        std::vector<ErrorReport> problems;
        EXPECT_EQ(ScriptObscript::declareAll(*fixture.mRuntime, table, 0.8, problems), 2u);
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0].script, "set");
        EXPECT_NE(fixture.compileOk("A", wrap("AddCount Crate")), 0u);
    }

    TEST(ObScriptDeclare, a_table_made_here_loads_back_and_declares_the_same_functions)
    {
        ScriptObscript::FunctionTable table;
        table.mEntries.push_back(entry("sure", "rl/", 10, 10));
        table.normalise();
        const auto parsed = ScriptObscript::parseTable(ScriptObscript::serialiseTable(table), "t");
        ASSERT_TRUE(parsed.ok);
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        std::vector<ErrorReport> problems;
        EXPECT_EQ(ScriptObscript::declareAll(*fixture.mRuntime, parsed.table, 0.8, problems), 1u);
        EXPECT_NE(fixture.compileOk("A", wrap("Sure Crate 5")), 0u);
    }
}
