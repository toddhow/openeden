#pragma once

// SHA-512 (FIPS 180-4), written for this project (task 0147 slice 3): the DirectML package the face editor downloads on
// the author's request is pinned by its SHA-512 (the hash the other pins of this project use). Standard library only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace face_editor
{
    class Sha512
    {
    public:
        Sha512();
        void update(const void* data, std::size_t size);
        void update(std::string_view bytes) { update(bytes.data(), bytes.size()); }
        /// The digest as 128 lower case hex characters. The object can not be updated after this.
        std::string finishHex();

    private:
        void block(const std::uint8_t* chunk);

        std::array<std::uint64_t, 8> mState;
        std::array<std::uint8_t, 128> mBuffer{};
        std::size_t mBuffered = 0;
        std::uint64_t mTotalBytes = 0;
    };

    std::string sha512Hex(std::string_view bytes);
}
