// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// An independent pack builder, written from the specification (docs/spec/eden-archive-format-v0.1.md) and using only
// BLAKE3 and zstd. It does not include any header of the library under test.
//
// How it works, in plain words. A pack is described by a PackSpec (entries, chunk size, flags). layoutPack() turns the
// spec into a PackImage: every record of the specification as an ordinary C++ value (header bytes, entry records,
// chunk records, indexes, the data region). A test can then change any value of the image on purpose, for example
// set a chunk's offset to a wrong number, and serialisePack() writes the bytes and recomputes the digests that the
// change would otherwise break (unless the test asks to keep them wrong). Everything is little-endian, as the
// specification says.
#ifndef EDENARCHIVE_TESTSUPPORT_EAPACKMODEL_HPP
#define EDENARCHIVE_TESTSUPPORT_EAPACKMODEL_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "eacodec.hpp"
#include "eautil.hpp"

namespace EdenTest
{
    /// Constants of the specification (sections 5, 6, 7.4, 7.7, 11), written out again on the test side.
    namespace Fmt
    {
        inline constexpr std::size_t headerSize = 512;
        inline constexpr std::size_t footerSize = 128;
        inline constexpr std::uint64_t dataOffset = 4096;
        inline constexpr std::uint64_t blockAlign = 4096;

        // Header field offsets (section 5).
        inline constexpr std::size_t hMagic = 0;
        inline constexpr std::size_t hVersionMajor = 8;
        inline constexpr std::size_t hVersionMinor = 10;
        inline constexpr std::size_t hHeaderSize = 12;
        inline constexpr std::size_t hPackFlags = 16;
        inline constexpr std::size_t hFileSize = 24;
        inline constexpr std::size_t hChunkSize = 32;
        inline constexpr std::size_t hEntryRecordSize = 36;
        inline constexpr std::size_t hChunkRecordSize = 40;
        inline constexpr std::size_t hCompressorVersion = 44;
        inline constexpr std::size_t hCompressorLevel = 48;
        inline constexpr std::size_t hReserved52 = 52;
        inline constexpr std::size_t hEntryCount = 56;
        inline constexpr std::size_t hPathEntryCount = 64;
        inline constexpr std::size_t hKeyEntryCount = 72;
        inline constexpr std::size_t hDataOffset = 80;
        inline constexpr std::size_t hDataSize = 88;
        inline constexpr std::size_t hMetadataOffset = 96;
        inline constexpr std::size_t hMetadataSize = 104;
        inline constexpr std::size_t hStringTableOffset = 112;
        inline constexpr std::size_t hStringTableSize = 120;
        inline constexpr std::size_t hEntryTableOffset = 128;
        inline constexpr std::size_t hPathIndexOffset = 136;
        inline constexpr std::size_t hKeyIndexOffset = 144;
        inline constexpr std::size_t hChunkTablesOffset = 152;
        inline constexpr std::size_t hChunkTablesSize = 160;
        inline constexpr std::size_t hProvenanceOffset = 168;
        inline constexpr std::size_t hProvenanceSize = 176;
        inline constexpr std::size_t hExtensionOffset = 184;
        inline constexpr std::size_t hExtensionSize = 192;
        inline constexpr std::size_t hGeneration = 200;
        inline constexpr std::size_t hSourceDigest = 208;
        inline constexpr std::size_t hManifestDigest = 240;
        inline constexpr std::size_t hMetadataDigest = 272;
        inline constexpr std::size_t hReserved304 = 304;
        inline constexpr std::size_t hHeaderChecksum = 480;

        // Footer field offsets (section 6), relative to the footer start.
        inline constexpr std::size_t fMagic = 0;
        inline constexpr std::size_t fVersionMajor = 8;
        inline constexpr std::size_t fVersionMinor = 10;
        inline constexpr std::size_t fFooterSize = 12;
        inline constexpr std::size_t fFileSize = 16;
        inline constexpr std::size_t fMetadataOffset = 24;
        inline constexpr std::size_t fMetadataSize = 32;
        inline constexpr std::size_t fMetadataDigest = 40;
        inline constexpr std::size_t fHeaderChecksum = 72;
        inline constexpr std::size_t fReserved = 104;

        // Entry record field offsets (section 7.4), relative to the record start.
        inline constexpr std::size_t eKey = 0;
        inline constexpr std::size_t ePathOffset = 8;
        inline constexpr std::size_t ePathLength = 16;
        inline constexpr std::size_t eType = 20;
        inline constexpr std::size_t eReserved22 = 22;
        inline constexpr std::size_t eFlags = 24;
        inline constexpr std::size_t eChunkCount = 28;
        inline constexpr std::size_t eSize = 32;
        inline constexpr std::size_t eChunkTableOffset = 40;
        inline constexpr std::size_t eContentHash = 48;
        inline constexpr std::size_t eProvenanceOffset = 80;
        inline constexpr std::size_t eHintMemory = 88;
        inline constexpr std::size_t eHintGroup = 92;
        inline constexpr std::size_t eLayoutA = 96;
        inline constexpr std::size_t eLayoutB = 100;
        inline constexpr std::size_t eReserved104 = 104;

        // Chunk record field offsets (section 7.7), relative to the record start.
        inline constexpr std::size_t cOffset = 0;
        inline constexpr std::size_t cStoredSize = 8;
        inline constexpr std::size_t cSize = 12;
        inline constexpr std::size_t cCodec = 16;
        inline constexpr std::size_t cFlags = 18;
        inline constexpr std::size_t cReserved = 20;
        inline constexpr std::size_t cHash = 24;

        // Flag bits (sections 11.4 to 11.6).
        inline constexpr std::uint64_t packDerived = 1ull << 0;
        inline constexpr std::uint32_t entryHasPath = 1u << 0;
        inline constexpr std::uint32_t entryHasKey = 1u << 1;
        inline constexpr std::uint32_t entryHasProvenance = 1u << 2;
        inline constexpr std::uint32_t entryPinned = 1u << 16;
        inline constexpr std::uint32_t entryCoarseFirst = 1u << 17;
        inline constexpr std::uint32_t entryHintGroup = 1u << 18;
        inline constexpr std::uint32_t entryHintMemSize = 1u << 19;
        inline constexpr std::uint16_t chunkHashPresent = 1u << 0;

        inline constexpr std::uint32_t defaultChunkSize = 131072;
    }

    /// What to do with the bytes of a chunk when the builder encodes it.
    enum class CodecChoice
    {
        Store, // codec 0
        Zstd, // codec 1, made by libzstd, content checksum off
        ZstdChecksum, // codec 1, made by libzstd, content checksum on
        HandRle, // codec 1, a hand-built run-length frame (the chunk must be one repeated byte)
        Auto, // the writer's rule of 9.2: zstd when it saves at least 1/16, else stored
    };

    /// A chunk written on purpose (instead of the default cutting).
    struct ChunkSpec
    {
        Bytes mRaw; // the chunk's content
        CodecChoice mChoice = CodecChoice::Store;
        std::optional<Bytes> mStored; // use these as the stored bytes instead of encoding mRaw
        std::optional<std::uint16_t> mCodecId; // write this codec id in the record (default from mChoice)
        std::uint64_t mGapBefore = 0; // zero bytes between the end of the previous chunk (or the block start) and it
    };

    struct ProvenanceSpec
    {
        std::string mToolId;
        Digest mSourceDigest{};
        Bytes mParameters;
    };

    struct EntrySpec
    {
        std::optional<std::string> mPath;
        std::optional<std::uint64_t> mKey;
        std::uint16_t mType = 0;
        /// OR-ed into the entry flags (the builder sets HAS_PATH, HAS_KEY and HAS_PROVENANCE itself).
        std::uint32_t mExtraFlags = 0;
        std::uint32_t mHintMemorySize = 0;
        std::uint32_t mHintGroup = 0;
        std::uint32_t mLayoutParamA = 0;
        std::uint32_t mLayoutParamB = 0;
        Bytes mContent;
        CodecChoice mCodec = CodecChoice::Store;
        /// When non-empty the content is these chunks, in this order, and mContent is ignored.
        std::vector<ChunkSpec> mChunks;
        /// Extra zero bytes before this entry's block, on top of the 4096 alignment.
        std::uint64_t mBlockGap = 0;
        std::optional<ProvenanceSpec> mProvenance;
    };

    struct ExtensionBlockSpec
    {
        std::uint32_t mId = 0;
        std::uint32_t mFlags = 0;
        Bytes mPayload;
    };

    struct PackImage;

    struct PackSpec
    {
        std::vector<EntrySpec> mEntries;
        std::uint32_t mChunkSize = Fmt::defaultChunkSize;
        std::uint64_t mPackFlags = 0;
        std::uint64_t mGeneration = 0;
        Digest mSourceDigest{};
        std::uint32_t mEntryRecordSize = 112;
        std::uint32_t mChunkRecordSize = 56;
        std::uint8_t mRecordPadByte = 0; // fills the bytes of a record beyond the 112 or 56 the specification defines
        int mZstdLevel = 3;
        /// Header values; the default is the zstd library's version and mZstdLevel when a chunk uses codec 1, else 0.
        std::optional<std::uint32_t> mCompressorVersion;
        std::optional<std::int32_t> mCompressorLevel;
        std::vector<ExtensionBlockSpec> mExtensions;
        /// True: the builder puts the entries in the canonical order (7.4). False: keeps the order given.
        bool mSortEntries = true;
        /// Called on the laid-out image before it is written; may change any value.
        std::function<void(PackImage&)> mMutate;
    };

    struct EntryRec
    {
        std::uint64_t mKey = 0;
        std::uint64_t mPathOffset = 0;
        std::uint32_t mPathLength = 0;
        std::uint16_t mType = 0;
        std::uint16_t mReserved22 = 0;
        std::uint32_t mFlags = 0;
        std::uint32_t mChunkCount = 0;
        std::uint64_t mSize = 0;
        std::uint64_t mChunkTableOffset = 0;
        Digest mContentHash{};
        std::uint64_t mProvenanceOffset = 0;
        std::uint32_t mHintMemorySize = 0;
        std::uint32_t mHintGroup = 0;
        std::uint32_t mLayoutParamA = 0;
        std::uint32_t mLayoutParamB = 0;
        std::uint64_t mReserved104 = 0;
    };

    struct ChunkRec
    {
        std::uint64_t mOffset = 0;
        std::uint32_t mStoredSize = 0;
        std::uint32_t mSize = 0;
        std::uint16_t mCodec = 0;
        std::uint16_t mFlags = Fmt::chunkHashPresent;
        std::uint32_t mReserved20 = 0;
        Digest mHash{};
    };

    struct IndexRec
    {
        std::uint64_t mFirst = 0; // pathHash or key
        std::uint64_t mEntryIndex = 0;
    };

    /// Every region of a pack as plain values. The vectors hold the records; the strides say how the entry and chunk
    /// records are padded when written.
    struct PackImage
    {
        Bytes mHeader; // 512 bytes; the metadata digest, manifest digest and checksum are filled in when written
        Bytes mFooter; // 128 bytes; filled from the header when written unless mKeepFooter
        Bytes mBody; // the file from byte 0 to metadataOffset (the header slot is overwritten when written)
        Bytes mStringTable;
        std::vector<EntryRec> mEntries;
        std::vector<IndexRec> mPathIndex;
        std::vector<IndexRec> mKeyIndex;
        std::vector<ChunkRec> mChunks;
        Bytes mProvenance;
        Bytes mExtension;
        std::size_t mEntryStride = 112;
        std::size_t mChunkStride = 56;
        std::uint8_t mPadByte = 0;
        // Set one of these to leave the matching field exactly as the test put it, wrong or not.
        bool mKeepManifest = false;
        bool mKeepMetadataDigest = false;
        bool mKeepHeaderChecksum = false;
        bool mKeepFooter = false;
    };

    /// The entries of a spec in the order they get in the pack: with mSortEntries, entries with a path sorted by
    /// path (7.3.2), then entries with only a key sorted by key.
    std::vector<EntrySpec> entryOrder(const PackSpec& spec);

    /// The path order of 7.3.2: bytewise unsigned, shorter first on a common prefix. Negative, zero or positive.
    int comparePaths(const std::string& a, const std::string& b);

    PackImage layoutPack(const PackSpec& spec);

    /// Recomputes the offsets and sizes of the metadata sub-regions, metadataSize, metadataOffset and fileSize in the
    /// header from the vectors of the image (not the counts, the data region or dataSize).
    void relayoutRegions(PackImage& image);

    /// Sets every entry's chunkTableOffset and provenanceOffset from the header's region offsets, the chunk counts and
    /// the provenance records, as a correct writer would (call it after relayoutRegions when a change moved a region).
    void fixDerivedOffsets(PackImage& image);

    /// Rebuilds the path index from the entries and string table of the image.
    void rebuildPathIndex(PackImage& image);
    /// Rebuilds the key index from the entries of the image.
    void rebuildKeyIndex(PackImage& image);

    /// Writes the pack: body, metadata, footer, with the digests recomputed (see the mKeep flags).
    Bytes serialisePack(const PackImage& image);

    /// layoutPack, then spec.mMutate if set, then serialisePack.
    Bytes buildPack(const PackSpec& spec);

    /// The manifest digest of section 13.5 computed from an image (needs the header for the pack-level fields).
    Digest manifestDigestOf(const PackImage& image);

    /// Absolute file offset of field `fieldOffset` of entry record `index` in the image's entry table.
    std::uint64_t entryFieldOffset(const PackImage& image, std::size_t index, std::size_t fieldOffset);
    /// Absolute file offset of field `fieldOffset` of chunk record `index` in the image's chunk tables.
    std::uint64_t chunkFieldOffset(const PackImage& image, std::size_t index, std::size_t fieldOffset);

    /// The encoded form of an entry record or chunk record (stride bytes, padded with `pad`).
    Bytes encodeEntryRec(const EntryRec& rec, std::size_t stride, std::uint8_t pad);
    Bytes encodeChunkRec(const ChunkRec& rec, std::size_t stride, std::uint8_t pad);

    /// One provenance record as 7.8 lays it out, padded to a multiple of 8.
    Bytes encodeProvenance(const ProvenanceSpec& spec);

    // ---- byte-level patching of a finished pack (Annex C) ----

    struct Recompute
    {
        bool mPathIndex = false; // rebuild the path index from the entry table and string table
        bool mMetadataDigest = true;
        bool mHeaderChecksum = true;
        bool mFooter = true; // copy version, metadataOffset, metadataSize, metadataDigest and headerChecksum
    };

    /// Writes `value` (little-endian, `width` bytes) at `offset` of a finished pack.
    void patch(Bytes& pack, std::size_t offset, std::uint64_t value, unsigned width);

    /// Recomputes, in this order, the path index, the metadata digest, the header checksum and the footer copies from
    /// the header the bytes hold. The footer's own fileSize and footerSize are never touched.
    void recompute(Bytes& pack, const Recompute& what = {});

    // ---- Annex B ----

    /// The pack of Annex B described as a spec (3 entries; entry 0's frame is hand-built).
    PackSpec annexBSpec();
    /// The bytes the builder makes from annexBSpec().
    Bytes annexBPack();
    /// The bytes of the hexadecimal dump of Annex B, parsed from the specification text embedded in the sources.
    Bytes annexBGolden();
}

#endif
