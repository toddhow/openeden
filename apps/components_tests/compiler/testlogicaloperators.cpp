// Task 0092: the logical operators "&&" and "||" as a parser option.
//
// In plain words: the scripts of Fallout 3 and New Vegas join conditions with "&&" (and) and "||" (or). Morrowind's
// scripts have no such operators, so the compiler reads them only when the caller asks
// (ParserOptions::mLogicalOperators together with Scanner::setLogicalOperators). With the option off nothing changes:
// the golden recording of Morrowind's compiled programs (testmorrowindprogramgolden.cpp) is the proof. These tests
// check the compiled shape; what the operators compute (precedence, nesting, truth tables) is checked by running
// scripts in script_obscript/testobscriptlogic.cpp.

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
    class LogicContext : public Compiler::Context
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

    Result compile(const std::string& body, bool logical, bool scannerToo = true)
    {
        QuietHandler handler;
        LogicContext context;
        Compiler::ParserOptions options;
        options.mLogicalOperators = logical;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        Result result;
        try
        {
            std::istringstream input("Begin T\nshort a\nshort b\nshort c\nshort r\n" + body + "\nEnd\n");
            Compiler::Scanner scanner(handler, input, nullptr);
            scanner.setLogicalOperators(logical && scannerToo);
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

    constexpr unsigned kAnd = 71;
    constexpr unsigned kOr = 72;

    TEST(CompilerLogicalOperators, the_option_is_off_by_default)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mLogicalOperators);
    }

    TEST(CompilerLogicalOperators, with_the_option_off_the_operators_are_not_read)
    {
        EXPECT_FALSE(compile("if ( a && b )\nendif", false).mOk);
        EXPECT_FALSE(compile("if ( a || b )\nendif", false).mOk);
    }

    TEST(CompilerLogicalOperators, the_scanner_must_be_told_too)
    {
        EXPECT_FALSE(compile("if ( a && b )\nendif", true, false).mOk);
    }

    TEST(CompilerLogicalOperators, and_and_or_compile_to_their_own_opcodes)
    {
        const Result both = compile("if ( a && b || c )\nendif", true);
        ASSERT_TRUE(both.mOk);
        EXPECT_TRUE(contains(both.mProgram, kAnd));
        EXPECT_TRUE(contains(both.mProgram, kOr));
        const Result onlyAnd = compile("if ( a && b )\nendif", true);
        ASSERT_TRUE(onlyAnd.mOk);
        EXPECT_TRUE(contains(onlyAnd.mProgram, kAnd));
        EXPECT_FALSE(contains(onlyAnd.mProgram, kOr));
    }

    TEST(CompilerLogicalOperators, and_binds_tighter_than_or)
    {
        // a || b && c is a || (b && c): the and comes first in the code, the or last.
        const Result loose = compile("if ( a || b && c )\nendif", true);
        const Result grouped = compile("if ( a || ( b && c ) )\nendif", true);
        ASSERT_TRUE(loose.mOk);
        ASSERT_TRUE(grouped.mOk);
        EXPECT_EQ(loose.mProgram.mInstructions, grouped.mProgram.mInstructions);
        const Result other = compile("if ( ( a || b ) && c )\nendif", true);
        ASSERT_TRUE(other.mOk);
        EXPECT_NE(loose.mProgram.mInstructions, other.mProgram.mInstructions);
    }

    TEST(CompilerLogicalOperators, comparisons_bind_tighter_than_both)
    {
        const Result loose = compile("if ( a == 1 && b != 2 || c < 3 )\nendif", true);
        const Result grouped = compile("if ( ( ( a == 1 ) && ( b != 2 ) ) || ( c < 3 ) )\nendif", true);
        ASSERT_TRUE(loose.mOk);
        ASSERT_TRUE(grouped.mOk);
        EXPECT_EQ(loose.mProgram.mInstructions, grouped.mProgram.mInstructions);
    }

    TEST(CompilerLogicalOperators, they_work_in_if_elseif_and_while_without_brackets_too)
    {
        EXPECT_TRUE(compile("if a && b\nelseif b || c\nendif\nwhile a || b\nendwhile", true).mOk);
    }

    TEST(CompilerLogicalOperators, a_float_operand_is_accepted)
    {
        const Result result = compile("float f\nif ( f && a )\nendif", true);
        EXPECT_TRUE(result.mOk);
    }

    TEST(CompilerLogicalOperators, malformed_forms_are_errors)
    {
        EXPECT_FALSE(compile("if ( a && )\nendif", true).mOk);
        EXPECT_FALSE(compile("if ( && a )\nendif", true).mOk);
        EXPECT_FALSE(compile("if ( a || || b )\nendif", true).mOk);
        EXPECT_FALSE(compile("if ( a & b )\nendif", true).mOk);
        EXPECT_FALSE(compile("if ( a | b )\nendif", true).mOk);
        EXPECT_FALSE(compile("if ( a &&& b )\nendif", true).mOk);
    }
}
