// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include "manifest.hpp"

#include <cstring>

#include "format.hpp"

namespace EdenArchive
{
    namespace
    {
        void put32(Hasher& hasher, std::uint32_t value)
        {
            std::uint8_t bytes[4];
            Format::wr32(bytes, value);
            hasher.update(bytes, sizeof(bytes));
        }

        void put64(Hasher& hasher, std::uint64_t value)
        {
            std::uint8_t bytes[8];
            Format::wr64(bytes, value);
            hasher.update(bytes, sizeof(bytes));
        }
    }

    ManifestBuilder::ManifestBuilder(std::uint64_t packFlags, std::uint64_t generation, const Hash& sourceDigest,
        std::uint32_t chunkSize, std::uint64_t entryCount)
    {
        static constexpr char tag[] = "EDENPAK-MANIFEST-1"; // 18 bytes, without the terminating zero
        mHasher.update(tag, sizeof(tag) - 1);
        put64(mHasher, packFlags);
        put64(mHasher, generation);
        mHasher.update(sourceDigest.data(), sourceDigest.size());
        put32(mHasher, chunkSize);
        put64(mHasher, entryCount);
    }

    void ManifestBuilder::beginEntry(std::uint32_t entryFlags, std::uint16_t entryType, std::uint64_t key,
        std::string_view path, std::uint64_t uncompressedSize, const Hash& contentHash, std::uint32_t hintMemorySize,
        std::uint32_t hintGroup, std::uint32_t layoutParamA, std::uint32_t layoutParamB, std::uint32_t chunkCount)
    {
        put32(mHasher, entryFlags);
        std::uint8_t typeBytes[4] = { 0, 0, 0, 0 }; // u16 type, then 2 zero bytes
        Format::wr16(typeBytes, entryType);
        mHasher.update(typeBytes, sizeof(typeBytes));
        put64(mHasher, key);
        put32(mHasher, static_cast<std::uint32_t>(path.size()));
        mHasher.update(path.data(), path.size());
        put64(mHasher, uncompressedSize);
        mHasher.update(contentHash.data(), contentHash.size());
        put32(mHasher, hintMemorySize);
        put32(mHasher, hintGroup);
        put32(mHasher, layoutParamA);
        put32(mHasher, layoutParamB);
        put32(mHasher, chunkCount);
    }

    void ManifestBuilder::addChunk(std::uint32_t uncompressedSize, const Hash& chunkHash)
    {
        put32(mHasher, uncompressedSize);
        mHasher.update(chunkHash.data(), chunkHash.size());
    }

    void ManifestBuilder::endEntry(std::span<const std::uint8_t> provenanceRecord)
    {
        put32(mHasher, static_cast<std::uint32_t>(provenanceRecord.size()));
        mHasher.update(provenanceRecord.data(), provenanceRecord.size());
    }

    Hash ManifestBuilder::finalize() const
    {
        return mHasher.finalize();
    }
}
