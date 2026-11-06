#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTCOMPILE_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTCOMPILE_H

// The ObScript front end (task 0092): turns source text into compiled blocks with the shared script compiler
// (components/compiler), after a pass of its own that enforces the size caps and finds the structure of the script
// (the ScriptName line, the local declarations and the Begin ... End blocks).
//
// Everything here is internal to the ObScript runtime; the contract is in sdk/include/openeden/script/.

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <components/compiler/extensions.hpp>
#include <components/compiler/locals.hpp>
#include <components/interpreter/program.hpp>

#include <openeden/script/obscript.hpp>

namespace ScriptObscript
{
    /// One function the compiler can call: registered by the game pack, or an unknown name accepted in tolerant mode.
    /// Entries live in a std::deque owned by the runtime, so a pointer to one stays valid.
    struct FunctionEntry
    {
        std::string mName; ///< Lower case.
        std::string mSpelling; ///< As first written (for messages).
        std::string mSignature; ///< The contract's characters; always contains a '/'.
        std::string mCompilerSignature; ///< The same arguments in the compiler's characters.
        std::string mArgKinds; ///< The contract's argument characters without the '/'.
        std::size_t mRequired = 0; ///< Arguments before the '/'.
        std::size_t mTotal = 0; ///< All arguments.
        openeden::script::ValueKind mReturns = openeden::script::ValueKind::Integer;
        bool mAllowsExplicit = true;
        bool mUnknown = false; ///< An unknown name accepted by the tolerant policy.
        bool mNoticeGiven = false; ///< An unknown name has been reported once.
        int mCode = 0; ///< Segment 3 opcode; mCode + 1 is the explicit reference form.
        openeden::script::FunctionHandler mHandler;
    };

    struct CompiledBlock
    {
        std::string mEvent; ///< Lower case; "" for a fragment.
        Interpreter::Program mProgram;
    };

    struct CompiledScript
    {
        std::string mName;
        openeden::script::ScriptKind mKind = openeden::script::ScriptKind::Script;
        std::vector<CompiledBlock> mBlocks;
        Compiler::Locals mLocals; ///< Names and types; ints and refs are longs.
        std::set<std::string> mRefLocals; ///< Lower-case names of the locals declared as ref.
        std::set<std::string> mFormLocals; ///< Same as mRefLocals; kept apart for readers of the layout.
    };

    struct CompileOutput
    {
        bool mOk = false;
        CompiledScript mScript;
        std::vector<openeden::script::ErrorReport> mReports;
    };

    /// What the compiler needs from the runtime.
    struct CompileServices
    {
        const openeden::script::ObScriptWorld* mWorld = nullptr;
        openeden::script::UnknownFunctionPolicy mPolicy = openeden::script::UnknownFunctionPolicy::Tolerant;
        /// Look up a registered or already accepted function (lower-case name); null when there is none.
        std::function<const FunctionEntry*(const std::string&)> mFind;
        /// Accept an unknown name (tolerant mode): returns its entry, or null when the table is full.
        std::function<const FunctionEntry*(const std::string& lowerName, const std::string& spelling)> mAddUnknown;
        /// The functions the compiler's command table starts from, in registration order.
        std::function<void(const std::function<void(const FunctionEntry&)>&)> mForEachFunction;
    };

    /// Compile \a source. Never throws; every problem is in the output's reports. A failure in one script has no
    /// effect on the next.
    CompileOutput compileScript(const openeden::script::ScriptSource& source, const CompileServices& services) noexcept;

    /// The compiler's argument characters for a contract signature, or an empty string with \a error filled when the
    /// signature is not valid. Always contains a '/', so that every function is a segment 3 opcode.
    std::string translateSignature(
        std::string_view signature, std::size_t& required, std::size_t& total, std::string& error);

    /// Is \a name usable as a function or variable name (letters, digits and underscores, not a language word)?
    bool isValidFunctionName(std::string_view name);
}

#endif
