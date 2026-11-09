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
    inline constexpr std::size_t kMaxCallDepth = 64; ///< Typed calls inside the arguments of typed calls ("Foo Foo 1").
    inline constexpr std::size_t kMaxBlocks = 256; ///< Begin ... End blocks in one script.
    inline constexpr std::size_t kMaxUnknownFunctions = 4096; ///< Distinct unknown function names in one runtime.
    inline constexpr std::size_t kMaxUnknownFunctionsPerScript = 512; ///< Distinct unknown names in ONE script.
    /// Names that stand before a dot and nothing known owns ("Gone.Probe 1": maybe a command whose result is the
    /// reference, maybe a form from a master that is not loaded). They have a bucket of their own so that they
    /// cannot use up the room for unknown functions.
    inline constexpr std::size_t kMaxUnknownBeforeDot = 1024; ///< In one runtime.
    inline constexpr std::size_t kMaxUnknownBeforeDotPerScript = 64; ///< In ONE script.
    inline constexpr std::size_t kMaxFunctions = 4096; ///< Registered functions in one runtime.
    inline constexpr std::size_t kMaxFunctionArguments = 16; ///< Arguments of one function.
    inline constexpr std::size_t kMaxScripts = 1u << 16; ///< Compiled scripts in one runtime.
    inline constexpr std::size_t kMaxInstances = 1u << 16; ///< Instances in one runtime (and in a save blob).

    inline constexpr std::uint64_t kDefaultMaxInstructions = 1000000; ///< Per run call, when the caller sets none.
    inline constexpr std::uint32_t kDefaultMaxCallDepth = 16; ///< Nested runs, when the caller sets none.

    /// The save blob layout this runtime writes. Version 1 (the variables by position, hidden ones included) is still
    /// read; version 2 stores the variables by name and leaves the compiler's hidden locals out.
    inline constexpr std::uint32_t kSaveFormatVersion = 2;
    inline constexpr std::uint32_t kPositionalSaveFormatVersion = 1;
    inline constexpr std::size_t kMaxSaveBlobBytes = 16u << 20; ///< A save chunk larger than this is refused unread.
}

#endif
