// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include "eacodec.hpp"

#include <blake3.h>

#include EDENARCHIVE_ZSTD_H

#include <algorithm>

namespace EdenTest
{
    Digest blake3Of(const void* data, std::size_t size)
    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        if (size > 0)
            blake3_hasher_update(&hasher, data, size);
        Digest out{};
        blake3_hasher_finalize(&hasher, out.data(), out.size());
        return out;
    }

    const Digest& emptyDigest()
    {
        static const Digest value = blake3Of(nullptr, 0);
        return value;
    }

    std::uint64_t pathHashOf(std::string_view path)
    {
        const Digest digest = blake3Of(path.data(), path.size());
        return getLe(Bytes(digest.begin(), digest.begin() + 8), 0, 8);
    }

    std::string hexOf(const Digest& digest)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (const std::uint8_t byte : digest)
        {
            out.push_back(digits[byte >> 4]);
            out.push_back(digits[byte & 15]);
        }
        return out;
    }

    bool digestFromHex(std::string_view text, Digest& out)
    {
        if (text.size() != 64)
            return false;
        const auto value = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < 32; ++i)
        {
            const int high = value(text[2 * i]);
            const int low = value(text[2 * i + 1]);
            if (high < 0 || low < 0)
                return false;
            out[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
        return true;
    }

    Bytes zstdCompress(const void* data, std::size_t size, int level, bool contentChecksum)
    {
        ZSTD_CCtx* context = ZSTD_createCCtx();
        if (context == nullptr)
            return {};
        // Single call, no worker threads, the content size is written (the default for a known size).
        ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, level);
        ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, contentChecksum ? 1 : 0);
        ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 1);
        ZSTD_CCtx_setParameter(context, ZSTD_c_nbWorkers, 0);
        Bytes out(ZSTD_compressBound(size));
        const std::size_t written = ZSTD_compress2(context, out.data(), out.size(), data, size);
        ZSTD_freeCCtx(context);
        if (ZSTD_isError(written) != 0)
            return {};
        out.resize(written);
        return out;
    }

    bool zstdDecompress(const Bytes& frame, std::size_t expectedSize, Bytes& out)
    {
        out.assign(expectedSize, 0);
        const std::size_t produced = ZSTD_decompress(out.data(), out.size(), frame.data(), frame.size());
        return ZSTD_isError(produced) == 0 && produced == expectedSize;
    }

    std::uint32_t zstdVersionNumber()
    {
        return static_cast<std::uint32_t>(ZSTD_VERSION_NUMBER);
    }

    Bytes makeRleFrame(std::uint8_t value, std::uint64_t count, const FrameOptions& options)
    {
        Bytes frame = { 0x28, 0xB5, 0x2F, 0xFD };
        const std::uint64_t declared = options.mDeclaredSize.value_or(count);

        // Frame_Content_Size_Flag (descriptor bits 7-6) and the size of the field (RFC 8878, 3.1.1.1.1).
        unsigned fcsFlag = 0;
        unsigned fcsBytes = 0;
        if (options.mWriteContentSize)
        {
            if (options.mSingleSegment && declared < 256)
            {
                fcsFlag = 0;
                fcsBytes = 1;
            }
            else if (options.mSingleSegment && declared < 256 + 65536)
            {
                fcsFlag = 1;
                fcsBytes = 2;
            }
            else if (declared <= 0xFFFFFFFFull)
            {
                fcsFlag = 2;
                fcsBytes = 4;
            }
            else
            {
                fcsFlag = 3;
                fcsBytes = 8;
            }
        }

        // Dictionary_ID_Flag (bits 1-0).
        unsigned dictFlag = 0;
        unsigned dictBytes = 0;
        if (options.mDictionaryId.has_value())
        {
            const std::uint32_t id = *options.mDictionaryId;
            if (id < 256)
            {
                dictFlag = 1;
                dictBytes = 1;
            }
            else if (id < 65536)
            {
                dictFlag = 2;
                dictBytes = 2;
            }
            else
            {
                dictFlag = 3;
                dictBytes = 4;
            }
        }

        const unsigned descriptor = (fcsFlag << 6) | ((options.mSingleSegment ? 1u : 0u) << 5) | dictFlag;
        frame.push_back(static_cast<std::uint8_t>(descriptor));
        if (!options.mSingleSegment)
        {
            // Window_Descriptor: the exponent in the high 5 bits, the mantissa (0) in the low 3.
            const unsigned log = std::clamp(options.mWindowLog, 10u, 41u);
            frame.push_back(static_cast<std::uint8_t>((log - 10) << 3));
        }
        if (dictBytes > 0)
            appendLe(frame, *options.mDictionaryId, dictBytes);
        if (fcsBytes > 0)
        {
            // A two-byte field stores the size minus 256.
            appendLe(frame, fcsBytes == 2 ? declared - 256 : declared, fcsBytes);
        }

        // Run-length blocks: a 3-byte header (last flag, type 1 = RLE in bits 1-2, size in bits 3-23) and one byte.
        std::uint64_t remaining = count;
        do
        {
            const std::uint64_t n = std::min<std::uint64_t>(remaining, 131072);
            remaining -= n;
            const std::uint32_t header = (remaining == 0 ? 1u : 0u) | (1u << 1) | (static_cast<std::uint32_t>(n) << 3);
            appendLe(frame, header, 3);
            frame.push_back(value);
        } while (remaining > 0);
        return frame;
    }

    Bytes makeSkippableFrame(const Bytes& payload, unsigned magicLow)
    {
        Bytes frame;
        appendLe(frame, 0x184D2A50u + (magicLow & 15u), 4);
        appendLe(frame, payload.size(), 4);
        appendBytes(frame, payload);
        return frame;
    }
}
