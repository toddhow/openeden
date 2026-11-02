// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The writer: produces the canonical pack of section 17.2 of the specification from a list of entries. The output
// does not depend on the order of the entries or on the number of threads.
#include <edenarchive/writer.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <new>
#include <numeric>
#include <thread>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <edenarchive/archive.hpp>

#include "format.hpp"
#include "manifest.hpp"
#include "pathclash.hpp"
#include "utf8.hpp"

namespace EdenArchive
{
    namespace
    {
        namespace F = Format;

        Error makeError(ErrorClass errorClass, const std::string& field, std::string message)
        {
            Error error;
            error.mClass = errorClass;
            error.mField = field;
            error.mMessage = std::move(message);
            return error;
        }

        // Where the pack goes. Bytes are written in order; the header is written last, at offset 0.
        class Sink
        {
        public:
            virtual ~Sink() = default;
            virtual bool write(const void* data, std::size_t size) = 0;
            virtual bool writeAt(std::uint64_t offset, const void* data, std::size_t size) = 0;
        };

        class MemorySink final : public Sink
        {
        public:
            explicit MemorySink(std::vector<std::uint8_t>& bytes)
                : mBytes(bytes)
            {
                mBytes.clear();
            }
            bool write(const void* data, std::size_t size) override
            {
                const auto* p = static_cast<const std::uint8_t*>(data);
                mBytes.insert(mBytes.end(), p, p + size);
                return true;
            }
            bool writeAt(std::uint64_t offset, const void* data, std::size_t size) override
            {
                if (offset > mBytes.size() || size > mBytes.size() - offset)
                    return false;
                std::memcpy(mBytes.data() + offset, data, size);
                return true;
            }

        private:
            std::vector<std::uint8_t>& mBytes;
        };

        class FileSink final : public Sink
        {
        public:
            explicit FileSink(std::ofstream& stream)
                : mStream(stream)
            {
            }
            bool write(const void* data, std::size_t size) override
            {
                mStream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                return mStream.good();
            }
            bool writeAt(std::uint64_t offset, const void* data, std::size_t size) override
            {
                mStream.seekp(static_cast<std::streamoff>(offset));
                mStream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                return mStream.good();
            }

        private:
            std::ofstream& mStream;
        };

        struct Prepared
        {
            const InputEntry* mInput = nullptr;
            std::uint64_t mSize = 0;
            std::uint32_t mFlags = 0;
            std::uint64_t mKey = 0;
        };

        struct ChunkOut
        {
            std::uint64_t mOffset = 0;
            std::uint32_t mStored = 0;
            std::uint32_t mSize = 0;
            std::uint16_t mCodec = 0;
            Hash mHash{};
        };

        struct EntryOut
        {
            Hash mContent{};
            std::uint64_t mFirstChunk = 0;
            std::uint32_t mChunkCount = 0;
        };

        // One chunk's work: hash it, and maybe compress it. Filled by worker threads, each work item by one thread.
        struct Work
        {
            const std::uint8_t* mData = nullptr;
            std::uint32_t mSize = 0;
            Hash mHash{};
            bool mUseZstd = false;
            bool mFailed = false;
            std::vector<std::uint8_t> mPacked;
        };

        struct Compressor
        {
            ZSTD_CCtx* mContext = nullptr;
            Compressor() = default;
            Compressor(const Compressor&) = delete;
            Compressor& operator=(const Compressor&) = delete;
            ~Compressor() { ZSTD_freeCCtx(mContext); }
        };

        void processChunk(Work& work, bool allowZstd, int level) noexcept
        {
            try
            {
                work.mHash = blake3(work.mData, work.mSize);
                work.mUseZstd = false;
                if (!allowZstd)
                    return;
                thread_local Compressor compressor;
                if (compressor.mContext == nullptr)
                {
                    compressor.mContext = ZSTD_createCCtx();
                    if (compressor.mContext == nullptr)
                    {
                        work.mFailed = true;
                        return;
                    }
                }
                ZSTD_CCtx* context = compressor.mContext;
                // The parameters of section 17.4, set explicitly so that they do not depend on library defaults.
                bool configured = !ZSTD_isError(ZSTD_CCtx_reset(context, ZSTD_reset_session_and_parameters));
                auto set = [&](ZSTD_cParameter parameter, int value) {
                    configured = configured && !ZSTD_isError(ZSTD_CCtx_setParameter(context, parameter, value));
                };
                set(ZSTD_c_compressionLevel, level);
                set(ZSTD_c_checksumFlag, 0);
                set(ZSTD_c_contentSizeFlag, 1);
                set(ZSTD_c_dictIDFlag, 0);
                set(ZSTD_c_nbWorkers, 0);
                set(ZSTD_c_windowLog, 23);
                set(ZSTD_c_useRowMatchFinder, ZSTD_ps_disable);
                if (!configured)
                {
                    work.mFailed = true;
                    return;
                }
                work.mPacked.resize(ZSTD_compressBound(work.mSize));
                const std::size_t packed
                    = ZSTD_compress2(context, work.mPacked.data(), work.mPacked.size(), work.mData, work.mSize);
                if (ZSTD_isError(packed))
                {
                    work.mFailed = true;
                    return;
                }
                const std::uint64_t u = work.mSize;
                if (packed <= u - (u >> 4)) // section 9.2: only when it saves at least 1/16
                {
                    work.mPacked.resize(packed);
                    work.mUseZstd = true;
                }
                else
                    work.mPacked.clear();
            }
            catch (...)
            {
                work.mFailed = true;
            }
        }

        void zeros(Sink& sink, std::uint64_t count, bool& ok)
        {
            static const std::uint8_t block[4096] = {};
            while (count > 0 && ok)
            {
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(count, sizeof(block)));
                ok = sink.write(block, n);
                count -= n;
            }
        }

        Error writeTo(const std::vector<InputEntry>& entries, Sink& sink, const WriteOptions& options)
        {
            const std::uint32_t chunkSize = options.mChunkSize;
            if (chunkSize < 4096 || chunkSize > F::maxChunkSize || (chunkSize & (chunkSize - 1)) != 0)
                return makeError(ErrorClass::Range, "chunkSize", "the chunk size is not a power of two from 4096 to 8 MiB");
            if (entries.size() > F::maxEntries)
                return makeError(ErrorClass::Limit, "entryCount", "more than 2^22 entries");

            // Validate and measure every entry before anything is written.
            std::vector<Prepared> prepared(entries.size());
            std::uint64_t totalContent = 0;
            std::uint64_t totalChunks = 0;
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const InputEntry& in = entries[i];
                Prepared& p = prepared[i];
                p.mInput = &in;
                const std::string label = "entry[" + std::to_string(i) + "]";
                if (!in.mPath && !in.mKey)
                    return makeError(ErrorClass::Range, label, "the entry has neither a path nor a key");
                if (in.mKey && *in.mKey == 0)
                    return makeError(ErrorClass::Range, label + ".key", "key 0 is not valid");
                if (in.mPath)
                {
                    std::string reason;
                    if (const ErrorClass c = checkStoredPath(*in.mPath, reason); c != ErrorClass::None)
                        return makeError(c, label + ".path", reason);
                }
                if ((in.mHintFlags & ~0x000F0000u) != 0)
                    return makeError(ErrorClass::Range, label + ".hintFlags", "only entry flag bits 16 to 19 are hints");
                if ((in.mHintMemorySize != 0 && (in.mHintFlags & entryFlagHintMemSize) == 0)
                    || (in.mHintGroup != 0 && (in.mHintFlags & entryFlagHintGroup) == 0))
                    return makeError(ErrorClass::Range, label + ".hint", "a hint value needs its hint flag");
                if (in.mType == 0 && (in.mLayoutParamA != 0 || in.mLayoutParamB != 0))
                    return makeError(ErrorClass::Range, label + ".layoutParam", "layout parameters need a typed entry");
                if (in.mType >= 0x10 && in.mType <= 0x13 && (!in.mKey || (*in.mKey >> 56) != (in.mType & 0xFFu)))
                    return makeError(
                        ErrorClass::Range, label + ".type", "a tile type needs a key whose tag is the low byte of the type");
                if (in.mProvenance)
                {
                    const ProvenanceInput& prov = *in.mProvenance;
                    if (prov.mToolId.empty() || prov.mToolId.size() > F::maxToolId)
                        return makeError(ErrorClass::Limit, label + ".provenance", "the tool id is not 1 to 255 bytes");
                    if (!isValidUtf8(prov.mToolId) || prov.mToolId.find('\0') != std::string::npos)
                        return makeError(ErrorClass::Name, label + ".provenance", "the tool id is not valid text");
                    if (prov.mParameters.size() > F::maxProvenanceParameters)
                        return makeError(ErrorClass::Limit, label + ".provenance", "more than 60,000 parameter bytes");
                }
                if (!in.mFile.empty())
                {
                    std::error_code ec;
                    p.mSize = std::filesystem::file_size(in.mFile, ec);
                    if (ec)
                    {
                        Error error = makeError(ErrorClass::Io, label + ".file", "cannot get the size of the input file");
                        error.mMessage += " '" + in.mFile.string() + "'";
                        return error;
                    }
                }
                else
                    p.mSize = in.mData.size();
                if (p.mSize > F::maxEntrySize)
                    return makeError(ErrorClass::Limit, label + ".size", "an entry larger than 4 GiB");
                totalContent += p.mSize;
                if (totalContent > F::maxPackContent)
                    return makeError(ErrorClass::Limit, label + ".size", "more than 2^40 content bytes");
                totalChunks += (p.mSize + chunkSize - 1) / chunkSize;
                if (totalChunks > F::maxChunksPerPack)
                    return makeError(ErrorClass::Limit, label + ".size", "more than 2^25 chunks");
                p.mFlags = in.mHintFlags | (in.mPath ? entryFlagHasPath : 0) | (in.mKey ? entryFlagHasKey : 0)
                    | (in.mProvenance ? entryFlagHasProvenance : 0);
                p.mKey = in.mKey ? *in.mKey : 0;
            }

            // The entry order of section 7.4: path entries by path, then key-only entries by key.
            std::vector<std::size_t> order(entries.size());
            std::iota(order.begin(), order.end(), std::size_t{ 0 });
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const InputEntry& x = entries[a];
                const InputEntry& y = entries[b];
                if (x.mPath && y.mPath)
                    return comparePaths(*x.mPath, *y.mPath) < 0;
                if (x.mPath != std::nullopt || y.mPath != std::nullopt)
                    return x.mPath.has_value();
                return *x.mKey < *y.mKey;
            });
            std::size_t pathEntries = 0;
            std::size_t keyEntries = 0;
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                const InputEntry& in = entries[order[k]];
                if (in.mPath)
                    ++pathEntries;
                if (in.mKey)
                    ++keyEntries;
                if (k > 0 && in.mPath && entries[order[k - 1]].mPath
                    && comparePaths(*entries[order[k - 1]].mPath, *in.mPath) == 0)
                    return makeError(ErrorClass::Duplicate, "path", "the path '" + *in.mPath + "' is given twice");
                if (k > 0 && !in.mPath && !entries[order[k - 1]].mPath && *entries[order[k - 1]].mKey == *in.mKey)
                    return makeError(ErrorClass::Duplicate, "key", "a key is given twice");
            }
            if (const std::optional<std::size_t> clash = findFileDirectoryClash(pathEntries,
                    [&](std::size_t k) { return std::string_view(*entries[order[k]].mPath); }))
                return makeError(ErrorClass::Name, "path",
                    "the path '" + *entries[order[*clash]].mPath + "' is also a directory of another path");
            {
                std::vector<std::uint64_t> keys;
                keys.reserve(keyEntries);
                for (const InputEntry& in : entries)
                {
                    if (in.mKey)
                        keys.push_back(*in.mKey);
                }
                std::sort(keys.begin(), keys.end());
                if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
                    return makeError(ErrorClass::Duplicate, "key", "a key is given twice");
            }

            // Data region. The first 4096 bytes are the header and zero padding; the header is written at the end.
            bool ok = true;
            zeros(sink, F::dataOffset, ok);
            std::uint64_t position = F::dataOffset;
            std::vector<ChunkOut> chunks;
            chunks.reserve(static_cast<std::size_t>(totalChunks));
            std::vector<EntryOut> outs(order.size());
            bool anyZstd = false;

            unsigned threads = options.mThreads == 0 ? std::thread::hardware_concurrency() : options.mThreads;
            threads = std::max(1u, threads);
            const std::uint64_t windowBytes = std::max<std::uint64_t>(
                chunkSize, options.mWorkWindow / chunkSize * chunkSize);

            std::vector<std::uint8_t> window;
            std::vector<Work> works;
            for (std::size_t k = 0; k < order.size() && ok; ++k)
            {
                const Prepared& p = prepared[order[k]];
                EntryOut& out = outs[k];
                out.mFirstChunk = chunks.size();
                if (p.mSize == 0)
                {
                    out.mContent = emptyHash();
                    continue;
                }
                std::uint64_t aligned = 0;
                F::alignUp(position, F::blockAlignment, aligned);
                zeros(sink, aligned - position, ok);
                position = aligned;

                const bool allowZstd = !options.mStoreOnly && p.mInput->mCompression == Compression::Auto;
                std::ifstream file;
                if (!p.mInput->mFile.empty())
                {
                    file.open(p.mInput->mFile, std::ios::binary);
                    if (!file)
                    {
                        Error error = makeError(ErrorClass::Io, "entry[" + std::to_string(order[k]) + "].file",
                            "cannot open the input file");
                        error.mMessage += " '" + p.mInput->mFile.string() + "'";
                        return error;
                    }
                }
                Hasher content;
                std::uint64_t done = 0;
                while (done < p.mSize && ok)
                {
                    const std::uint64_t take = std::min(windowBytes, p.mSize - done);
                    const std::uint8_t* base = nullptr;
                    if (p.mInput->mFile.empty())
                        base = p.mInput->mData.data() + done;
                    else
                    {
                        window.resize(static_cast<std::size_t>(take));
                        file.read(reinterpret_cast<char*>(window.data()), static_cast<std::streamsize>(take));
                        if (static_cast<std::uint64_t>(file.gcount()) != take)
                            return makeError(ErrorClass::Io, "entry[" + std::to_string(order[k]) + "].file",
                                "the input file is shorter than it was when measured");
                        base = window.data();
                    }
                    const std::size_t pieces = static_cast<std::size_t>((take + chunkSize - 1) / chunkSize);
                    works.assign(pieces, Work{});
                    for (std::size_t q = 0; q < pieces; ++q)
                    {
                        works[q].mData = base + q * chunkSize;
                        works[q].mSize = static_cast<std::uint32_t>(std::min<std::uint64_t>(chunkSize, take - q * chunkSize));
                    }
                    if (threads == 1 || pieces == 1)
                    {
                        for (Work& w : works)
                            processChunk(w, allowZstd, options.mZstdLevel);
                    }
                    else
                    {
                        std::atomic<std::size_t> next{ 0 };
                        auto worker = [&]() {
                            for (std::size_t q = next.fetch_add(1); q < pieces; q = next.fetch_add(1))
                                processChunk(works[q], allowZstd, options.mZstdLevel);
                        };
                        std::vector<std::thread> pool;
                        const std::size_t count = std::min<std::size_t>(threads, pieces);
                        pool.reserve(count - 1);
                        try
                        {
                            for (std::size_t t = 1; t < count; ++t)
                                pool.emplace_back(worker);
                        }
                        catch (...)
                        {
                            // Could not start more threads: the ones running and this thread do all the work.
                        }
                        worker();
                        for (std::thread& t : pool)
                            t.join();
                    }
                    for (const Work& w : works)
                    {
                        if (w.mFailed)
                            return makeError(ErrorClass::Io, "chunk", "compressing a chunk failed");
                        ChunkOut chunk;
                        chunk.mOffset = position;
                        chunk.mSize = w.mSize;
                        chunk.mHash = w.mHash;
                        if (w.mUseZstd)
                        {
                            chunk.mCodec = codecZstd;
                            chunk.mStored = static_cast<std::uint32_t>(w.mPacked.size());
                            ok = ok && sink.write(w.mPacked.data(), w.mPacked.size());
                            anyZstd = true;
                        }
                        else
                        {
                            chunk.mCodec = codecStore;
                            chunk.mStored = w.mSize;
                            ok = ok && sink.write(w.mData, w.mSize);
                        }
                        content.update(w.mData, w.mSize);
                        position += chunk.mStored;
                        chunks.push_back(chunk);
                        ++out.mChunkCount;
                    }
                    done += take;
                }
                out.mContent = content.finalize();
            }
            if (!ok)
                return makeError(ErrorClass::Io, "output", "writing the pack failed");

            const std::uint64_t dataEnd = position;
            const std::uint64_t dataSize = chunks.empty() ? 0 : dataEnd - F::dataOffset;
            std::uint64_t metadataOffset = 0;
            F::alignUp(chunks.empty() ? F::dataOffset : dataEnd, F::blockAlignment, metadataOffset);
            zeros(sink, metadataOffset - (chunks.empty() ? F::dataOffset : dataEnd), ok);

            // Metadata region, in the order of section 7.1.
            std::vector<std::uint8_t> strings;
            std::vector<std::uint8_t> entryTable(order.size() * F::entryRecordSize, 0);
            std::vector<std::uint8_t> chunkTables(chunks.size() * F::chunkRecordSize, 0);
            std::vector<std::uint8_t> provenance;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> pathIndex;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> keyIndex;
            pathIndex.reserve(pathEntries);
            keyIndex.reserve(keyEntries);

            std::uint64_t stringBytes = 0;
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                if (entries[order[k]].mPath)
                    stringBytes += entries[order[k]].mPath->size();
            }
            std::uint64_t stringTableSize = 0;
            F::alignUp(stringBytes, 8, stringTableSize);
            strings.assign(static_cast<std::size_t>(stringTableSize), 0);

            const std::uint64_t entryTableOffset = metadataOffset + stringTableSize;
            const std::uint64_t entryTableSize = entryTable.size();
            const std::uint64_t pathIndexOffset = entryTableOffset + entryTableSize;
            const std::uint64_t keyIndexOffset = pathIndexOffset + pathEntries * 16;
            const std::uint64_t chunkTablesOffset = keyIndexOffset + keyEntries * 16;
            const std::uint64_t provenanceOffset = chunkTablesOffset + chunkTables.size();

            std::uint64_t stringPosition = 0;
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                const InputEntry& in = entries[order[k]];
                const Prepared& p = prepared[order[k]];
                const EntryOut& out = outs[k];
                std::uint8_t* rec = entryTable.data() + k * F::entryRecordSize;
                F::wr64(rec + F::E::key, p.mKey);
                if (in.mPath)
                {
                    std::memcpy(strings.data() + stringPosition, in.mPath->data(), in.mPath->size());
                    F::wr64(rec + F::E::pathOffset, stringPosition);
                    F::wr32(rec + F::E::pathLength, static_cast<std::uint32_t>(in.mPath->size()));
                    pathIndex.emplace_back(pathHash(*in.mPath), k);
                    stringPosition += in.mPath->size();
                }
                if (in.mKey)
                    keyIndex.emplace_back(*in.mKey, k);
                F::wr16(rec + F::E::type, in.mType);
                F::wr32(rec + F::E::flags, p.mFlags);
                F::wr32(rec + F::E::chunkCount, out.mChunkCount);
                F::wr64(rec + F::E::size, p.mSize);
                if (out.mChunkCount > 0)
                    F::wr64(rec + F::E::chunkTableOffset, chunkTablesOffset + out.mFirstChunk * F::chunkRecordSize);
                std::memcpy(rec + F::E::contentHash, out.mContent.data(), 32);
                F::wr32(rec + F::E::hintMemorySize, in.mHintMemorySize);
                F::wr32(rec + F::E::hintGroup, in.mHintGroup);
                F::wr32(rec + F::E::layoutParamA, in.mLayoutParamA);
                F::wr32(rec + F::E::layoutParamB, in.mLayoutParamB);
                if (in.mProvenance)
                {
                    const ProvenanceInput& prov = *in.mProvenance;
                    std::uint64_t recordSize = 0;
                    F::alignUp(48 + prov.mToolId.size() + prov.mParameters.size(), 8, recordSize);
                    const std::size_t at = provenance.size();
                    provenance.resize(at + static_cast<std::size_t>(recordSize), 0);
                    std::uint8_t* pr = provenance.data() + at;
                    F::wr32(pr, static_cast<std::uint32_t>(recordSize));
                    F::wr16(pr + 4, static_cast<std::uint16_t>(prov.mToolId.size()));
                    std::memcpy(pr + 8, prov.mSourceDigest.data(), 32);
                    F::wr32(pr + 40, static_cast<std::uint32_t>(prov.mParameters.size()));
                    std::memcpy(pr + 48, prov.mToolId.data(), prov.mToolId.size());
                    if (!prov.mParameters.empty())
                        std::memcpy(pr + 48 + prov.mToolId.size(), prov.mParameters.data(), prov.mParameters.size());
                    F::wr64(rec + F::E::provenanceOffset, provenanceOffset + at);
                }
                for (std::uint32_t c = 0; c < out.mChunkCount; ++c)
                {
                    const ChunkOut& chunk = chunks[out.mFirstChunk + c];
                    std::uint8_t* cr = chunkTables.data() + (out.mFirstChunk + c) * F::chunkRecordSize;
                    F::wr64(cr + F::C::offset, chunk.mOffset);
                    F::wr32(cr + F::C::storedSize, chunk.mStored);
                    F::wr32(cr + F::C::size, chunk.mSize);
                    F::wr16(cr + F::C::codec, chunk.mCodec);
                    F::wr16(cr + F::C::flags, F::chunkFlagHashPresent);
                    std::memcpy(cr + F::C::hash, chunk.mHash.data(), 32);
                }
            }
            std::sort(pathIndex.begin(), pathIndex.end());
            std::sort(keyIndex.begin(), keyIndex.end());
            std::vector<std::uint8_t> pathIndexBytes(pathIndex.size() * 16);
            for (std::size_t k = 0; k < pathIndex.size(); ++k)
            {
                F::wr64(pathIndexBytes.data() + k * 16, pathIndex[k].first);
                F::wr64(pathIndexBytes.data() + k * 16 + 8, pathIndex[k].second);
            }
            std::vector<std::uint8_t> keyIndexBytes(keyIndex.size() * 16);
            for (std::size_t k = 0; k < keyIndex.size(); ++k)
            {
                F::wr64(keyIndexBytes.data() + k * 16, keyIndex[k].first);
                F::wr64(keyIndexBytes.data() + k * 16 + 8, keyIndex[k].second);
            }

            // The manifest digest of section 13.5.
            const std::uint64_t packFlags = options.mDerived ? packFlagDerived : 0;
            const std::uint64_t generation = options.mDerived ? options.mGeneration : 0;
            Hash sourceDigest{};
            if (options.mDerived)
                sourceDigest = options.mSourceDigest;
            ManifestBuilder manifest(packFlags, generation, sourceDigest, chunkSize, order.size());
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                const InputEntry& in = entries[order[k]];
                const Prepared& p = prepared[order[k]];
                const EntryOut& out = outs[k];
                manifest.beginEntry(p.mFlags, in.mType, p.mKey, in.mPath ? std::string_view(*in.mPath) : std::string_view(),
                    p.mSize, out.mContent, in.mHintMemorySize, in.mHintGroup, in.mLayoutParamA, in.mLayoutParamB,
                    out.mChunkCount);
                for (std::uint32_t c = 0; c < out.mChunkCount; ++c)
                {
                    const ChunkOut& chunk = chunks[out.mFirstChunk + c];
                    manifest.addChunk(chunk.mSize, chunk.mHash);
                }
                std::span<const std::uint8_t> record;
                if (in.mProvenance)
                {
                    const std::uint64_t at = F::rd64(entryTable.data() + k * F::entryRecordSize + F::E::provenanceOffset)
                        - provenanceOffset;
                    const std::uint32_t size = F::rd32(provenance.data() + at);
                    record = std::span<const std::uint8_t>(provenance.data() + at, size);
                }
                manifest.endEntry(record);
            }

            // Digest of the metadata region, computed over its seven parts in order.
            Hasher metadataHasher;
            metadataHasher.update(strings.data(), strings.size());
            metadataHasher.update(entryTable.data(), entryTable.size());
            metadataHasher.update(pathIndexBytes.data(), pathIndexBytes.size());
            metadataHasher.update(keyIndexBytes.data(), keyIndexBytes.size());
            metadataHasher.update(chunkTables.data(), chunkTables.size());
            metadataHasher.update(provenance.data(), provenance.size());
            const Hash metadataDigest = metadataHasher.finalize();
            const std::uint64_t metadataSize = strings.size() + entryTable.size() + pathIndexBytes.size()
                + keyIndexBytes.size() + chunkTables.size() + provenance.size();

            ok = ok && sink.write(strings.data(), strings.size()) && sink.write(entryTable.data(), entryTable.size())
                && sink.write(pathIndexBytes.data(), pathIndexBytes.size())
                && sink.write(keyIndexBytes.data(), keyIndexBytes.size())
                && sink.write(chunkTables.data(), chunkTables.size()) && sink.write(provenance.data(), provenance.size());

            const std::uint64_t fileSize = metadataOffset + metadataSize + F::footerSize;
            if (metadataSize > F::maxMetadataSize || fileSize > F::maxFileSize)
                return makeError(ErrorClass::Limit, "metadataSize", "the pack would be larger than the format allows");
            std::uint8_t header[512] = {};
            std::memcpy(header, F::magic, 8);
            F::wr16(header + F::H::versionMajor, 0);
            F::wr16(header + F::H::versionMinor, 1);
            F::wr32(header + F::H::headerSize, 512);
            F::wr64(header + F::H::packFlags, packFlags);
            F::wr64(header + F::H::fileSize, fileSize);
            F::wr32(header + F::H::chunkSize, chunkSize);
            F::wr32(header + F::H::entryRecordSize, F::entryRecordSize);
            F::wr32(header + F::H::chunkRecordSize, F::chunkRecordSize);
            if (anyZstd)
            {
                F::wr32(header + F::H::compressorVersion, ZSTD_versionNumber());
                F::wr32(header + F::H::compressorLevel, static_cast<std::uint32_t>(options.mZstdLevel));
            }
            F::wr64(header + F::H::entryCount, order.size());
            F::wr64(header + F::H::pathEntryCount, pathEntries);
            F::wr64(header + F::H::keyEntryCount, keyEntries);
            F::wr64(header + F::H::dataOffset, F::dataOffset);
            F::wr64(header + F::H::dataSize, dataSize);
            F::wr64(header + F::H::metadataOffset, metadataOffset);
            F::wr64(header + F::H::metadataSize, metadataSize);
            F::wr64(header + F::H::stringTableOffset, metadataOffset);
            F::wr64(header + F::H::stringTableSize, strings.size());
            F::wr64(header + F::H::entryTableOffset, entryTableOffset);
            F::wr64(header + F::H::pathIndexOffset, pathIndexOffset);
            F::wr64(header + F::H::keyIndexOffset, keyIndexOffset);
            F::wr64(header + F::H::chunkTablesOffset, chunkTablesOffset);
            F::wr64(header + F::H::chunkTablesSize, chunkTables.size());
            F::wr64(header + F::H::provenanceOffset, provenanceOffset);
            F::wr64(header + F::H::provenanceSize, provenance.size());
            F::wr64(header + F::H::extensionOffset, provenanceOffset + provenance.size());
            F::wr64(header + F::H::extensionSize, 0);
            F::wr64(header + F::H::generation, generation);
            std::memcpy(header + F::H::sourceDigest, sourceDigest.data(), 32);
            const Hash manifestDigest = manifest.finalize();
            std::memcpy(header + F::H::manifestDigest, manifestDigest.data(), 32);
            std::memcpy(header + F::H::metadataDigest, metadataDigest.data(), 32);
            const Hash checksum = blake3(header, F::H::checksum);
            std::memcpy(header + F::H::checksum, checksum.data(), 32);

            std::uint8_t footer[128] = {};
            std::memcpy(footer, F::footerMagic, 8);
            F::wr16(footer + F::F::versionMajor, 0);
            F::wr16(footer + F::F::versionMinor, 1);
            F::wr32(footer + F::F::footerSize, 128);
            F::wr64(footer + F::F::fileSize, fileSize);
            F::wr64(footer + F::F::metadataOffset, metadataOffset);
            F::wr64(footer + F::F::metadataSize, metadataSize);
            std::memcpy(footer + F::F::metadataDigest, metadataDigest.data(), 32);
            std::memcpy(footer + F::F::headerChecksum, checksum.data(), 32);
            ok = ok && sink.write(footer, sizeof(footer)) && sink.writeAt(0, header, sizeof(header));
            if (!ok)
                return makeError(ErrorClass::Io, "output", "writing the pack failed");
            return Error{};
        }
    }

    Error writePackToMemory(
        const std::vector<InputEntry>& entries, std::vector<std::uint8_t>& output, const WriteOptions& options) noexcept
    {
        try
        {
            MemorySink sink(output);
            Error error = writeTo(entries, sink, options);
            if (error)
                output.clear();
            return error;
        }
        catch (const std::bad_alloc&)
        {
            output.clear();
            return makeError(ErrorClass::Limit, "memory", "out of memory");
        }
        catch (...)
        {
            output.clear();
            return makeError(ErrorClass::Io, "write", "an unexpected exception while writing the pack");
        }
    }

    Error writePack(const std::vector<InputEntry>& entries, const std::filesystem::path& output,
        const WriteOptions& options) noexcept
    {
        std::filesystem::path temporary = output;
        try
        {
            temporary += ".tmp";
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            Error result;
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                if (!stream)
                    return makeError(ErrorClass::Io, "output", "cannot create '" + temporary.string() + "'");
                FileSink sink(stream);
                result = writeTo(entries, sink, options);
                stream.flush();
                if (!result && !stream.good())
                    result = makeError(ErrorClass::Io, "output", "writing '" + temporary.string() + "' failed");
            }
            if (!result)
            {
                std::filesystem::rename(temporary, output, ec);
                if (ec)
                    result = makeError(ErrorClass::Io, "output", "cannot replace '" + output.string() + "': " + ec.message());
            }
            if (result)
                std::filesystem::remove(temporary, ec);
            return result;
        }
        catch (...)
        {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            return makeError(ErrorClass::Io, "write", "an unexpected exception while writing the pack");
        }
    }
}
