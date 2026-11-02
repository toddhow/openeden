// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The validator: a reader that also decodes everything and, in canonical mode, checks the canonical form (section 17).
#include <edenarchive/validator.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>
#include <string>

#include <edenarchive/archive.hpp>

#include "archiveimpl.hpp"
#include "format.hpp"
#include "manifest.hpp"
#include "pathclash.hpp"

namespace EdenArchive
{
    namespace
    {
        namespace F = Format;

        class Checker
        {
        public:
            Checker(const Archive& archive, const ValidateOptions& options, ValidationReport& report)
                : mArchive(archive)
                , mImpl(archive.impl())
                , mOptions(options)
                , mReport(report)
            {
            }

            void run();

        private:
            bool add(ErrorClass errorClass, std::uint64_t offset, std::string field, std::string message)
            {
                if (mReport.mFindings.size() < mOptions.mMaxFindings)
                {
                    Error error;
                    error.mClass = errorClass;
                    error.mOffset = offset;
                    error.mField = std::move(field);
                    error.mMessage = std::move(message);
                    mReport.mFindings.push_back(std::move(error));
                }
                return mReport.mFindings.size() < mOptions.mMaxFindings;
            }
            bool full() const { return mReport.mFindings.size() >= mOptions.mMaxFindings; }

            void manifest();
            void directoryPrefixes();
            void gaps();
            void content();
            void canonical();

            const Archive& mArchive;
            const Archive::Impl& mImpl;
            const ValidateOptions& mOptions;
            ValidationReport& mReport;
        };

        void Checker::run()
        {
            manifest();
            gaps();
            if (mOptions.mCanonical)
            {
                directoryPrefixes();
                canonical();
            }
            if (mOptions.mVerifyContent)
                content();
        }

        // The manifest digest of the header must be the one computed from the entries (section 13.5).
        void Checker::manifest()
        {
            const PackInfo& info = mImpl.mInfo;
            ManifestBuilder builder(info.mPackFlags, info.mGeneration, info.mSourceDigest, info.mChunkSize, info.mEntryCount);
            for (std::size_t i = 0; i < mArchive.entryCount(); ++i)
            {
                const EntryInfo entry = mArchive.entry(i);
                builder.beginEntry(entry.mFlags, entry.mType, entry.mKey, entry.mHasPath ? entry.mPath : std::string_view(),
                    entry.mSize, entry.mContentHash, entry.mHintMemorySize, entry.mHintGroup, entry.mLayoutParamA,
                    entry.mLayoutParamB, entry.mChunkCount);
                for (std::uint32_t k = 0; k < entry.mChunkCount; ++k)
                {
                    const ChunkInfo chunk = mArchive.chunk(i, k);
                    builder.addChunk(chunk.mSize, chunk.mHash);
                }
                std::span<const std::uint8_t> record;
                if (entry.mHasProvenance)
                {
                    const std::uint8_t* raw = mImpl.entryRecord(i);
                    const std::uint8_t* prov = mImpl.at(F::rd64(raw + F::E::provenanceOffset));
                    record = std::span<const std::uint8_t>(prov, F::rd32(prov));
                }
                builder.endEntry(record);
            }
            if (builder.finalize() != info.mManifestDigest)
                add(ErrorClass::Checksum, F::H::manifestDigest, "manifestDigest",
                    "the manifest digest does not match the entries");
        }

        // A path must not also be a directory of another path (section 7.3.2).
        void Checker::directoryPrefixes()
        {
            std::size_t pathEntries = 0;
            while (pathEntries < mArchive.entryCount() && mArchive.entry(pathEntries).mHasPath)
                ++pathEntries;
            const std::optional<std::size_t> clash = findFileDirectoryClash(
                pathEntries, [this](std::size_t i) { return mArchive.entry(i).mPath; });
            if (clash)
                add(ErrorClass::Name, mImpl.entryRecordOffset(*clash) + F::E::pathOffset,
                    "entry[" + std::to_string(*clash) + "].path", "the path is also a directory of another path");
        }

        // Bytes between the chunks of the data region are covered by no digest and must be zero (section 5).
        void Checker::gaps()
        {
            std::uint64_t previousEnd = mImpl.mInfo.mDataOffset;
            for (std::size_t i = 0; i < mArchive.entryCount() && !full(); ++i)
            {
                const EntryInfo entry = mArchive.entry(i);
                for (std::uint32_t k = 0; k < entry.mChunkCount && !full(); ++k)
                {
                    const ChunkInfo chunk = mArchive.chunk(i, k);
                    bool failed = false;
                    std::uint8_t buffer[4096];
                    std::uint64_t at = previousEnd;
                    while (at < chunk.mOffset && !failed)
                    {
                        const std::size_t n = static_cast<std::size_t>(
                            std::min<std::uint64_t>(sizeof(buffer), chunk.mOffset - at));
                        if (!mImpl.mSource->read(at, buffer, n))
                        {
                            failed = true;
                            add(ErrorClass::Io, at, "gap", "the source could not be read");
                            break;
                        }
                        std::size_t nonZero = 0;
                        while (nonZero < n && buffer[nonZero] == 0)
                            ++nonZero;
                        if (nonZero < n)
                        {
                            add(ErrorClass::Range, at + nonZero, "gap", "a byte between two chunks is not zero");
                            break;
                        }
                        at += n;
                    }
                    previousEnd = chunk.mOffset + chunk.mStoredSize;
                }
            }
        }

        // Canonical form (section 17.2): everything a canonical writer fixes.
        void Checker::canonical()
        {
            const PackInfo& info = mImpl.mInfo;
            std::uint8_t header[512];
            std::uint8_t footer[128];
            if (!mImpl.mSource->read(0, header, sizeof(header))
                || !mImpl.mSource->read(info.mFileSize - F::footerSize, footer, sizeof(footer)))
            {
                add(ErrorClass::Io, 0, "header", "the source could not be read");
                return;
            }
            auto allZero = [](const std::uint8_t* p, std::size_t n) {
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (p[i] != 0)
                        return false;
                }
                return true;
            };
            if (!allZero(header + F::H::reserved52, 4))
                add(ErrorClass::Range, F::H::reserved52, "reserved", "a reserved header field is not zero");
            if (!allZero(header + F::H::reservedBegin, F::H::checksum - F::H::reservedBegin))
                add(ErrorClass::Range, F::H::reservedBegin, "reserved", "reserved header bytes are not zero");
            if (!allZero(footer + F::F::reservedBegin, 128 - F::F::reservedBegin))
                add(ErrorClass::Range, info.mFileSize - F::footerSize + F::F::reservedBegin, "footer.reserved",
                    "reserved footer bytes are not zero");
            if ((info.mPackFlags >> 32) != 0)
                add(ErrorClass::Range, F::H::packFlags, "packFlags", "an ignorable pack flag is set");
            if (info.mEntryRecordSize != F::entryRecordSize)
                add(ErrorClass::Range, F::H::entryRecordSize, "entryRecordSize", "a canonical pack has entryRecordSize 112");
            if (info.mChunkRecordSize != F::chunkRecordSize)
                add(ErrorClass::Range, F::H::chunkRecordSize, "chunkRecordSize", "a canonical pack has chunkRecordSize 56");
            if (info.mExtensionSize != 0)
                add(ErrorClass::Range, F::H::extensionSize, "extensionSize", "a canonical pack has no extension blocks");
            if ((info.mPackFlags & packFlagDerived) == 0)
            {
                if (info.mGeneration != 0)
                    add(ErrorClass::Range, F::H::generation, "generation", "generation is set without DERIVED");
                if (!allZero(header + F::H::sourceDigest, 32))
                    add(ErrorClass::Range, F::H::sourceDigest, "sourceDigest", "sourceDigest is set without DERIVED");
            }

            bool anyZstd = false;
            std::uint64_t previousEnd = info.mDataOffset;
            for (std::size_t i = 0; i < mArchive.entryCount() && !full(); ++i)
            {
                const EntryInfo entry = mArchive.entry(i);
                const std::uint8_t* record = mImpl.entryRecord(i);
                const std::uint64_t base = mImpl.entryRecordOffset(i);
                const std::string label = "entry[" + std::to_string(i) + "].";
                if (!allZero(record + F::E::reserved16, 2) || !allZero(record + F::E::reserved64, 8))
                    add(ErrorClass::Range, base, label + "reserved", "a reserved entry field is not zero");
                if ((entry.mFlags & 0xFFF00000u) != 0)
                    add(ErrorClass::Range, base + F::E::flags, label + "entryFlags", "an ignorable entry flag is set");
                if ((entry.mFlags & entryFlagHintMemSize) == 0 && entry.mHintMemorySize != 0)
                    add(ErrorClass::Range, base + F::E::hintMemorySize, label + "hintMemorySize",
                        "a hint value is set while its flag is clear");
                if ((entry.mFlags & entryFlagHintGroup) == 0 && entry.mHintGroup != 0)
                    add(ErrorClass::Range, base + F::E::hintGroup, label + "hintGroup",
                        "a hint value is set while its flag is clear");
                if (entry.mType == 0 && (entry.mLayoutParamA != 0 || entry.mLayoutParamB != 0))
                    add(ErrorClass::Range, base + F::E::layoutParamA, label + "layoutParam",
                        "layout parameters are set on a generic entry");
                if (entry.mHasProvenance)
                {
                    const std::uint8_t* prov = mImpl.at(F::rd64(record + F::E::provenanceOffset));
                    const std::uint32_t used = 48 + F::rd16(prov + 4) + F::rd32(prov + 40);
                    if (!allZero(prov + used, F::rd32(prov) - used) || !allZero(prov + 6, 2) || !allZero(prov + 44, 4))
                        add(ErrorClass::Range, F::rd64(record + F::E::provenanceOffset), label + "provenance",
                            "reserved or padding bytes of a provenance record are not zero");
                }
                for (std::uint32_t k = 0; k < entry.mChunkCount && !full(); ++k)
                {
                    const ChunkInfo chunk = mArchive.chunk(i, k);
                    const std::uint64_t table = F::rd64(record + F::E::chunkTableOffset) + k * info.mChunkRecordSize;
                    const std::string chunkLabel = label + "chunk[" + std::to_string(k) + "].";
                    if (!allZero(mImpl.at(table + F::C::reserved), 4))
                        add(ErrorClass::Range, table + F::C::reserved, chunkLabel + "reserved",
                            "a reserved chunk field is not zero");
                    if (chunk.mFlags != F::chunkFlagHashPresent)
                        add(ErrorClass::Range, table + F::C::flags, chunkLabel + "chunkFlags",
                            "a canonical chunk has exactly the flag HASH_PRESENT");
                    if (chunk.mCodec > codecZstd)
                        add(ErrorClass::Range, table + F::C::codec, chunkLabel + "codec",
                            "a canonical pack uses only the codecs store and zstd");
                    const bool last = k + 1 == entry.mChunkCount;
                    if (last ? chunk.mSize > info.mChunkSize : chunk.mSize != info.mChunkSize)
                        add(ErrorClass::Range, table + F::C::size, chunkLabel + "uncompressedSize",
                            "the chunks do not follow the default layout");
                    if (chunk.mCodec == codecZstd)
                    {
                        anyZstd = true;
                        if (chunk.mStoredSize > chunk.mSize - (chunk.mSize >> 4))
                            add(ErrorClass::Range, table + F::C::codec, chunkLabel + "codec",
                                "a compressed chunk must save at least 1/16");
                    }
                    std::uint64_t expected = previousEnd;
                    if (k == 0)
                        F::alignUp(previousEnd, F::blockAlignment, expected);
                    if (chunk.mOffset != expected)
                        add(ErrorClass::Range, table + F::C::offset, chunkLabel + "offset",
                            "the chunk is not where the canonical layout puts it");
                    previousEnd = chunk.mOffset + chunk.mStoredSize;
                }
            }
            if (!anyZstd && (info.mCompressorVersion != 0 || info.mCompressorLevel != 0))
                add(ErrorClass::Range, F::H::compressorVersion, "compressorVersion",
                    "compressor fields are set although no chunk uses zstd");
        }

        void Checker::content()
        {
            DecodeBudget budget;
            budget.mRemaining = mOptions.mDecodeBudget;
            for (std::size_t i = 0; i < mArchive.entryCount() && !full(); ++i)
            {
                const std::uint64_t before = budget.mRemaining;
                const Error error = mArchive.verifyEntry(i, &budget);
                mReport.mBytesDecoded += before - budget.mRemaining;
                if (error)
                {
                    add(error.mClass, error.mOffset, "entry[" + std::to_string(i) + "]." + error.mField, error.mMessage);
                    if (error.mClass == ErrorClass::Limit)
                        break;
                }
            }
        }
    }

    ValidationReport validate(std::shared_ptr<const Source> source, const ValidateOptions& options) noexcept
    {
        ValidationReport report;
        ValidateOptions limited = options;
        limited.mMaxFindings = std::max<std::size_t>(1, options.mMaxFindings); // 0 would hide every finding
        try
        {
            Error error;
            const std::unique_ptr<Archive> archive = Archive::open(std::move(source), OpenOptions{}, error);
            if (!archive)
            {
                report.mFindings.push_back(std::move(error));
                return report;
            }
            report.mOpened = true;
            Checker checker(*archive, limited, report);
            checker.run();
        }
        catch (const std::bad_alloc&)
        {
            Error error;
            error.mClass = ErrorClass::Limit;
            error.mField = "memory";
            error.mMessage = "out of memory"; // short enough to need no allocation
            report.mFindings.push_back(std::move(error));
        }
        catch (...)
        {
            Error error;
            error.mClass = ErrorClass::Io;
            error.mField = "validate";
            error.mMessage = "an unexpected exception while validating";
            report.mFindings.push_back(std::move(error));
        }
        return report;
    }
}
