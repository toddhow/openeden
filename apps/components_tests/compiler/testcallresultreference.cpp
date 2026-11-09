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

    Result compile(const std::string& body, bool option)
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

    bool hasString(const Interpreter::Program& program, const std::string& text)
    {
        for (const std::string& literal : program.mStrings)
        {
            if (literal == text)
            {
                return true;
            }
        }
        return false;
    }

    TEST(CompilerCallResultReference, off_the_dot_after_a_command_is_an_error_as_in_morrowind)
    {
        // At statement level the rest of the line is skipped with a warning (Morrowind drops extra words).
        EXPECT_FALSE(hasString(compile("getfoo.bar 1", false).mProgram, "$ref0"));
        EXPECT_FALSE(compile("set b to getfoo.bar 1", false).mOk);
    }

    TEST(CompilerCallResultReference, on_a_statement_and_an_expression_compile_with_a_hidden_local)
    {
        const Result statement = compile("getfoo.bar 1", true);
        ASSERT_TRUE(statement.mOk) << (statement.mErrors.empty() ? "" : statement.mErrors[0]);
        EXPECT_TRUE(hasString(statement.mProgram, "$ref0"));

        const Result expression = compile("set b to getfoo.bar 1", true);
        ASSERT_TRUE(expression.mOk) << (expression.mErrors.empty() ? "" : expression.mErrors[0]);
        EXPECT_TRUE(hasString(expression.mProgram, "$ref0"));
    }

    TEST(CompilerCallResultReference, on_a_command_with_skipped_arguments_stops_at_the_dot)
    {
        const Result result = compile("skipper.bar 2\nset b to skipper 1 2", true);
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors[0]);
    }

    TEST(CompilerCallResultReference, a_nested_use_gets_the_next_depth)
    {
        const Result result = compile("getfoo.bar getfoo.bar 1", true);
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors[0]);
        EXPECT_TRUE(hasString(result.mProgram, "$ref0"));
        EXPECT_TRUE(hasString(result.mProgram, "$ref1"));
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
