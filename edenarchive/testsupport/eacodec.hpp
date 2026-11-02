// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// BLAKE3 and zstd for the independent test builder. These call blake3.h and zstd.h directly, not the library under
// test, so a bug in the library's own hashing or framing cannot hide in both sides.
#ifndef EDENARCHIVE_TESTSUPPORT_EACODEC_HPP
#define EDENARCHIVE_TESTSUPPORT_EACODEC_HPP

#include <optional>
#include <string>
#include <string_view>

#include "eautil.hpp"

namespace EdenTest
{
    /// BLAKE3-256 of a byte range.
    Digest blake3Of(const void* data, std::size_t size);
    inline Digest blake3Of(const Bytes& bytes)
    {
        return blake3Of(bytes.data(), bytes.size());
    }

    /// The BLAKE3-256 of zero bytes.
    const Digest& emptyDigest();

    /// First 8 bytes of the BLAKE3-256 of the path, as a little-endian number (specification 7.3.3).
    std::uint64_t pathHashOf(std::string_view path);

    std::string hexOf(const Digest& digest);
    /// Parses 64 hexadecimal digits. Returns false for anything else.
    bool digestFromHex(std::string_view text, Digest& out);

    /// A whole zstd frame made by libzstd (single call, one thread, content size written). Returns an empty vector on
    /// failure.
    Bytes zstdCompress(const void* data, std::size_t size, int level, bool contentChecksum);

    /// Decodes one frame with libzstd into exactly `expectedSize` bytes. Returns false on any problem.
    bool zstdDecompress(const Bytes& frame, std::size_t expectedSize, Bytes& out);

    /// The version number of the zstd library this program was built with (ZSTD_VERSION_NUMBER).
    std::uint32_t zstdVersionNumber();

    /// Knobs for a hand-built zstd frame (specification Annex B and section 9.2). The defaults give a normal frame.
    struct FrameOptions
    {
        /// Single_Segment_Flag: the window equals the content size and there is no window descriptor.
        bool mSingleSegment = true;
        /// Used when mSingleSegment is false: the window is 2^mWindowLog bytes (10 to 41).
        unsigned mWindowLog = 17;
        /// False writes a frame without Frame_Content_Size (only possible when mSingleSegment is false).
        bool mWriteContentSize = true;
        /// The content size written into the header; empty means the real count.
        std::optional<std::uint64_t> mDeclaredSize;
        /// A Dictionary_ID to write (0 writes a field holding zero).
        std::optional<std::uint32_t> mDictionaryId;
    };

    /// A zstd frame made only of run-length blocks: `count` copies of `value`. Annex B's entry 0 is
    /// makeRleFrame('A', 64). Blocks are at most 128 KiB each.
    Bytes makeRleFrame(std::uint8_t value, std::uint64_t count, const FrameOptions& options = {});

    /// A skippable frame (magic 0x184D2A50 + magicLow, 0 to 15) holding `payload`.
    Bytes makeSkippableFrame(const Bytes& payload, unsigned magicLow = 0);
}

#endif
