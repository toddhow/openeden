#ifndef INTERPRETER_MATHOPCODES_H_INCLUDED
#define INTERPRETER_MATHOPCODES_H_INCLUDED

#include <cmath>
#include <stdexcept>

#include "opcodes.hpp"
#include "runtime.hpp"

namespace Interpreter
{
    template <typename T>
    class OpAddInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) + getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpSubInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) - getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpMulInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) * getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpDivInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T left = getData<T>(runtime[0]);

            if (left == 0)
                throw std::runtime_error("division by zero");

            T result = getData<T>(runtime[1]) / left;

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    /// "&&" and "||" (task 0092): both integers are on the stack, the result is 1 or 0.
    template <bool And>
    class OpLogical : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            const bool left = runtime[1].mInteger != 0;
            const bool right = runtime[0].mInteger != 0;

            runtime.pop();

            runtime[0].mInteger = And ? (left && right) : (left || right);
        }
    };

    template <typename T, typename C>
    class OpCompare : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            int result = C()(getData<T>(runtime[1]), getData<T>(runtime[0]));

            runtime.pop();

            runtime[0].mInteger = result;
        }
    };
}

#endif
