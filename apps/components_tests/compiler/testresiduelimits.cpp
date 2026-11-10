// Task 0092, the last compile limits on the New Vegas masters: the "%" operator and a minus sign that follows a
// command whose arguments are skipped.
//
// In plain words: both are parser options that are off by default, so Morrowind's compiler is untouched (the golden
// recording of its compiled programs, testmorrowindprogramgolden.cpp, is the proof). With the first option on, "%" is
// a binary operator that binds as tightly as "*" and "/". With the second on, "Command - x" (a minus followed by a
// name or a bracket) ends the skipped arguments and the minus is a subtraction; "Command - 1" and "Command -1" still
// skip the number, exactly as before, because that has always compiled.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>

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
    class ResidueContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override
        {
            return { ' ', false };
        }
        bool isId(const ESM::RefId&) const override { return false; }
    };

    class QuietHandler : public Compiler::ErrorHandler
    {
        void report(const std::string&, const Compiler::TokenLoc&, Type) override {}
        void report(const std::string&, Type) override {}
    };

    struct Result
    {
        bool mOk = false;
        Interpreter::Program mProgram;
    };

    struct Mode
    {
        bool mModulo = false;
        bool mModuloScanner = true;
        bool mBinaryMinus = false;
    };

    Result compile(const std::string& body, const Mode& mode)
    {
        QuietHandler handler;
        ResidueContext context;
        Compiler::Extensions extensions;
        extensions.registerFunction("skipper", 'l', "/zzzzzzzzzzzz", 0x20000, -1);
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mModuloOperator = mode.mModulo;
        options.mBinaryMinusAfterSkippedArguments = mode.mBinaryMinus;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        Result result;
        try
        {
            std::istringstream input("Begin T\nlong a\nlong b\nlong c\n" + body + "\nEnd\n");
            Compiler::Scanner scanner(handler, input, &extensions);
            scanner.setModuloOperator(mode.mModulo && mode.mModuloScanner);
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            return result;
        }
        result.mOk = handler.isGood();
        result.mProgram = parser.getProgram();
        return result;
    }

    bool contains(const Interpreter::Program& program, unsigned segment5Code)
    {
        for (const auto code : program.mInstructions)
        {
            if (code == (0xc8000000u | segment5Code))
            {
                return true;
            }
        }
        return false;
    }

    constexpr unsigned kMod = 74;

    TEST(CompilerResidueLimits, both_options_are_off_by_default)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mModuloOperator);
        EXPECT_FALSE(options.mBinaryMinusAfterSkippedArguments);
    }

    TEST(CompilerResidueLimits, with_modulo_off_the_percent_sign_is_not_read)
    {
        EXPECT_FALSE(compile("Set a to b % c", Mode{}).mOk);
    }

    TEST(CompilerResidueLimits, the_scanner_must_be_told_too)
    {
        Mode mode;
        mode.mModulo = true;
        mode.mModuloScanner = false;
        EXPECT_FALSE(compile("Set a to b % c", mode).mOk);
    }

    TEST(CompilerResidueLimits, modulo_compiles_to_its_own_opcode)
    {
        Mode mode;
        mode.mModulo = true;
        const Result result = compile("Set a to b % c", mode);
        ASSERT_TRUE(result.mOk);
        EXPECT_TRUE(contains(result.mProgram, kMod));
    }

    TEST(CompilerResidueLimits, modulo_works_in_a_condition_with_logical_operands_around_it)
    {
        Mode mode;
        mode.mModulo = true;
        const Result result = compile("if ( a % 2 == 1 )\n Set b to 1\nendif", mode);
        ASSERT_TRUE(result.mOk);
        EXPECT_TRUE(contains(result.mProgram, kMod));
    }

    TEST(CompilerResidueLimits, a_percent_sign_without_a_right_hand_side_is_an_error)
    {
        Mode mode;
        mode.mModulo = true;
        EXPECT_FALSE(compile("Set a to b %", mode).mOk);
        EXPECT_FALSE(compile("Set a to % b", mode).mOk);
    }

    TEST(CompilerResidueLimits, a_minus_and_a_name_after_a_skipping_command_is_an_error_when_off)
    {
        EXPECT_FALSE(compile("Set a to skipper - b", Mode{}).mOk);
    }

    TEST(CompilerResidueLimits, a_minus_and_a_name_after_a_skipping_command_is_a_subtraction_when_on)
    {
        Mode mode;
        mode.mBinaryMinus = true;
        const Result result = compile("Set a to skipper - b", mode);
        ASSERT_TRUE(result.mOk);
        // the program calls skipper, fetches b and subtracts
        Mode plain;
        const Result noArguments = compile("Set a to skipper", plain);
        ASSERT_TRUE(noArguments.mOk);
        EXPECT_GT(result.mProgram.mInstructions.size(), noArguments.mProgram.mInstructions.size());
    }

    TEST(CompilerResidueLimits, a_minus_and_a_bracket_after_a_skipping_command_is_a_subtraction_when_on)
    {
        Mode mode;
        mode.mBinaryMinus = true;
        EXPECT_TRUE(compile("Set a to skipper - ( b + c )", mode).mOk);
    }

    TEST(CompilerResidueLimits, a_minus_and_a_number_still_skips_the_number)
    {
        Mode on;
        on.mBinaryMinus = true;
        for (const char* body : { "Set a to skipper - 1", "Set a to skipper -1", "Set a to skipper - 1.5",
                 "Set a to skipper 3 -2", "skipper - 4" })
        {
            const Result off = compile(body, Mode{});
            const Result with = compile(body, on);
            EXPECT_EQ(off.mOk, with.mOk) << body;
            EXPECT_EQ(off.mProgram.mInstructions, with.mProgram.mInstructions) << body;
            EXPECT_EQ(off.mProgram.mIntegers, with.mProgram.mIntegers) << body;
        }
    }

    TEST(CompilerResidueLimits, a_script_without_such_a_minus_compiles_the_same_with_the_options_on)
    {
        Mode on;
        on.mBinaryMinus = true;
        on.mModulo = true;
        for (const char* body : { "Set a to b - c", "Set a to b * ( c - 1 )", "Set a to skipper", "Set a to -b",
                 "skipper 1 2 3\nSet b to a / 2" })
        {
            const Result off = compile(body, Mode{});
            const Result with = compile(body, on);
            EXPECT_EQ(off.mOk, with.mOk) << body;
            EXPECT_EQ(off.mProgram.mInstructions, with.mProgram.mInstructions) << body;
        }
    }
}
