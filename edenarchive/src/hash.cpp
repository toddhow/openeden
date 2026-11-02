// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include <edenarchive/hash.hpp>

#include <blake3.h>

#include "format.hpp"

namespace EdenArchive
{
    struct Hasher::State
    {
        blake3_hasher mHasher;
    };

    Hasher::Hasher()
        : mState(std::make_unique<State>())
    {
        blake3_hasher_init(&mState->mHasher);
    }

    Hasher::~Hasher() = default;

    void Hasher::update(const void* data, std::size_t size) noexcept
    {
        if (size != 0)
            blake3_hasher_update(&mState->mHasher, data, size);
    }

    Hash Hasher::finalize() const noexcept
    {
        Hash out;
        blake3_hasher_finalize(&mState->mHasher, out.data(), out.size());
        return out;
    }

    Hash blake3(const void* data, std::size_t size) noexcept
    {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        if (size != 0)
            blake3_hasher_update(&hasher, data, size);
        Hash out;
        blake3_hasher_finalize(&hasher, out.data(), out.size());
        return out;
    }

    const Hash& emptyHash() noexcept
    {
        static const Hash hash = blake3(nullptr, 0);
        return hash;
    }

    std::uint64_t pathHash(std::string_view path) noexcept
    {
        const Hash digest = blake3(path.data(), path.size());
        return Format::rd64(digest.data());
    }

    std::string toHex(const Hash& hash)
    {
        static const char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint8_t b : hash)
        {
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 15]);
        }
        return out;
    }
}
