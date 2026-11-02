// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_SRC_MANIFEST_HPP
#define EDENARCHIVE_SRC_MANIFEST_HPP

#include <cstdint>
#include <span>
#include <string_view>

#include <edenarchive/hash.hpp>

namespace EdenArchive
{
    /// Builds the manifest digest of section 13.5 piece by piece, in entry order. Used by the writer (while it
    /// writes) and by the validator (to recompute it from an open pack). Not thread-safe.
    class ManifestBuilder
    {
    public:
        ManifestBuilder(std::uint64_t packFlags, std::uint64_t generation, const Hash& sourceDigest,
            std::uint32_t chunkSize, std::uint64_t entryCount);

        /// Starts an entry. `path` is empty for an entry with no path.
        void beginEntry(std::uint32_t entryFlags, std::uint16_t entryType, std::uint64_t key, std::string_view path,
            std::uint64_t uncompressedSize, const Hash& contentHash, std::uint32_t hintMemorySize,
            std::uint32_t hintGroup, std::uint32_t layoutParamA, std::uint32_t layoutParamB, std::uint32_t chunkCount);
        void addChunk(std::uint32_t uncompressedSize, const Hash& chunkHash);
        /// Ends the entry. `provenanceRecord` is the whole record including its padding, or empty for none.
        void endEntry(std::span<const std::uint8_t> provenanceRecord);

        Hash finalize() const;

    private:
        Hasher mHasher;
    };
}

#endif
