#ifndef OPENEDEN_COMPONENTS_LIP_LIP_H
#define OPENEDEN_COMPONENTS_LIP_LIP_H

// Task 0146 (stage b): the `.lip` lip-sync file of Skyrim SE voice lines (the part `Fuz::split` hands over). Formats
// layer: standard library only. Read only (no writer).
//
// Provenance and verification: docs/tasks/0146-skyrim-lip-sync.md section 6. The grammar below was found out by
// reading the open-source decoder of the Mudcrab project (GPL-3.0-or-later; its documentation page and decoder text),
// after the first attempt from the user's data alone had found only the run-length shape; every fact is marked
// VERIFIED where this code's reader reproduces it on the user's own files (all 73,118 + ... lip parts of
// `Skyrim - Voices_en0.bsa`, see the task log).
//
//   0   u32 version (1)
//   4   u32 grid size (in-memory size of the expanded animation; not used)
//   8   u32 number of curves (13 or 9; not used)
//   12  u16 frames: the number of rows of the grid
//   14  u16 (3 or 2 or 6: not used)
//   16  i32 first: timing pre-roll in frames (-120..0)
//   20  u16 16 when the timing field is where it should be
//   22  u16 not used
//   24+extra  the payload, extra in 0..8 (some files have extra header bytes): zero-run coding of little-endian f32:
//             a 0x00 byte is followed by a u16 count (non-zero) of zero bytes; any other byte is a literal.
//   The decoded bytes are `count` floats; they fill a grid of `frames` rows of 33 slots, starting at slot
//   p0 = (6 - count) mod 33 of row 0, so that the data ends at slot 6 of the last row. A candidate `extra` is accepted
//   only if the decoding succeeds, every float is finite and in [-2, 2], and the row count equals `frames`; the
//   smallest such `extra` wins.
//   Slots 6 to 21 of a row are the 16 FaceGen visemes (the names of the head `.tri` morphs): Aah, BigAah, BMP, ChJSh,
//   DST, Eee, Eh, FV, I, K, N, Oh, OohQ, R, Th, W. The other slots are other curves and are kept, not interpreted.
//   Frame f is at time (f + first) / 30 seconds.
//
// Untrusted input: every size is capped before it is used; nothing throws.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Lip
{
    inline constexpr std::size_t kSlots = 33;
    inline constexpr std::size_t kVisemeBase = 6;
    inline constexpr std::size_t kVisemes = 16;
    inline constexpr float kFramesPerSecond = 30.f;
    inline constexpr std::array<std::string_view, kVisemes> kVisemeNames{ "Aah", "BigAah", "BMP", "ChJSh", "DST",
        "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" };

    struct Limits
    {
        std::uint64_t mMaxBytes = 4ull * 1024 * 1024; ///< the largest lip part in the game's data is 32 KiB
        std::uint64_t mMaxExpandedBytes = 4ull * 1024 * 1024; ///< decoded size
        std::uint32_t mMaxExtraHeaderBytes = 8;
    };

    struct Track
    {
        std::uint32_t mFrames = 0;
        std::int32_t mFirst = 0; ///< pre-roll in frames; meaningful only when mTimingKnown
        bool mTimingKnown = false;
        std::uint32_t mExtraHeaderBytes = 0;
        std::vector<float> mGrid; ///< mFrames * kSlots, row-major; slots the data does not reach are 0
    };

    struct Result
    {
        bool mOk = false;
        Track mTrack;
        std::string mError; ///< "name: what was wrong"; empty when mOk
    };

    [[nodiscard]] Result read(std::string_view bytes, std::string_view name = {}, const Limits& limits = {});

    /// The time of frame `frame` in seconds, or NaN when the timing is not known.
    [[nodiscard]] float frameTime(const Track& track, std::uint32_t frame);

    /// The 16 viseme weights at `seconds` of the line: linear between the two frames around it; before the first frame
    /// and after the last they are 0 (a closed mouth). Returns false (and zeros) when the timing is not known or the
    /// track has no frame. Allocates nothing.
    bool sampleVisemes(const Track& track, float seconds, std::array<float, kVisemes>& out);
}

#endif
