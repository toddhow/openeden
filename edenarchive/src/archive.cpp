// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The reader: opens a pack with every check of section 14.2 of the specification and reads entries (14.3).
// Everything in a pack is untrusted. Each number is checked before it is used, sums and products are checked for
// overflow or bounded by a limit first, and nothing is allocated from a size that has not passed its limit.
#include <edenarchive/archive.hpp>

#include <algorithm>
#include <cstring>
#include <new>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "archiveimpl.hpp"
#include "format.hpp"
#include "utf8.hpp"

// The pinned zstd (docs/dependencies.md): a header of another version on the include path must not compile.
static_assert(ZSTD_VERSION_NUMBER == 10507, "the pinned zstd 1.5.7 header is expected");

namespace EdenArchive
{
    namespace
    {
        namespace F = Format;

        Error makeError(ErrorClass errorClass, std::uint64_t offset, const char* field, std::string message)
        {
            Error error;
            error.mClass = errorClass;
            error.mOffset = offset;
            error.mField = field;
            error.mMessage = std::move(message);
            return error;
        }

        Error ioFailure(std::uint64_t offset, const char* field)
        {
            return makeError(ErrorClass::Io, offset, field, "the source could not be read");
        }

        std::string indexed(const char* what, std::uint64_t index, const char* field)
        {
            return std::string(what) + "[" + std::to_string(index) + "]." + field;
        }

        // Returns the absolute offset of the first non-zero byte in [begin, end), or `end` if all are zero.
        // `failed` is set if the source cannot be read.
        std::uint64_t firstNonZero(const Source& source, std::uint64_t begin, std::uint64_t end, bool& failed)
        {
            failed = false;
            std::uint8_t buffer[4096];
            std::uint64_t position = begin;
            while (position < end)
            {
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof(buffer), end - position));
                if (!source.read(position, buffer, n))
                {
                    failed = true;
                    return position;
                }
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (buffer[i] != 0)
                        return position + i;
                }
                position += n;
            }
            return end;
        }

        // Thread-local buffers for chunk reads, so a read allocates nothing once they have grown to the sizes in use
        // (at most 16 MiB of stored bytes and 8 MiB of decoded bytes, the limits of section 12).
        struct Scratch
        {
            std::vector<std::uint8_t> mStored;
            std::vector<std::uint8_t> mDecoded;
            ZSTD_DCtx* mContext = nullptr;

            Scratch() = default;
            Scratch(const Scratch&) = delete;
            Scratch& operator=(const Scratch&) = delete;
            ~Scratch() { ZSTD_freeDCtx(mContext); }
        };

        Scratch& scratch()
        {
            thread_local Scratch instance;
            return instance;
        }

        // The checks of section 14.2 item 1 to 12, in order. Fills `impl` on success.
        class Opener
        {
        public:
            Opener(Archive::Impl& impl, const OpenOptions& options)
                : mImpl(impl)
                , mOptions(options)
                , mInfo(impl.mInfo)
            {
            }

            Error run(std::shared_ptr<const Source> source);

        private:
            Error identification();
            Error versionAndSizes();
            Error checksums();
            Error fileSize();
            Error countsAndStrides();
            Error regions();
            Error metadataDigest();
            Error extensionBlocks();
            Error entries();
            Error chunks();
            Error pathIndex();
            Error keyIndex();

            Archive::Impl& mImpl;
            const OpenOptions& mOptions;
            PackInfo& mInfo;
            const Source* mSource = nullptr;
            std::uint64_t mSize = 0;
            std::uint8_t mHeader[512] = {};
            std::uint8_t mFooter[128] = {};
            std::uint64_t mFooterOffset = 0;
            std::uint64_t mDataEnd = 0;
            std::uint64_t mTotalChunks = 0;
        };

        Error Opener::run(std::shared_ptr<const Source> source)
        {
            mImpl.mSource = std::move(source);
            mSource = mImpl.mSource.get();
            mSize = mSource->size();
            for (const auto step :
                { &Opener::identification, &Opener::versionAndSizes, &Opener::checksums, &Opener::fileSize,
                    &Opener::countsAndStrides, &Opener::regions, &Opener::metadataDigest, &Opener::extensionBlocks,
                    &Opener::entries, &Opener::chunks, &Opener::pathIndex, &Opener::keyIndex })
            {
                if (Error error = (this->*step)())
                    return error;
            }
            return Error{};
        }

        // Item 1.
        Error Opener::identification()
        {
            if (mSize < 7)
                return makeError(ErrorClass::Magic, 0, "magic", "the file is shorter than the magic");
            std::uint8_t head[8] = {};
            if (!mSource->read(0, head, mSize < 8 ? 7 : 8))
                return ioFailure(0, "magic");
            if (std::memcmp(head, F::magic, 7) != 0)
                return makeError(ErrorClass::Magic, 0, "magic", "the file does not begin with EDENPAK");
            if (mSize < 8)
                return makeError(ErrorClass::Truncated, 7, "magic", "the file ends inside the magic");
            if (head[7] != F::magic[7])
                return makeError(ErrorClass::Version, 7, "magic", "unsupported container generation");
            if (mSize < F::headerSize + F::footerSize)
                return makeError(
                    ErrorClass::Truncated, 0, "fileSize", "the file is shorter than a header and a footer");
            mFooterOffset = mSize - F::footerSize;
            if (!mSource->read(mFooterOffset, mFooter, sizeof(mFooter)))
                return ioFailure(mFooterOffset, "footer");
            if (std::memcmp(mFooter, F::footerMagic, 8) != 0)
                return makeError(
                    ErrorClass::Truncated, mFooterOffset, "footerMagic", "the footer is missing or not at the end");
            if (!mSource->read(0, mHeader, sizeof(mHeader)))
                return ioFailure(0, "header");
            return Error{};
        }

        // Item 2.
        Error Opener::versionAndSizes()
        {
            mInfo.mVersionMajor = F::rd16(mHeader + F::H::versionMajor);
            mInfo.mVersionMinor = F::rd16(mHeader + F::H::versionMinor);
            if (mInfo.mVersionMajor != 0)
                return makeError(ErrorClass::Version, F::H::versionMajor, "versionMajor",
                    "this reader supports exactly version 0.1");
            if (mInfo.mVersionMinor != 1)
                return makeError(ErrorClass::Version, F::H::versionMinor, "versionMinor",
                    "this reader supports exactly version 0.1");
            if (F::rd32(mHeader + F::H::headerSize) != F::headerSize)
                return makeError(ErrorClass::Version, F::H::headerSize, "headerSize", "headerSize must be 512");
            if (F::rd32(mFooter + F::F::footerSize) != F::footerSize)
                return makeError(
                    ErrorClass::Range, mFooterOffset + F::F::footerSize, "footerSize", "footerSize must be 128");
            return Error{};
        }

        // Item 3.
        Error Opener::checksums()
        {
            const Hash checksum = blake3(mHeader, F::H::checksum);
            if (std::memcmp(checksum.data(), mHeader + F::H::checksum, 32) != 0)
                return makeError(
                    ErrorClass::Checksum, F::H::checksum, "headerChecksum", "the header checksum is wrong");

            struct Pair
            {
                std::size_t mHeader, mFooter, mSize;
                const char* mName;
            };
            static const Pair pairs[] = {
                { F::H::versionMajor, F::F::versionMajor, 2, "versionMajor" },
                { F::H::versionMinor, F::F::versionMinor, 2, "versionMinor" },
                { F::H::fileSize, F::F::fileSize, 8, "fileSize" },
                { F::H::metadataOffset, F::F::metadataOffset, 8, "metadataOffset" },
                { F::H::metadataSize, F::F::metadataSize, 8, "metadataSize" },
                { F::H::metadataDigest, F::F::metadataDigest, 32, "metadataDigest" },
                { F::H::checksum, F::F::headerChecksum, 32, "headerChecksum" },
            };
            for (const Pair& pair : pairs)
            {
                if (std::memcmp(mHeader + pair.mHeader, mFooter + pair.mFooter, pair.mSize) != 0)
                    return makeError(ErrorClass::Checksum, mFooterOffset + pair.mFooter, pair.mName,
                        "the footer does not agree with the header");
            }
            mInfo.mFileSize = F::rd64(mHeader + F::H::fileSize);
            std::memcpy(mInfo.mMetadataDigest.data(), mHeader + F::H::metadataDigest, 32);
            std::memcpy(mInfo.mHeaderChecksum.data(), mHeader + F::H::checksum, 32);
            return Error{};
        }

        // Item 4.
        Error Opener::fileSize()
        {
            if (mSize > F::maxFileSize)
                return makeError(ErrorClass::Limit, 0, "fileSize", "the file is larger than 2^44 bytes");
            if (mInfo.mFileSize != mSize)
                return makeError(ErrorClass::Truncated, F::H::fileSize, "fileSize", "fileSize is not the real size");
            return Error{};
        }

        // Item 5.
        Error Opener::countsAndStrides()
        {
            mInfo.mPackFlags = F::rd64(mHeader + F::H::packFlags);
            if ((mInfo.mPackFlags & 0xFFFFFFFEull) != 0)
                return makeError(
                    ErrorClass::Unsupported, F::H::packFlags, "packFlags", "an unknown critical pack flag");
            mInfo.mChunkSize = F::rd32(mHeader + F::H::chunkSize);
            if (mInfo.mChunkSize < 4096 || mInfo.mChunkSize > F::maxChunkSize
                || (mInfo.mChunkSize & (mInfo.mChunkSize - 1)) != 0)
                return makeError(ErrorClass::Range, F::H::chunkSize, "chunkSize",
                    "chunkSize is not a power of two from 4096 to 8 MiB");
            mInfo.mEntryRecordSize = F::rd32(mHeader + F::H::entryRecordSize);
            if (mInfo.mEntryRecordSize % 8 != 0 || mInfo.mEntryRecordSize < F::entryRecordSize
                || mInfo.mEntryRecordSize > F::maxEntryStride)
                return makeError(ErrorClass::Range, F::H::entryRecordSize, "entryRecordSize",
                    "entryRecordSize must be a multiple of 8 from 112 to 512");
            mInfo.mChunkRecordSize = F::rd32(mHeader + F::H::chunkRecordSize);
            if (mInfo.mChunkRecordSize % 8 != 0 || mInfo.mChunkRecordSize < F::chunkRecordSize
                || mInfo.mChunkRecordSize > F::maxChunkStride)
                return makeError(ErrorClass::Range, F::H::chunkRecordSize, "chunkRecordSize",
                    "chunkRecordSize must be a multiple of 8 from 56 to 256");
            mInfo.mCompressorVersion = F::rd32(mHeader + F::H::compressorVersion);
            mInfo.mCompressorLevel = static_cast<std::int32_t>(F::rd32(mHeader + F::H::compressorLevel));
            mInfo.mEntryCount = F::rd64(mHeader + F::H::entryCount);
            if (mInfo.mEntryCount > F::maxEntries)
                return makeError(ErrorClass::Limit, F::H::entryCount, "entryCount", "more than 2^22 entries");
            mInfo.mPathEntryCount = F::rd64(mHeader + F::H::pathEntryCount);
            mInfo.mKeyEntryCount = F::rd64(mHeader + F::H::keyEntryCount);
            if (mInfo.mPathEntryCount > mInfo.mEntryCount)
                return makeError(
                    ErrorClass::Range, F::H::pathEntryCount, "pathEntryCount", "more path entries than entries");
            if (mInfo.mKeyEntryCount > mInfo.mEntryCount)
                return makeError(
                    ErrorClass::Range, F::H::keyEntryCount, "keyEntryCount", "more key entries than entries");
            if (mInfo.mEntryCount > mInfo.mPathEntryCount + mInfo.mKeyEntryCount)
                return makeError(ErrorClass::Range, F::H::entryCount, "entryCount",
                    "more entries than path entries and key entries together");
            return Error{};
        }

        // Item 6.
        Error Opener::regions()
        {
            auto u64 = [this](std::size_t at) { return F::rd64(mHeader + at); };
            mInfo.mMetadataSize = u64(F::H::metadataSize);
            if (mInfo.mMetadataSize > F::maxMetadataSize)
                return makeError(ErrorClass::Limit, F::H::metadataSize, "metadataSize", "more than 2^32 bytes");
            if (mInfo.mMetadataSize > mOptions.mMaxMetadataSize)
                return makeError(
                    ErrorClass::Limit, F::H::metadataSize, "metadataSize", "more than this reader is set to accept");
            mInfo.mExtensionSize = u64(F::H::extensionSize);
            if (mInfo.mExtensionSize > F::maxExtensionSize)
                return makeError(ErrorClass::Limit, F::H::extensionSize, "extensionSize", "more than 16 MiB");

            mInfo.mDataOffset = u64(F::H::dataOffset);
            if (mInfo.mDataOffset != F::dataOffset)
                return makeError(ErrorClass::Range, F::H::dataOffset, "dataOffset", "dataOffset must be 4096");
            mInfo.mDataSize = u64(F::H::dataSize);
            if (mInfo.mDataSize > mSize || mSize - mInfo.mDataSize < mInfo.mDataOffset)
                return makeError(ErrorClass::Range, F::H::dataSize, "dataSize", "the data region is outside the file");
            mDataEnd = mInfo.mDataOffset + mInfo.mDataSize;
            std::uint64_t expectedMetadataOffset = 0;
            if (!F::alignUp(mDataEnd, F::blockAlignment, expectedMetadataOffset))
                return makeError(ErrorClass::Range, F::H::dataSize, "dataSize", "overflow");
            if (mInfo.mEntryCount == 0 && mInfo.mDataSize != 0)
                return makeError(ErrorClass::Range, F::H::dataSize, "dataSize", "a pack without entries has no data");
            mInfo.mMetadataOffset = u64(F::H::metadataOffset);
            if (mInfo.mMetadataOffset != expectedMetadataOffset)
                return makeError(ErrorClass::Range, F::H::metadataOffset, "metadataOffset",
                    "metadataOffset is not the first multiple of 4096 after the data");
            if (mInfo.mMetadataOffset > mSize || mInfo.mMetadataSize > mSize - mInfo.mMetadataOffset
                || mSize - mInfo.mMetadataOffset - mInfo.mMetadataSize != F::footerSize)
                return makeError(ErrorClass::Range, F::H::metadataSize, "metadataSize",
                    "the metadata region does not end where the footer begins");

            // The sub-regions tile the metadata region exactly, in the order of section 7.1.
            mInfo.mStringTableSize = u64(F::H::stringTableSize);
            mInfo.mChunkTablesSize = u64(F::H::chunkTablesSize);
            mInfo.mProvenanceSize = u64(F::H::provenanceSize);
            if (mInfo.mStringTableSize % 8 != 0)
                return makeError(ErrorClass::Range, F::H::stringTableSize, "stringTableSize", "not a multiple of 8");
            if (mInfo.mChunkTablesSize % 8 != 0)
                return makeError(ErrorClass::Range, F::H::chunkTablesSize, "chunkTablesSize", "not a multiple of 8");
            if (mInfo.mProvenanceSize % 8 != 0)
                return makeError(ErrorClass::Range, F::H::provenanceSize, "provenanceSize", "not a multiple of 8");
            if (mInfo.mExtensionSize % 8 != 0)
                return makeError(ErrorClass::Range, F::H::extensionSize, "extensionSize", "not a multiple of 8");
            struct Sub
            {
                std::uint64_t* mOffsetTarget;
                std::size_t mField;
                const char* mName;
                std::uint64_t mSize;
            };
            const Sub subs[] = {
                { &mInfo.mStringTableOffset, F::H::stringTableOffset, "stringTableOffset", mInfo.mStringTableSize },
                { &mInfo.mEntryTableOffset, F::H::entryTableOffset, "entryTableOffset",
                    mInfo.mEntryCount * mInfo.mEntryRecordSize },
                { &mInfo.mPathIndexOffset, F::H::pathIndexOffset, "pathIndexOffset", mInfo.mPathEntryCount * 16 },
                { &mInfo.mKeyIndexOffset, F::H::keyIndexOffset, "keyIndexOffset", mInfo.mKeyEntryCount * 16 },
                { &mInfo.mChunkTablesOffset, F::H::chunkTablesOffset, "chunkTablesOffset", mInfo.mChunkTablesSize },
                { &mInfo.mProvenanceOffset, F::H::provenanceOffset, "provenanceOffset", mInfo.mProvenanceSize },
                { &mInfo.mExtensionOffset, F::H::extensionOffset, "extensionOffset", mInfo.mExtensionSize },
            };
            std::uint64_t expected = mInfo.mMetadataOffset;
            for (const Sub& sub : subs)
            {
                *sub.mOffsetTarget = u64(sub.mField);
                if (*sub.mOffsetTarget != expected)
                    return makeError(ErrorClass::Range, sub.mField, sub.mName,
                        "the sub-region does not start where the previous one ends");
                // Each size is at most 2^32 (metadataSize) or a product of bounded numbers, so this cannot overflow.
                if (sub.mSize > mInfo.mMetadataSize - (expected - mInfo.mMetadataOffset))
                    return makeError(ErrorClass::Range, F::H::metadataSize, "metadataSize",
                        "the sub-regions are larger than the metadata region");
                expected += sub.mSize;
            }
            if (expected - mInfo.mMetadataOffset != mInfo.mMetadataSize)
                return makeError(ErrorClass::Range, F::H::metadataSize, "metadataSize",
                    "metadataSize is not the sum of the sub-region sizes");

            // The padding between the header and the data, and between the data and the metadata, is zero.
            bool failed = false;
            std::uint64_t bad = firstNonZero(*mSource, F::headerSize, mInfo.mDataOffset, failed);
            if (failed)
                return ioFailure(bad, "padding");
            if (bad != mInfo.mDataOffset)
                return makeError(ErrorClass::Range, bad, "padding", "a padding byte after the header is not zero");
            bad = firstNonZero(*mSource, mDataEnd, mInfo.mMetadataOffset, failed);
            if (failed)
                return ioFailure(bad, "padding");
            if (bad != mInfo.mMetadataOffset)
                return makeError(ErrorClass::Range, bad, "padding", "a padding byte before the metadata is not zero");

            mInfo.mGeneration = u64(F::H::generation);
            std::memcpy(mInfo.mSourceDigest.data(), mHeader + F::H::sourceDigest, 32);
            std::memcpy(mInfo.mManifestDigest.data(), mHeader + F::H::manifestDigest, 32);
            return Error{};
        }

        // Item 7 (and the read of the metadata region itself).
        Error Opener::metadataDigest()
        {
            mImpl.mMeta.resize(static_cast<std::size_t>(mInfo.mMetadataSize));
            std::uint64_t done = 0;
            while (done < mInfo.mMetadataSize)
            {
                const std::size_t n = static_cast<std::size_t>(
                    std::min<std::uint64_t>(std::uint64_t{ 16 } << 20, mInfo.mMetadataSize - done));
                if (!mSource->read(mInfo.mMetadataOffset + done, mImpl.mMeta.data() + done, n))
                    return ioFailure(mInfo.mMetadataOffset + done, "metadata");
                done += n;
            }
            if (!mOptions.mVerifyMetadataDigest)
                return Error{};
            const Hash digest = blake3(mImpl.mMeta.data(), mImpl.mMeta.size());
            if (digest != mInfo.mMetadataDigest)
                return makeError(ErrorClass::Checksum, F::H::metadataDigest, "metadataDigest",
                    "the metadata digest does not match the metadata region");
            return Error{};
        }

        // Item 8.
        Error Opener::extensionBlocks()
        {
            const std::uint64_t size = mInfo.mExtensionSize;
            std::uint64_t position = 0;
            std::uint64_t count = 0;
            while (position < size)
            {
                const std::uint64_t absolute = mInfo.mExtensionOffset + position;
                const std::uint64_t remaining = size - position;
                if (remaining < 16)
                    return makeError(ErrorClass::Range, absolute, "extension.header", "a block header does not fit");
                const std::uint8_t* block = mImpl.at(absolute);
                const std::uint32_t id = F::rd32(block);
                const std::uint32_t flags = F::rd32(block + 4);
                const std::uint64_t payload = F::rd64(block + 8);
                if (payload > remaining - 16)
                    return makeError(
                        ErrorClass::Range, absolute + 8, "extension.payloadSize", "the payload does not fit");
                std::uint64_t padded = 0;
                if (!F::alignUp(payload, 8, padded) || padded > remaining - 16)
                    return makeError(
                        ErrorClass::Range, absolute + 8, "extension.payloadSize", "the padded payload does not fit");
                if (++count > F::maxExtensionBlocks)
                    return makeError(ErrorClass::Limit, absolute, "extension", "more than 1024 extension blocks");
                if (id != 1 && (flags & 1) != 0)
                    return makeError(ErrorClass::Unsupported, absolute, "extension.blockId",
                        "an unknown block that must be understood");
                if (id == 1 && payload > 1024)
                    return makeError(ErrorClass::Limit, absolute + 8, "extension.payloadSize",
                        "the writer-info block is longer than 1024 bytes");
                position += 16 + padded;
            }
            return Error{};
        }

        // Item 9.
        Error Opener::entries()
        {
            const std::size_t count = static_cast<std::size_t>(mInfo.mEntryCount);
            const std::uint8_t* strings = mImpl.at(mInfo.mStringTableOffset);
            const std::uint64_t stringSize = mInfo.mStringTableSize;
            std::string_view previousPath;
            bool havePath = false;
            bool sawKeyOnly = false;
            bool haveKey = false;
            std::uint64_t previousKey = 0;
            std::uint64_t pathEnd = 0;
            std::uint64_t contentSum = 0;
            std::uint64_t chunkRecords = 0;
            std::uint64_t provenancePosition = 0;
            std::uint64_t pathEntries = 0;
            std::uint64_t keyEntries = 0;
            mImpl.mUniform.assign(count, 0);
            mImpl.mStartsOf.assign(count, 0);

            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint8_t* record = mImpl.entryRecord(i);
                const std::uint64_t base = mImpl.entryRecordOffset(i);
                const std::uint32_t flags = F::rd32(record + F::E::flags);

                if ((flags & 0xFFF8u) != 0)
                    return makeError(ErrorClass::Unsupported, base + F::E::flags,
                        indexed("entry", i, "entryFlags").c_str(), "an unknown critical entry flag");
                const bool hasPath = (flags & entryFlagHasPath) != 0;
                const bool hasKey = (flags & entryFlagHasKey) != 0;
                const bool hasProvenance = (flags & entryFlagHasProvenance) != 0;
                if (!hasPath && !hasKey)
                    return makeError(ErrorClass::Range, base + F::E::flags, indexed("entry", i, "entryFlags").c_str(),
                        "the entry has neither a path nor a key");
                const std::uint64_t key = F::rd64(record + F::E::key);
                if ((key != 0) != hasKey)
                    return makeError(ErrorClass::Range, base + F::E::key, indexed("entry", i, "key").c_str(),
                        "key is zero with HAS_KEY or not zero without it");
                if (!hasPath && (F::rd64(record + F::E::pathOffset) != 0 || F::rd32(record + F::E::pathLength) != 0))
                    return makeError(ErrorClass::Range,
                        base + (F::rd32(record + F::E::pathLength) != 0 ? F::E::pathLength : F::E::pathOffset),
                        indexed("entry", i, "pathLength").c_str(), "path fields set without HAS_PATH");
                if (!hasProvenance && F::rd64(record + F::E::provenanceOffset) != 0)
                    return makeError(ErrorClass::Range, base + F::E::provenanceOffset,
                        indexed("entry", i, "provenanceOffset").c_str(), "provenanceOffset set without HAS_PROVENANCE");
                const std::uint16_t type = F::rd16(record + F::E::type);
                if (type >= 0x10 && type <= 0x13 && (!hasKey || (key >> 56) != (type & 0xFFu)))
                    return makeError(ErrorClass::Range, base + F::E::type, indexed("entry", i, "entryType").c_str(),
                        "a tile type needs a key whose tag is the low byte of the type");

                const std::uint64_t size = F::rd64(record + F::E::size);
                if (size > F::maxEntrySize)
                    return makeError(ErrorClass::Limit, base + F::E::size,
                        indexed("entry", i, "uncompressedSize").c_str(), "an entry larger than 4 GiB");
                contentSum += size;
                if (contentSum > F::maxPackContent)
                    return makeError(ErrorClass::Limit, base + F::E::size,
                        indexed("entry", i, "uncompressedSize").c_str(), "more than 2^40 content bytes in the pack");
                const std::uint32_t chunkCount = F::rd32(record + F::E::chunkCount);
                if (chunkCount > F::maxChunksPerEntry)
                    return makeError(ErrorClass::Limit, base + F::E::chunkCount,
                        indexed("entry", i, "chunkCount").c_str(), "more than 2^20 chunks in an entry");
                mTotalChunks += chunkCount;
                if (mTotalChunks > F::maxChunksPerPack)
                    return makeError(ErrorClass::Limit, base + F::E::chunkCount,
                        indexed("entry", i, "chunkCount").c_str(), "more than 2^25 chunks in the pack");
                if ((chunkCount == 0) != (size == 0))
                    return makeError(ErrorClass::Range, base + (size == 0 ? F::E::size : F::E::chunkCount),
                        indexed("entry", i, size == 0 ? "uncompressedSize" : "chunkCount").c_str(),
                        "chunkCount is zero exactly when uncompressedSize is zero");
                if (size == 0)
                {
                    if (F::rd64(record + F::E::chunkTableOffset) != 0)
                        return makeError(ErrorClass::Range, base + F::E::chunkTableOffset,
                            indexed("entry", i, "chunkTableOffset").c_str(), "an empty entry has a chunk table");
                    if (std::memcmp(record + F::E::contentHash, emptyHash().data(), 32) != 0)
                        return makeError(ErrorClass::Range, base + F::E::contentHash,
                            indexed("entry", i, "contentHash").c_str(), "the hash of an empty entry is wrong");
                }

                if (hasPath)
                {
                    ++pathEntries;
                    const std::uint32_t length = F::rd32(record + F::E::pathLength);
                    if (length == 0 || length > F::maxPathLength)
                        return makeError(ErrorClass::Limit, base + F::E::pathLength,
                            indexed("entry", i, "pathLength").c_str(), "a path of 0 or more than 1024 bytes");
                    const std::uint64_t offset = F::rd64(record + F::E::pathOffset);
                    if (offset > stringSize || length > stringSize - offset)
                        return makeError(ErrorClass::Range, base + F::E::pathLength,
                            indexed("entry", i, "pathLength").c_str(), "the path is outside the string table");
                    const std::string_view path(reinterpret_cast<const char*>(strings) + offset, length);
                    std::string reason;
                    if (const ErrorClass nameClass = checkStoredPath(path, reason); nameClass != ErrorClass::None)
                        return makeError(
                            nameClass, base + F::E::pathOffset, indexed("entry", i, "path").c_str(), reason);
                    if (havePath)
                    {
                        const int order = comparePaths(previousPath, path);
                        if (order == 0)
                            return makeError(ErrorClass::Duplicate, base + F::E::pathOffset,
                                indexed("entry", i, "path").c_str(), "the same path twice");
                        if (order > 0)
                            return makeError(ErrorClass::Order, base + F::E::pathOffset,
                                indexed("entry", i, "path").c_str(), "paths are not in ascending order");
                    }
                    if (sawKeyOnly)
                        return makeError(ErrorClass::Order, base + F::E::flags,
                            indexed("entry", i, "entryFlags").c_str(), "a path entry after a key-only entry");
                    if (offset != pathEnd)
                        return makeError(ErrorClass::Range, base + F::E::pathOffset,
                            indexed("entry", i, "pathOffset").c_str(),
                            "paths must follow each other in the string table");
                    pathEnd = offset + length;
                    previousPath = path;
                    havePath = true;
                }
                if (hasKey)
                {
                    ++keyEntries;
                    if (!hasPath)
                    {
                        sawKeyOnly = true;
                        if (haveKey && key <= previousKey)
                            return makeError(key == previousKey ? ErrorClass::Duplicate : ErrorClass::Order,
                                base + F::E::key, indexed("entry", i, "key").c_str(),
                                "key-only entries must have ascending keys");
                        previousKey = key;
                        haveKey = true;
                    }
                }

                if (hasProvenance)
                {
                    const std::uint64_t expected = mInfo.mProvenanceOffset + provenancePosition;
                    const std::uint64_t remaining = mInfo.mProvenanceSize - provenancePosition;
                    const std::uint64_t at = F::rd64(record + F::E::provenanceOffset);
                    const std::string field = indexed("entry", i, "provenanceOffset");
                    if (at != expected || remaining < 48)
                        return makeError(ErrorClass::Range, base + F::E::provenanceOffset, field.c_str(),
                            "provenance records must follow each other in the provenance region");
                    const std::uint8_t* prov = mImpl.at(at);
                    const std::uint32_t recordSize = F::rd32(prov);
                    const std::uint32_t toolIdLength = F::rd16(prov + 4);
                    const std::uint32_t parametersLength = F::rd32(prov + 40);
                    if (recordSize > F::maxProvenanceRecord)
                        return makeError(ErrorClass::Limit, at, field.c_str(), "a provenance record over 65,536 bytes");
                    if (toolIdLength == 0 || toolIdLength > F::maxToolId)
                        return makeError(ErrorClass::Limit, at + 4, field.c_str(), "toolIdLength is not 1 to 255");
                    if (parametersLength > F::maxProvenanceParameters)
                        return makeError(ErrorClass::Limit, at + 40, field.c_str(), "more than 60,000 parameter bytes");
                    std::uint64_t rounded = 0;
                    F::alignUp(48 + toolIdLength + static_cast<std::uint64_t>(parametersLength), 8, rounded);
                    if (recordSize != rounded || recordSize > remaining)
                        return makeError(ErrorClass::Range, at, field.c_str(),
                            "recordSize is not the rounded size of the record or does not fit");
                    const std::string_view toolId(reinterpret_cast<const char*>(prov) + 48, toolIdLength);
                    if (!isValidUtf8(toolId) || toolId.find('\0') != std::string_view::npos)
                        return makeError(ErrorClass::Name, at + 48, field.c_str(), "the tool id is not valid text");
                    provenancePosition += recordSize;
                }

                if (chunkCount > 0)
                {
                    const std::uint64_t expected = mInfo.mChunkTablesOffset + chunkRecords * mInfo.mChunkRecordSize;
                    const std::uint64_t tableBytes = static_cast<std::uint64_t>(chunkCount) * mInfo.mChunkRecordSize;
                    if (F::rd64(record + F::E::chunkTableOffset) != expected
                        || tableBytes > mInfo.mChunkTablesSize - chunkRecords * mInfo.mChunkRecordSize)
                        return makeError(ErrorClass::Range, base + F::E::chunkTableOffset,
                            indexed("entry", i, "chunkTableOffset").c_str(),
                            "chunk tables must follow each other in the chunk tables region");
                    chunkRecords += chunkCount;
                }
            }

            std::uint64_t paddedPaths = 0;
            F::alignUp(pathEnd, 8, paddedPaths);
            if (paddedPaths != stringSize)
                return makeError(ErrorClass::Range, F::H::stringTableSize, "stringTableSize",
                    "the string table is not the paths rounded up to a multiple of 8");
            for (std::uint64_t k = pathEnd; k < stringSize; ++k)
            {
                if (strings[k] != 0)
                    return makeError(ErrorClass::Range, mInfo.mStringTableOffset + k, "stringTable.padding",
                        "a padding byte of the string table is not zero");
            }
            if (provenancePosition != mInfo.mProvenanceSize)
                return makeError(ErrorClass::Range, F::H::provenanceSize, "provenanceSize",
                    "provenanceSize is not the sum of the record sizes");
            if (chunkRecords * mInfo.mChunkRecordSize != mInfo.mChunkTablesSize)
                return makeError(ErrorClass::Range, F::H::chunkTablesSize, "chunkTablesSize",
                    "chunkTablesSize is not the size of all chunk records");
            if (pathEntries != mInfo.mPathEntryCount)
                return makeError(ErrorClass::Range, F::H::pathEntryCount, "pathEntryCount",
                    "pathEntryCount is not the number of entries with a path");
            if (keyEntries != mInfo.mKeyEntryCount)
                return makeError(ErrorClass::Range, F::H::keyEntryCount, "keyEntryCount",
                    "keyEntryCount is not the number of entries with a key");
            return Error{};
        }

        // Item 10.
        Error Opener::chunks()
        {
            const std::size_t count = static_cast<std::size_t>(mInfo.mEntryCount);
            std::uint64_t previousEnd = mInfo.mDataOffset;
            std::vector<std::uint64_t> starts; // where each chunk of the current entry starts in its content
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint8_t* record = mImpl.entryRecord(i);
                const std::uint32_t chunkCount = F::rd32(record + F::E::chunkCount);
                if (chunkCount == 0)
                    continue;
                const std::uint64_t entrySize = F::rd64(record + F::E::size);
                std::uint64_t table = F::rd64(record + F::E::chunkTableOffset);
                std::uint64_t sum = 0;
                bool uniform = true;
                starts.clear();
                for (std::uint32_t k = 0; k < chunkCount; ++k, table += mInfo.mChunkRecordSize)
                {
                    const std::uint8_t* chunk = mImpl.at(table);
                    const auto field = [&](const char* tail) {
                        return "chunk[" + std::to_string(i) + "." + std::to_string(k) + "]." + tail;
                    };
                    const std::uint64_t offset = F::rd64(chunk + F::C::offset);
                    const std::uint32_t stored = F::rd32(chunk + F::C::storedSize);
                    const std::uint32_t uncompressed = F::rd32(chunk + F::C::size);
                    const std::uint16_t codec = F::rd16(chunk + F::C::codec);
                    const std::uint16_t chunkFlags = F::rd16(chunk + F::C::flags);

                    if (uncompressed < 1 || uncompressed > F::maxChunkSize)
                        return makeError(ErrorClass::Limit, table + F::C::size, field("uncompressedSize").c_str(),
                            "a chunk of 0 or more than 8 MiB");
                    if (stored < 1 || stored > F::maxStoredSize)
                        return makeError(ErrorClass::Limit, table + F::C::storedSize, field("storedSize").c_str(),
                            "stored bytes of 0 or more than 2^24");
                    if (codec == codecStore && stored != uncompressed)
                        return makeError(ErrorClass::Range, table + F::C::storedSize, field("storedSize").c_str(),
                            "a stored chunk must have storedSize equal to uncompressedSize");
                    if (codec == codecZstd && stored > F::zstdStoredBound(uncompressed))
                        return makeError(ErrorClass::Limit, table + F::C::storedSize, field("storedSize").c_str(),
                            "storedSize is above the zstd bound");
                    if ((chunkFlags & F::chunkFlagHashPresent) == 0)
                        return makeError(ErrorClass::Range, table + F::C::flags, field("chunkFlags").c_str(),
                            "HASH_PRESENT must be set");
                    if (offset < mInfo.mDataOffset || offset > mDataEnd || stored > mDataEnd - offset)
                        return makeError(ErrorClass::Range, table + F::C::offset, field("offset").c_str(),
                            "the stored bytes are outside the data region");
                    if (k == 0 && offset % F::blockAlignment != 0)
                        return makeError(ErrorClass::Range, table + F::C::offset, field("offset").c_str(),
                            "the first chunk of an entry must start at a multiple of 4096");
                    if (offset < previousEnd)
                        return makeError(ErrorClass::Overlap, table + F::C::offset, field("offset").c_str(),
                            "chunks overlap or are out of order");
                    previousEnd = offset + stored;
                    starts.push_back(sum);
                    sum += uncompressed;
                    if (sum > entrySize)
                        return makeError(ErrorClass::Range, table + F::C::size, field("uncompressedSize").c_str(),
                            "the chunks hold more bytes than the entry");
                    if (k + 1 < chunkCount && uncompressed != mInfo.mChunkSize)
                        uniform = false;
                    if (chunkCount == 1 && std::memcmp(chunk + F::C::hash, record + F::E::contentHash, 32) != 0)
                        return makeError(ErrorClass::Range, table + F::C::hash, field("chunkHash").c_str(),
                            "the chunk hash of a one-chunk entry must equal the content hash");
                }
                if (sum != entrySize)
                    return makeError(ErrorClass::Range, mImpl.entryRecordOffset(i) + F::E::size,
                        indexed("entry", i, "uncompressedSize").c_str(), "the chunks do not add up to the entry size");
                mImpl.mUniform[i] = uniform ? 1 : 0;
                if (!uniform)
                {
                    mImpl.mStartsOf[i] = static_cast<std::uint32_t>(mImpl.mStarts.size());
                    mImpl.mStarts.insert(mImpl.mStarts.end(), starts.begin(), starts.end());
                }
            }
            if (previousEnd != mDataEnd)
                return makeError(
                    ErrorClass::Range, F::H::dataSize, "dataSize", "dataSize does not end where the last chunk ends");
            return Error{};
        }

        // Item 11.
        Error Opener::pathIndex()
        {
            const std::uint64_t count = mInfo.mPathEntryCount;
            const std::uint8_t* records = mImpl.at(mInfo.mPathIndexOffset);
            std::uint64_t previousHash = 0;
            std::uint64_t previousIndex = 0;
            for (std::uint64_t k = 0; k < count; ++k)
            {
                const std::uint8_t* record = records + k * 16;
                const std::uint64_t absolute = mInfo.mPathIndexOffset + k * 16;
                const std::uint64_t hash = F::rd64(record);
                const std::uint64_t entryIndex = F::rd64(record + 8);
                if (entryIndex >= mInfo.mEntryCount)
                    return makeError(ErrorClass::Index, absolute + 8, indexed("pathIndex", k, "entryIndex").c_str(),
                        "entryIndex is outside the entry table");
                const std::uint8_t* entryRecord = mImpl.entryRecord(static_cast<std::size_t>(entryIndex));
                if ((F::rd32(entryRecord + F::E::flags) & entryFlagHasPath) == 0)
                    return makeError(ErrorClass::Index, absolute + 8, indexed("pathIndex", k, "entryIndex").c_str(),
                        "the entry has no path");
                const std::string_view path(reinterpret_cast<const char*>(mImpl.at(mInfo.mStringTableOffset))
                        + F::rd64(entryRecord + F::E::pathOffset),
                    F::rd32(entryRecord + F::E::pathLength));
                if (hash != pathHash(path))
                    return makeError(ErrorClass::Index, absolute, indexed("pathIndex", k, "pathHash").c_str(),
                        "the path hash is wrong");
                if (k > 0 && (hash < previousHash || (hash == previousHash && entryIndex <= previousIndex)))
                    return makeError(ErrorClass::Order, absolute, indexed("pathIndex", k, "pathHash").c_str(),
                        "the path index is not strictly ascending");
                previousHash = hash;
                previousIndex = entryIndex;
            }
            return Error{};
        }

        // Item 12.
        Error Opener::keyIndex()
        {
            const std::uint64_t count = mInfo.mKeyEntryCount;
            const std::uint8_t* records = mImpl.at(mInfo.mKeyIndexOffset);
            std::uint64_t previousKey = 0;
            for (std::uint64_t k = 0; k < count; ++k)
            {
                const std::uint8_t* record = records + k * 16;
                const std::uint64_t absolute = mInfo.mKeyIndexOffset + k * 16;
                const std::uint64_t key = F::rd64(record);
                const std::uint64_t entryIndex = F::rd64(record + 8);
                if (entryIndex >= mInfo.mEntryCount)
                    return makeError(ErrorClass::Index, absolute + 8, indexed("keyIndex", k, "entryIndex").c_str(),
                        "entryIndex is outside the entry table");
                const std::uint8_t* entryRecord = mImpl.entryRecord(static_cast<std::size_t>(entryIndex));
                if ((F::rd32(entryRecord + F::E::flags) & entryFlagHasKey) == 0)
                    return makeError(ErrorClass::Index, absolute + 8, indexed("keyIndex", k, "entryIndex").c_str(),
                        "the entry has no key");
                if (F::rd64(entryRecord + F::E::key) != key)
                    return makeError(ErrorClass::Index, absolute, indexed("keyIndex", k, "key").c_str(),
                        "the key differs from the entry's key");
                if (k > 0 && key <= previousKey)
                    return makeError(key == previousKey ? ErrorClass::Duplicate : ErrorClass::Order, absolute,
                        indexed("keyIndex", k, "key").c_str(), "the key index is not strictly ascending");
                previousKey = key;
            }
            return Error{};
        }

        struct DecodedChunk
        {
            const std::uint8_t* mData = nullptr;
            std::size_t mSize = 0;
        };

        // Reads, decodes and verifies one chunk into this thread's scratch buffers (section 14.3 items 1 to 3).
        // The result stays valid until this thread's next call.
        Error loadChunk(const Archive::Impl& impl, const ChunkInfo& chunk, DecodeBudget* budget, DecodedChunk& out)
        {
            if ((chunk.mFlags & 0x00FE) != 0)
                return makeError(
                    ErrorClass::Unsupported, chunk.mOffset, "chunkFlags", "an unknown critical chunk flag");
            if (chunk.mCodec != codecStore && chunk.mCodec != codecZstd)
                return makeError(ErrorClass::Unsupported, chunk.mOffset, "codec", "an unknown codec");
            // Defence in depth: the record was checked at open, but the source may have changed since.
            if (chunk.mSize < 1 || chunk.mSize > F::maxChunkSize || chunk.mStoredSize < 1
                || chunk.mStoredSize > F::maxStoredSize)
                return makeError(ErrorClass::Limit, chunk.mOffset, "chunk", "a chunk size outside the limits");
            if (chunk.mCodec == codecStore && chunk.mStoredSize != chunk.mSize)
                return makeError(ErrorClass::Range, chunk.mOffset, "chunk", "a stored chunk with different sizes");
            if (budget != nullptr)
            {
                if (budget->mRemaining < chunk.mSize)
                    return makeError(ErrorClass::Limit, chunk.mOffset, "chunk", "the decoded-byte budget is used up");
                budget->mRemaining -= chunk.mSize;
            }

            Scratch& buffers = scratch();
            if (buffers.mStored.size() < chunk.mStoredSize)
                buffers.mStored.resize(chunk.mStoredSize);
            if (!impl.mSource->read(chunk.mOffset, buffers.mStored.data(), chunk.mStoredSize))
                return ioFailure(chunk.mOffset, "chunk");

            const std::uint8_t* decoded = nullptr;
            if (chunk.mCodec == codecStore)
                decoded = buffers.mStored.data();
            else
            {
                const std::uint8_t* stored = buffers.mStored.data();
                ZSTD_frameHeader header;
                const std::size_t needed = ZSTD_getFrameHeader(&header, stored, chunk.mStoredSize);
                if (ZSTD_isError(needed) || needed != 0)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.frame", "the frame header is not valid");
                if (header.frameType != ZSTD_frame)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.frame", "a skippable frame");
                if (header.frameContentSize != chunk.mSize)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.contentSize",
                        "the frame content size is missing or differs from the chunk record");
                if (header.dictID != 0)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.dictionary", "a dictionary is used");
                if (header.windowSize > F::maxChunkSize)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.window", "the window is above 8 MiB");
                const std::size_t frameSize = ZSTD_findFrameCompressedSize(stored, chunk.mStoredSize);
                if (ZSTD_isError(frameSize) || frameSize != chunk.mStoredSize)
                    return makeError(
                        ErrorClass::Codec, chunk.mOffset, "zstd.frame", "the stored bytes are not exactly one frame");
                if (buffers.mContext == nullptr)
                {
                    buffers.mContext = ZSTD_createDCtx();
                    if (buffers.mContext == nullptr)
                        return makeError(ErrorClass::Limit, chunk.mOffset, "zstd", "out of memory");
                    ZSTD_DCtx_setParameter(buffers.mContext, ZSTD_d_windowLogMax, 23);
                }
                if (buffers.mDecoded.size() < chunk.mSize)
                    buffers.mDecoded.resize(chunk.mSize);
                const std::size_t produced = ZSTD_decompressDCtx(
                    buffers.mContext, buffers.mDecoded.data(), chunk.mSize, stored, chunk.mStoredSize);
                if (ZSTD_isError(produced) || produced != chunk.mSize)
                    return makeError(ErrorClass::Codec, chunk.mOffset, "zstd.frame",
                        "the frame does not decode to the declared size");
                decoded = buffers.mDecoded.data();
            }

            if (blake3(decoded, chunk.mSize) != chunk.mHash)
                return makeError(ErrorClass::Hash, chunk.mOffset, "chunkHash", "the chunk does not match its hash");
            out.mData = decoded;
            out.mSize = chunk.mSize;
            return Error{};
        }
    }

    Archive::Archive(std::unique_ptr<Impl> impl) noexcept
        : mImpl(std::move(impl))
    {
    }

    Archive::~Archive() = default;

    std::unique_ptr<Archive> Archive::open(
        std::shared_ptr<const Source> source, const OpenOptions& options, Error& error) noexcept
    {
        error = Error{};
        if (!source)
        {
            error = makeError(ErrorClass::Io, 0, "source", "no source");
            return nullptr;
        }
        try
        {
            auto impl = std::make_unique<Impl>();
            Opener opener(*impl, options);
            error = opener.run(std::move(source));
            if (error)
                return nullptr;
            return std::make_unique<Archive>(std::move(impl));
        }
        catch (const std::bad_alloc&)
        {
            error = makeError(ErrorClass::Limit, 0, "memory", "out of memory");
        }
        catch (...)
        {
            error = makeError(ErrorClass::Io, 0, "open", "an unexpected exception while opening the pack");
        }
        return nullptr;
    }

    std::unique_ptr<Archive> Archive::open(
        const std::filesystem::path& path, const OpenOptions& options, Error& error) noexcept
    {
        std::shared_ptr<const Source> source = openFileSource(path, error);
        if (!source)
            return nullptr;
        return open(std::move(source), options, error);
    }

    const PackInfo& Archive::info() const noexcept
    {
        return mImpl->mInfo;
    }

    const Source& Archive::source() const noexcept
    {
        return *mImpl->mSource;
    }

    std::size_t Archive::entryCount() const noexcept
    {
        return static_cast<std::size_t>(mImpl->mInfo.mEntryCount);
    }

    EntryInfo Archive::entry(std::size_t index) const noexcept
    {
        const Impl& impl = *mImpl;
        const std::uint8_t* record = impl.entryRecord(index);
        EntryInfo info;
        info.mIndex = index;
        info.mFlags = F::rd32(record + F::E::flags);
        info.mHasPath = (info.mFlags & entryFlagHasPath) != 0;
        info.mHasKey = (info.mFlags & entryFlagHasKey) != 0;
        info.mHasProvenance = (info.mFlags & entryFlagHasProvenance) != 0;
        if (info.mHasPath)
            info.mPath = std::string_view(reinterpret_cast<const char*>(impl.at(impl.mInfo.mStringTableOffset))
                    + F::rd64(record + F::E::pathOffset),
                F::rd32(record + F::E::pathLength));
        info.mKey = F::rd64(record + F::E::key);
        info.mType = F::rd16(record + F::E::type);
        info.mChunkCount = F::rd32(record + F::E::chunkCount);
        info.mSize = F::rd64(record + F::E::size);
        std::memcpy(info.mContentHash.data(), record + F::E::contentHash, 32);
        info.mHintMemorySize = F::rd32(record + F::E::hintMemorySize);
        info.mHintGroup = F::rd32(record + F::E::hintGroup);
        info.mLayoutParamA = F::rd32(record + F::E::layoutParamA);
        info.mLayoutParamB = F::rd32(record + F::E::layoutParamB);
        return info;
    }

    ChunkInfo Archive::chunk(std::size_t entryIndex, std::uint32_t chunkIndex) const noexcept
    {
        const Impl& impl = *mImpl;
        const std::uint8_t* record = impl.entryRecord(entryIndex);
        const std::uint64_t table = F::rd64(record + F::E::chunkTableOffset)
            + static_cast<std::uint64_t>(chunkIndex) * impl.mInfo.mChunkRecordSize;
        const std::uint8_t* raw = impl.at(table);
        ChunkInfo info;
        info.mOffset = F::rd64(raw + F::C::offset);
        info.mStoredSize = F::rd32(raw + F::C::storedSize);
        info.mSize = F::rd32(raw + F::C::size);
        info.mCodec = F::rd16(raw + F::C::codec);
        info.mFlags = F::rd16(raw + F::C::flags);
        std::memcpy(info.mHash.data(), raw + F::C::hash, 32);
        return info;
    }

    std::optional<ProvenanceInfo> Archive::provenance(std::size_t entryIndex) const noexcept
    {
        const Impl& impl = *mImpl;
        const std::uint8_t* record = impl.entryRecord(entryIndex);
        if ((F::rd32(record + F::E::flags) & entryFlagHasProvenance) == 0)
            return std::nullopt;
        const std::uint8_t* prov = impl.at(F::rd64(record + F::E::provenanceOffset));
        ProvenanceInfo info;
        const std::uint32_t toolIdLength = F::rd16(prov + 4);
        const std::uint32_t parametersLength = F::rd32(prov + 40);
        info.mToolId = std::string_view(reinterpret_cast<const char*>(prov) + 48, toolIdLength);
        std::memcpy(info.mSourceDigest.data(), prov + 8, 32);
        info.mParameters = std::span<const std::uint8_t>(prov + 48 + toolIdLength, parametersLength);
        return info;
    }

    std::optional<std::size_t> Archive::findPath(std::string_view normalizedPath) const noexcept
    {
        const Impl& impl = *mImpl;
        const std::uint64_t wanted = pathHash(normalizedPath);
        const std::uint8_t* records = impl.at(impl.mInfo.mPathIndexOffset);
        std::uint64_t low = 0;
        std::uint64_t high = impl.mInfo.mPathEntryCount;
        while (low < high)
        {
            const std::uint64_t middle = low + (high - low) / 2;
            if (F::rd64(records + middle * 16) < wanted)
                low = middle + 1;
            else
                high = middle;
        }
        for (std::uint64_t k = low; k < impl.mInfo.mPathEntryCount && F::rd64(records + k * 16) == wanted; ++k)
        {
            const std::size_t index = static_cast<std::size_t>(F::rd64(records + k * 16 + 8));
            if (entry(index).mPath == normalizedPath)
                return index;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> Archive::findKey(std::uint64_t key) const noexcept
    {
        const Impl& impl = *mImpl;
        const std::uint8_t* records = impl.at(impl.mInfo.mKeyIndexOffset);
        std::uint64_t low = 0;
        std::uint64_t high = impl.mInfo.mKeyEntryCount;
        while (low < high)
        {
            const std::uint64_t middle = low + (high - low) / 2;
            const std::uint64_t found = F::rd64(records + middle * 16);
            if (found == key)
                return static_cast<std::size_t>(F::rd64(records + middle * 16 + 8));
            if (found < key)
                low = middle + 1;
            else
                high = middle;
        }
        return std::nullopt;
    }

    Error Archive::readRange(std::size_t entryIndex, std::uint64_t offset, std::size_t size, void* destination,
        DecodeBudget* budget) const noexcept
    {
        try
        {
            const Impl& impl = *mImpl;
            if (entryIndex >= entryCount())
                return makeError(ErrorClass::Range, 0, "entry", "no such entry");
            const EntryInfo info = entry(entryIndex);
            if (offset > info.mSize || size > info.mSize - offset)
                return makeError(ErrorClass::Range, impl.entryRecordOffset(entryIndex) + F::E::size, "range",
                    "the range is outside the entry");
            if (size == 0)
                return Error{};

            std::uint32_t chunkIndex = 0;
            std::uint64_t chunkStart = 0;
            if (impl.mUniform[entryIndex] != 0)
            {
                chunkIndex = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(offset / impl.mInfo.mChunkSize, info.mChunkCount - 1));
                chunkStart = static_cast<std::uint64_t>(chunkIndex) * impl.mInfo.mChunkSize;
            }
            else
            {
                const std::uint64_t* starts = impl.mStarts.data() + impl.mStartsOf[entryIndex];
                const std::uint64_t* found = std::upper_bound(starts, starts + info.mChunkCount, offset);
                chunkIndex = static_cast<std::uint32_t>(found - starts) - 1;
                chunkStart = starts[chunkIndex];
            }

            std::uint8_t* out = static_cast<std::uint8_t*>(destination);
            std::uint64_t position = offset;
            std::uint64_t remaining = size;
            while (remaining > 0)
            {
                if (chunkIndex >= info.mChunkCount)
                    return makeError(ErrorClass::Range, 0, "chunk", "the chunks end before the entry does");
                const ChunkInfo record = chunk(entryIndex, chunkIndex);
                DecodedChunk decoded;
                if (Error error = loadChunk(impl, record, budget, decoded))
                    return error;
                const std::uint64_t inChunk = position - chunkStart;
                const std::uint64_t take = std::min<std::uint64_t>(remaining, decoded.mSize - inChunk);
                std::memcpy(out, decoded.mData + inChunk, static_cast<std::size_t>(take));
                out += take;
                position += take;
                remaining -= take;
                chunkStart += decoded.mSize;
                ++chunkIndex;
            }
            return Error{};
        }
        catch (const std::bad_alloc&)
        {
            return makeError(ErrorClass::Limit, 0, "memory", "out of memory");
        }
        catch (...)
        {
            return makeError(ErrorClass::Io, 0, "read", "an unexpected exception while reading");
        }
    }

    Error Archive::readEntry(
        std::size_t entryIndex, std::vector<std::uint8_t>& out, DecodeBudget* budget) const noexcept
    {
        try
        {
            out.clear();
            if (entryIndex >= entryCount())
                return makeError(ErrorClass::Range, 0, "entry", "no such entry");
            const Impl& impl = *mImpl;
            const EntryInfo info = entry(entryIndex);
            if (budget != nullptr && budget->mRemaining < info.mSize)
                return makeError(ErrorClass::Limit, 0, "entry", "the decoded-byte budget is too small for the entry");
            // The output grows chunk by chunk, after each chunk has been verified: a pack that declares 4 GiB but
            // whose chunks fail costs no more than the chunks decoded so far.
            out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(info.mSize, std::uint64_t{ 16 } << 20)));
            Hasher content;
            for (std::uint32_t k = 0; k < info.mChunkCount; ++k)
            {
                DecodedChunk decoded;
                if (Error error = loadChunk(impl, chunk(entryIndex, k), budget, decoded))
                {
                    out.clear();
                    return error;
                }
                out.insert(out.end(), decoded.mData, decoded.mData + decoded.mSize);
                content.update(decoded.mData, decoded.mSize);
            }
            const Hash hash = info.mChunkCount == 0 ? emptyHash() : content.finalize();
            if (hash != info.mContentHash)
            {
                out.clear();
                return makeError(ErrorClass::Hash, impl.entryRecordOffset(entryIndex) + F::E::contentHash,
                    "contentHash", "the entry does not match its content hash");
            }
            return Error{};
        }
        catch (const std::bad_alloc&)
        {
            out.clear();
            return makeError(ErrorClass::Limit, 0, "memory", "out of memory");
        }
        catch (...)
        {
            out.clear();
            return makeError(ErrorClass::Io, 0, "read", "an unexpected exception while reading");
        }
    }

    Error Archive::verifyEntry(std::size_t entryIndex, DecodeBudget* budget) const noexcept
    {
        try
        {
            const Impl& impl = *mImpl;
            if (entryIndex >= entryCount())
                return makeError(ErrorClass::Range, 0, "entry", "no such entry");
            const EntryInfo info = entry(entryIndex);
            Hasher content;
            for (std::uint32_t k = 0; k < info.mChunkCount; ++k)
            {
                DecodedChunk decoded;
                if (Error error = loadChunk(impl, chunk(entryIndex, k), budget, decoded))
                    return error;
                content.update(decoded.mData, decoded.mSize);
            }
            const Hash hash = info.mChunkCount == 0 ? emptyHash() : content.finalize();
            if (hash != info.mContentHash)
                return makeError(ErrorClass::Hash, impl.entryRecordOffset(entryIndex) + F::E::contentHash,
                    "contentHash", "the entry does not match its content hash");
            return Error{};
        }
        catch (const std::bad_alloc&)
        {
            return makeError(ErrorClass::Limit, 0, "memory", "out of memory");
        }
        catch (...)
        {
            return makeError(ErrorClass::Io, 0, "read", "an unexpected exception while reading");
        }
    }
}
