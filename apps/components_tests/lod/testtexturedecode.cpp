// Task 0055 (albedo bake): decoding the first mip level of a land texture on the CPU. Pure code; hand-derived blocks.
#include <gtest/gtest.h>

#include <components/lod/texturedecode.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Lod;

    using Bytes = std::vector<std::uint8_t>;
    using Pixel = std::array<int, 4>;

    constexpr SourceFormat allFormats[] = { SourceFormat::Rgba8, SourceFormat::Bgra8, SourceFormat::Rgb8,
        SourceFormat::Bgr8, SourceFormat::Grey8, SourceFormat::Bc1, SourceFormat::Bc2, SourceFormat::Bc3 };

    Pixel pixelAt(const Rgba8Image& image, std::uint32_t x, std::uint32_t y)
    {
        const std::size_t offset = (static_cast<std::size_t>(y) * image.mWidth + x) * 4;
        return { image.mPixels[offset], image.mPixels[offset + 1], image.mPixels[offset + 2],
            image.mPixels[offset + 3] };
    }

    void expectPixel(const Rgba8Image& image, std::uint32_t x, std::uint32_t y, const Pixel& expected, int tolerance)
    {
        const Pixel actual = pixelAt(image, x, y);
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            EXPECT_NEAR(actual[channel], expected[channel], tolerance)
                << "pixel " << x << ", " << y << " channel " << channel;
        }
    }

    // Row byte of a BC1 colour block: two bits a pixel, pixel 0 in the lowest bits.
    std::uint8_t packRow(unsigned a, unsigned b, unsigned c, unsigned d)
    {
        return static_cast<std::uint8_t>(a | (b << 2) | (c << 4) | (d << 6));
    }

    // A BC1 colour block: colour 0, colour 1 (little endian), then one byte of indices per row.
    Bytes colourBlock(std::uint16_t colour0, std::uint16_t colour1, const std::array<std::uint8_t, 4>& rows)
    {
        return { static_cast<std::uint8_t>(colour0 & 0xFF), static_cast<std::uint8_t>(colour0 >> 8),
            static_cast<std::uint8_t>(colour1 & 0xFF), static_cast<std::uint8_t>(colour1 >> 8), rows[0], rows[1],
            rows[2], rows[3] };
    }

    void append(Bytes& target, const Bytes& more)
    {
        target.insert(target.end(), more.begin(), more.end());
    }

    // The sixteen 3-bit alpha indices of a BC3 block as six little endian bytes (pixel 0 in the lowest bits).
    Bytes alphaIndexBytes(const std::array<unsigned, 16>& indices)
    {
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < 16; ++i)
        {
            bits |= static_cast<std::uint64_t>(indices[i] & 7u) << (3 * i);
        }
        Bytes bytes;
        for (std::size_t i = 0; i < 6; ++i)
        {
            bytes.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
        }
        return bytes;
    }

    constexpr std::uint16_t red565 = 0xF800;
    constexpr std::uint16_t blue565 = 0x001F;
    constexpr std::uint16_t green565 = 0x07E0;
    const std::array<std::uint8_t, 4> allIndexZero = { 0, 0, 0, 0 };

    TEST(LodTextureDecode, mip_level_bytes_of_every_format)
    {
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc1, 4, 4), 8u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc1, 1, 1), 8u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc1, 5, 5), 32u); // 2 x 2 blocks
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc1, 8, 4), 16u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc1, 5, 3), 16u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc2, 4, 4), 16u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc2, 5, 5), 64u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc3, 4, 4), 16u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bc3, 9, 4), 48u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Rgba8, 3, 2), 24u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bgra8, 3, 2), 24u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Rgb8, 3, 2), 18u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Bgr8, 3, 2), 18u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Grey8, 3, 2), 6u);
        EXPECT_EQ(mipLevelBytes(SourceFormat::Rgba8, 16384, 16384), std::uint64_t{ 1 } << 30);
    }

    TEST(LodTextureDecode, mip_level_bytes_is_zero_for_a_zero_size)
    {
        for (const SourceFormat format : allFormats)
        {
            EXPECT_EQ(mipLevelBytes(format, 0, 5), 0u) << static_cast<int>(format);
            EXPECT_EQ(mipLevelBytes(format, 5, 0), 0u) << static_cast<int>(format);
            EXPECT_EQ(mipLevelBytes(format, 0, 0), 0u) << static_cast<int>(format);
        }
    }

    TEST(LodTextureDecode, rgba8_is_copied)
    {
        const Bytes bytes = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Rgba8, 2, 2, bytes, image));
        EXPECT_EQ(image.mWidth, 2u);
        EXPECT_EQ(image.mHeight, 2u);
        EXPECT_EQ(image.mPixels, bytes);
    }

    TEST(LodTextureDecode, bgra8_swaps_red_and_blue)
    {
        const Bytes bytes = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bgra8, 2, 2, bytes, image));
        const Bytes expected = { 3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12, 15, 14, 13, 16 };
        EXPECT_EQ(image.mPixels, expected);
    }

    TEST(LodTextureDecode, rgb8_gets_alpha_255)
    {
        const Bytes bytes = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Rgb8, 2, 2, bytes, image));
        const Bytes expected = { 10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255 };
        EXPECT_EQ(image.mPixels, expected);
    }

    TEST(LodTextureDecode, bgr8_swaps_and_gets_alpha_255)
    {
        const Bytes bytes = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bgr8, 2, 2, bytes, image));
        const Bytes expected = { 30, 20, 10, 255, 60, 50, 40, 255, 90, 80, 70, 255, 120, 110, 100, 255 };
        EXPECT_EQ(image.mPixels, expected);
    }

    TEST(LodTextureDecode, grey8_is_copied_to_red_green_and_blue)
    {
        const Bytes bytes = { 0, 77, 128, 255 };
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Grey8, 2, 2, bytes, image));
        const Bytes expected = { 0, 0, 0, 255, 77, 77, 77, 255, 128, 128, 128, 255, 255, 255, 255, 255 };
        EXPECT_EQ(image.mPixels, expected);
    }

    TEST(LodTextureDecode, bc1_four_colour_mode)
    {
        // colour 0 red, colour 1 blue, colour 0 > colour 1: index 2 = (2 c0 + c1) / 3, index 3 = (c0 + 2 c1) / 3.
        const Bytes bytes = colourBlock(
            red565, blue565, { packRow(0, 1, 2, 3), packRow(3, 2, 1, 0), packRow(0, 0, 0, 0), packRow(1, 1, 1, 1) });
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, image));
        EXPECT_EQ(image.mWidth, 4u);
        EXPECT_EQ(image.mHeight, 4u);
        ASSERT_EQ(image.mPixels.size(), 64u);

        const Pixel palette[4] = { { 255, 0, 0, 255 }, { 0, 0, 255, 255 }, { 170, 0, 85, 255 }, { 85, 0, 170, 255 } };
        const unsigned indices[4][4] = { { 0, 1, 2, 3 }, { 3, 2, 1, 0 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 } };
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                expectPixel(image, x, y, palette[indices[y][x]], 1);
            }
        }
    }

    TEST(LodTextureDecode, bc1_three_colour_mode_has_a_transparent_black_index)
    {
        // colour 0 <= colour 1: index 2 is the midpoint, index 3 is transparent black.
        const Bytes bytes = colourBlock(
            blue565, red565, { packRow(0, 1, 2, 3), packRow(3, 2, 1, 0), packRow(2, 2, 2, 2), packRow(3, 3, 3, 3) });
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, image));

        const Pixel palette[4] = { { 0, 0, 255, 255 }, { 255, 0, 0, 255 }, { 127, 0, 127, 255 }, { 0, 0, 0, 0 } };
        const unsigned indices[4][4] = { { 0, 1, 2, 3 }, { 3, 2, 1, 0 }, { 2, 2, 2, 2 }, { 3, 3, 3, 3 } };
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                const unsigned index = indices[y][x];
                if (index == 3)
                {
                    expectPixel(image, x, y, palette[3], 0);
                }
                else if (index == 2)
                {
                    expectPixel(image, x, y, { 127, 0, 127, 255 }, 1); // 127.5 rounds either way
                }
                else
                {
                    expectPixel(image, x, y, palette[index], 0);
                }
            }
        }
    }

    TEST(LodTextureDecode, bc1_equal_endpoints_use_the_three_colour_mode)
    {
        const Bytes bytes = colourBlock(
            0xFFFF, 0xFFFF, { packRow(0, 1, 2, 3), packRow(2, 2, 2, 2), packRow(3, 3, 3, 3), packRow(0, 0, 0, 0) });
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, image));
        expectPixel(image, 0, 0, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 1, 0, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 2, 0, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 3, 0, { 0, 0, 0, 0 }, 0);
        expectPixel(image, 0, 1, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 0, 2, { 0, 0, 0, 0 }, 0);
    }

    TEST(LodTextureDecode, bc1_565_endpoints_expand_by_bit_replication)
    {
        // 0xFFFF -> 255, 255, 255. 0x8410 = 10000 100000 10000 -> (16 << 3 | 16 >> 2, 32 << 2 | 32 >> 4, ...) =
        // (132, 130, 132). Colour 0 > colour 1, so index 0 and 1 are the endpoints themselves.
        const Bytes bytes = colourBlock(
            0xFFFF, 0x8410, { packRow(0, 1, 0, 1), packRow(1, 0, 1, 0), packRow(0, 0, 1, 1), packRow(1, 1, 0, 0) });
        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, image));
        expectPixel(image, 0, 0, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 1, 0, { 132, 130, 132, 255 }, 0);
        expectPixel(image, 0, 1, { 132, 130, 132, 255 }, 0);
        expectPixel(image, 1, 1, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 0, 2, { 255, 255, 255, 255 }, 0);
        expectPixel(image, 2, 2, { 132, 130, 132, 255 }, 0);
        expectPixel(image, 0, 3, { 132, 130, 132, 255 }, 0);
        expectPixel(image, 3, 3, { 255, 255, 255, 255 }, 0);

        const Bytes greenBlock = colourBlock(green565, 0, allIndexZero);
        Rgba8Image green;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, greenBlock, green));
        expectPixel(green, 2, 2, { 0, 255, 0, 255 }, 0);
    }

    TEST(LodTextureDecode, bc3_alpha_with_eight_interpolated_values)
    {
        // a0 = 252 > a1 = 0: index 0 = a0, 1 = a1, k = 2..7: ((8 - k) a0 + (k - 1) a1) / 7 = 216, 180, 144, 108,
        // 72, 36.
        std::array<unsigned, 16> alphaIndices{};
        for (unsigned i = 0; i < 16; ++i)
        {
            alphaIndices[i] = i % 8;
        }
        Bytes bytes = { 252, 0 };
        append(bytes, alphaIndexBytes(alphaIndices));
        append(bytes, colourBlock(red565, blue565, allIndexZero));
        ASSERT_EQ(bytes.size(), 16u);

        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc3, 4, 4, bytes, image));
        const int alphaOfIndex[8] = { 252, 0, 216, 180, 144, 108, 72, 36 };
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                expectPixel(image, x, y, { 255, 0, 0, alphaOfIndex[(y * 4 + x) % 8] }, 1);
            }
        }
    }

    TEST(LodTextureDecode, bc3_alpha_with_six_values_plus_zero_and_255)
    {
        // a0 = 0 <= a1 = 250: index 0 = a0, 1 = a1, k = 2..5: ((6 - k) a0 + (k - 1) a1) / 5 = 50, 100, 150, 200,
        // index 6 = 0, index 7 = 255.
        std::array<unsigned, 16> alphaIndices{};
        for (unsigned i = 0; i < 16; ++i)
        {
            alphaIndices[i] = i % 8;
        }
        Bytes bytes = { 0, 250 };
        append(bytes, alphaIndexBytes(alphaIndices));
        append(bytes, colourBlock(red565, blue565, allIndexZero));

        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc3, 4, 4, bytes, image));
        const int alphaOfIndex[8] = { 0, 250, 50, 100, 150, 200, 0, 255 };
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                expectPixel(image, x, y, { 255, 0, 0, alphaOfIndex[(y * 4 + x) % 8] }, 1);
            }
        }
    }

    TEST(LodTextureDecode, bc2_explicit_alpha_nibbles_are_expanded)
    {
        // Pixel i has the 4-bit alpha i, two pixels a byte, the first pixel in the low nibble: alpha = i * 17.
        Bytes bytes;
        for (unsigned j = 0; j < 8; ++j)
        {
            bytes.push_back(static_cast<std::uint8_t>((2 * j) | ((2 * j + 1) << 4)));
        }
        append(bytes, colourBlock(green565, 0, allIndexZero));
        ASSERT_EQ(bytes.size(), 16u);

        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc2, 4, 4, bytes, image));
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                expectPixel(image, x, y, { 0, 255, 0, static_cast<int>((y * 4 + x) * 17) }, 0);
            }
        }
    }

    TEST(LodTextureDecode, block_formats_decode_only_the_visible_pixels_of_a_partial_block)
    {
        // 5 x 3: two blocks side by side, the second one mostly outside. Block 0 is red, block 1 is blue.
        Bytes bytes = colourBlock(red565, blue565, allIndexZero);
        append(bytes, colourBlock(blue565, 0, allIndexZero));
        ASSERT_EQ(bytes.size(), mipLevelBytes(SourceFormat::Bc1, 5, 3));

        Rgba8Image image;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 5, 3, bytes, image));
        EXPECT_EQ(image.mWidth, 5u);
        EXPECT_EQ(image.mHeight, 3u);
        ASSERT_EQ(image.mPixels.size(), 5u * 3u * 4u);
        for (std::uint32_t y = 0; y < 3; ++y)
        {
            for (std::uint32_t x = 0; x < 5; ++x)
            {
                expectPixel(image, x, y, x < 4 ? Pixel{ 255, 0, 0, 255 } : Pixel{ 0, 0, 255, 255 }, 0);
            }
        }
    }

    TEST(LodTextureDecode, bytes_after_the_first_level_are_ignored)
    {
        Bytes bytes = colourBlock(red565, blue565, allIndexZero);
        Rgba8Image exact;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, exact));

        bytes.insert(bytes.end(), 40, 0xFF); // later mip levels
        Rgba8Image longer;
        ASSERT_TRUE(decodeTexture(SourceFormat::Bc1, 4, 4, bytes, longer));
        EXPECT_EQ(longer.mWidth, 4u);
        EXPECT_EQ(longer.mHeight, 4u);
        EXPECT_EQ(longer.mPixels, exact.mPixels);

        const Bytes rgba = { 1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99 };
        Rgba8Image rgbaImage;
        ASSERT_TRUE(decodeTexture(SourceFormat::Rgba8, 2, 1, rgba, rgbaImage));
        const Bytes expected = { 1, 2, 3, 4, 5, 6, 7, 8 };
        EXPECT_EQ(rgbaImage.mPixels, expected);
    }

    TEST(LodTextureDecode, a_span_that_is_too_short_is_refused_and_leaves_the_output_empty)
    {
        for (const SourceFormat format : allFormats)
        {
            const std::uint64_t needed = mipLevelBytes(format, 8, 8);
            ASSERT_GT(needed, 1u);
            const Bytes shortBytes(static_cast<std::size_t>(needed - 1), 0x55);
            Rgba8Image image;
            image.mWidth = 99;
            image.mHeight = 98;
            image.mPixels.assign(10, 1);
            EXPECT_FALSE(decodeTexture(format, 8, 8, shortBytes, image)) << static_cast<int>(format);
            EXPECT_TRUE(image.mPixels.empty()) << static_cast<int>(format);

            const Bytes exactBytes(static_cast<std::size_t>(needed), 0x55);
            Rgba8Image fine;
            EXPECT_TRUE(decodeTexture(format, 8, 8, exactBytes, fine)) << static_cast<int>(format);
        }
        Rgba8Image none;
        EXPECT_FALSE(decodeTexture(SourceFormat::Rgba8, 2, 2, std::span<const std::uint8_t>(), none));
        EXPECT_TRUE(none.mPixels.empty());
    }

    TEST(LodTextureDecode, a_zero_or_too_large_size_is_refused)
    {
        const Bytes plenty(1 << 16, 0x33);
        Rgba8Image image;
        image.mPixels.assign(4, 7);
        EXPECT_FALSE(decodeTexture(SourceFormat::Grey8, 0, 4, plenty, image));
        EXPECT_TRUE(image.mPixels.empty());
        EXPECT_FALSE(decodeTexture(SourceFormat::Grey8, 4, 0, plenty, image));
        EXPECT_TRUE(image.mPixels.empty());

        // 16384 is the largest side; the bytes are there for both of these.
        const Bytes wide(16385, 0x33);
        EXPECT_FALSE(decodeTexture(SourceFormat::Grey8, 16385, 1, wide, image));
        EXPECT_TRUE(image.mPixels.empty());
        EXPECT_FALSE(decodeTexture(SourceFormat::Grey8, 1, 16385, wide, image));
        EXPECT_TRUE(image.mPixels.empty());

        EXPECT_TRUE(decodeTexture(SourceFormat::Grey8, 16384, 1, wide, image));
        EXPECT_EQ(image.mWidth, 16384u);
        EXPECT_EQ(image.mHeight, 1u);
        EXPECT_EQ(image.mPixels.size(), 16384u * 4u);
    }
}
