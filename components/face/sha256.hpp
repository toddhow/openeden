#ifndef OPENEDEN_COMPONENTS_FACE_SHA256_H
#define OPENEDEN_COMPONENTS_FACE_SHA256_H

// SHA-256 (FIPS 180-4), written for this project (task 0147): the curve file records the SHA-256 of its audio, and Eden
// Studio checks the downloaded model files against hashes recorded in our docs. Standard library only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Face
{
    class Sha256
    {
    public:
        Sha256();
        void update(const void* data, std::size_t size);
        void update(std::string_view bytes) { update(bytes.data(), bytes.size()); }
        /// The digest as 64 lower case hex characters. The object can not be updated after this.
        std::string finishHex();

    private:
        void block(const std::uint8_t* chunk);

        std::array<std::uint32_t, 8> mState;
        std::array<std::uint8_t, 64> mBuffer{};
        std::size_t mBuffered = 0;
        std::uint64_t mTotalBytes = 0;
    };

    std::string sha256Hex(std::string_view bytes);
}

#endif
