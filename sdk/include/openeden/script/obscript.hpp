#pragma once

// The ObScript runtime contract (task 0092, decision 0014): compile and run the procedural script language of
// Oblivion, Fallout 3 and Fallout New Vegas ("ObScript"). The runtime lives in the common compatibility pack; a game
// pack supplies the world (forms, variables, globals), the commands (functions) and the save chunk. The runtime holds
// no authority over the world or the save: it only runs scripts, and hands back bytes when asked.
//
// See host.hpp for the rules every script contract keeps (standard types only, no exception across the contract, main
// thread only, 32-bit forms).
//
// Function signatures use one character per argument. All arguments are required until a '/', after which every
// argument is optional:
//   f  float            l  integer          s  short (integer)       r  a form or reference (integer)
//   c  string, lower-cased                  S  string, case kept
// A function with optional arguments gets the count of the ones actually given as the size of CallArgs::arguments.

#include <openeden/script/host.hpp>
#include <openeden/version.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::script
{
    /// Identifies a compiled script. 0 is never a valid id.
    using ScriptId = std::uint32_t;
    /// Identifies a running copy of a script (its local variables). 0 is never a valid id.
    using InstanceId = std::uint32_t;

    enum class ScriptKind : std::uint8_t
    {
        /// A whole script: a "ScriptName" line, local declarations, then "Begin <event>" ... "End" blocks.
        Script,
        /// A piece of script with no Begin, as dialogue and quest result scripts are stored. Runs as the event "".
        Fragment,
    };

    struct ScriptSource
    {
        std::string name; ///< Used in error reports, and as the key a save refers to.
        std::string text;
        ScriptKind kind = ScriptKind::Script;
    };

    struct CompileResult
    {
        bool ok = false;
        ScriptId script = 0; ///< Valid only when ok.
        std::vector<ErrorReport> reports; ///< Errors and warnings, in source order.
    };

    struct RunResult
    {
        bool ok = false; ///< False when the script was stopped by an error or its budget.
        bool blockFound = false; ///< False when the script has no block for the event (nothing ran, ok is true).
        std::uint64_t instructionsExecuted = 0;
        ErrorReport error; ///< Valid when !ok.
    };

    /// What the game pack implements: the world as scripts see it. All calls come from the main thread. Implementations
    /// must not throw (a throw is caught by the runtime and ends that script with an error).
    class ObScriptWorld
    {
    public:
        virtual ~ObScriptWorld() = default;

        /// Does \a editorId name a form? Names are compared by the world, case-insensitively. Used when a script is
        /// compiled (is this name a form?) and each time a line that uses a form as a value runs (what is its id?); a
        /// name that no longer resolves reads as 0, "none". The run time calls
        /// happen in the middle of the instruction loop, which allocates nothing: an implementation should not allocate
        /// either (take a view of the name, do not build a string).
        virtual bool resolveForm(std::string_view editorId, FormId& form) const = 0;

        /// Does \a owner (a quest or object) have a script variable \a name, and of which kind?
        virtual bool findVariable(FormId owner, std::string_view name, ValueKind& kind) const = 0;
        virtual Value getVariable(FormId owner, std::string_view name) const = 0;
        virtual void setVariable(FormId owner, std::string_view name, Value value) = 0;

        /// Global variables (Float, Integer). Kind::String means "no such global".
        virtual ValueKind globalKind(std::string_view name) const = 0;
        virtual Value getGlobal(std::string_view name) const = 0;
        virtual void setGlobal(std::string_view name, Value value) = 0;

        /// A message the script printed (message boxes and the runtime's one-line notices).
        virtual void report(std::string_view message) = 0;
    };

    /// What a registered function receives.
    struct CallArgs
    {
        FormId self = 0; ///< The reference the running script is attached to.
        FormId reference = 0; ///< The explicit reference ("ref.Function"), or 0.
        std::span<const Value> arguments; ///< In the order of the signature; only the optional ones given.
    };

    using FunctionHandler = std::function<Value(const CallArgs&)>;

    struct FunctionDescriptor
    {
        std::string name; ///< As written in scripts; matched case-insensitively.
        std::string signature; ///< See the top of this file.
        ValueKind returns = ValueKind::Integer; ///< Integer or Float.
        bool allowsExplicitReference = true;
    };

    class ObScriptContract
    {
    public:
        static constexpr std::string_view kId = "openeden.script.obscript";
        /// 1.2 (2026-11-06): additive. declareFunction (a function with a shape and no behaviour yet, from a table).
        /// It is the LAST virtual function of the class, so that a consumer built against 1.0 or 1.1 keeps the table
        /// slot of every call it makes (new virtual functions go at the end).
        /// 1.1 (2026-11-06): additive. A form used as a value in a script ("Set x to SomeRef", "If ( x == Player )")
        /// is looked up with ObScriptWorld::resolveForm at the moment the line runs; the name "Player" is not special
        /// to the runtime, the world resolves it like any other name.
        static constexpr sdk::Version kVersion{ 1, 3 }; // 1.3 (2026-11-09, toddhow): RestoreResult gained trailing fields

        virtual ~ObScriptContract() = default;

        /// Add a function to the table the compiler uses. Fails (false, \a error filled) on a duplicate name, a bad
        /// signature or a full table. Register before compiling: scripts compiled earlier do not see it.
        virtual bool registerFunction(
            const FunctionDescriptor& descriptor, FunctionHandler handler, ErrorReport& error) noexcept = 0;

        /// Compile source text. Never reads compiled bytecode. A failure in one script does not affect the next.
        virtual CompileResult compile(const ScriptSource& source) noexcept = 0;

        /// Make a running copy of a compiled script attached to \a self. Returns 0 for an unknown script.
        virtual InstanceId createInstance(ScriptId script, FormId self) noexcept = 0;

        /// Run the block for \a event (case-insensitive; "" for a fragment). Time and order come from the caller: the
        /// runtime reads no clock.
        virtual RunResult run(InstanceId instance, std::string_view event, const Budget& budget = {}) noexcept = 0;

        /// A local variable of an instance by name; an Integer 0 for an unknown one.
        virtual Value getLocal(InstanceId instance, std::string_view name) const noexcept = 0;

        /// Set a local variable by name (the value is converted to the variable's type). False for an unknown
        /// instance or variable.
        virtual bool setLocal(InstanceId instance, std::string_view name, Value value) noexcept = 0;

        /// The state of every instance (locals and the form each is attached to) as versioned bytes.
        virtual SaveBlob serialise() const noexcept = 0;

        /// Replace the state of the instances from \a blob. Scripts must be compiled again first (the blob refers to
        /// them by name). A refused blob (wrong version, truncated, a count out of range, an unknown script) changes
        /// nothing.
        virtual RestoreResult restore(const SaveBlob& blob) noexcept = 0;

        /// Declare a function that has a shape and no behaviour yet, as a table derived from the player's data gives
        /// them: scripts that call it compile with typed arguments, and running it logs once per name and returns
        /// zero (of its return type), as an unknown function does. The signature may also be "*": the arguments are
        /// not known, so the compiler skips them. A name that is already registered or declared is refused. Later
        /// registerFunction with the same name and the same signature and return type gives the declaration a handler;
        /// any other registerFunction on a declared name is refused, because scripts compiled against the declaration
        /// depend on its shape. Register and declare before compiling.
        virtual bool declareFunction(const FunctionDescriptor& descriptor, ErrorReport& error) noexcept
        {
            error = ErrorReport();
            error.script = descriptor.name;
            error.message = "this runtime does not support declared functions";
            return false;
        }
    };
}
