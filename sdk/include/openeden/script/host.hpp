#pragma once

// Shared pieces of the script runtime contracts (task 0092, decision 0014): the value type, the located error report,
// the per-call budget and the versioned save blob. Both runtimes (ObScript now, Papyrus later) use these.
//
// Rules this header keeps (the root guide's "contracts carry no implementation and no third-party types"):
//  - only standard types; no plugin-record, engine, renderer, UI-toolkit or physics type appears here;
//  - a form or a reference is a 32-bit integer (FormId), a string is a std::string_view that is valid for the call;
//  - nothing throws across the contract: every failure is an ErrorReport in a result;
//  - main thread only: a runtime is not thread safe and must be called from the game's simulation phase.
//
// The contract types are in a NAMED namespace (an anonymous one gives each library a type of its own and aborts on
// GCC).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::script
{
    /// A form or reference as the game pack sees it. 0 means "none".
    using FormId = std::uint32_t;

    /// What a script value holds. Forms travel as integers; a String is only ever an argument, never a stored value.
    enum class ValueKind : std::uint8_t
    {
        Integer,
        Float,
        Form,
        String,
    };

    struct Value
    {
        ValueKind kind = ValueKind::Integer;
        std::int32_t integer = 0; ///< Integer and Form values.
        float number = 0.0f; ///< Float values.
        std::string_view text; ///< String arguments; valid only for the duration of the call.

        static constexpr Value makeInteger(std::int32_t value) noexcept
        {
            return { ValueKind::Integer, value, 0.0f, {} };
        }
        static constexpr Value makeFloat(float value) noexcept { return { ValueKind::Float, 0, value, {} }; }
        static constexpr Value makeForm(FormId value) noexcept
        {
            return { ValueKind::Form, static_cast<std::int32_t>(value), 0.0f, {} };
        }
        static constexpr Value makeString(std::string_view value) noexcept
        {
            return { ValueKind::String, 0, 0.0f, value };
        }
    };

    enum class Severity : std::uint8_t
    {
        Warning,
        Error,
    };

    /// A located problem. line and column are 1-based; 0 means "no position" (a file level problem or a run time one).
    struct ErrorReport
    {
        Severity severity = Severity::Error;
        std::string script;
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        std::string message;

        /// "script:line:column: message", or "script: message" without a position.
        std::string format() const
        {
            std::string text = script;
            if (line != 0)
            {
                text += ':' + std::to_string(line) + ':' + std::to_string(column);
            }
            text += ": " + message;
            return text;
        }
    };

    /// Limits for one run call. 0 in a field means "the runtime's default" (never "unlimited").
    struct Budget
    {
        std::uint64_t maxInstructions = 0; ///< Instructions one call may execute before it is stopped.
        std::uint32_t maxCallDepth = 0; ///< Nested script calls one call may make.
    };

    /// How the runtime treats a name it does not know (the extender policy of decision 0014's amendment).
    enum class UnknownFunctionPolicy : std::uint8_t
    {
        Tolerant, ///< An unknown function is accepted, logs once per name and does nothing (returns zero).
        Strict, ///< An unknown function is a located compile error. For tests.
    };

    /// The bytes a runtime hands to its game pack to store, with a version of their own so that a runtime fix does not
    /// invalidate saves (decision 0014, open question 2).
    struct SaveBlob
    {
        std::uint32_t formatVersion = 0;
        std::vector<std::uint8_t> bytes;
    };

    /// Result of a restore: success, or the reason the blob was refused.
    struct RestoreResult
    {
        bool ok = false;
        ErrorReport error;
    };
}
