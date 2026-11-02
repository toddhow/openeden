// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include "eapackmodel.hpp"

#include <algorithm>
#include <cstring>

namespace EdenTest
{
    namespace
    {
        constexpr std::uint8_t packMagic[8] = { 0x45, 0x44, 0x45, 0x4E, 0x50, 0x41, 0x4B, 0x01 };
        constexpr std::uint8_t footerMagic[8] = { 0x45, 0x44, 0x45, 0x4E, 0x45, 0x4E, 0x44, 0x01 };
        constexpr std::string_view manifestTag = "EDENPAK-MANIFEST-1";

        /// One chunk with its raw and stored bytes, ready to be placed in the data region.
        struct PreparedChunk
        {
            Bytes mRaw;
            Bytes mStored;
            std::uint16_t mCodec = 0;
            std::uint64_t mGapBefore = 0;
        };

        PreparedChunk encodeChunk(const Bytes& raw, CodecChoice choice, int level)
        {
            PreparedChunk chunk;
            chunk.mRaw = raw;
            const auto store = [&] {
                chunk.mStored = raw;
                chunk.mCodec = 0;
            };
            switch (choice)
            {
                case CodecChoice::Store:
                    store();
                    break;
                case CodecChoice::Zstd:
                case CodecChoice::ZstdChecksum:
                    chunk.mStored = zstdCompress(raw.data(), raw.size(), level, choice == CodecChoice::ZstdChecksum);
                    chunk.mCodec = 1;
                    break;
                case CodecChoice::HandRle:
                    chunk.mStored = makeRleFrame(raw.empty() ? 0 : raw[0], raw.size());
                    chunk.mCodec = 1;
                    break;
                case CodecChoice::Auto:
                {
                    Bytes z = zstdCompress(raw.data(), raw.size(), level, false);
                    // Section 9.2: use zstd only when it is smaller by at least 1/16.
                    if (!z.empty() && z.size() <= raw.size() - (raw.size() >> 4))
                    {
                        chunk.mStored = std::move(z);
                        chunk.mCodec = 1;
                    }
                    else
                        store();
                    break;
                }
            }
            return chunk;
        }

        struct PreparedEntry
        {
            Bytes mContent;
            std::vector<PreparedChunk> mChunks;
        };

        PreparedEntry prepareEntry(const EntrySpec& entry, std::uint32_t chunkSize, int level)
        {
            PreparedEntry prepared;
            if (!entry.mChunks.empty())
            {
                for (const ChunkSpec& spec : entry.mChunks)
                {
                    PreparedChunk chunk;
                    if (spec.mStored.has_value())
                    {
                        chunk.mRaw = spec.mRaw;
                        chunk.mStored = *spec.mStored;
                        chunk.mCodec = spec.mChoice == CodecChoice::Store ? 0 : 1;
                    }
                    else
                        chunk = encodeChunk(spec.mRaw, spec.mChoice, level);
                    if (spec.mCodecId.has_value())
                        chunk.mCodec = *spec.mCodecId;
                    chunk.mGapBefore = spec.mGapBefore;
                    appendBytes(prepared.mContent, chunk.mRaw);
                    prepared.mChunks.push_back(std::move(chunk));
                }
                return prepared;
            }
            prepared.mContent = entry.mContent;
            const std::size_t step = std::max<std::uint32_t>(chunkSize, 1);
            for (std::size_t at = 0; at < prepared.mContent.size(); at += step)
            {
                const std::size_t n = std::min(step, prepared.mContent.size() - at);
                const Bytes piece(prepared.mContent.begin() + static_cast<std::ptrdiff_t>(at),
                    prepared.mContent.begin() + static_cast<std::ptrdiff_t>(at + n));
                prepared.mChunks.push_back(encodeChunk(piece, entry.mCodec, level));
            }
            return prepared;
        }

        void putDigest(Bytes& bytes, std::size_t pos, const Digest& digest)
        {
            for (std::size_t i = 0; i < digest.size() && pos + i < bytes.size(); ++i)
                bytes[pos + i] = digest[i];
        }

        Digest getDigest(const Bytes& bytes, std::size_t pos)
        {
            Digest out{};
            for (std::size_t i = 0; i < out.size() && pos + i < bytes.size(); ++i)
                out[i] = bytes[pos + i];
            return out;
        }
    }

    int comparePaths(const std::string& a, const std::string& b)
    {
        const std::size_t common = std::min(a.size(), b.size());
        const int c = common == 0 ? 0 : std::memcmp(a.data(), b.data(), common);
        if (c != 0)
            return c < 0 ? -1 : 1;
        if (a.size() == b.size())
            return 0;
        return a.size() < b.size() ? -1 : 1;
    }

    std::vector<EntrySpec> entryOrder(const PackSpec& spec)
    {
        std::vector<EntrySpec> entries = spec.mEntries;
        if (!spec.mSortEntries)
            return entries;
        const auto group = [](const EntrySpec& e) { return e.mPath.has_value() ? 0 : (e.mKey.has_value() ? 1 : 2); };
        std::stable_sort(entries.begin(), entries.end(), [&](const EntrySpec& a, const EntrySpec& b) {
            const int ga = group(a);
            const int gb = group(b);
            if (ga != gb)
                return ga < gb;
            if (ga == 0)
                return comparePaths(*a.mPath, *b.mPath) < 0;
            if (ga == 1)
                return *a.mKey < *b.mKey; // unsigned
            return false;
        });
        return entries;
    }

    Bytes encodeEntryRec(const EntryRec& rec, std::size_t stride, std::uint8_t pad)
    {
        Bytes out(std::max<std::size_t>(stride, 112), pad);
        std::fill(out.begin(), out.begin() + 112, std::uint8_t{ 0 });
        putLe(out, Fmt::eKey, rec.mKey, 8);
        putLe(out, Fmt::ePathOffset, rec.mPathOffset, 8);
        putLe(out, Fmt::ePathLength, rec.mPathLength, 4);
        putLe(out, Fmt::eType, rec.mType, 2);
        putLe(out, Fmt::eReserved22, rec.mReserved22, 2);
        putLe(out, Fmt::eFlags, rec.mFlags, 4);
        putLe(out, Fmt::eChunkCount, rec.mChunkCount, 4);
        putLe(out, Fmt::eSize, rec.mSize, 8);
        putLe(out, Fmt::eChunkTableOffset, rec.mChunkTableOffset, 8);
        putDigest(out, Fmt::eContentHash, rec.mContentHash);
        putLe(out, Fmt::eProvenanceOffset, rec.mProvenanceOffset, 8);
        putLe(out, Fmt::eHintMemory, rec.mHintMemorySize, 4);
        putLe(out, Fmt::eHintGroup, rec.mHintGroup, 4);
        putLe(out, Fmt::eLayoutA, rec.mLayoutParamA, 4);
        putLe(out, Fmt::eLayoutB, rec.mLayoutParamB, 4);
        putLe(out, Fmt::eReserved104, rec.mReserved104, 8);
        out.resize(stride);
        return out;
    }

    Bytes encodeChunkRec(const ChunkRec& rec, std::size_t stride, std::uint8_t pad)
    {
        Bytes out(std::max<std::size_t>(stride, 56), pad);
        std::fill(out.begin(), out.begin() + 56, std::uint8_t{ 0 });
        putLe(out, Fmt::cOffset, rec.mOffset, 8);
        putLe(out, Fmt::cStoredSize, rec.mStoredSize, 4);
        putLe(out, Fmt::cSize, rec.mSize, 4);
        putLe(out, Fmt::cCodec, rec.mCodec, 2);
        putLe(out, Fmt::cFlags, rec.mFlags, 2);
        putLe(out, Fmt::cReserved, rec.mReserved20, 4);
        putDigest(out, Fmt::cHash, rec.mHash);
        out.resize(stride);
        return out;
    }

    Bytes encodeProvenance(const ProvenanceSpec& spec)
    {
        const std::uint64_t used = 48 + spec.mToolId.size() + spec.mParameters.size();
        const std::uint64_t recordSize = alignUp(used, 8);
        Bytes out;
        appendLe(out, recordSize, 4);
        appendLe(out, spec.mToolId.size(), 2);
        appendLe(out, 0, 2);
        appendBytes(out, spec.mSourceDigest.data(), spec.mSourceDigest.size());
        appendLe(out, spec.mParameters.size(), 4);
        appendLe(out, 0, 4);
        appendText(out, spec.mToolId);
        appendBytes(out, spec.mParameters);
        out.resize(recordSize, 0);
        return out;
    }

    std::uint64_t entryFieldOffset(const PackImage& image, std::size_t index, std::size_t fieldOffset)
    {
        return getLe(image.mHeader, Fmt::hEntryTableOffset, 8) + index * image.mEntryStride + fieldOffset;
    }

    std::uint64_t chunkFieldOffset(const PackImage& image, std::size_t index, std::size_t fieldOffset)
    {
        return getLe(image.mHeader, Fmt::hChunkTablesOffset, 8) + index * image.mChunkStride + fieldOffset;
    }

    void relayoutRegions(PackImage& image)
    {
        Bytes& h = image.mHeader;
        const std::uint64_t metadataOffset = image.mBody.size();
        std::uint64_t at = metadataOffset;
        putLe(h, Fmt::hMetadataOffset, metadataOffset, 8);

        putLe(h, Fmt::hStringTableOffset, at, 8);
        putLe(h, Fmt::hStringTableSize, image.mStringTable.size(), 8);
        at += image.mStringTable.size();

        putLe(h, Fmt::hEntryTableOffset, at, 8);
        at += image.mEntries.size() * image.mEntryStride;

        putLe(h, Fmt::hPathIndexOffset, at, 8);
        at += image.mPathIndex.size() * 16;

        putLe(h, Fmt::hKeyIndexOffset, at, 8);
        at += image.mKeyIndex.size() * 16;

        putLe(h, Fmt::hChunkTablesOffset, at, 8);
        putLe(h, Fmt::hChunkTablesSize, image.mChunks.size() * image.mChunkStride, 8);
        at += image.mChunks.size() * image.mChunkStride;

        putLe(h, Fmt::hProvenanceOffset, at, 8);
        putLe(h, Fmt::hProvenanceSize, image.mProvenance.size(), 8);
        at += image.mProvenance.size();

        putLe(h, Fmt::hExtensionOffset, at, 8);
        putLe(h, Fmt::hExtensionSize, image.mExtension.size(), 8);
        at += image.mExtension.size();

        putLe(h, Fmt::hMetadataSize, at - metadataOffset, 8);
        putLe(h, Fmt::hFileSize, at + Fmt::footerSize, 8);
    }

    namespace
    {
        /// The bytes of an entry's path, or an empty string when the (offset, length) pair is outside the table.
        std::string pathOfEntry(const PackImage& image, const EntryRec& entry)
        {
            if ((entry.mFlags & Fmt::entryHasPath) == 0)
                return {};
            const std::uint64_t size = image.mStringTable.size();
            if (entry.mPathOffset > size || entry.mPathLength > size - entry.mPathOffset)
                return {};
            return std::string(image.mStringTable.begin() + static_cast<std::ptrdiff_t>(entry.mPathOffset),
                image.mStringTable.begin() + static_cast<std::ptrdiff_t>(entry.mPathOffset + entry.mPathLength));
        }
    }

    void rebuildPathIndex(PackImage& image)
    {
        image.mPathIndex.clear();
        for (std::size_t i = 0; i < image.mEntries.size(); ++i)
        {
            if ((image.mEntries[i].mFlags & Fmt::entryHasPath) != 0)
                image.mPathIndex.push_back({ pathHashOf(pathOfEntry(image, image.mEntries[i])), i });
        }
        std::sort(image.mPathIndex.begin(), image.mPathIndex.end(), [](const IndexRec& a, const IndexRec& b) {
            return a.mFirst != b.mFirst ? a.mFirst < b.mFirst : a.mEntryIndex < b.mEntryIndex;
        });
    }

    void rebuildKeyIndex(PackImage& image)
    {
        image.mKeyIndex.clear();
        for (std::size_t i = 0; i < image.mEntries.size(); ++i)
        {
            if ((image.mEntries[i].mFlags & Fmt::entryHasKey) != 0)
                image.mKeyIndex.push_back({ image.mEntries[i].mKey, i });
        }
        std::sort(image.mKeyIndex.begin(), image.mKeyIndex.end(), [](const IndexRec& a, const IndexRec& b) {
            return a.mFirst != b.mFirst ? a.mFirst < b.mFirst : a.mEntryIndex < b.mEntryIndex;
        });
    }

    void fixDerivedOffsets(PackImage& image)
    {
        const std::uint64_t chunkTables = getLe(image.mHeader, Fmt::hChunkTablesOffset, 8);
        const std::uint64_t provenanceBase = getLe(image.mHeader, Fmt::hProvenanceOffset, 8);
        std::uint64_t chunkCursor = 0;
        std::uint64_t provenanceCursor = 0;
        for (EntryRec& rec : image.mEntries)
        {
            rec.mChunkTableOffset = rec.mChunkCount > 0 ? chunkTables + image.mChunkStride * chunkCursor : 0;
            chunkCursor += rec.mChunkCount;
            if ((rec.mFlags & Fmt::entryHasProvenance) != 0)
            {
                rec.mProvenanceOffset = provenanceBase + provenanceCursor;
                provenanceCursor += getLe(image.mProvenance, provenanceCursor, 4);
            }
            else
                rec.mProvenanceOffset = 0;
        }
    }

    PackImage layoutPack(const PackSpec& spec)
    {
        PackImage image;
        image.mEntryStride = spec.mEntryRecordSize;
        image.mChunkStride = spec.mChunkRecordSize;
        image.mPadByte = spec.mRecordPadByte;

        const std::vector<EntrySpec> order = entryOrder(spec);
        std::vector<PreparedEntry> prepared;
        prepared.reserve(order.size());
        bool anyZstd = false;
        for (const EntrySpec& entry : order)
        {
            prepared.push_back(prepareEntry(entry, spec.mChunkSize, spec.mZstdLevel));
            for (const PreparedChunk& chunk : prepared.back().mChunks)
                anyZstd = anyZstd || chunk.mCodec == 1;
        }

        // Data region (section 8.1 and 17.2 step 6): each block starts at a multiple of 4096, chunks back to back.
        struct Placement
        {
            std::uint64_t mOffset;
            const Bytes* mBytes;
        };
        std::vector<Placement> placements;
        std::vector<std::vector<std::uint64_t>> chunkOffsets(order.size());
        std::uint64_t position = Fmt::dataOffset;
        std::uint64_t lastEnd = 0;
        bool anyChunk = false;
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            if (prepared[i].mChunks.empty())
                continue;
            position = alignUp(position, Fmt::blockAlign) + order[i].mBlockGap;
            for (const PreparedChunk& chunk : prepared[i].mChunks)
            {
                position += chunk.mGapBefore;
                chunkOffsets[i].push_back(position);
                placements.push_back({ position, &chunk.mStored });
                position += chunk.mStored.size();
            }
            lastEnd = position;
            anyChunk = true;
        }
        const std::uint64_t dataSize = anyChunk ? lastEnd - Fmt::dataOffset : 0;
        const std::uint64_t metadataOffset = alignUp(Fmt::dataOffset + dataSize, Fmt::blockAlign);
        image.mBody.assign(metadataOffset, 0);
        for (const Placement& p : placements)
            std::copy(p.mBytes->begin(), p.mBytes->end(), image.mBody.begin() + static_cast<std::ptrdiff_t>(p.mOffset));

        // String table, entry records, chunk records, provenance, extension blocks.
        std::uint64_t stringBytes = 0;
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            const EntrySpec& entry = order[i];
            EntryRec rec;
            rec.mFlags = entry.mExtraFlags;
            if (entry.mPath.has_value())
            {
                rec.mFlags |= Fmt::entryHasPath;
                rec.mPathOffset = stringBytes;
                rec.mPathLength = static_cast<std::uint32_t>(entry.mPath->size());
                appendText(image.mStringTable, *entry.mPath);
                stringBytes += entry.mPath->size();
            }
            if (entry.mKey.has_value())
            {
                rec.mFlags |= Fmt::entryHasKey;
                rec.mKey = *entry.mKey;
            }
            if (entry.mProvenance.has_value())
            {
                rec.mFlags |= Fmt::entryHasProvenance;
                appendBytes(image.mProvenance, encodeProvenance(*entry.mProvenance));
            }
            rec.mType = entry.mType;
            rec.mHintMemorySize = entry.mHintMemorySize;
            rec.mHintGroup = entry.mHintGroup;
            rec.mLayoutParamA = entry.mLayoutParamA;
            rec.mLayoutParamB = entry.mLayoutParamB;
            rec.mSize = prepared[i].mContent.size();
            rec.mChunkCount = static_cast<std::uint32_t>(prepared[i].mChunks.size());
            rec.mContentHash = blake3Of(prepared[i].mContent);
            image.mEntries.push_back(rec);
            for (std::size_t c = 0; c < prepared[i].mChunks.size(); ++c)
            {
                const PreparedChunk& chunk = prepared[i].mChunks[c];
                ChunkRec chunkRec;
                chunkRec.mOffset = chunkOffsets[i][c];
                chunkRec.mStoredSize = static_cast<std::uint32_t>(chunk.mStored.size());
                chunkRec.mSize = static_cast<std::uint32_t>(chunk.mRaw.size());
                chunkRec.mCodec = chunk.mCodec;
                chunkRec.mHash = blake3Of(chunk.mRaw);
                image.mChunks.push_back(chunkRec);
            }
        }
        image.mStringTable.resize(alignUp(image.mStringTable.size(), 8), 0);
        rebuildPathIndex(image);
        rebuildKeyIndex(image);

        for (const ExtensionBlockSpec& block : spec.mExtensions)
        {
            appendLe(image.mExtension, block.mId, 4);
            appendLe(image.mExtension, block.mFlags, 4);
            appendLe(image.mExtension, block.mPayload.size(), 8);
            appendBytes(image.mExtension, block.mPayload);
            image.mExtension.resize(alignUp(image.mExtension.size(), 8), 0);
        }

        // Header.
        Bytes& h = image.mHeader;
        h.assign(Fmt::headerSize, 0);
        std::memcpy(h.data(), packMagic, 8);
        putLe(h, Fmt::hVersionMajor, 0, 2);
        putLe(h, Fmt::hVersionMinor, 1, 2);
        putLe(h, Fmt::hHeaderSize, Fmt::headerSize, 4);
        putLe(h, Fmt::hPackFlags, spec.mPackFlags, 8);
        putLe(h, Fmt::hChunkSize, spec.mChunkSize, 4);
        putLe(h, Fmt::hEntryRecordSize, spec.mEntryRecordSize, 4);
        putLe(h, Fmt::hChunkRecordSize, spec.mChunkRecordSize, 4);
        putLe(h, Fmt::hCompressorVersion, spec.mCompressorVersion.value_or(anyZstd ? zstdVersionNumber() : 0), 4);
        putLe(h, Fmt::hCompressorLevel,
            static_cast<std::uint32_t>(spec.mCompressorLevel.value_or(anyZstd ? spec.mZstdLevel : 0)), 4);
        putLe(h, Fmt::hEntryCount, image.mEntries.size(), 8);
        putLe(h, Fmt::hPathEntryCount, image.mPathIndex.size(), 8);
        putLe(h, Fmt::hKeyEntryCount, image.mKeyIndex.size(), 8);
        putLe(h, Fmt::hDataOffset, Fmt::dataOffset, 8);
        putLe(h, Fmt::hDataSize, dataSize, 8);
        putLe(h, Fmt::hGeneration, spec.mGeneration, 8);
        putDigest(h, Fmt::hSourceDigest, spec.mSourceDigest);
        relayoutRegions(image);

        fixDerivedOffsets(image);

        image.mFooter.assign(Fmt::footerSize, 0);
        return image;
    }

    Digest manifestDigestOf(const PackImage& image)
    {
        const Bytes& h = image.mHeader;
        Bytes m;
        appendText(m, manifestTag);
        appendLe(m, getLe(h, Fmt::hPackFlags, 8), 8);
        appendLe(m, getLe(h, Fmt::hGeneration, 8), 8);
        appendBytes(m, h.data() + Fmt::hSourceDigest, 32);
        appendLe(m, getLe(h, Fmt::hChunkSize, 4), 4);
        appendLe(m, image.mEntries.size(), 8);
        const std::uint64_t provenanceBase = getLe(h, Fmt::hProvenanceOffset, 8);
        std::size_t chunkCursor = 0;
        for (const EntryRec& e : image.mEntries)
        {
            appendLe(m, e.mFlags, 4);
            appendLe(m, e.mType, 2);
            appendLe(m, 0, 2);
            appendLe(m, e.mKey, 8);
            appendLe(m, e.mPathLength, 4);
            if ((e.mFlags & Fmt::entryHasPath) != 0)
                appendText(m, pathOfEntry(image, e));
            appendLe(m, e.mSize, 8);
            appendBytes(m, e.mContentHash.data(), 32);
            appendLe(m, e.mHintMemorySize, 4);
            appendLe(m, e.mHintGroup, 4);
            appendLe(m, e.mLayoutParamA, 4);
            appendLe(m, e.mLayoutParamB, 4);
            appendLe(m, e.mChunkCount, 4);
            for (std::uint32_t c = 0; c < e.mChunkCount; ++c, ++chunkCursor)
            {
                const ChunkRec chunk = chunkCursor < image.mChunks.size() ? image.mChunks[chunkCursor] : ChunkRec{};
                appendLe(m, chunk.mSize, 4);
                appendBytes(m, chunk.mHash.data(), 32);
            }
            std::uint64_t recordSize = 0;
            if ((e.mFlags & Fmt::entryHasProvenance) != 0 && e.mProvenanceOffset >= provenanceBase)
            {
                const std::uint64_t rel = e.mProvenanceOffset - provenanceBase;
                if (rel <= image.mProvenance.size() && image.mProvenance.size() - rel >= 4)
                {
                    const std::uint64_t size = getLe(image.mProvenance, rel, 4);
                    if (size <= image.mProvenance.size() - rel)
                        recordSize = size;
                }
            }
            appendLe(m, recordSize, 4);
            if (recordSize > 0)
            {
                const std::uint64_t rel = e.mProvenanceOffset - provenanceBase;
                m.insert(m.end(), image.mProvenance.begin() + static_cast<std::ptrdiff_t>(rel),
                    image.mProvenance.begin() + static_cast<std::ptrdiff_t>(rel + recordSize));
            }
        }
        return blake3Of(m);
    }

    Bytes serialisePack(const PackImage& image)
    {
        Bytes header = image.mHeader;
        header.resize(Fmt::headerSize, 0);
        Bytes file = image.mBody;
        if (file.size() < Fmt::headerSize)
            file.resize(Fmt::headerSize, 0);
        appendBytes(file, image.mStringTable);
        for (const EntryRec& rec : image.mEntries)
            appendBytes(file, encodeEntryRec(rec, image.mEntryStride, image.mPadByte));
        for (const IndexRec& rec : image.mPathIndex)
        {
            appendLe(file, rec.mFirst, 8);
            appendLe(file, rec.mEntryIndex, 8);
        }
        for (const IndexRec& rec : image.mKeyIndex)
        {
            appendLe(file, rec.mFirst, 8);
            appendLe(file, rec.mEntryIndex, 8);
        }
        for (const ChunkRec& rec : image.mChunks)
            appendBytes(file, encodeChunkRec(rec, image.mChunkStride, image.mPadByte));
        appendBytes(file, image.mProvenance);
        appendBytes(file, image.mExtension);
        const std::size_t footerPos = file.size();
        file.resize(footerPos + Fmt::footerSize, 0);

        if (!image.mKeepMetadataDigest)
        {
            // What a reader computes: the bytes the header says, clamped to the file before the footer.
            const std::uint64_t offset = std::min<std::uint64_t>(getLe(header, Fmt::hMetadataOffset, 8), footerPos);
            const std::uint64_t size = getLe(header, Fmt::hMetadataSize, 8);
            const std::uint64_t end = size > footerPos - offset ? footerPos : offset + size;
            putDigest(header, Fmt::hMetadataDigest, blake3Of(file.data() + offset, end - offset));
        }
        if (!image.mKeepManifest)
            putDigest(header, Fmt::hManifestDigest, manifestDigestOf(image));
        if (!image.mKeepHeaderChecksum)
            putDigest(header, Fmt::hHeaderChecksum, blake3Of(header.data(), Fmt::hHeaderChecksum));
        std::copy(header.begin(), header.end(), file.begin());

        Bytes footer(Fmt::footerSize, 0);
        if (image.mKeepFooter)
        {
            footer = image.mFooter;
            footer.resize(Fmt::footerSize, 0);
        }
        else
        {
            std::memcpy(footer.data(), footerMagic, 8);
            putLe(footer, Fmt::fVersionMajor, getLe(header, Fmt::hVersionMajor, 2), 2);
            putLe(footer, Fmt::fVersionMinor, getLe(header, Fmt::hVersionMinor, 2), 2);
            putLe(footer, Fmt::fFooterSize, Fmt::footerSize, 4);
            putLe(footer, Fmt::fFileSize, getLe(header, Fmt::hFileSize, 8), 8);
            putLe(footer, Fmt::fMetadataOffset, getLe(header, Fmt::hMetadataOffset, 8), 8);
            putLe(footer, Fmt::fMetadataSize, getLe(header, Fmt::hMetadataSize, 8), 8);
            putDigest(footer, Fmt::fMetadataDigest, getDigest(header, Fmt::hMetadataDigest));
            putDigest(footer, Fmt::fHeaderChecksum, getDigest(header, Fmt::hHeaderChecksum));
            for (std::size_t i = Fmt::fReserved; i < Fmt::footerSize && i < image.mFooter.size(); ++i)
                footer[i] = image.mFooter[i];
        }
        std::copy(footer.begin(), footer.end(), file.begin() + static_cast<std::ptrdiff_t>(footerPos));
        return file;
    }

    Bytes buildPack(const PackSpec& spec)
    {
        PackImage image = layoutPack(spec);
        if (spec.mMutate)
            spec.mMutate(image);
        return serialisePack(image);
    }
}
