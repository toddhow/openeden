// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_ARCHIVE_HPP
#define EDENARCHIVE_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "error.hpp"
#include "hash.hpp"
#include "source.hpp"

namespace EdenArchive
{
    /// Codec ids (specification section 11.1).
    inline constexpr std::uint16_t codecStore = 0;
    inline constexpr std::uint16_t codecZstd = 1;

    /// Entry flag bits (section 11.5).
    inline constexpr std::uint32_t entryFlagHasPath = 1u << 0;
    inline constexpr std::uint32_t entryFlagHasKey = 1u << 1;
    inline constexpr std::uint32_t entryFlagHasProvenance = 1u << 2;
    inline constexpr std::uint32_t entryFlagPinned = 1u << 16;
    inline constexpr std::uint32_t entryFlagCoarseFirst = 1u << 17;
    inline constexpr std::uint32_t entryFlagHintGroup = 1u << 18;
    inline constexpr std::uint32_t entryFlagHintMemSize = 1u << 19;

    /// Pack flag bits (section 11.4).
    inline constexpr std::uint64_t packFlagDerived = 1u << 0;

    /// The decoded header of a pack (section 5).
    struct PackInfo
    {
        std::uint16_t mVersionMajor = 0;
        std::uint16_t mVersionMinor = 0;
        std::uint64_t mPackFlags = 0;
        std::uint64_t mFileSize = 0;
        std::uint32_t mChunkSize = 0;
        std::uint32_t mEntryRecordSize = 0;
        std::uint32_t mChunkRecordSize = 0;
        std::uint32_t mCompressorVersion = 0;
        std::int32_t mCompressorLevel = 0;
        std::uint64_t mEntryCount = 0;
        std::uint64_t mPathEntryCount = 0;
        std::uint64_t mKeyEntryCount = 0;
        std::uint64_t mDataOffset = 0;
        std::uint64_t mDataSize = 0;
        std::uint64_t mMetadataOffset = 0;
        std::uint64_t mMetadataSize = 0;
        std::uint64_t mStringTableOffset = 0;
        std::uint64_t mStringTableSize = 0;
        std::uint64_t mEntryTableOffset = 0;
        std::uint64_t mPathIndexOffset = 0;
        std::uint64_t mKeyIndexOffset = 0;
        std::uint64_t mChunkTablesOffset = 0;
        std::uint64_t mChunkTablesSize = 0;
        std::uint64_t mProvenanceOffset = 0;
        std::uint64_t mProvenanceSize = 0;
        std::uint64_t mExtensionOffset = 0;
        std::uint64_t mExtensionSize = 0;
        std::uint64_t mGeneration = 0;
        Hash mSourceDigest{};
        Hash mManifestDigest{};
        Hash mMetadataDigest{};
        Hash mHeaderChecksum{};
    };

    /// One entry record (section 7.4), decoded. mPath points into the open archive and is valid while it lives.
    struct EntryInfo
    {
        std::size_t mIndex = 0;
        bool mHasPath = false;
        bool mHasKey = false;
        bool mHasProvenance = false;
        std::string_view mPath;
        std::uint64_t mKey = 0;
        std::uint16_t mType = 0;
        std::uint32_t mFlags = 0;
        std::uint32_t mChunkCount = 0;
        std::uint64_t mSize = 0;
        Hash mContentHash{};
        std::uint32_t mHintMemorySize = 0;
        std::uint32_t mHintGroup = 0;
        std::uint32_t mLayoutParamA = 0;
        std::uint32_t mLayoutParamB = 0;
    };

    /// One chunk record (section 7.7), decoded.
    struct ChunkInfo
    {
        std::uint64_t mOffset = 0;
        std::uint32_t mStoredSize = 0;
        std::uint32_t mSize = 0;
        std::uint16_t mCodec = 0;
        std::uint16_t mFlags = 0;
        Hash mHash{};
    };

    /// A provenance record (section 7.8). The views point into the open archive.
    struct ProvenanceInfo
    {
        std::string_view mToolId;
        Hash mSourceDigest{};
        std::span<const std::uint8_t> mParameters;
    };

    struct OpenOptions
    {
        /// Check the metadata digest (section 14.2 item 7). Only a caller that just wrote the pack itself should
        /// turn this off.
        bool mVerifyMetadataDigest = true;
        /// A lower cap than the specification's 2^32 for the metadata region; open holds the whole region in memory.
        /// A reader MAY be configured with lower limits (section 12); it never accepts higher ones.
        std::uint64_t mMaxMetadataSize = std::uint64_t{ 1 } << 30;
    };

    /// A budget of decoded bytes (section 15.3). Reads that would exceed it fail with ErrorClass::Limit. Not
    /// thread-safe: give each thread its own.
    struct DecodeBudget
    {
        std::uint64_t mRemaining = ~std::uint64_t{ 0 };
    };

    /// An open, validated pack. Immutable after open: every const member function may be called from several threads
    /// at once (reads use positional reads and thread-local scratch buffers). No function throws.
    class Archive
    {
    public:
        ~Archive();
        Archive(const Archive&) = delete;
        Archive& operator=(const Archive&) = delete;

        /// Opens and checks a pack (section 14.2, items 1 to 12). On failure returns nullptr and sets `error`;
        /// on success `error` is cleared. The archive keeps a shared reference to the source.
        static std::unique_ptr<Archive> open(
            std::shared_ptr<const Source> source, const OpenOptions& options, Error& error) noexcept;
        static std::unique_ptr<Archive> open(
            const std::filesystem::path& path, const OpenOptions& options, Error& error) noexcept;

        const PackInfo& info() const noexcept;
        const Source& source() const noexcept;
        std::size_t entryCount() const noexcept;

        /// index must be below entryCount().
        EntryInfo entry(std::size_t index) const noexcept;
        /// chunkIndex must be below entry(entryIndex).mChunkCount.
        ChunkInfo chunk(std::size_t entryIndex, std::uint32_t chunkIndex) const noexcept;
        std::optional<ProvenanceInfo> provenance(std::size_t entryIndex) const noexcept;

        /// Lookup by normalized path (section 14.5): path hash, binary search, then a byte comparison.
        std::optional<std::size_t> findPath(std::string_view normalizedPath) const noexcept;
        /// Lookup by key: binary search of the key index.
        std::optional<std::size_t> findKey(std::uint64_t key) const noexcept;

        /// Copies [offset, offset + size) of the entry's content into `destination` (section 14.3). Decodes and
        /// verifies only the chunks that overlap the range, each against its chunk hash before any byte is copied.
        /// A damaged or unsupported chunk fails only reads that touch it. A zero-length read is valid at any offset
        /// up to and including the entry's size.
        Error readRange(std::size_t entryIndex, std::uint64_t offset, std::size_t size, void* destination,
            DecodeBudget* budget = nullptr) const noexcept;

        /// Reads the whole entry and checks its content hash.
        Error readEntry(
            std::size_t entryIndex, std::vector<std::uint8_t>& out, DecodeBudget* budget = nullptr) const noexcept;

        /// Decodes every chunk of the entry, verifies each chunk hash and the content hash, without keeping the bytes.
        Error verifyEntry(std::size_t entryIndex, DecodeBudget* budget = nullptr) const noexcept;

        struct Impl;
        /// Use open(); public only so that std::make_unique can build it.
        explicit Archive(std::unique_ptr<Impl> impl) noexcept;
        const Impl& impl() const noexcept { return *mImpl; }

    private:
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
