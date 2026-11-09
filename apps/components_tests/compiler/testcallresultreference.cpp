// Task 0092, gap D: a command's result as the reference of the next command, as a parser option.
//
// In plain words: "GetFoo.Bar 1" calls GetFoo and then calls Bar on what it returned. Morrowind's compiler rejects
// the dot after a command; the option ParserOptions::mCallResultReferences accepts it, keeps the result in a hidden
// local named "$ref<depth>", and compiles everything without such a dot exactly as before. Generated scripts only.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/tokenloc.hpp>
#include <components/esm/refid.hpp>
#include <components/interpreter/program.hpp>

namespace
{
    class RefContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override
        {
            return { ' ', false };
        }
        bool isId(const ESM::RefId& name) const override { return name.getRefIdString() == "someref"; }
    };

    class CollectingHandler : public Compiler::ErrorHandler
    {
    public:
        std::vector<std::string> mErrors;

    private:
        void report(const std::string& message, const Compiler::TokenLoc&, Type type) override
        {
            if (type == ErrorMessage)
            {
                mErrors.push_back(message);
            }
        }
        void report(const std::string& message, Type type) override
        {
            if (type == ErrorMessage)
            {
                mErrors.push_back(message);
            }
        }
    };

    struct Result
    {
        bool mOk = false;
        std::vector<std::string> mErrors;
        Interpreter::Program mProgram;
    };

    Result compile(const std::string& body, bool option, std::size_t maxCallDepth = 0)
    {
        CollectingHandler handler;
        RefContext context;
        Compiler::Extensions extensions;
        extensions.registerFunction("getfoo", 'l', "", 33554432, 33554433);
        extensions.registerFunction("bar", 'l', "l", 33554434, 33554435);
        extensions.registerFunction("skipper", 'l', "/zzzz", 33554436, 33554437);
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mDotExplicitReference = true;
        options.mReferenceLocals = true;
        options.mQuietSkippedArguments = true;
        options.mCallResultReferences = option;
        options.mMaxCallDepth = maxCallDepth;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        Result result;
        try
        {
            std::istringstream input("Begin T\nshort a\nlong b\n" + body + "\nEnd\n");
            Compiler::Scanner scanner(handler, input, &extensions);
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            result.mErrors = handler.mErrors.empty() ? std::vector<std::string>{ "thrown" } : handler.mErrors;
            return result;
        }
        result.mOk = handler.isGood();
        result.mErrors = handler.mErrors;
        result.mProgram = parser.getProgram();
        return result;
    }

    TEST(CompilerCallResultReference, off_the_dot_after_a_command_is_an_error_as_in_morrowind)
    {
        // At statement level the rest of the line is skipped with a warning (Morrowind drops extra words).
        EXPECT_TRUE(compile("getfoo.bar 1", false).mOk);
        EXPECT_FALSE(compile("set b to getfoo.bar 1", false).mOk);
    }

    TEST(CompilerCallResultReference, on_a_statement_and_an_expression_compile_to_a_call_then_a_member_call)
    {
        // The behaviour is run in testobscriptcallref.cpp; here the compiled program must differ from the one for
        // "getfoo" alone (it stores the result and calls bar on it) and from a call of bar on nothing.
        const Result plain = compile("getfoo", true);
        const Result bare = compile("bar 1", true);
        const Result statement = compile("getfoo.bar 1", true);
        ASSERT_TRUE(statement.mOk) << (statement.mErrors.empty() ? "" : statement.mErrors[0]);
        EXPECT_GT(statement.mProgram.mInstructions.size(), plain.mProgram.mInstructions.size());
        EXPECT_GT(statement.mProgram.mInstructions.size(), bare.mProgram.mInstructions.size());

        const Result expression = compile("set b to getfoo.bar 1", true);
        ASSERT_TRUE(expression.mOk) << (expression.mErrors.empty() ? "" : expression.mErrors[0]);
        EXPECT_GT(expression.mProgram.mInstructions.size(), statement.mProgram.mInstructions.size());
    }

    TEST(CompilerCallResultReference, on_a_command_with_skipped_arguments_stops_at_the_dot)
    {
        const Result result = compile("skipper.bar 2\nset b to skipper 1 2", true);
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors[0]);
    }

    TEST(CompilerCallResultReference, a_nested_use_compiles_and_is_not_the_same_as_the_flat_one)
    {
        const Result nested = compile("getfoo.bar getfoo.bar 1", true);
        ASSERT_TRUE(nested.mOk) << (nested.mErrors.empty() ? "" : nested.mErrors[0]);
        const Result flat = compile("getfoo.bar 1", true);
        EXPECT_GT(nested.mProgram.mInstructions.size(), flat.mProgram.mInstructions.size());
    }

    TEST(CompilerCallResultReference, a_result_can_be_the_reference_of_a_second_call)
    {
        EXPECT_TRUE(compile("getfoo.getfoo.bar 1", true).mOk);
        EXPECT_TRUE(compile("set b to getfoo.getfoo.bar 1", true).mOk);
        EXPECT_FALSE(compile("set b to getfoo.getfoo.bar 1", false).mOk);
    }

    TEST(CompilerCallResultReference, the_depth_cap_bounds_hostile_nesting_with_the_option_on)
    {
        std::string deep;
        for (int i = 0; i < 70; ++i)
        {
            deep += "getfoo.bar ";
        }
        deep += "1";
        const Result capped = compile(deep, true, 64);
        EXPECT_FALSE(capped.mOk);
        ASSERT_FALSE(capped.mErrors.empty());
        EXPECT_NE(capped.mErrors[0].find("nesting"), std::string::npos) << capped.mErrors[0];

        std::string hostile;
        for (int i = 0; i < 50000; ++i)
        {
            hostile += "getfoo.bar ";
        }
        hostile += "1";
        EXPECT_FALSE(compile(hostile, true, 64).mOk);

        std::string allowed;
        for (int i = 0; i < 63; ++i)
        {
            allowed += "getfoo.bar ";
        }
        allowed += "1";
        EXPECT_TRUE(compile(allowed, true, 64).mOk);
    }

    TEST(CompilerCallResultReference, a_comment_the_end_of_the_text_or_stray_dots_after_a_call_are_not_members)
    {
        EXPECT_TRUE(compile("set b to getfoo ; note", true).mOk);
        EXPECT_TRUE(compile("getfoo ; note", true).mOk);
        EXPECT_TRUE(compile("getfoo.", true).mOk);
        EXPECT_TRUE(compile("getfoo . ", true).mOk);
        EXPECT_TRUE(compile("getfoo ...", true).mOk);
        EXPECT_FALSE(compile("set b to getfoo.", true).mOk);
        EXPECT_FALSE(compile("set b to getfoo.nothing", true).mOk);
        EXPECT_FALSE(compile("if ( getfoo. )\nendif", true).mOk);
    }

    TEST(CompilerCallResultReference, scripts_without_the_dot_compile_to_the_same_bytes_on_and_off)
    {
        const std::vector<std::string> bodies = { "set b to getfoo", "someref.bar 3", "set b to getfoo + 2",
            "if ( getfoo == 2 )\nset a to 1\nendif", "skipper 1 2 3\nset b to bar 4" };
        for (const std::string& body : bodies)
        {
            const Result off = compile(body, false);
            const Result on = compile(body, true);
            ASSERT_TRUE(off.mOk) << body;
            ASSERT_TRUE(on.mOk) << body;
            EXPECT_EQ(off.mProgram.mInstructions, on.mProgram.mInstructions) << body;
            EXPECT_EQ(off.mProgram.mStrings, on.mProgram.mStrings) << body;
        }
    }
}
