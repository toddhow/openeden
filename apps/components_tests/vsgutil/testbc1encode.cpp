// Task 0089: the BC1 encoder. Generated data only; the decoder is the one the terrain arrays use.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/bc1encode.hpp>

#include <vsg/core/Array2D.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <tuple>
#include <vector>

namespace
{
    using Image = std::vector<uint8_t>;

    struct Lcg
    {
        uint32_t state = 777;
        uint8_t next()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<uint8_t>(state >> 24);
        }
    };

    Image make(uint32_t w, uint32_t h, auto&& texel)
    {
        Image image(static_cast<size_t>(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                uint8_t* t = &image[(static_cast<size_t>(y) * w + x) * 4];
                texel(x, y, t);
                t[3] = 255;
            }
        }
        return image;
    }

    // Decodes with the engine's own BC1 decoder (top level only), w and h multiples of 4.
    Image decodeBc1(const Image& blocks, uint32_t w, uint32_t h)
    {
        vsg::Data::Properties properties;
        properties.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        auto image = vsg::block64Array2D::create(w / 4, h / 4, properties);
        std::memcpy(image->dataPointer(), blocks.data(), blocks.size());
        const auto texels = vsgUtil::detail::decodeBlockCompressed(*image);
        Image out(texels.size() * 4);
        std::memcpy(out.data(), texels.data(), out.size());
        return out;
    }

    double psnr(const Image& a, const Image& b)
    {
        double sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < a.size(); i += 4)
        {
            for (int c = 0; c < 3; ++c)
            {
                const double d = double(a[i + c]) - double(b[i + c]);
                sum += d * d;
                ++count;
            }
        }
        if (sum == 0)
        {
            return 99.0;
        }
        return 10.0 * std::log10(255.0 * 255.0 / (sum / double(count)));
    }

    Image encode(const Image& image, uint32_t w, uint32_t h)
    {
        Image out(vsgUtil::Bc1Encode::encodedSize(w, h), 0xee);
        vsgUtil::Bc1Encode::encodeImage(image.data(), w, h, out.data());
        return out;
    }

    // Measured on the first run of the encoder (gradient 37.88 dB, noise 13.58 dB), then set a little lower. Uniform
    // RGB noise is the worst case for any BC1 encoder: four colours on one line keep a third of the variance of an
    // isotropic cloud, which bounds the PSNR near 12.5 dB plus what the line's four steps recover.
    constexpr double gradientFloorDb = 37.0;
    constexpr double checkerFloorDb = 40.0;
    constexpr double noiseFloorDb = 13.0;
}

TEST(Bc1Encode, the_size_counts_partial_blocks)
{
    EXPECT_EQ(vsgUtil::Bc1Encode::encodedSize(4, 4), 8u);
    EXPECT_EQ(vsgUtil::Bc1Encode::encodedSize(1, 1), 8u);
    EXPECT_EQ(vsgUtil::Bc1Encode::encodedSize(5, 4), 16u);
    EXPECT_EQ(vsgUtil::Bc1Encode::encodedSize(2048, 2048), 2048u * 2048u / 2);
}

TEST(Bc1Encode, a_smooth_gradient_stays_above_the_floor)
{
    const Image src = make(64, 64, [](uint32_t x, uint32_t y, uint8_t* t) {
        t[0] = static_cast<uint8_t>(x * 4);
        t[1] = static_cast<uint8_t>(y * 4);
        t[2] = static_cast<uint8_t>(128 + x * 2 - y);
    });
    const Image decoded = decodeBc1(encode(src, 64, 64), 64, 64);
    EXPECT_GT(psnr(src, decoded), gradientFloorDb);
}

TEST(Bc1Encode, a_checker_of_two_colours_stays_above_the_floor)
{
    const Image src = make(64, 64, [](uint32_t x, uint32_t y, uint8_t* t) {
        const bool on = ((x / 8) + (y / 8)) % 2 == 0;
        t[0] = on ? 220 : 30;
        t[1] = on ? 190 : 60;
        t[2] = on ? 40 : 160;
    });
    const Image decoded = decodeBc1(encode(src, 64, 64), 64, 64);
    EXPECT_GT(psnr(src, decoded), checkerFloorDb);
}

TEST(Bc1Encode, uniform_noise_stays_above_the_floor)
{
    Lcg random;
    const Image src = make(64, 64, [&random](uint32_t, uint32_t, uint8_t* t) {
        t[0] = random.next();
        t[1] = random.next();
        t[2] = random.next();
    });
    const Image decoded = decodeBc1(encode(src, 64, 64), 64, 64);
    EXPECT_GT(psnr(src, decoded), noiseFloorDb);
}

TEST(Bc1Encode, a_solid_block_comes_back_within_the_565_quantisation)
{
    for (auto [r, g, b] : std::initializer_list<std::tuple<int, int, int>>{
             { 0, 0, 0 }, { 255, 255, 255 }, { 131, 16, 0 }, { 200, 100, 50 }, { 7, 250, 99 } })
    {
        const Image src = make(4, 4, [=](uint32_t, uint32_t, uint8_t* t) {
            t[0] = static_cast<uint8_t>(r);
            t[1] = static_cast<uint8_t>(g);
            t[2] = static_cast<uint8_t>(b);
        });
        const Image decoded = decodeBc1(encode(src, 4, 4), 4, 4);
        for (size_t i = 0; i < decoded.size(); i += 4)
        {
            EXPECT_LE(std::abs(int(decoded[i]) - r), 4) << r << "," << g << "," << b;
            EXPECT_LE(std::abs(int(decoded[i + 1]) - g), 2);
            EXPECT_LE(std::abs(int(decoded[i + 2]) - b), 4);
        }
    }
}

TEST(Bc1Encode, a_block_is_never_in_three_colour_mode_unless_it_is_one_colour)
{
    Lcg random;
    for (int n = 0; n < 500; ++n)
    {
        uint8_t texels[64];
        for (auto& v : texels)
        {
            v = random.next();
        }
        uint8_t block[8];
        vsgUtil::Bc1Encode::encodeBlock(texels, block);
        const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
        const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
        if (c0 <= c1)
        {
            // Equal endpoints with index 0 everywhere, or the block would use the transparent index.
            EXPECT_EQ(c0, c1);
            EXPECT_EQ(block[4] | block[5] | block[6] | block[7], 0);
        }
    }
}

TEST(Bc1Encode, an_image_that_is_not_a_multiple_of_four_repeats_its_edge)
{
    // 5x3: the second block column and the lower rows come from edge texels. A solid image must stay solid.
    const Image src = make(5, 3, [](uint32_t, uint32_t, uint8_t* t) {
        t[0] = 90;
        t[1] = 180;
        t[2] = 60;
    });
    const Image blocks = encode(src, 5, 3);
    ASSERT_EQ(blocks.size(), 16u);
    const Image decoded = decodeBc1(blocks, 8, 4);
    for (size_t i = 0; i < decoded.size(); i += 4)
    {
        EXPECT_LE(std::abs(int(decoded[i]) - 90), 4);
        EXPECT_LE(std::abs(int(decoded[i + 1]) - 180), 2);
    }
}

TEST(Bc1Encode, one_by_one_and_two_by_two_images_encode)
{
    const Image one = make(1, 1, [](uint32_t, uint32_t, uint8_t* t) {
        t[0] = 10;
        t[1] = 200;
        t[2] = 30;
    });
    const Image blocks = encode(one, 1, 1);
    ASSERT_EQ(blocks.size(), 8u);
    const Image decoded = decodeBc1(blocks, 4, 4);
    EXPECT_LE(std::abs(int(decoded[0]) - 10), 4);
    EXPECT_LE(std::abs(int(decoded[1]) - 200), 2);
}

TEST(Bc1Encode, the_same_input_gives_the_same_bytes_every_time)
{
    Lcg random;
    const Image src = make(96, 64, [&random](uint32_t x, uint32_t y, uint8_t* t) {
        t[0] = static_cast<uint8_t>(x + (random.next() >> 5));
        t[1] = static_cast<uint8_t>(y * 3 + (random.next() >> 4));
        t[2] = random.next();
    });
    const Image first = encode(src, 96, 64);
    for (int run = 0; run < 3; ++run)
    {
        EXPECT_EQ(encode(src, 96, 64), first);
    }
}

TEST(Bc1Encode, the_encoded_image_decodes_the_same_each_time)
{
    Lcg random;
    const Image src = make(32, 32, [&random](uint32_t, uint32_t, uint8_t* t) {
        t[0] = random.next();
        t[1] = random.next();
        t[2] = random.next();
    });
    const Image blocks = encode(src, 32, 32);
    EXPECT_EQ(decodeBc1(blocks, 32, 32), decodeBc1(blocks, 32, 32));
}

TEST(Bc1Encode, encoding_a_decoded_image_again_loses_little)
{
    // decode(encode(x)) is already on the BC1 grid, so a second pass must not get worse than the first.
    const Image src = make(64, 64, [](uint32_t x, uint32_t y, uint8_t* t) {
        t[0] = static_cast<uint8_t>(x * 4);
        t[1] = static_cast<uint8_t>((x ^ y) * 4);
        t[2] = static_cast<uint8_t>(y * 4);
    });
    const Image once = decodeBc1(encode(src, 64, 64), 64, 64);
    const Image twice = decodeBc1(encode(once, 64, 64), 64, 64);
    EXPECT_GT(psnr(once, twice), 40.0);
}

TEST(Bc1Encode, a_two_thousand_square_image_encodes_in_well_under_a_second_on_one_thread)
{
    Lcg random;
    const uint32_t size = 2048;
    const Image src = make(size, size, [&random](uint32_t x, uint32_t y, uint8_t* t) {
        t[0] = static_cast<uint8_t>(x / 8 + (random.next() >> 6));
        t[1] = static_cast<uint8_t>(y / 8 + (random.next() >> 6));
        t[2] = static_cast<uint8_t>((x + y) / 16);
    });
    Image out(vsgUtil::Bc1Encode::encodedSize(size, size));
    const auto start = std::chrono::steady_clock::now();
    vsgUtil::Bc1Encode::encodeImage(src.data(), size, size, out.data());
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // The layer build encodes dozens of these on several threads; one must stay cheap. A generous bound for a loaded,
    // unoptimised machine, the measured time is in the task's stage log.
    EXPECT_LT(seconds, 3.0);
}
