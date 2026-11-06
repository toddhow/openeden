// Task 0089: the BC4 decoder and encoder and the one channel resize. Generated data only.

#include <components/vsgutil/bc4.hpp>
#include <components/vsgutil/r8resize.hpp>
#include <components/vsgutil/rgbaresize.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    using Gray = std::vector<uint8_t>;

    struct Lcg
    {
        uint32_t state = 4242;
        uint8_t next()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<uint8_t>(state >> 24);
        }
    };

    // A block from two endpoints and sixteen 3 bit indices, packed as the format says (48 bits, texel p at bit 3p,
    // little endian), written here independently of the code under test.
    std::array<uint8_t, 8> blockOf(uint8_t a0, uint8_t a1, const std::array<int, 16>& indices)
    {
        uint64_t bits = 0;
        for (int p = 0; p < 16; ++p)
        {
            bits |= static_cast<uint64_t>(indices[p] & 7) << (3 * p);
        }
        std::array<uint8_t, 8> block{ a0, a1 };
        for (int k = 0; k < 6; ++k)
        {
            block[2 + k] = static_cast<uint8_t>((bits >> (8 * k)) & 255);
        }
        return block;
    }

    std::array<int, 16> indexRun()
    {
        std::array<int, 16> indices{};
        for (int p = 0; p < 16; ++p)
        {
            indices[p] = p % 8;
        }
        return indices;
    }

    std::array<uint8_t, 16> decode(const std::array<uint8_t, 8>& block)
    {
        std::array<uint8_t, 16> out;
        out.fill(0xcd);
        vsgUtil::Bc4::decodeBlock(block.data(), out.data());
        return out;
    }

    Gray encodeAndDecode(const Gray& src, uint32_t w, uint32_t h)
    {
        std::vector<uint8_t> blocks(vsgUtil::Bc4::encodedSize(w, h), 0xee);
        vsgUtil::Bc4::encodeImage(src.data(), w, h, blocks.data());
        const uint32_t paddedW = (w + 3) / 4 * 4;
        const uint32_t paddedH = (h + 3) / 4 * 4;
        Gray padded(static_cast<size_t>(paddedW) * paddedH, 0xcd);
        vsgUtil::Bc4::decodeImage(blocks.data(), paddedW, paddedH, padded.data());
        Gray out(static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                out[static_cast<size_t>(y) * w + x] = padded[static_cast<size_t>(y) * paddedW + x];
            }
        }
        return out;
    }

    double psnr(const Gray& a, const Gray& b)
    {
        double sum = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = double(a[i]) - double(b[i]);
            sum += d * d;
        }
        if (sum == 0)
        {
            return 99.0;
        }
        return 10.0 * std::log10(255.0 * 255.0 / (sum / double(a.size())));
    }

    // Measured on the first run of the encoder (gradient 53.2 dB, checker exact, uniform noise 30.4 dB), then set a
    // little lower. Uniform noise is the worst case: eight levels over the range leave a step of about 36 between
    // palette entries, which bounds an eight level quantiser near 27.7 dB before the endpoint refinement.
    constexpr double gradientFloorDb = 52.0;
    constexpr double checkerFloorDb = 90.0;
    constexpr double noiseFloorDb = 29.0;
}

TEST(Bc4Decode, the_eight_value_mode_when_the_first_endpoint_is_larger)
{
    // a0 = 200 > a1 = 40: the six values between are ((7 - k) a0 + k a1) / 7, rounded to nearest, k = 1 to 6.
    const auto out = decode(blockOf(200, 40, indexRun()));
    const uint8_t expected[8] = { 200, 40, 177, 154, 131, 109, 86, 63 };
    for (int p = 0; p < 16; ++p)
    {
        EXPECT_EQ(out[p], expected[p % 8]) << "texel " << p;
    }
}

TEST(Bc4Decode, the_six_value_mode_when_the_first_endpoint_is_not_larger)
{
    // a0 = 40 <= a1 = 200: four values between (over 5), then 0 and 255.
    const auto out = decode(blockOf(40, 200, indexRun()));
    const uint8_t expected[8] = { 40, 200, 72, 104, 136, 168, 0, 255 };
    for (int p = 0; p < 16; ++p)
    {
        EXPECT_EQ(out[p], expected[p % 8]) << "texel " << p;
    }
}

TEST(Bc4Decode, equal_endpoints_are_the_six_value_mode)
{
    const auto out = decode(blockOf(100, 100, indexRun()));
    const uint8_t expected[8] = { 100, 100, 100, 100, 100, 100, 0, 255 };
    for (int p = 0; p < 16; ++p)
    {
        EXPECT_EQ(out[p], expected[p % 8]) << "texel " << p;
    }
}

TEST(Bc4Decode, the_index_extremes_and_the_bit_order)
{
    // Texel 0 is the lowest three bits of byte 2; texel 15 the highest three bits of byte 7.
    std::array<int, 16> indices{};
    indices[0] = 7;
    indices[15] = 1;
    const auto out = decode(blockOf(200, 40, indices));
    EXPECT_EQ(out[0], 63);
    EXPECT_EQ(out[15], 40);
    for (int p = 1; p < 15; ++p)
    {
        EXPECT_EQ(out[p], 200) << p;
    }
    // All indices 7 in the six value mode are 255; all 6 are 0.
    std::array<int, 16> sevens;
    sevens.fill(7);
    std::array<int, 16> sixes;
    sixes.fill(6);
    for (const uint8_t v : decode(blockOf(0, 255, sevens)))
    {
        EXPECT_EQ(v, 255);
    }
    for (const uint8_t v : decode(blockOf(0, 255, sixes)))
    {
        EXPECT_EQ(v, 0);
    }
    // The raw bytes: 0xff in all six index bytes is index 7 everywhere (a1 = 40 here, the last value of the mode).
    const std::array<uint8_t, 8> raw{ 200, 40, 255, 255, 255, 255, 255, 255 };
    for (const uint8_t v : decode(raw))
    {
        EXPECT_EQ(v, 63);
    }
}

TEST(Bc4Decode, an_image_lays_its_blocks_row_by_row)
{
    // 8x4: two blocks side by side, solid 10 and solid 250 (equal endpoints, index 0).
    const std::vector<uint8_t> blocks = { 10, 10, 0, 0, 0, 0, 0, 0, 250, 250, 0, 0, 0, 0, 0, 0 };
    Gray out(32, 0xcd);
    vsgUtil::Bc4::decodeImage(blocks.data(), 8, 4, out.data());
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            EXPECT_EQ(out[y * 8 + x], x < 4 ? 10 : 250) << x << "," << y;
        }
    }
}

TEST(Bc4Encode, the_size_counts_partial_blocks)
{
    EXPECT_EQ(vsgUtil::Bc4::encodedSize(4, 4), 8u);
    EXPECT_EQ(vsgUtil::Bc4::encodedSize(1, 1), 8u);
    EXPECT_EQ(vsgUtil::Bc4::encodedSize(5, 4), 16u);
    EXPECT_EQ(vsgUtil::Bc4::encodedSize(2048, 2048), 2048u * 2048u / 2);
}

TEST(Bc4Encode, a_solid_block_comes_back_exactly)
{
    for (const int v : { 0, 1, 77, 128, 254, 255 })
    {
        const Gray src(16, static_cast<uint8_t>(v));
        const Gray decoded = encodeAndDecode(src, 4, 4);
        for (const uint8_t d : decoded)
        {
            EXPECT_EQ(d, v);
        }
    }
}

TEST(Bc4Encode, two_values_come_back_exactly)
{
    Gray src(16);
    for (int p = 0; p < 16; ++p)
    {
        src[p] = (p * 5) % 3 == 0 ? 10 : 200;
    }
    EXPECT_EQ(encodeAndDecode(src, 4, 4), src);
}

TEST(Bc4Encode, the_six_value_mode_keeps_zero_and_255_exact)
{
    // 0 and 255 are the mode's own extra values; the others lie on a0 + k (a1 - a0) / 5 with a0 = 60, a1 = 150.
    const uint8_t values[8] = { 0, 255, 60, 78, 96, 114, 132, 150 };
    Gray src(16);
    for (int p = 0; p < 16; ++p)
    {
        src[p] = values[p % 8];
    }
    EXPECT_EQ(encodeAndDecode(src, 4, 4), src);
}

TEST(Bc4Encode, a_smooth_gradient_stays_above_the_floor)
{
    Gray src(64 * 64);
    for (uint32_t y = 0; y < 64; ++y)
    {
        for (uint32_t x = 0; x < 64; ++x)
        {
            src[y * 64 + x] = static_cast<uint8_t>(x * 3 + y);
        }
    }
    EXPECT_GT(psnr(src, encodeAndDecode(src, 64, 64)), gradientFloorDb);
}

TEST(Bc4Encode, a_checker_stays_above_the_floor)
{
    Gray src(64 * 64);
    for (uint32_t y = 0; y < 64; ++y)
    {
        for (uint32_t x = 0; x < 64; ++x)
        {
            src[y * 64 + x] = ((x / 8) + (y / 8)) % 2 == 0 ? 230 : 25;
        }
    }
    EXPECT_GT(psnr(src, encodeAndDecode(src, 64, 64)), checkerFloorDb);
}

TEST(Bc4Encode, uniform_noise_stays_above_the_floor)
{
    Lcg random;
    Gray src(64 * 64);
    for (auto& v : src)
    {
        v = random.next();
    }
    EXPECT_GT(psnr(src, encodeAndDecode(src, 64, 64)), noiseFloorDb);
}

TEST(Bc4Encode, the_result_depends_only_on_the_input)
{
    Lcg random;
    Gray src(32 * 32);
    for (auto& v : src)
    {
        v = random.next();
    }
    std::vector<uint8_t> a(vsgUtil::Bc4::encodedSize(32, 32), 0x11);
    std::vector<uint8_t> b(a.size(), 0x99);
    vsgUtil::Bc4::encodeImage(src.data(), 32, 32, a.data());
    vsgUtil::Bc4::encodeImage(src.data(), 32, 32, b.data());
    EXPECT_EQ(a, b);
}

TEST(Bc4Encode, an_image_that_is_not_a_multiple_of_four_repeats_its_edge)
{
    const Gray src(5 * 3, 90);
    std::vector<uint8_t> blocks(vsgUtil::Bc4::encodedSize(5, 3), 0xee);
    ASSERT_EQ(blocks.size(), 16u);
    vsgUtil::Bc4::encodeImage(src.data(), 5, 3, blocks.data());
    Gray decoded(8 * 4, 0xcd);
    vsgUtil::Bc4::decodeImage(blocks.data(), 8, 4, decoded.data());
    for (const uint8_t d : decoded)
    {
        EXPECT_EQ(d, 90);
    }
}

TEST(R8Resize, the_same_size_is_a_copy)
{
    Lcg random;
    Gray src(13 * 7);
    for (auto& v : src)
    {
        v = random.next();
    }
    Gray dst(src.size(), 0xcd);
    vsgUtil::R8Resize::resize(src.data(), 13, 7, dst.data(), 13, 7);
    EXPECT_EQ(dst, src);
}

TEST(R8Resize, equals_the_rgba_resize_of_the_same_values_in_every_direction)
{
    Lcg random;
    const uint32_t sizes[][4] = { { 16, 16, 8, 8 }, { 8, 8, 32, 32 }, { 13, 7, 5, 11 }, { 64, 16, 16, 64 },
        { 4, 4, 1, 1 }, { 1, 1, 4, 4 }, { 9, 5, 9, 2 } };
    for (const auto& s : sizes)
    {
        Gray src(static_cast<size_t>(s[0]) * s[1]);
        std::vector<uint8_t> rgba(src.size() * 4);
        for (size_t i = 0; i < src.size(); ++i)
        {
            src[i] = random.next();
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = src[i];
        }
        Gray dst(static_cast<size_t>(s[2]) * s[3], 0xcd);
        std::vector<uint8_t> expected(dst.size() * 4);
        vsgUtil::R8Resize::resize(src.data(), s[0], s[1], dst.data(), s[2], s[3]);
        vsgUtil::RgbaResize::resize(rgba.data(), s[0], s[1], expected.data(), s[2], s[3]);
        for (size_t i = 0; i < dst.size(); ++i)
        {
            EXPECT_EQ(dst[i], expected[i * 4]) << s[0] << "x" << s[1] << " to " << s[2] << "x" << s[3] << " at " << i;
        }
    }
}
