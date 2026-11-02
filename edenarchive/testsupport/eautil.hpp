// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// Small helpers shared by the independent pack builder, the corpus and the tests. Nothing here includes the
// edenarchive library headers: the test side is written from the specification alone.
#ifndef EDENARCHIVE_TESTSUPPORT_EAUTIL_HPP
#define EDENARCHIVE_TESTSUPPORT_EAUTIL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace EdenTest
{
    using Bytes = std::vector<std::uint8_t>;
    using Digest = std::array<std::uint8_t, 32>;

    /// Writes `value` little-endian into `width` bytes at `pos`. Bytes that fall outside the vector are ignored.
    inline void putLe(Bytes& bytes, std::size_t pos, std::uint64_t value, unsigned width)
    {
        for (unsigned i = 0; i < width; ++i)
        {
            if (pos + i < bytes.size())
                bytes[pos + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    }

    /// Reads a little-endian number of `width` bytes at `pos`. Bytes outside the vector read as zero.
    inline std::uint64_t getLe(const Bytes& bytes, std::size_t pos, unsigned width)
    {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
        {
            if (pos + i < bytes.size())
                value |= static_cast<std::uint64_t>(bytes[pos + i]) << (8 * i);
        }
        return value;
    }

    inline void appendLe(Bytes& bytes, std::uint64_t value, unsigned width)
    {
        for (unsigned i = 0; i < width; ++i)
            bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }

    inline void appendBytes(Bytes& bytes, const void* data, std::size_t size)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }

    inline void appendBytes(Bytes& bytes, const Bytes& more)
    {
        bytes.insert(bytes.end(), more.begin(), more.end());
    }

    inline void appendText(Bytes& bytes, std::string_view text)
    {
        appendBytes(bytes, text.data(), text.size());
    }

    inline Bytes bytesOf(std::string_view text)
    {
        return Bytes(text.begin(), text.end());
    }

    inline std::string textOf(const Bytes& bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    /// Rounds up to a multiple of `unit` (a power of two or not; unit must be non-zero).
    inline std::uint64_t alignUp(std::uint64_t value, std::uint64_t unit)
    {
        return (value + unit - 1) / unit * unit;
    }

    /// A deterministic stream of 64-bit numbers (SplitMix64). Explicit so that the numbers are the same on every
    /// compiler and standard library; the standard distributions are not.
    class SplitMix
    {
    public:
        explicit SplitMix(std::uint64_t seed)
            : mState(seed)
        {
        }

        std::uint64_t next()
        {
            mState += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = mState;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        /// A number in [0, bound); bound must be non-zero. The small bias does not matter for tests.
        std::uint64_t below(std::uint64_t bound) { return next() % bound; }

    private:
        std::uint64_t mState;
    };

    /// `size` pseudo-random bytes that depend only on `seed`.
    inline Bytes patternBytes(std::size_t size, std::uint64_t seed)
    {
        SplitMix gen(seed);
        Bytes out(size);
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>(gen.next() >> 24);
        return out;
    }

    /// `size` bytes of repeating readable text (compresses well).
    inline Bytes textBytes(std::size_t size)
    {
        static constexpr std::string_view line
            = "The quick brown fox jumps over the lazy dog. Eden Archive test text.\n";
        Bytes out(size);
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>(line[i % line.size()]);
        return out;
    }
}

#endif
