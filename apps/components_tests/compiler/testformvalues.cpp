// Task 0092 item 1: a form or a reference used as a VALUE, as a parser option.
//
// In plain words: in the scripts of Fallout 3 and New Vegas a name that stands for a form can be used like a number:
// "Set target to SomeRef", "If ( GetActionRef == Player )", "If ( target == other )". Morrowind's scripts cannot do
// this (a name that is an id may only be followed by "->" or "."), so the compiler reads it only when the caller asks
// (ParserOptions::mFormValues). With the option off nothing changes: the golden recording of Morrowind's compiled
// programs (testmorrowindprogramgolden.cpp) is the proof. These tests check the compiled shape and the rules for
// mixing forms with numbers; running the scripts is checked in script_obscript/testobscriptforms.cpp.
//
// The rules: a form value is an integer (the form id) that is fetched by name at run time (segment 5 opcode 73); it
// may be compared with == and != (a comparison with < > <= >= is a warning); it may not be used in arithmetic (an
// error); it may be stored in any local variable or member; a name that is not a local, a global or a form is a
// warning (an error where warnings are errors) and counts as zero.

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
#include <components/misc/strings/algorithm.hpp>

namespace
{
    class FormContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string& name) const override { return name == "someglobal" ? 'l' : ' '; }
        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const override
        {
            if (Misc::StringUtils::ciEqual(id.getRefIdString(), "someref") && name == "stage")
            {
                return { 'l', false };
            }
            return { ' ', false };
        }
        bool isId(const ESM::RefId& name) const override
        {
            // The id keeps the spelling that was interned first in the process (Morrowind.esm's "Player" if
            // ContentFileTest ran before), so the names are compared without regard to case.
            const std::string text = name.getRefIdString();
            for (const char* known : { "someref", "otherref", "player", "target" })
                if (Misc::StringUtils::ciEqual(text, known))
                    return true;
            return false;
        }
    };

    class CountingHandler : public Compiler::ErrorHandler
    {
    public:
        std::vector<std::string> mMessages;

    private:
        void report(const std::string& message, const Compiler::TokenLoc&, Type) override
        {
            mMessages.push_back(message);
        }
        void report(const std::string& message, Type) override { mMessages.push_back(message); }
    };

    struct Result
    {
        bool mOk = false;
        int mWarnings = 0;
        int mErrors = 0;
        std::vector<std::string> mMessages;
        Interpreter::Program mProgram;
    };

    Result compile(const std::string& body, bool formValues, int warningsMode = 1)
    {
        CountingHandler handler;
        handler.setWarningsMode(warningsMode);
        FormContext context;
        Compiler::ParserOptions options;
        options.mFormValues = formValues;
        options.mReferenceLocals = true;
        options.mDotExplicitReference = true;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        Result result;
        try
        {
            std::istringstream input("Begin T\nshort a\nlong b\nfloat f\nshort target\n" + body + "\nEnd\n");
            Compiler::Scanner scanner(handler, input, nullptr);
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            result.mErrors = handler.countErrors() == 0 ? 1 : handler.countErrors();
            result.mMessages = handler.mMessages;
            return result;
        }
        result.mOk = handler.isGood();
        result.mWarnings = handler.countWarnings();
        result.mErrors = handler.countErrors();
        result.mMessages = handler.mMessages;
        result.mProgram = parser.getProgram();
        return result;
    }

    constexpr unsigned kFetchForm = 73;

    bool containsFetchForm(const Interpreter::Program& program)
    {
        for (const auto code : program.mInstructions)
        {
            if (code == (0xc8000000u | kFetchForm))
            {
                return true;
            }
        }
        return false;
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

    TEST(CompilerFormValues, the_option_is_off_by_default)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mFormValues);
    }

    TEST(CompilerFormValues, with_the_option_off_a_form_is_not_a_value)
    {
        EXPECT_FALSE(compile("set a to someref", false).mOk);
        EXPECT_FALSE(compile("if ( a == someref )\nendif", false).mOk);
        EXPECT_FALSE(compile("set a to player", false).mOk);
    }

    TEST(CompilerFormValues, a_form_can_be_assigned)
    {
        const Result result = compile("set a to someref", true);
        ASSERT_TRUE(result.mOk);
        EXPECT_EQ(result.mWarnings, 0);
        EXPECT_TRUE(containsFetchForm(result.mProgram));
        EXPECT_TRUE(hasString(result.mProgram, "someref"));
    }

    TEST(CompilerFormValues, the_player_is_a_form_like_any_other)
    {
        const Result result = compile("set b to player", true);
        ASSERT_TRUE(result.mOk);
        EXPECT_TRUE(hasString(result.mProgram, "player"));
    }

    TEST(CompilerFormValues, a_form_can_be_compared_with_equal_and_not_equal)
    {
        const Result equal = compile("if ( b == someref )\nendif", true);
        ASSERT_TRUE(equal.mOk);
        EXPECT_EQ(equal.mWarnings, 0);
        EXPECT_TRUE(containsFetchForm(equal.mProgram));
        const Result notEqual = compile("if ( someref != otherref )\nendif", true);
        ASSERT_TRUE(notEqual.mOk);
        EXPECT_EQ(notEqual.mWarnings, 0);
        const Result left = compile("if ( player == b )\nendif", true);
        ASSERT_TRUE(left.mOk);
    }

    TEST(CompilerFormValues, a_form_can_stand_alone_in_a_condition_and_in_a_logical_expression)
    {
        ASSERT_TRUE(compile("if ( someref )\nendif", true).mOk);
    }

    TEST(CompilerFormValues, ordering_forms_is_a_warning_not_an_error)
    {
        const Result result = compile("if ( b < someref )\nendif", true);
        ASSERT_TRUE(result.mOk);
        EXPECT_EQ(result.mWarnings, 1);
    }

    TEST(CompilerFormValues, arithmetic_on_a_form_is_an_error)
    {
        for (const char* line : { "set a to someref + 1", "set a to 1 - someref", "set a to someref * 2",
                 "set a to 2 / someref", "set a to - someref" })
        {
            SCOPED_TRACE(line);
            const Result result = compile(line, true);
            EXPECT_FALSE(result.mOk);
            EXPECT_GE(result.mErrors, 1);
        }
    }

    TEST(CompilerFormValues, a_form_can_be_stored_in_any_kind_of_variable)
    {
        EXPECT_TRUE(compile("set a to someref", true).mOk);
        EXPECT_TRUE(compile("set b to someref", true).mOk);
        EXPECT_TRUE(compile("set f to someref", true).mOk);
        EXPECT_TRUE(compile("set someref.stage to otherref", true).mOk);
    }

    TEST(CompilerFormValues, a_local_that_names_a_form_is_a_form_value_too)
    {
        // "target" is a local and Context::isId accepts it (it holds a reference): not usable in arithmetic.
        EXPECT_TRUE(compile("if ( target == someref )\nendif", true).mOk);
        EXPECT_TRUE(compile("set a to target", true).mOk);
        EXPECT_FALSE(compile("set a to target + 1", true).mOk);
    }

    TEST(CompilerFormValues, explicit_references_are_unchanged)
    {
        const Result member = compile("set a to someref.stage", true);
        ASSERT_TRUE(member.mOk);
        EXPECT_FALSE(containsFetchForm(member.mProgram));
        const Result global = compile("set a to someglobal", true);
        ASSERT_TRUE(global.mOk);
        EXPECT_FALSE(containsFetchForm(global.mProgram));
    }

    TEST(CompilerFormValues, an_unknown_name_is_a_located_warning_and_an_error_where_warnings_are_errors)
    {
        const Result tolerant = compile("set a to nobodyhere", true);
        ASSERT_TRUE(tolerant.mOk);
        EXPECT_EQ(tolerant.mWarnings, 1);
        ASSERT_FALSE(tolerant.mMessages.empty());
        EXPECT_NE(tolerant.mMessages.back().find("nobodyhere"), std::string::npos);

        const Result strict = compile("set a to nobodyhere", true, 2);
        EXPECT_FALSE(strict.mOk);
        EXPECT_GE(strict.mErrors, 1);
    }
}
