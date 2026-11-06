// Task 0092 stage b: the compiler's parser options.
//
// In plain words: Morrowind scripts call a command on another object with an arrow ("player->AddItem ..."). The scripts
// of Oblivion, Fallout 3 and New Vegas use a dot ("player.AddItem ..."). The dot already means "a variable of that
// script" in Morrowind scripts, so it can only be read as a command call when the caller asks for it. The option is off
// by default, and with it off the compiler reads the dot exactly as before (the golden recording in
// testmorrowindprogramgolden.cpp covers that). These tests check the option itself.

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
    constexpr int sCodeDoThing = 0x20001; // segment 3
    constexpr int sCodeGetThing = 0x20003;

    class OptionsContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId&) const override
        {
            if (name == "stage")
            {
                return { 'l', true };
            }
            return { ' ', false };
        }
        bool isId(const ESM::RefId& name) const override
        {
            return name == ESM::RefId::stringRefId("player") || name == ESM::RefId::stringRefId("myquest");
        }
    };

    class QuietHandler : public Compiler::ErrorHandler
    {
        void report(const std::string&, const Compiler::TokenLoc&, Type) override {}
        void report(const std::string&, Type) override {}
    };

    struct Compiled
    {
        bool mOk = false;
        Interpreter::Program mProgram;
    };

    Compiled compile(const std::string& source, bool dotOption)
    {
        Compiler::Extensions extensions;
        extensions.registerInstruction("dothing", "l/l", sCodeDoThing, sCodeDoThing + 1);
        extensions.registerFunction("getthing", 'l', "/l", sCodeGetThing, sCodeGetThing + 1);

        QuietHandler handler;
        OptionsContext context;
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mDotExplicitReference = dotOption;
        context.setOptions(options);

        Compiler::FileParser parser(handler, context);
        Compiled result;
        try
        {
            std::istringstream input(source);
            Compiler::Scanner scanner(handler, input, &extensions);
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

    TEST(CompilerParserOptions, the_default_is_morrowinds_behaviour)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mDotExplicitReference);
        OptionsContext context;
        EXPECT_FALSE(context.getOptions().mDotExplicitReference);
    }

    TEST(CompilerParserOptions, a_dot_call_of_an_instruction_equals_the_arrow_call_with_the_option_on)
    {
        const Compiled dot = compile("Begin T\nplayer.DoThing 3\nEnd\n", true);
        const Compiled arrow = compile("Begin T\nplayer->DoThing 3\nEnd\n", false);
        ASSERT_TRUE(dot.mOk);
        ASSERT_TRUE(arrow.mOk);
        EXPECT_EQ(dot.mProgram.mInstructions, arrow.mProgram.mInstructions);
        EXPECT_EQ(dot.mProgram.mIntegers, arrow.mProgram.mIntegers);
        EXPECT_EQ(dot.mProgram.mStrings, arrow.mProgram.mStrings);
    }

    TEST(CompilerParserOptions, a_dot_call_of_a_function_in_an_expression_equals_the_arrow_call_with_the_option_on)
    {
        const std::string head = "Begin T\nshort a\nSet a to ";
        const Compiled dot = compile(head + "player.GetThing 2\nEnd\n", true);
        const Compiled arrow = compile(head + "player->GetThing 2\nEnd\n", false);
        ASSERT_TRUE(dot.mOk);
        ASSERT_TRUE(arrow.mOk);
        EXPECT_EQ(dot.mProgram.mInstructions, arrow.mProgram.mInstructions);
        EXPECT_EQ(dot.mProgram.mIntegers, arrow.mProgram.mIntegers);
        EXPECT_EQ(dot.mProgram.mStrings, arrow.mProgram.mStrings);
    }

    TEST(CompilerParserOptions, a_dot_call_in_a_condition_compiles_with_the_option_on)
    {
        const Compiled dot = compile("Begin T\nif ( player.GetThing == 1 )\nendif\nEnd\n", true);
        const Compiled arrow = compile("Begin T\nif ( player->GetThing == 1 )\nendif\nEnd\n", false);
        ASSERT_TRUE(dot.mOk);
        EXPECT_EQ(dot.mProgram.mInstructions, arrow.mProgram.mInstructions);
    }

    TEST(CompilerParserOptions, with_the_option_off_the_dot_call_is_not_read_as_a_call)
    {
        EXPECT_FALSE(compile("Begin T\nplayer.DoThing 3\nEnd\n", false).mOk);
    }

    TEST(CompilerParserOptions, a_member_variable_still_reads_as_a_variable_with_the_option_on)
    {
        const std::string text = "Begin T\nshort a\nSet a to myquest.stage\nSet myquest.stage to a + 1\nEnd\n";
        const Compiled on = compile(text, true);
        const Compiled off = compile(text, false);
        ASSERT_TRUE(on.mOk);
        ASSERT_TRUE(off.mOk);
        EXPECT_EQ(on.mProgram.mInstructions, off.mProgram.mInstructions);
        EXPECT_EQ(on.mProgram.mStrings, off.mProgram.mStrings);
    }

    TEST(CompilerParserOptions, the_arrow_call_is_unchanged_by_the_option)
    {
        const std::string text = "Begin T\nplayer->DoThing 3\nEnd\n";
        const Compiled on = compile(text, true);
        const Compiled off = compile(text, false);
        ASSERT_TRUE(on.mOk);
        EXPECT_EQ(on.mProgram.mInstructions, off.mProgram.mInstructions);
        EXPECT_EQ(on.mProgram.mStrings, off.mProgram.mStrings);
    }
}
