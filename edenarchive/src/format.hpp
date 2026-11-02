// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// Constants of the format (docs/spec/eden-archive-format-v0.1.md) and little-endian helpers. Internal.
#ifndef EDENARCHIVE_SRC_FORMAT_HPP
#define EDENARCHIVE_SRC_FORMAT_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EdenArchive::Format
{
    inline constexpr std::uint8_t magic[8] = { 'E', 'D', 'E', 'N', 'P', 'A', 'K', 1 };
    inline constexpr std::uint8_t footerMagic[8] = { 'E', 'D', 'E', 'N', 'E', 'N', 'D', 1 };

    inline constexpr std::uint64_t headerSize = 512;
    inline constexpr std::uint64_t footerSize = 128;
    inline constexpr std::uint64_t dataOffset = 4096;
    inline constexpr std::uint64_t blockAlignment = 4096;
    inline constexpr std::uint32_t entryRecordSize = 112;
    inline constexpr std::uint32_t chunkRecordSize = 56;
    inline constexpr std::uint32_t defaultChunkSize = 131072;

    // Limits, section 12.
    inline constexpr std::uint64_t maxPathLength = 1024;
    inline constexpr std::uint64_t maxComponentLength = 255;
    inline constexpr std::uint64_t maxEntries = std::uint64_t{ 1 } << 22;
    inline constexpr std::uint64_t maxChunksPerEntry = std::uint64_t{ 1 } << 20;
    inline constexpr std::uint64_t maxChunksPerPack = std::uint64_t{ 1 } << 25;
    inline constexpr std::uint64_t maxChunkSize = std::uint64_t{ 1 } << 23;
    inline constexpr std::uint64_t maxStoredSize = std::uint64_t{ 1 } << 24;
    inline constexpr std::uint64_t maxEntrySize = std::uint64_t{ 1 } << 32;
    inline constexpr std::uint64_t maxPackContent = std::uint64_t{ 1 } << 40;
    inline constexpr std::uint64_t maxFileSize = std::uint64_t{ 1 } << 44;
    inline constexpr std::uint64_t maxMetadataSize = std::uint64_t{ 1 } << 32;
    inline constexpr std::uint64_t maxEntryStride = 512;
    inline constexpr std::uint64_t maxChunkStride = 256;
    inline constexpr std::uint64_t maxExtensionSize = std::uint64_t{ 16 } << 20;
    inline constexpr std::uint64_t maxExtensionBlocks = 1024;
    inline constexpr std::uint64_t maxProvenanceRecord = 65536;
    inline constexpr std::uint64_t maxProvenanceParameters = 60000;
    inline constexpr std::uint64_t maxToolId = 255;

    // Header field offsets, section 5.
    namespace H
    {
        inline constexpr std::size_t versionMajor = 8, versionMinor = 10, headerSize = 12, packFlags = 16,
                                     fileSize = 24, chunkSize = 32, entryRecordSize = 36, chunkRecordSize = 40,
                                     compressorVersion = 44, compressorLevel = 48, reserved52 = 52, entryCount = 56,
                                     pathEntryCount = 64, keyEntryCount = 72, dataOffset = 80, dataSize = 88,
                                     metadataOffset = 96, metadataSize = 104, stringTableOffset = 112,
                                     stringTableSize = 120, entryTableOffset = 128, pathIndexOffset = 136,
                                     keyIndexOffset = 144, chunkTablesOffset = 152, chunkTablesSize = 160,
                                     provenanceOffset = 168, provenanceSize = 176, extensionOffset = 184,
                                     extensionSize = 192, generation = 200, sourceDigest = 208, manifestDigest = 240,
                                     metadataDigest = 272, reservedBegin = 304, checksum = 480;
    }
    // Footer field offsets, section 6.
    namespace F
    {
        inline constexpr std::size_t versionMajor = 8, versionMinor = 10, footerSize = 12, fileSize = 16,
                                     metadataOffset = 24, metadataSize = 32, metadataDigest = 40,
                                     headerChecksum = 72, reservedBegin = 104;
    }
    // Entry record field offsets, section 7.4.
    namespace E
    {
        inline constexpr std::size_t key = 0, pathOffset = 8, pathLength = 16, type = 20, reserved16 = 22, flags = 24,
                                     chunkCount = 28, size = 32, chunkTableOffset = 40, contentHash = 48,
                                     provenanceOffset = 80, hintMemorySize = 88, hintGroup = 92, layoutParamA = 96,
                                     layoutParamB = 100, reserved64 = 104;
    }
    // Chunk record field offsets, section 7.7.
    namespace C
    {
        inline constexpr std::size_t offset = 0, storedSize = 8, size = 12, codec = 16, flags = 18, reserved = 20,
                                     hash = 24;
    }

    inline constexpr std::uint16_t chunkFlagHashPresent = 1;

    inline std::uint16_t rd16(const std::uint8_t* p) noexcept
    {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }
    inline std::uint32_t rd32(const std::uint8_t* p) noexcept
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
            | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }
    inline std::uint64_t rd64(const std::uint8_t* p) noexcept
    {
        return static_cast<std::uint64_t>(rd32(p)) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32);
    }
    inline void wr16(std::uint8_t* p, std::uint16_t v) noexcept
    {
        p[0] = static_cast<std::uint8_t>(v);
        p[1] = static_cast<std::uint8_t>(v >> 8);
    }
    inline void wr32(std::uint8_t* p, std::uint32_t v) noexcept
    {
        for (int i = 0; i < 4; ++i)
            p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
    inline void wr64(std::uint8_t* p, std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i)
            p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    }

    /// Smallest multiple of `alignment` (a power of two) that is at least `value`; false on overflow.
    inline bool alignUp(std::uint64_t value, std::uint64_t alignment, std::uint64_t& result) noexcept
    {
        const std::uint64_t mask = alignment - 1;
        if (value > ~std::uint64_t{ 0 } - mask)
            return false;
        result = (value + mask) & ~mask;
        return true;
    }

    /// The bound of section 9.2 (zstd's ZSTD_COMPRESSBOUND) for a chunk of `u` content bytes.
    inline std::uint64_t zstdStoredBound(std::uint64_t u) noexcept
    {
        const std::uint64_t margin = u < 131072 ? ((131072 - u) >> 11) : 0;
        return u + (u >> 8) + margin;
    }
}

#endif
