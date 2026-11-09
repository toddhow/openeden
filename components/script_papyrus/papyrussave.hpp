#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSSAVE_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSSAVE_H

// The script state blob of the Papyrus runtime (task 0093 stage e): the constants of its layout and the caps that
// restore() holds a blob to. The runtime only produces and consumes the bytes; the game pack stores them in its own
// save chunk (key "script.papyrus").
//
// Layout (all numbers little-endian, the same bytes on every machine; version 1):
//
//   header     u32 magic ("PAPS"), u32 format version, u64 body length, u64 FNV-1a checksum of the body
//   body       u64 script time of the last tick (an IEEE double), u64 next stack sequence, u32 next park counter
//              texts     u32 count; each: u32 length, bytes
//              scripts   u32 count; each: u32 name text, u64 chain digest, u32 variable count,
//                        each variable: u32 name text, u8 declared type kind
//              heap      u32 count; each: u8 type (1 array, 2 struct), array: u8 element kind, u32 count, values;
//                        struct: u32 script, u32 name text, u32 count, values
//              instances u32 count; each: u8 attach kind, u32 form id, u32 alias index, u32 effect key, u32 script,
//                        u32 state text, u32 count, values
//              stacks    u32 count, in creation order (strictly increasing sequence); each: u8 flags (1 parked,
//                        2 started, 4 waiting for the host), u64 sequence, u32 instance, u32 event name text,
//                        u32 argument count, parked only: u64 wake time (a double), u64 latent token, u32 pool index;
//                        u32 frame count, frames, u32 value count, values
//                        frame: u32 script, u32 function, u32 pc, value self, u8 result destination kind,
//                        u32 result destination slot
//   value      u8 kind (the Vm::Kind numbers), then: Int, Float, Bool: u32 bits; String: u32 text; Form: u32 form id,
//              u32 class name text; Instance: u32 instance id; Array, Struct: u32 heap index; None: nothing
//
// Names (class, function, variable and struct names, a form's class) are written in lower case: the runtime compares
// them without regard to case, and a blob written, restored and written again is byte for byte the same. A state name
// and the text of a string value keep the case the script gave them. Nothing in the blob is a pointer or an id that
// belongs to one run: texts, scripts and heap objects are numbered in the order the writer meets them.

#include <cstddef>
#include <cstdint>

namespace ScriptPapyrus::Save
{
    inline constexpr std::uint32_t kMagic = 0x53504150u; ///< "PAPS" as little-endian bytes 'P' 'A' 'P' 'S'.
    inline constexpr std::uint32_t kFormatVersion = 1;
    inline constexpr std::size_t kHeaderBytes = 24;
    inline constexpr std::size_t kMaxBlobBytes = 64u << 20;
    inline constexpr std::size_t kMaxTexts = 1u << 21;
    inline constexpr std::size_t kMaxFrames = 1u << 16; ///< Frames of one stack.
    inline constexpr std::size_t kMaxStackValues = 1u << 20; ///< Values of one stack (the interpreter's own cap).
    inline constexpr std::size_t kMaxTotalValues = 1u << 22; ///< Values in the whole blob.
    inline constexpr std::size_t kMaxPoolSlots = 1u << 18; ///< Highest pool index of a parked stack, plus one.
    inline constexpr std::size_t kMaxQueuedStacks = 1u << 16; ///< Stacks waiting to start, as the runtime's own cap.

    inline constexpr std::uint64_t kFnvBasis = 1469598103934665603ull;
    inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    inline std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size, std::uint64_t hash = kFnvBasis) noexcept
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            hash = (hash ^ data[i]) * kFnvPrime;
        }
        return hash;
    }

    inline std::uint64_t fnvMix(std::uint64_t hash, std::uint64_t value) noexcept
    {
        for (int i = 0; i < 8; ++i)
        {
            hash = (hash ^ ((value >> (8 * i)) & 0xFFu)) * kFnvPrime;
        }
        return hash;
    }
}

#endif
