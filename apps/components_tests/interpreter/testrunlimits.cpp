// Task 0092 stage b: the instruction budget and the call depth cap of Interpreter::run.
//
// In plain words: the interpreter used to run a program until the program said stop. A script that jumps backwards
// forever would therefore hold the game for ever. The seam adds an optional limit that a caller sets; the default is
// "no limit", which is what Morrowind uses. A program that stays under its limit must behave exactly as before.
//
// The programs here are built by hand from the interpreter's own opcode numbers (no compiler, no game text): segment 0
// opcode 0 pushes the number in its low 24 bits, segment 0 opcode 2 jumps back by its low 24 bits.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <components/interpreter/context.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/program.hpp>

namespace
{
    class NullContext : public Interpreter::Context
    {
    public:
        ESM::RefId getTarget() const override { return ESM::RefId(); }
        int getLocalShort(int) const override { return 0; }
        int getLocalLong(int) const override { return 0; }
        float getLocalFloat(int) const override { return 0; }
        void setLocalShort(int, int) override {}
        void setLocalLong(int, int) override {}
        void setLocalFloat(int, float) override {}
        void messageBox(std::string_view, const std::vector<std::string>&) override {}
        void report(const std::string&) override {}
        int getGlobalShort(std::string_view) const override { return 0; }
        int getGlobalLong(std::string_view) const override { return 0; }
        float getGlobalFloat(std::string_view) const override { return 0; }
        void setGlobalShort(std::string_view, int) override {}
        void setGlobalLong(std::string_view, int) override {}
        void setGlobalFloat(std::string_view, float) override {}
        std::vector<std::string> getGlobals() const override { return {}; }
        char getGlobalType(std::string_view) const override { return ' '; }
        std::string getActionBinding(std::string_view) const override { return {}; }
        std::string_view getActorName() const override { return {}; }
        std::string_view getNPCRace() const override { return {}; }
        std::string_view getNPCClass() const override { return {}; }
        std::string_view getNPCFaction() const override { return {}; }
        std::string_view getNPCRank() const override { return {}; }
        std::string_view getPCName() const override { return {}; }
        std::string_view getPCRace() const override { return {}; }
        std::string_view getPCClass() const override { return {}; }
        std::string_view getPCRank() const override { return {}; }
        std::string_view getPCNextRank() const override { return {}; }
        int getPCBounty() const override { return 0; }
        std::string_view getCurrentCellName() const override { return {}; }
        int getMemberShort(ESM::RefId, std::string_view, bool) const override { return 0; }
        int getMemberLong(ESM::RefId, std::string_view, bool) const override { return 0; }
        float getMemberFloat(ESM::RefId, std::string_view, bool) const override { return 0; }
        void setMemberShort(ESM::RefId, std::string_view, int, bool) override {}
        void setMemberLong(ESM::RefId, std::string_view, int, bool) override {}
        void setMemberFloat(ESM::RefId, std::string_view, float, bool) override {}
    };

    constexpr Interpreter::Type_Code push(unsigned value)
    {
        return value;
    }

    constexpr Interpreter::Type_Code jumpBackward(unsigned count)
    {
        return (2u << 24) | count;
    }

    // push 0; jump back to the push: an endless loop that grows the stack by one number per round.
    Interpreter::Program endlessLoop()
    {
        Interpreter::Program program;
        program.mInstructions = { push(0), jumpBackward(1) };
        return program;
    }

    // n pushes: ends by running off the end.
    Interpreter::Program pushes(int n)
    {
        Interpreter::Program program;
        for (int i = 0; i < n; ++i)
        {
            program.mInstructions.push_back(push(static_cast<unsigned>(i)));
        }
        return program;
    }

    struct InterpreterRunLimits : public ::testing::Test
    {
        InterpreterRunLimits() { Interpreter::installOpcodes(mInterpreter); }
        Interpreter::Interpreter mInterpreter;
        NullContext mContext;
    };

    TEST_F(InterpreterRunLimits, an_endless_loop_is_stopped_by_the_instruction_budget)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 1000;
        const Interpreter::Program program = endlessLoop();
        EXPECT_THROW(mInterpreter.run(program, mContext, limits), Interpreter::BudgetExceeded);
    }

    TEST_F(InterpreterRunLimits, the_exception_names_what_ran_out)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 50;
        const Interpreter::Program program = endlessLoop();
        try
        {
            mInterpreter.run(program, mContext, limits);
            FAIL() << "the loop was not stopped";
        }
        catch (const Interpreter::BudgetExceeded& e)
        {
            EXPECT_NE(std::string(e.what()).find("instruction budget"), std::string::npos) << e.what();
            EXPECT_EQ(e.mExecuted, 50u);
        }
    }

    TEST_F(InterpreterRunLimits, a_program_under_its_budget_runs_to_the_end_like_before)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 10;
        const Interpreter::Program program = pushes(10);
        EXPECT_NO_THROW(mInterpreter.run(program, mContext, limits));
        EXPECT_EQ(mInterpreter.getInstructionsExecuted(), 10u);
    }

    TEST_F(InterpreterRunLimits, one_instruction_over_the_budget_is_stopped)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 9;
        const Interpreter::Program program = pushes(10);
        EXPECT_THROW(mInterpreter.run(program, mContext, limits), Interpreter::BudgetExceeded);
    }

    TEST_F(InterpreterRunLimits, the_default_run_has_no_limit_and_still_counts)
    {
        const Interpreter::Program program = pushes(5000);
        EXPECT_NO_THROW(mInterpreter.run(program, mContext));
        EXPECT_EQ(mInterpreter.getInstructionsExecuted(), 5000u);
    }

    TEST_F(InterpreterRunLimits, the_interpreter_is_usable_after_a_stopped_run)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 100;
        const Interpreter::Program loop = endlessLoop();
        EXPECT_THROW(mInterpreter.run(loop, mContext, limits), Interpreter::BudgetExceeded);

        const Interpreter::Program program = pushes(20);
        EXPECT_NO_THROW(mInterpreter.run(program, mContext, limits));
        EXPECT_EQ(mInterpreter.getInstructionsExecuted(), 20u);
    }

    TEST_F(InterpreterRunLimits, the_count_starts_again_for_every_call)
    {
        Interpreter::RunLimits limits;
        limits.mMaxInstructions = 10;
        const Interpreter::Program program = pushes(8);
        for (int i = 0; i < 5; ++i)
        {
            EXPECT_NO_THROW(mInterpreter.run(program, mContext, limits));
        }
    }

    // An opcode that runs another program through the same interpreter, to build call depth.
    class OpRecurse : public Interpreter::Opcode0
    {
    public:
        OpRecurse(Interpreter::Interpreter& interpreter, const Interpreter::Program& program)
            : mInterpreter(interpreter)
            , mProgram(program)
        {
        }

        void execute(Interpreter::Runtime& runtime) override { mInterpreter.run(mProgram, runtime.getContext()); }

    private:
        Interpreter::Interpreter& mInterpreter;
        const Interpreter::Program& mProgram;
    };

    TEST_F(InterpreterRunLimits, nested_runs_beyond_the_depth_cap_are_stopped)
    {
        Interpreter::Program program;
        // segment 5 (the top six bits are 0x32), opcode 500 = OpRecurse, which runs this same program again.
        program.mInstructions = { (0x32u << 26) | 500u };
        mInterpreter.installSegment5<OpRecurse>(500, mInterpreter, program);

        Interpreter::RunLimits limits;
        limits.mMaxDepth = 8;
        limits.mMaxInstructions = 1000000;
        try
        {
            mInterpreter.run(program, mContext, limits);
            FAIL() << "the recursion was not stopped";
        }
        catch (const Interpreter::BudgetExceeded& e)
        {
            EXPECT_NE(std::string(e.what()).find("call depth"), std::string::npos) << e.what();
        }
    }

    TEST_F(InterpreterRunLimits, nested_runs_inside_the_depth_cap_work)
    {
        const Interpreter::Program inner = pushes(3);
        Interpreter::Program outer;
        outer.mInstructions = { (0x32u << 26) | 500u, (0x32u << 26) | 500u };
        mInterpreter.installSegment5<OpRecurse>(500, mInterpreter, inner);

        Interpreter::RunLimits limits;
        limits.mMaxDepth = 4;
        EXPECT_NO_THROW(mInterpreter.run(outer, mContext, limits));
    }
}
