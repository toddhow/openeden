#include "sha512.hpp"

#include <algorithm>
#include <cstring>

namespace face_editor
{
    namespace
    {
        constexpr std::uint64_t kK[80]
            = { 0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
                  0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
                  0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
                  0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
                  0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
                  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
                  0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
                  0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
                  0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
                  0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
                  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
                  0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
                  0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
                  0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
                  0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
                  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
                  0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
                  0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
                  0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
                  0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

        inline std::uint64_t rotr(std::uint64_t x, unsigned n)
        {
            return (x >> n) | (x << (64 - n));
        }
    }

    Sha512::Sha512()
        : mState{ 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
            0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL }
    {
    }

    void Sha512::block(const std::uint8_t* chunk)
    {
        std::uint64_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            std::uint64_t v = 0;
            for (int b = 0; b < 8; ++b)
                v = (v << 8) | chunk[i * 8 + b];
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i)
        {
            const std::uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const std::uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint64_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
        std::uint64_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];
        for (int i = 0; i < 80; ++i)
        {
            const std::uint64_t s1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const std::uint64_t ch = (e & f) ^ (~e & g);
            const std::uint64_t t1 = h + s1 + ch + kK[i] + w[i];
            const std::uint64_t s0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        mState[0] += a;
        mState[1] += b;
        mState[2] += c;
        mState[3] += d;
        mState[4] += e;
        mState[5] += f;
        mState[6] += g;
        mState[7] += h;
    }

    void Sha512::update(const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        mTotalBytes += size;
        while (size > 0)
        {
            const std::size_t take = std::min(size, mBuffer.size() - mBuffered);
            std::memcpy(mBuffer.data() + mBuffered, bytes, take);
            mBuffered += take;
            bytes += take;
            size -= take;
            if (mBuffered == mBuffer.size())
            {
                block(mBuffer.data());
                mBuffered = 0;
            }
        }
    }

    std::string Sha512::finishHex()
    {
        // Padding: 0x80, zeros up to 112 mod 128, then the 128 bit big endian bit length (the high half is 0 here).
        const std::uint64_t bits = mTotalBytes * 8;
        std::uint8_t pad[128 + 16] = { 0x80 };
        const std::size_t padLength = (mBuffered < 112 ? 112 : 240) - mBuffered;
        std::uint8_t length[16] = {};
        for (int i = 0; i < 8; ++i)
            length[8 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        // update() changes mTotalBytes, but the length was taken above.
        update(pad, padLength);
        update(length, 16);
        static const char* sDigits = "0123456789abcdef";
        std::string hex;
        hex.reserve(128);
        for (std::uint64_t word : mState)
            for (int i = 7; i >= 0; --i)
            {
                const auto byte = static_cast<std::uint8_t>(word >> (8 * i));
                hex.push_back(sDigits[byte >> 4]);
                hex.push_back(sDigits[byte & 15]);
            }
        return hex;
    }

    std::string sha512Hex(std::string_view bytes)
    {
        Sha512 h;
        h.update(bytes);
        return h.finishHex();
    }
}
