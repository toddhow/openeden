#include "interpreter.hpp"

#include <cassert>
#include <format>
#include <stdexcept>
#include <string>

#include "opcodes.hpp"
#include "program.hpp"

namespace Interpreter
{
    namespace
    {
        [[noreturn]] void abortUnknownCode(int segment, int opcode)
        {
            const std::string error = std::format("unknown opcode {} in segment {}", opcode, segment);
            throw std::runtime_error(error);
        }

        [[noreturn]] void abortUnknownSegment(Type_Code code)
        {
            const std::string error = std::format("opcode outside of the allocated segment range: {}", code);
            throw std::runtime_error(error);
        }

        template <typename T>
        auto& getDispatcher(const T& segment, unsigned int seg, int opcode)
        {
            auto it = segment.find(opcode);
            if (it == segment.end())
            {
                abortUnknownCode(seg, opcode);
            }
            return it->second;
        }
    }

    [[noreturn]] void Interpreter::abortDuplicateInstruction(std::string_view name, int code)
    {
        throw std::invalid_argument(
            std::format("Duplicated interpreter instruction code in segment {}: {:#x}", name, code));
    }

    void Interpreter::execute(Type_Code code)
    {
        unsigned int segSpec = code >> 30;

        switch (segSpec)
        {
            case 0:
            {
                const int opcode = code >> 24;
                const unsigned int arg0 = code & 0xffffff;

                return getDispatcher(mSegment0, 0, opcode)->execute(mRuntime, arg0);
            }

            case 2:
            {
                const int opcode = (code >> 20) & 0x3ff;
                const unsigned int arg0 = code & 0xfffff;

                return getDispatcher(mSegment2, 2, opcode)->execute(mRuntime, arg0);
            }
        }

        segSpec = code >> 26;

        switch (segSpec)
        {
            case 0x30:
            {
                const int opcode = (code >> 8) & 0x3ffff;
                const unsigned int arg0 = code & 0xff;

                return getDispatcher(mSegment3, 3, opcode)->execute(mRuntime, arg0);
            }

            case 0x32:
            {
                const int opcode = code & 0x3ffffff;

                return getDispatcher(mSegment5, 5, opcode)->execute(mRuntime);
            }
        }

        abortUnknownSegment(code);
    }

    void Interpreter::begin()
    {
        if (mRunning)
        {
            mCallstack.push(mRuntime);
            mRuntime.clear();
        }
        else
        {
            mRunning = true;
        }
    }

    void Interpreter::end()
    {
        if (mCallstack.empty())
        {
            mRuntime.clear();
            mRunning = false;
        }
        else
        {
            mRuntime = mCallstack.top();
            mCallstack.pop();
        }
    }

    void Interpreter::run(const Program& program, Context& context)
    {
        run(program, context, RunLimits{});
    }

    void Interpreter::run(const Program& program, Context& context, const RunLimits& limits)
    {
        // The limits belong to the outermost call and stay in force for runs nested inside it.
        const bool outermost = !mRunning;
        begin();
        if (outermost)
        {
            mLimits = limits;
            mExecuted = 0;
            mDepth = 0;
        }
        ++mDepth;

        try
        {
            if (mLimits.mMaxDepth != 0 && mDepth > mLimits.mMaxDepth)
            {
                throw BudgetExceeded(
                    std::format("script call depth limit of {} reached", mLimits.mMaxDepth), mExecuted);
            }

            mRuntime.configure(program, context);

            while (mRuntime.getPC() >= 0 && static_cast<std::size_t>(mRuntime.getPC()) < program.mInstructions.size())
            {
                if (mLimits.mMaxInstructions != 0 && mExecuted >= mLimits.mMaxInstructions)
                {
                    throw BudgetExceeded(
                        std::format("instruction budget of {} reached", mLimits.mMaxInstructions), mExecuted);
                }
                ++mExecuted;

                const Type_Code instruction = program.mInstructions[mRuntime.getPC()];
                mRuntime.setPC(mRuntime.getPC() + 1);
                execute(instruction);
            }
        }
        catch (...)
        {
            --mDepth;
            end();
            throw;
        }

        --mDepth;
        end();
    }
}
