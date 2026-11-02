// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_HASH_HPP
#define EDENARCHIVE_HASH_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "error.hpp" // first, so that ErrorClass::Hash is declared before the type Hash (GCC -Wshadow)

namespace EdenArchive
{
    /// BLAKE3-256 digest (specification section 13.1).
    using Hash = std::array<std::uint8_t, 32>;

    /// BLAKE3-256 of a byte range.
    Hash blake3(const void* data, std::size_t size) noexcept;

    /// The BLAKE3-256 digest of zero bytes (the content hash of an empty entry).
    const Hash& emptyHash() noexcept;

    /// The path hash of section 7.3.3: the first 8 bytes of the BLAKE3-256 of the path, as a little-endian u64.
    std::uint64_t pathHash(std::string_view path) noexcept;

    /// Lower-case hexadecimal text of a digest.
    std::string toHex(const Hash& hash);

    /// Incremental BLAKE3-256. Owns its state; not thread-safe (one hasher per thread).
    class Hasher
    {
    public:
        Hasher();
        ~Hasher();
        Hasher(const Hasher&) = delete;
        Hasher& operator=(const Hasher&) = delete;
        void update(const void* data, std::size_t size) noexcept;
        Hash finalize() const noexcept;

    private:
        struct State;
        std::unique_ptr<State> mState;
    };
}

#endif
