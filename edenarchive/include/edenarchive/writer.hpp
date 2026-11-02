// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_WRITER_HPP
#define EDENARCHIVE_WRITER_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "error.hpp"
#include "hash.hpp"

namespace EdenArchive
{
    /// What a writer may do with an entry's chunks (section 9.2, "Choosing the codec").
    enum class Compression : std::uint8_t
    {
        Auto, // zstd when it saves at least 1/16, else stored
        Store, // always stored
    };

    struct ProvenanceInput
    {
        std::string mToolId; // 1 to 255 bytes of UTF-8, no NUL
        Hash mSourceDigest{};
        std::vector<std::uint8_t> mParameters; // at most 60,000 bytes
    };

    /// One file to put in a pack. If mFile is not empty the writer reads that file (it is never modified or
    /// deleted), otherwise it uses mData.
    struct InputEntry
    {
        std::optional<std::string> mPath; // normalized, section 7.3.1; the writer does NOT repair names
        std::optional<std::uint64_t> mKey;
        std::uint16_t mType = 0;
        std::uint32_t mHintFlags = 0; // entry flag bits 16 to 19 only (PINNED, COARSE_FIRST, HINT_GROUP, HINT_MEMSIZE)
        std::uint32_t mHintMemorySize = 0;
        std::uint32_t mHintGroup = 0;
        std::uint32_t mLayoutParamA = 0;
        std::uint32_t mLayoutParamB = 0;
        Compression mCompression = Compression::Auto;
        std::vector<std::uint8_t> mData;
        std::filesystem::path mFile;
        std::optional<ProvenanceInput> mProvenance;
    };

    struct WriteOptions
    {
        std::uint32_t mChunkSize = 131072; // power of two, 4096 to 8 MiB
        int mZstdLevel = 3; // section 17.4
        unsigned mThreads = 1; // 0 means the hardware concurrency; the output never depends on it
        /// Store mode (the repacking default): every chunk is stored (codec 0), whatever the entry's mCompression.
        /// Optimise mode (false): entries with Compression::Auto may use zstd.
        bool mStoreOnly = false;
        bool mDerived = false;
        std::uint64_t mGeneration = 0;
        Hash mSourceDigest{};
        /// Bytes of input held in memory at once while chunks are compressed (a soft cap).
        std::uint64_t mWorkWindow = std::uint64_t{ 64 } << 20;
    };

    /// Writes a canonical pack (section 17.2). The order of `entries` does not matter; the entries are validated
    /// (names, duplicates, limits) and sorted. The pack is written to a temporary name next to `output` and renamed
    /// over it only on success; `output` is never edited in place. Never throws.
    Error writePack(const std::vector<InputEntry>& entries, const std::filesystem::path& output,
        const WriteOptions& options) noexcept;

    /// The same, into memory (tests, tools that stream).
    Error writePackToMemory(const std::vector<InputEntry>& entries, std::vector<std::uint8_t>& output,
        const WriteOptions& options) noexcept;
}

#endif
