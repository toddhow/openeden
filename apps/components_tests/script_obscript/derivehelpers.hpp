#ifndef OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_DERIVEHELPERS_H
#define OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_DERIVEHELPERS_H

// Task 0092 item 2: builders for the compiled bytes the derivation reads. Every byte is written here from the format
// description at the top of components/script_obscript/derive.hpp: no game file was copied, and the commands are
// invented ("Frobnicate" and friends have the opcodes 0x2000 and up, which no game command uses).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace DeriveTest
{
    using Bytes = std::string;

    inline void u16(Bytes& out, unsigned value)
    {
        out += static_cast<char>(value & 0xFF);
        out += static_cast<char>((value >> 8) & 0xFF);
    }

    inline void i32(Bytes& out, std::int32_t value)
    {
        const std::uint32_t bits = static_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8)
        {
            out += static_cast<char>((bits >> shift) & 0xFF);
        }
    }

    inline Bytes statement(unsigned opcode, const Bytes& payload = {})
    {
        Bytes out;
        u16(out, opcode);
        u16(out, static_cast<unsigned>(payload.size()));
        return out + payload;
    }

    inline Bytes referencePrefix(unsigned index)
    {
        Bytes out;
        u16(out, 0x1C);
        u16(out, index);
        return out;
    }

    // Arguments of a command.
    namespace Arg
    {
        inline Bytes ref(unsigned index)
        {
            Bytes out = "r";
            u16(out, index);
            return out;
        }
        inline Bytes number(std::int32_t value)
        {
            Bytes out = "n";
            i32(out, value);
            return out;
        }
        inline Bytes real(double value)
        {
            Bytes out = "z";
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            for (int shift = 0; shift < 64; shift += 8)
            {
                out += static_cast<char>((bits >> shift) & 0xFF);
            }
            return out;
        }
        inline Bytes variable(unsigned index)
        {
            Bytes out = "s";
            u16(out, index);
            return out;
        }
        /// A word the engine knows or a string: no tag, a plain u16 (what is behind it cannot be read).
        inline Bytes untagged(unsigned value)
        {
            Bytes out;
            u16(out, value);
            return out;
        }
    }

    /// The payload of a command statement: the count, then the arguments.
    inline Bytes payload(const std::vector<Bytes>& arguments)
    {
        Bytes out;
        u16(out, static_cast<unsigned>(arguments.size()));
        for (const Bytes& argument : arguments)
        {
            out += argument;
        }
        return out;
    }

    // Pieces of an expression.
    namespace Expr
    {
        inline Bytes ref(unsigned index)
        {
            Bytes out = " r";
            u16(out, index);
            return out;
        }
        inline Bytes number(const std::string& digits)
        {
            return " " + digits;
        }
        inline Bytes op(const std::string& symbol)
        {
            return " " + symbol;
        }
        /// A call; \a onReference is true when it directly follows a reference token ("ref.Command").
        inline Bytes call(unsigned opcode, const Bytes& callPayload, bool onReference = false)
        {
            Bytes out = onReference ? "X" : " X";
            u16(out, opcode);
            u16(out, static_cast<unsigned>(callPayload.size()));
            return out + callPayload;
        }
        /// The payload of a call with no arguments inside an expression is empty (no count).
        inline Bytes noArguments()
        {
            return {};
        }
    }

    inline Bytes ifStatement(const Bytes& expression, unsigned opcode = 0x16)
    {
        Bytes body;
        u16(body, 2);
        u16(body, static_cast<unsigned>(expression.size()));
        return statement(opcode, body + expression);
    }

    /// Set: the target bytes (a variable token, with a reference before it for another script's variable), then the
    /// expression with its length.
    inline Bytes setStatement(const Bytes& target, const Bytes& expression)
    {
        Bytes body = target;
        u16(body, static_cast<unsigned>(expression.size()));
        return statement(0x15, body + expression);
    }

    inline Bytes scriptName()
    {
        return statement(0x1D);
    }

    inline Bytes begin()
    {
        Bytes body;
        u16(body, 0);
        i32(body, 0);
        return statement(0x10, body);
    }

    inline Bytes end()
    {
        return statement(0x11);
    }

    inline Bytes endIf()
    {
        return statement(0x19);
    }
}

#endif
