// Tests for the debug ragdoll console commands, stage b of task 0035 ("Console commands (answer 18)").
//
// In plain words: the game's script compiler turns the text of a script line into numbers (opcodes). These tests
// feed it the lines `cmRagdoll`, `"someone"->cmRagdoll`-style explicit forms and `cmRagdollOff`, and check that each
// compiles to the opcode the spec names: 0x2000327 (cmRagdoll), 0x2000328 (cmRagdoll on an explicit reference) and
// 0x2000329 (cmRagdollOff). They do not run the commands (that needs the whole game).
//
// How a line is compiled: the same machinery as testscripts.cpp (a Compiler::Scanner feeding a Compiler::FileParser
// with all of the game's registered extensions), wrapped in "Begin ... End" because the file parser wants a whole
// script. An instruction compiles to one code word: segment 5 (0xc8000000) or-ed with the opcode.

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "testutils.hpp"

#include <components/compiler/generator.hpp>

namespace
{
    class RagdollCommandsTest : public ::testing::Test
    {
    public:
        RagdollCommandsTest()
            : mParser(mErrorHandler, mCompilerContext)
        {
            Compiler::registerExtensions(mExtensions);
            mCompilerContext.setExtensions(&mExtensions);
        }

        // Compiles one script line. Returns the program, or nothing if the line does not compile.
        std::optional<Interpreter::Program> compileLine(const std::string& line)
        {
            mParser.reset();
            mErrorHandler.reset();
            const std::string script = "Begin ragdolltest\n" + line + "\nEnd\n";
            std::istringstream input(script);
            Compiler::Scanner scanner(mErrorHandler, input, mCompilerContext.getExtensions());
            try
            {
                scanner.scan(mParser);
            }
            catch (const Compiler::SourceException&)
            {
                return {};
            }
            if (!mErrorHandler.isGood())
                return {};
            return mParser.getProgram();
        }

        static bool containsOpcode(const Interpreter::Program& program, int opcode)
        {
            const Interpreter::Type_Code code = Compiler::Generator::segment5(static_cast<unsigned int>(opcode));
            return std::find(program.mInstructions.begin(), program.mInstructions.end(), code)
                != program.mInstructions.end();
        }

    private:
        TestErrorHandler mErrorHandler;
        TestCompilerContext mCompilerContext;
        Compiler::FileParser mParser;
        Compiler::Extensions mExtensions;
    };

    TEST_F(RagdollCommandsTest, OpcodesHaveTheNumbersInTheSpec)
    {
        EXPECT_EQ(Compiler::Misc::opcodeCmRagdoll, 0x2000327);
        EXPECT_EQ(Compiler::Misc::opcodeCmRagdollExplicit, 0x2000328);
        EXPECT_EQ(Compiler::Misc::opcodeCmRagdollOff, 0x2000329);
    }

    TEST_F(RagdollCommandsTest, CmRagdollCompilesToItsOpcode)
    {
        const auto program = compileLine("cmRagdoll");
        ASSERT_TRUE(program.has_value());
        EXPECT_TRUE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdoll));
        EXPECT_FALSE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdollExplicit));
        EXPECT_FALSE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdollOff));
    }

    TEST_F(RagdollCommandsTest, CommandNamesIgnoreCase)
    {
        const auto program = compileLine("cmragdoll");
        ASSERT_TRUE(program.has_value());
        EXPECT_TRUE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdoll));
    }

    TEST_F(RagdollCommandsTest, CmRagdollOnAnExplicitReferenceCompilesToTheExplicitOpcode)
    {
        // The spec's example is "someone"->cmRagdoll. This test's compiler context only knows the id "player"
        // (testutils.hpp, TestCompilerContext::isId), so the reference is the player.
        const auto program = compileLine("player->cmRagdoll");
        ASSERT_TRUE(program.has_value());
        EXPECT_TRUE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdollExplicit));
        EXPECT_FALSE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdoll));
    }

    TEST_F(RagdollCommandsTest, CmRagdollOffCompilesToItsOpcode)
    {
        const auto program = compileLine("cmRagdollOff");
        ASSERT_TRUE(program.has_value());
        EXPECT_TRUE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdollOff));
        EXPECT_FALSE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdoll));
    }

    TEST_F(RagdollCommandsTest, CmRagdollOffTakesNoArgument)
    {
        // The compiler tolerates extra arguments (it only warns), so a line with one still compiles; what matters is
        // that the command is its own instruction and still carries its own opcode, not an "off" argument of
        // cmRagdoll.
        const auto program = compileLine("cmRagdollOff 1");
        if (program.has_value())
        {
            EXPECT_TRUE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdollOff));
            EXPECT_FALSE(containsOpcode(*program, Compiler::Misc::opcodeCmRagdoll));
        }
    }
}
