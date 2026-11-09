// Task 0092, fixes of the fresh review of 2026-11-08 (items 1, 4, 5 and 8).
//
// In plain words: a script that chains typed calls a hundred thousand deep must give a located error and not overflow
// the stack; a script that fails to compile must leave no unknown function names behind; the contract's old calls keep
// their places in the table of virtual functions; and a save restore or a seed file cannot be used to corrupt the
// runtime or fill memory.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <components/script_obscript/functiontable.hpp>
#include <components/script_obscript/obscriptlimits.hpp>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using namespace openeden::script;

    CompileResult compileText(ObScriptContract& runtime, const std::string& name, const std::string& text)
    {
        ScriptSource source;
        source.name = name;
        source.text = text;
        return runtime.compile(source);
    }

    std::string firstError(const CompileResult& result)
    {
        for (const ErrorReport& report : result.reports)
        {
            if (report.severity == Severity::Error)
            {
                return report.format();
            }
        }
        return {};
    }

    bool registerNumeric(ObScriptContract& runtime, const std::string& name, const std::string& signature)
    {
        FunctionDescriptor descriptor;
        descriptor.name = name;
        descriptor.signature = signature;
        ErrorReport error;
        return runtime.registerFunction(
            descriptor, [](const CallArgs&) { return Value::makeInteger(1); }, error);
    }

    // ---- item 1: chained calls

    std::string chain(std::size_t calls)
    {
        std::string text = "ScriptName Chain\nshort a\nBegin GameMode\nSet a to ";
        for (std::size_t i = 0; i < calls; ++i)
        {
            text += "Numeric ";
        }
        text += "1\nEnd\n";
        return text;
    }

    TEST(ObScriptReview, a_hundred_thousand_chained_calls_give_an_error_and_do_not_overflow_the_stack)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(registerNumeric(*fixture.mRuntime, "Numeric", "l"));
        const CompileResult result = compileText(*fixture.mRuntime, "Chain", chain(100000));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(firstError(result).find("nesting"), std::string::npos) << firstError(result);
    }

    TEST(ObScriptReview, a_chain_inside_the_cap_still_compiles)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Strict);
        ASSERT_TRUE(registerNumeric(*fixture.mRuntime, "Numeric", "l"));
        const CompileResult result
            = compileText(*fixture.mRuntime, "Chain", chain(ScriptObscript::Limits::kMaxCallDepth - 4));
        EXPECT_TRUE(result.ok) << firstError(result);
    }

    // ---- item 4: unknown names of a failed script

    std::string manyUnknown(std::size_t count, const std::string& prefix, bool valid)
    {
        std::string text = "ScriptName Many\nBegin GameMode\n";
        for (std::size_t i = 0; i < count; ++i)
        {
            text += prefix + std::to_string(i) + " 1\n";
        }
        if (!valid)
        {
            text += "Set to\n";
        }
        text += "End\n";
        return text;
    }

    TEST(ObScriptReview, a_script_that_fails_leaves_no_unknown_function_names_behind)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        EXPECT_FALSE(compileText(*fixture.mRuntime, "Bad", manyUnknown(50, "Junk", false)).ok);
        // the name of a failed script's unknown call can be registered by the host afterwards
        EXPECT_TRUE(registerNumeric(*fixture.mRuntime, "Junk7", "l"));
        // and a good script still compiles
        const CompileResult good = compileText(*fixture.mRuntime, "Good", manyUnknown(50, "Other", true));
        EXPECT_TRUE(good.ok) << firstError(good);
        std::size_t blocks = 0;
        std::size_t unknown = 0;
        ASSERT_TRUE(ScriptObscript::inspect(*fixture.mRuntime, good.script, blocks, unknown));
        EXPECT_EQ(unknown, 50u) << "only the names of scripts that compiled count";
    }

    TEST(ObScriptReview, many_failing_scripts_cannot_fill_the_table_for_later_scripts)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        for (int round = 0; round < 100; ++round)
        {
            EXPECT_FALSE(
                compileText(*fixture.mRuntime, "Bad", manyUnknown(100, "J" + std::to_string(round) + "x", false)).ok);
        }
        const CompileResult good = compileText(*fixture.mRuntime, "Good", manyUnknown(100, "Fine", true));
        EXPECT_TRUE(good.ok) << firstError(good);
    }

    TEST(ObScriptReview, one_script_cannot_name_more_unknown_functions_than_the_per_script_cap)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        const std::size_t cap = ScriptObscript::Limits::kMaxUnknownFunctionsPerScript;
        const CompileResult over = compileText(*fixture.mRuntime, "Many", manyUnknown(cap + 1, "U", true));
        EXPECT_FALSE(over.ok);
        EXPECT_NE(firstError(over).find("unknown functions"), std::string::npos) << firstError(over);
        const CompileResult at = compileText(*fixture.mRuntime, "Many", manyUnknown(cap, "V", true));
        EXPECT_TRUE(at.ok) << firstError(at);
    }

    // ---- item 5: the old calls keep their places in the table of virtual functions

    // The contract as 1.1 declared it. A consumer built against it calls through these slots.
    class LegacyContract
    {
    public:
        virtual ~LegacyContract() = default;
        virtual bool registerFunction(
            const FunctionDescriptor& descriptor, FunctionHandler handler, ErrorReport& error) noexcept = 0;
        virtual CompileResult compile(const ScriptSource& source) noexcept = 0;
        virtual InstanceId createInstance(ScriptId script, FormId self) noexcept = 0;
        virtual RunResult run(InstanceId instance, std::string_view event, const Budget& budget = {}) noexcept = 0;
        virtual Value getLocal(InstanceId instance, std::string_view name) const noexcept = 0;
        virtual bool setLocal(InstanceId instance, std::string_view name, Value value) noexcept = 0;
        virtual SaveBlob serialise() const noexcept = 0;
        virtual RestoreResult restore(const SaveBlob& blob) noexcept = 0;
    };

    TEST(ObScriptReview, a_consumer_built_against_contract_1_1_still_reaches_the_right_functions)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        ObScriptContract* current = fixture.mRuntime.get();
        LegacyContract* legacy = reinterpret_cast<LegacyContract*>(current);
        ScriptSource source;
        source.name = "Old";
        source.text = "ScriptName Old\nshort x\nBegin GameMode\nSet x to 5\nEnd\n";
        const CompileResult compiled = legacy->compile(source);
        ASSERT_TRUE(compiled.ok) << firstError(compiled);
        const InstanceId instance = legacy->createInstance(compiled.script, sCrate);
        ASSERT_NE(instance, 0u);
        EXPECT_TRUE(legacy->run(instance, "GameMode").ok);
        EXPECT_EQ(legacy->getLocal(instance, "x").integer, 5);
        EXPECT_TRUE(legacy->setLocal(instance, "x", Value::makeInteger(9)));
        const SaveBlob blob = legacy->serialise();
        EXPECT_FALSE(blob.bytes.empty());
        EXPECT_TRUE(legacy->restore(blob).ok);
    }

    // ---- item 8: restore

    TEST(ObScriptReview, a_save_with_a_next_instance_number_of_zero_is_refused)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        SaveBlob blob;
        blob.formatVersion = ScriptObscript::Limits::kSaveFormatVersion;
        blob.bytes.assign(8, 0); // no instances, next id 0
        EXPECT_FALSE(fixture.mRuntime->restore(blob).ok);
        const CompileResult compiled
            = compileText(*fixture.mRuntime, "Fine", "ScriptName Fine\nshort x\nBegin GameMode\nSet x to 1\nEnd\n");
        ASSERT_TRUE(compiled.ok);
        EXPECT_NE(fixture.mRuntime->createInstance(compiled.script, sCrate), 0u);
    }

    TEST(ObScriptReview, a_host_function_cannot_restore_the_runtime_that_is_running_it)
    {
        RuntimeFixture fixture(UnknownFunctionPolicy::Tolerant);
        ObScriptContract* runtime = fixture.mRuntime.get();
        const SaveBlob saved = runtime->serialise();
        bool restoredInside = true;
        FunctionDescriptor descriptor;
        descriptor.name = "RestoreNow";
        ErrorReport error;
        ASSERT_TRUE(runtime->registerFunction(
            descriptor,
            [&](const CallArgs&) {
                restoredInside = runtime->restore(saved).ok;
                return Value::makeInteger(0);
            },
            error))
            << error.format();
        const CompileResult compiled = compileText(*runtime, "Inside",
            "ScriptName Inside\nshort x\nBegin GameMode\nSet x to 1\nRestoreNow\nSet x to x + 1\nEnd\n");
        ASSERT_TRUE(compiled.ok) << firstError(compiled);
        const InstanceId instance = runtime->createInstance(compiled.script, sCrate);
        ASSERT_NE(instance, 0u);
        const RunResult result = runtime->run(instance, "GameMode");
        EXPECT_TRUE(result.ok) << result.error.format();
        EXPECT_FALSE(restoredInside);
        EXPECT_EQ(runtime->getLocal(instance, "x").integer, 2) << "the instance survived the refused restore";
    }

    // ---- item 8: the seed

    TEST(ObScriptReview, a_seed_over_the_byte_cap_is_refused_whole)
    {
        ScriptObscript::FunctionTable table;
        std::vector<ErrorReport> errors;
        const std::string seed(ScriptObscript::TableLimits::kMaxBytes + 1, '#');
        EXPECT_EQ(ScriptObscript::mergeSeed(table, seed, "seed.txt", errors), 0u);
        ASSERT_EQ(errors.size(), 1u);
        EXPECT_NE(errors[0].message.find("too large"), std::string::npos);
    }

    TEST(ObScriptReview, a_seed_of_many_bad_lines_gives_a_bounded_list_of_errors)
    {
        ScriptObscript::FunctionTable table;
        std::vector<ErrorReport> errors;
        std::string seed;
        for (int i = 0; i < 100000; ++i)
        {
            seed += "garbage line\n";
        }
        EXPECT_EQ(ScriptObscript::mergeSeed(table, seed, "seed.txt", errors), 0u);
        EXPECT_LE(errors.size(), ScriptObscript::TableLimits::kMaxSeedErrors + 1);
        EXPECT_GE(errors.size(), 2u);
    }
    // Review A6: entries the seed could not add because the table is full are reported (once, as a warning).
    TEST(ObScriptReview, seed_entries_skipped_because_the_table_is_full_are_reported_once)
    {
        ScriptObscript::FunctionTable table;
        for (std::size_t i = 0; i < ScriptObscript::TableLimits::kMaxEntries; ++i)
        {
            ScriptObscript::TableEntry entry;
            char name[16];
            std::snprintf(name, sizeof(name), "f%07zu", i);
            entry.mName = name;
            entry.mSignature = "/";
            table.mEntries.push_back(std::move(entry));
        }
        std::vector<ErrorReport> errors;
        const std::string seed
            = "function zzone 0 / l 0 0 0 0 seed READ a page\n"
              "function zztwo 0 / l 0 0 0 0 seed READ a page\n";
        EXPECT_EQ(ScriptObscript::mergeSeed(table, seed, "seed.txt", errors), 0u);
        ASSERT_EQ(errors.size(), 1u) << "one summary, not one line per entry";
        EXPECT_EQ(errors[0].severity, openeden::script::Severity::Warning);
        EXPECT_NE(errors[0].message.find("full"), std::string::npos) << errors[0].message;
        EXPECT_NE(errors[0].message.find("2"), std::string::npos) << "the number skipped";
    }
}
