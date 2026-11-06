#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTLIMITS_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTLIMITS_H

#include <cstddef>
#include <cstdint>

namespace ScriptObscript::Limits
{
    // Caps on hostile or broken input (task 0092). They are far above anything a shipped script needs (the largest
    // Fallout New Vegas script source measured in research note 0046 is 33,364 bytes) and low enough that a script
    // cannot make the compiler use unbounded time or memory. Each is tested at the cap and one past it.

    inline constexpr std::size_t kMaxSourceBytes = 1u << 20; ///< Bytes of source text.
    inline constexpr std::size_t kMaxLines = 20000; ///< Lines of source text.
    inline constexpr std::size_t kMaxIdentifierLength = 128; ///< Characters in a name.
    inline constexpr std::size_t kMaxLocals = 1024; ///< Local variables in one script.
    inline constexpr std::size_t kMaxNesting = 64; ///< Brackets inside brackets in one line.
    inline constexpr std::size_t kMaxBlocks = 256; ///< Begin ... End blocks in one script.
    inline constexpr std::size_t kMaxUnknownFunctions = 4096; ///< Distinct unknown function names in one runtime.
    inline constexpr std::size_t kMaxFunctions = 4096; ///< Registered functions in one runtime.
    inline constexpr std::size_t kMaxFunctionArguments = 16; ///< Arguments of one function.
    inline constexpr std::size_t kMaxScripts = 1u << 16; ///< Compiled scripts in one runtime.
    inline constexpr std::size_t kMaxInstances = 1u << 16; ///< Instances in one runtime (and in a save blob).

    inline constexpr std::uint64_t kDefaultMaxInstructions = 1000000; ///< Per run call, when the caller sets none.
    inline constexpr std::uint32_t kDefaultMaxCallDepth = 16; ///< Nested runs, when the caller sets none.

    inline constexpr std::uint32_t kSaveFormatVersion = 1; ///< The save blob layout this runtime writes and reads.
}

#endif
