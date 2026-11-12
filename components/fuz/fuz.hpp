#ifndef OPENEDEN_COMPONENTS_FUZ_FUZ_H
#define OPENEDEN_COMPONENTS_FUZ_FUZ_H

// The .fuz container of Bethesda voice lines (task 0143, milestone M-S4). Formats layer: standard library only, no
// engine, game rule or decoder. A .fuz holds a .lip file (lip sync, not used here) and the audio (an xWMA file in
// Skyrim SE). Layout, VERIFIED on every file of "Skyrim - Voices_en0.bsa" (75,408 files, task 0143 stage log):
//
//   offset 0   "FUZE"
//   offset 4   u32 version, 1 in every file
//   offset 8   u32 size of the .lip part
//   offset 12  the .lip bytes
//   then       the audio: a RIFF file ("RIFF", u32 size, form type "XWMA"); its RIFF size + 8 equals the rest of the
//              file in every file
//
// Untrusted input: every size is checked against the bytes present before anything is read, and the whole input is
// capped. Nothing here throws. The result points into the caller's bytes: keep them alive while the parts are used.

#include <cstdint>
#include <string>
#include <string_view>

namespace Fuz
{
    struct Limits
    {
        std::uint64_t mMaxBytes = 64ull * 1024 * 1024; ///< the whole file; a voice line is under 1 MiB in the game's data
        std::uint32_t mMaxLipBytes = 4u * 1024 * 1024; ///< the largest .lip part in the game's data is 32 KiB
    };

    struct Parts
    {
        std::string_view mLip; ///< may be empty
        std::string_view mAudio; ///< exactly the RIFF file (bytes after its declared size are dropped)
        bool mAudioIsXwma = false; ///< the RIFF form type is "XWMA"
        std::uint64_t mTrailingBytes = 0; ///< bytes after the RIFF file that were dropped
    };

    struct Result
    {
        bool mOk = false;
        Parts mParts;
        std::string mError; ///< "what was wrong (offset N)"; empty when mOk
    };

    /// Splits a .fuz. `name` is only used in the error text.
    [[nodiscard]] Result split(std::string_view bytes, std::string_view name = {}, const Limits& limits = {});

    /// True when the bytes start with the "FUZE" magic (a cheap test before `split`).
    [[nodiscard]] bool hasFuzMagic(std::string_view bytes);
}

#endif
