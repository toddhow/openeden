#ifndef INTERPRETER_INTERPRETER_H_INCLUDED
#define INTERPRETER_INTERPRETER_H_INCLUDED

#include <cstdint>
#include <map>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>
#include <utility>

#include "opcodes.hpp"
#include "runtime.hpp"
#include "types.hpp"

namespace Interpreter
{
    struct Program;

    /// Optional limits for one outermost call of Interpreter::run. 0 means "no limit", which is what the Morrowind game
    /// uses (task 0092).
    struct RunLimits
    {
        std::uint64_t mMaxInstructions = 0; ///< Instructions one call may execute, nested runs included.
        std::uint32_t mMaxDepth = 0; ///< How many runs may be nested inside each other (the first is depth 1).
    };

    /// Thrown by Interpreter::run when a limit is reached. The interpreter is usable again afterwards.
    class BudgetExceeded : public std::runtime_error
    {
    public:
        BudgetExceeded(const std::string& message, std::uint64_t executed)
            : std::runtime_error(message)
            , mExecuted(executed)
        {
        }

        std::uint64_t mExecuted;
    };

    class Interpreter
    {
        std::stack<Runtime> mCallstack;
        bool mRunning = false;
        RunLimits mLimits;
        std::uint64_t mExecuted = 0;
        std::uint32_t mDepth = 0;
        Runtime mRuntime;
        std::map<int, std::unique_ptr<Opcode1>> mSegment0;
        std::map<int, std::unique_ptr<Opcode1>> mSegment2;
        std::map<int, std::unique_ptr<Opcode1>> mSegment3;
        std::map<int, std::unique_ptr<Opcode0>> mSegment5;

        void execute(Type_Code code);

        void begin();

        void end();

        [[noreturn]] void abortDuplicateInstruction(std::string_view name, int code);

        template <typename T, typename... Args>
        void installSegment(auto& segment, std::string_view name, int code, Args&&... args)
        {
            if (segment.find(code) != segment.end())
                abortDuplicateInstruction(name, code);
            segment.emplace(code, std::make_unique<T>(std::forward<Args>(args)...));
        }

    public:
        Interpreter() = default;

        Interpreter(const Interpreter&) = delete;
        Interpreter& operator=(const Interpreter&) = delete;

        template <typename T, typename... TArgs>
        void installSegment0(int code, TArgs&&... args)
        {
            installSegment<T>(mSegment0, "0", code, std::forward<TArgs>(args)...);
        }

        template <typename T, typename... TArgs>
        void installSegment2(int code, TArgs&&... args)
        {
            installSegment<T>(mSegment2, "2", code, std::forward<TArgs>(args)...);
        }

        template <typename T, typename... TArgs>
        void installSegment3(int code, TArgs&&... args)
        {
            installSegment<T>(mSegment3, "3", code, std::forward<TArgs>(args)...);
        }

        template <typename T, typename... TArgs>
        void installSegment5(int code, TArgs&&... args)
        {
            installSegment<T>(mSegment5, "5", code, std::forward<TArgs>(args)...);
        }

        void run(const Program& program, Context& context);

        /// As above, with limits. The limits of the outermost call stay in force for runs nested inside it (a nested
        /// call's own  limits are ignored). Throws BudgetExceeded when one is reached.
        void run(const Program& program, Context& context, const RunLimits& limits);

        /// Instructions executed by the last outermost call of run (also after it threw).
        std::uint64_t getInstructionsExecuted() const { return mExecuted; }
    };
}

#endif
