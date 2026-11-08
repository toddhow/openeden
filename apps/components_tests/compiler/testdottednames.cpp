// Task 0092 item 3: names that the scripts of Fallout 3 and New Vegas write with a dot, as parser options.
//
// In plain words, three habits of those scripts that Morrowind's compiler rejects:
//  - a member name may begin with digits: "Set SomeRef.2ndStage to 1" (the scanner reads ".2" as a number);
//  - a command whose arguments we do not know (declared without a shape, the 'z' kind) may be given a dotted name,
//    a command name or a keyword-looking member as an argument: "Set a to Skipper SomeRef.stage";
//  - "Set SomeRef.name to 1" where the variable is not known is not a reason to refuse the whole script.
// Each is an option that is off by default, so Morrowind compiles exactly as before (the golden recording of
// testmorrowindprogramgolden.cpp is the proof for the whole compiler). Generated scripts only.

#include <gtest/gtest.h>

#include <set>
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
    class DotContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const override
        {
            static const std::set<std::string> members = { "stage", "2ndstage", "7", "end", "skipper" };
            if (id.getRefIdString() == "someref" && members.contains(name))
            {
                return { 'l', false };
            }
            return { ' ', false };
        }
        bool isId(const ESM::RefId& name) const override
        {
            const std::string text = name.getRefIdString();
            return text == "someref" || text == "otherref";
        }
    };

    class CollectingHandler : public Compiler::ErrorHandler
    {
    public:
        std::vector<std::string> mMessages;
        std::vector<std::string> mErrors;

    private:
        void report(const std::string& message, const Compiler::TokenLoc&, Type type) override
        {
            mMessages.push_back(message);
            if (type == ErrorMessage)
            {
                mErrors.push_back(message);
            }
        }
        void report(const std::string& message, Type type) override
        {
            mMessages.push_back(message);
            if (type == ErrorMessage)
            {
                mErrors.push_back(message);
            }
        }
    };

    struct Flags
    {
        bool mDigits = false;
        bool mDotted = false;
        bool mUnknownMembers = false;
    };

    struct Result
    {
        bool mOk = false;
        std::vector<std::string> mMessages;
        std::vector<std::string> mErrors;
        Interpreter::Program mProgram;

        bool hasWarning(const std::string& text) const
        {
            for (const std::string& message : mMessages)
            {
                if (message == text)
                {
                    return true;
                }
            }
            return false;
        }
    };

    // "skipper" takes up to twelve arguments and throws them away; "otherfn" is a second command (a keyword).
    Result compile(const std::string& body, const Flags& flags)
    {
        CollectingHandler handler;
        DotContext context;
        Compiler::Extensions extensions;
        extensions.registerFunction("skipper", 'l', "/zzzzzzzzzzzz", 0x20000, 0x20001);
        extensions.registerFunction("otherfn", 'l', "/zzzzzzzzzzzz", 0x20002, 0x20003);
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mDotExplicitReference = true;
        options.mReferenceLocals = true;
        options.mQuietSkippedArguments = true;
        options.mLogicalOperators = true;
        options.mDigitLeadingMembers = flags.mDigits;
        options.mDottedSkippedArguments = flags.mDotted;
        options.mUnknownMembersAreWarnings = flags.mUnknownMembers;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        Result result;
        try
        {
            std::istringstream input("Begin T\nshort a\nlong b\nfloat f\n" + body + "\nEnd\n");
            Compiler::Scanner scanner(handler, input, &extensions);
            scanner.setLogicalOperators(true);
            scanner.setDigitLeadingMembers(flags.mDigits);
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            result.mMessages = handler.mMessages;
            result.mErrors = handler.mErrors.empty() ? std::vector<std::string>{ "thrown" } : handler.mErrors;
            return result;
        }
        result.mOk = handler.isGood();
        result.mMessages = handler.mMessages;
        result.mErrors = handler.mErrors;
        result.mProgram = parser.getProgram();
        return result;
    }

    Flags digits()
    {
        Flags flags;
        flags.mDigits = true;
        return flags;
    }

    Flags dotted()
    {
        Flags flags;
        flags.mDotted = true;
        return flags;
    }

    Flags unknownMembers()
    {
        Flags flags;
        flags.mUnknownMembers = true;
        return flags;
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

    TEST(CompilerDottedNames, the_options_are_off_by_default)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mDigitLeadingMembers);
        EXPECT_FALSE(options.mDottedSkippedArguments);
        EXPECT_FALSE(options.mUnknownMembersAreWarnings);
    }

    // ---- members that begin with digits --------------------------------------------------------------------

    TEST(CompilerDottedNames, a_member_that_begins_with_digits_is_refused_without_the_option)
    {
        EXPECT_FALSE(compile("set someref.2ndstage to 1", Flags()).mOk);
        EXPECT_FALSE(compile("if ( someref.2ndstage == 1 )\nendif", Flags()).mOk);
    }

    TEST(CompilerDottedNames, a_member_that_begins_with_digits_can_be_assigned)
    {
        const Result result = compile("set someref.2ndStage to 1", digits());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_TRUE(hasString(result.mProgram, "2ndstage"));
    }

    TEST(CompilerDottedNames, a_member_that_begins_with_digits_can_be_read)
    {
        const Result inSet = compile("set a to someref.2ndstage", digits());
        ASSERT_TRUE(inSet.mOk) << (inSet.mErrors.empty() ? "" : inSet.mErrors.front());
        const Result inIf = compile("if ( someref.2ndstage == 1 ) && ( a )\n set a to 2\nendif", digits());
        ASSERT_TRUE(inIf.mOk) << (inIf.mErrors.empty() ? "" : inIf.mErrors.front());
    }

    TEST(CompilerDottedNames, a_leading_dot_and_digits_without_letters_is_still_a_float)
    {
        // No letter follows the digits, so the dot belongs to a float, as before.
        const Result result = compile("set f to .5", digits());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
    }

    TEST(CompilerDottedNames, floating_point_literals_read_as_before_with_the_option_on)
    {
        for (const char* line :
            { "set f to 1.5", "set f to .25", "set f to 3.0 + .5", "set f to 0.5 * 2.0", "set a to 2 - 1" })
        {
            SCOPED_TRACE(line);
            const Result off = compile(line, Flags());
            const Result on = compile(line, digits());
            ASSERT_TRUE(off.mOk);
            ASSERT_TRUE(on.mOk);
            EXPECT_EQ(off.mProgram.mInstructions, on.mProgram.mInstructions);
        }
    }

    TEST(CompilerDottedNames, an_ordinary_member_is_unchanged_by_the_option)
    {
        const Result off = compile("set someref.stage to 1\nset a to someref.stage", Flags());
        const Result on = compile("set someref.stage to 1\nset a to someref.stage", digits());
        ASSERT_TRUE(off.mOk);
        ASSERT_TRUE(on.mOk);
        EXPECT_EQ(off.mProgram.mInstructions, on.mProgram.mInstructions);
        EXPECT_EQ(off.mProgram.mStrings, on.mProgram.mStrings);
    }

    // ---- dotted names as skipped arguments -----------------------------------------------------------------

    TEST(CompilerDottedNames, a_dotted_skipped_argument_is_refused_without_the_option)
    {
        EXPECT_FALSE(compile("set a to skipper someref.stage", Flags()).mOk);
        EXPECT_FALSE(compile("if ( skipper someref.stage > 1 )\nendif", Flags()).mOk);
    }

    TEST(CompilerDottedNames, a_dotted_name_can_be_a_skipped_argument_in_a_set)
    {
        const Result result = compile("set a to skipper someref.stage", dotted());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_TRUE(result.mMessages.empty());
    }

    TEST(CompilerDottedNames, a_dotted_name_can_be_a_skipped_argument_in_a_condition)
    {
        const Result result = compile("if ( skipper someref.stage > 1 )\n set a to 1\nendif", dotted());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
    }

    TEST(CompilerDottedNames, a_dotted_argument_followed_by_more_arguments_and_an_operator)
    {
        const Result result = compile("set a to skipper someref.stage 2 otherref.stage + 1", dotted());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        const Result nested
            = compile("if ( skipper someref.stage 1 ) && ( skipper otherref.stage ) || ( a )\nendif", dotted());
        ASSERT_TRUE(nested.mOk) << (nested.mErrors.empty() ? "" : nested.mErrors.front());
    }

    TEST(CompilerDottedNames, a_dotted_argument_may_begin_with_digits_when_both_options_are_on)
    {
        Flags both;
        both.mDigits = true;
        both.mDotted = true;
        const Result result = compile("set a to skipper someref.2ndstage", both);
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
    }

    TEST(CompilerDottedNames, a_member_spelled_like_a_keyword_is_a_skipped_argument)
    {
        // "end" is the language's own word, "otherfn" is another command; after a dot both are just names.
        const Result base = compile("set a to skipper someref.end", dotted());
        ASSERT_TRUE(base.mOk) << (base.mErrors.empty() ? "" : base.mErrors.front());
        const Result command = compile("set a to skipper someref.otherfn", dotted());
        ASSERT_TRUE(command.mOk) << (command.mErrors.empty() ? "" : command.mErrors.front());
    }

    TEST(CompilerDottedNames, another_command_can_be_a_skipped_argument)
    {
        const Result result = compile("set a to skipper otherfn", dotted());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_FALSE(compile("set a to skipper otherfn", Flags()).mOk);
    }

    TEST(CompilerDottedNames, plain_skipped_arguments_read_as_before_with_the_option_on)
    {
        for (const char* line : { "set a to skipper 1 2 3", "set a to skipper someref otherref",
                 "skipper 1\nset a to 1", "set a to skipper", "set a to ( skipper 1 ) + 2",
                 "if ( skipper 1 2 == 3 )\nendif", "set a to skipper -1 -2", "skipper someref.stage 1 2" })
        {
            SCOPED_TRACE(line);
            const Result off = compile(line, Flags());
            const Result on = compile(line, dotted());
            EXPECT_EQ(off.mOk, on.mOk);
            if (off.mOk && on.mOk)
            {
                EXPECT_EQ(off.mProgram.mInstructions, on.mProgram.mInstructions);
            }
        }
    }

    TEST(CompilerDottedNames, a_dot_at_the_end_of_the_line_is_an_error_not_a_hang)
    {
        EXPECT_FALSE(compile("set a to skipper someref.", dotted()).mOk);
        EXPECT_FALSE(compile("set a to skipper someref.\nset b to 1", dotted()).mOk);
    }

    TEST(CompilerDottedNames, the_end_of_the_file_after_a_name_argument_is_not_an_exception)
    {
        CollectingHandler handler;
        DotContext context;
        Compiler::Extensions extensions;
        extensions.registerFunction("skipper", 'l', "/zzzzzzzzzzzz", 0x20000, 0x20001);
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mDottedSkippedArguments = true;
        options.mQuietSkippedArguments = true;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        std::istringstream input("Begin T\nshort a\nset a to skipper someref");
        Compiler::Scanner scanner(handler, input, &extensions);
        try
        {
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
        }
        catch (const std::exception&)
        {
            FAIL() << "an exception other than the compiler's own escaped";
        }
        SUCCEED();
    }

    // ---- members the context does not know ----------------------------------------------------------------

    TEST(CompilerDottedNames, an_unknown_member_is_an_error_without_the_option)
    {
        const Result result = compile("set someref.nothing to 1", Flags());
        EXPECT_FALSE(result.mOk);
    }

    TEST(CompilerDottedNames, an_unknown_member_is_a_warning_with_the_option_and_the_line_is_skipped)
    {
        const Result result = compile("set someref.nothing to 1 + b\nset a to 2", unknownMembers());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_TRUE(result.hasWarning("Unknown variable"));
        // The next line still compiled: the literal of its assignment is there and the skipped one's is not.
        EXPECT_FALSE(hasString(result.mProgram, "nothing"));
    }

    TEST(CompilerDottedNames, an_unknown_member_of_another_reference_is_also_a_warning)
    {
        const Result result = compile("set otherref.stage to 1\nset a to 2", unknownMembers());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_TRUE(result.hasWarning("Unknown variable"));
    }

    TEST(CompilerDottedNames, an_unknown_member_spelled_like_a_command_is_a_warning_too)
    {
        const Result result = compile("set otherref.skipper to 1\nset a to 2", unknownMembers());
        ASSERT_TRUE(result.mOk) << (result.mErrors.empty() ? "" : result.mErrors.front());
        EXPECT_TRUE(result.hasWarning("Unknown variable"));
        EXPECT_FALSE(compile("set otherref.skipper to 1", Flags()).mOk);
    }

    TEST(CompilerDottedNames, a_known_member_is_unchanged_by_the_option)
    {
        const Result off = compile("set someref.stage to 1", Flags());
        const Result on = compile("set someref.stage to 1", unknownMembers());
        ASSERT_TRUE(off.mOk);
        ASSERT_TRUE(on.mOk);
        EXPECT_EQ(off.mProgram.mInstructions, on.mProgram.mInstructions);
        EXPECT_FALSE(on.hasWarning("Unknown variable"));
    }
}
