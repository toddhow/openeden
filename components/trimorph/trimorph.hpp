#ifndef OPENEDEN_COMPONENTS_TRIMORPH_TRIMORPH_H
#define OPENEDEN_COMPONENTS_TRIMORPH_TRIMORPH_H

// Task 0146 (slice 2): the FaceGen `.tri` morph file of Bethesda heads (magic "FRTRI003") and the weighted sum of its
// morph deltas. Formats layer: standard library only, no engine, renderer or game rule.
//
// Layout (provenance: docs/tasks/0146-skyrim-lip-sync.md section 2.2). Written first from memory, then VERIFIED on the
// user's Skyrim SE files `femalehead.tri`, `malehead.tri` and `maleheadargonian.tri`: reading with this layout ends
// EXACTLY at the end of each file, and the biggest mover of "Aah" is on the midline below the nose, moving down and
// back (a jaw opening), which settles that the deltas are interleaved x, y, z per vertex.
//
//   0   char[8]  "FRTRI003"
//   8   i32 x 12 vertices, triangles, quads, label vertices, surface label vertices, texture coordinates, flags,
//                morphs, static morphs, modifiers, modifier vertices, reserved
//   56  8 bytes  not read (zero in the data)
//   64  vertices  3 x f32 each
//       triangles 3 x i32 each
//       when (flags & 1): texture coordinates 2 x f32 each, then one triangle of 3 x i32 per triangle
//       morphs    each: i32 name length, the name (a trailing NUL, if any, is dropped), f32 scale,
//                 3 x i16 per vertex (a delta is value * scale, in the units of the vertices)
//
// Only what the data contains is accepted: files with quads, label vertices, static morphs or modifiers are refused
// (their layouts were never seen; "I don't know" beats a guess). Untrusted input: every count is capped and checked
// against the bytes present before anything is allocated, nothing is read outside the input, nothing throws.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TriMorph
{
    struct Limits
    {
        std::uint64_t mMaxBytes = 64ull * 1024 * 1024; ///< the largest head file in the game's data is under 600 KiB
        std::uint32_t mMaxVertices = 100000; ///< the data's heads have under 1,500
        std::uint32_t mMaxTriangles = 200000;
        std::uint32_t mMaxMorphsInFile = 1024; ///< the data's heads have 44 to 47
        std::uint32_t mMaxNameLength = 64;
    };

    struct Morph
    {
        std::string mName;
        float mScale = 0.f;
        std::vector<std::int16_t> mDeltas; ///< three per vertex (x, y, z), raw; the delta is value * mScale
    };

    struct File
    {
        std::uint32_t mVertices = 0;
        std::uint32_t mMorphsInFile = 0;
        std::vector<Morph> mMorphs; ///< those kept (see `read`), in file order
        std::uint64_t mTrailingBytes = 0; ///< bytes after the last morph (zero in the data)
    };

    struct Result
    {
        bool mOk = false;
        File mFile;
        std::string mError; ///< "name: what was wrong (offset N)"; empty when mOk
    };

    /// Parses `bytes`. `wanted`: names of the morphs to keep, compared without regard to case; empty keeps every morph.
    /// A name that is not in the file is simply not kept (see File::mMorphs).
    [[nodiscard]] Result read(std::string_view bytes, std::string_view name = {},
        std::span<const std::string_view> wanted = {}, const Limits& limits = {});

    /// One term of the weighted sum: `mMorph` indexes File::mMorphs.
    struct Term
    {
        std::uint32_t mMorph = 0;
        float mWeight = 0.f;
    };

    /// out[i] = base[i] + sum over terms of weight * scale * delta[i]. `base` and `out` must have File::mVertices
    /// elements each and may be the same storage only if equal; every term must index a kept morph with a full set of
    /// deltas, else nothing is written and false is returned. Terms with a non-finite weight are skipped. Allocates
    /// nothing; deterministic (terms are summed in the order given).
    bool apply(const File& file, std::span<const std::array<float, 3>> base, std::span<const Term> terms,
        std::span<std::array<float, 3>> out);
}

#endif
