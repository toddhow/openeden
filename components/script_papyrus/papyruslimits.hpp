#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSLIMITS_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSLIMITS_H

#include <cstddef>
#include <cstdint>

namespace ScriptPapyrus::Limits
{
    // Caps on hostile or broken ".pex" input (task 0093). Each is about four times the maximum measured over every
    // installed script of Skyrim SE and Fallout 4 (research note 0046 section 12), and each is tested at the cap and
    // one past it. A count is checked against its cap, and against the bytes that are left in the file, BEFORE the
    // allocation it governs.

    inline constexpr std::size_t kMaxFileBytes = 4u << 20; ///< Measured maximum 541,342.
    inline constexpr std::size_t kMaxStrings = 65534; ///< The two byte count; 0xFFFF announces a four byte count.
    inline constexpr std::size_t kMaxExtendedStrings = 262144; ///< Largest accepted four byte string count.
    inline constexpr std::size_t kMaxStringBytes = 64u << 10; ///< Measured maximum 3,134.
    inline constexpr std::size_t kMaxObjects = 16; ///< Measured: 1 in every file.
    inline constexpr std::size_t kMaxMembersPerObject = 8192; ///< Properties, and variables, each. Measured 1,011.
    inline constexpr std::size_t kMaxStates = 1024; ///< Measured 128.
    inline constexpr std::size_t kMaxFunctionsPerState = 4096; ///< Measured 691.
    inline constexpr std::size_t kMaxParametersAndLocals = 4096; ///< Each. Measured 1,056 locals, 46 parameters (the
                                                                 ///< spec's 1,024 is below the measured maximum).
    inline constexpr std::size_t kMaxInstructions = 32768; ///< Per function. Measured 5,149. (The spec said 65,536,
                                                           ///< which a two byte count cannot reach.)
    inline constexpr std::size_t kMaxCallArguments = 256; ///< Measured 46.
    inline constexpr std::size_t kMaxStructs = 256; ///< Measured 18.
    inline constexpr std::size_t kMaxStructMembers = 256; ///< Measured 34.
    inline constexpr std::size_t kMaxTrailingPadding = 64; ///< Zero bytes tolerated after the last object. Measured 51.
    inline constexpr std::size_t kMaxUserFlags = 1024; ///< Measured 6.
    inline constexpr std::size_t kMaxPropertyGroups = 1024; ///< Debug records.
    inline constexpr std::size_t kMaxStructOrders = 256; ///< Debug records.
    inline constexpr std::size_t kMaxFunctionsPerFile = 65536; ///< All states of all objects together.
    inline constexpr std::size_t kMaxInstructionsPerFile = 1u << 20; ///< All functions together (memory bound).
    inline constexpr std::size_t kMaxScripts = 1u << 16; ///< Loaded scripts in one runtime.
    inline constexpr std::size_t kMaxLinkReports = 256; ///< Entries in LinkReport::unresolved.
}

#endif
