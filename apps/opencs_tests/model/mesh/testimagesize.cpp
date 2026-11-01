#include "apps/opencs/model/mesh/texturereport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        using Bytes = std::vector<unsigned char>;
        using Size = std::pair<std::uint32_t, std::uint32_t>;

        void putLe32(Bytes& b, size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b[at + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
        }

        void putBe32(Bytes& b, size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b[at + 3 - i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
        }

        void putLe16(Bytes& b, size_t at, std::uint32_t v)
        {
            b[at] = static_cast<unsigned char>(v & 0xff);
            b[at + 1] = static_cast<unsigned char>((v >> 8) & 0xff);
        }

        Bytes dds(std::uint32_t w, std::uint32_t h, size_t size = 20)
        {
            Bytes b(size, 0);
            b[0] = 'D';
            b[1] = 'D';
            b[2] = 'S';
            b[3] = ' ';
            if (size >= 20)
            {
                putLe32(b, 12, h);
                putLe32(b, 16, w);
            }
            return b;
        }

        Bytes tga(std::uint8_t mapType, std::uint8_t imageType, std::uint32_t w, std::uint32_t h, size_t size = 18)
        {
            Bytes b(size, 0);
            b[1] = mapType;
            b[2] = imageType;
            if (size >= 16)
            {
                putLe16(b, 12, w);
                putLe16(b, 14, h);
            }
            return b;
        }

        Bytes bmp(std::int32_t w, std::int32_t h, size_t size = 26)
        {
            Bytes b(size, 0);
            b[0] = 'B';
            b[1] = 'M';
            if (size >= 26)
            {
                putLe32(b, 18, static_cast<std::uint32_t>(w));
                putLe32(b, 22, static_cast<std::uint32_t>(h));
            }
            return b;
        }

        Bytes png(std::uint32_t w, std::uint32_t h, size_t size = 24)
        {
            Bytes b(size, 0);
            const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
            for (int i = 0; i < 8; ++i)
                b[i] = sig[i];
            if (size >= 24)
            {
                putBe32(b, 16, w);
                putBe32(b, 20, h);
            }
            return b;
        }
    }

    TEST(CSMMeshImageSizeTest, ddsWidthAndHeight)
    {
        const auto r = imageSizeFromHeader(dds(512, 256));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(512, 256));
    }

    TEST(CSMMeshImageSizeTest, ddsUsesFullThirtyTwoBits)
    {
        const auto r = imageSizeFromHeader(dds(70000, 65537));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(70000, 65537));
    }

    TEST(CSMMeshImageSizeTest, ddsLongerBufferAccepted)
    {
        const auto r = imageSizeFromHeader(dds(64, 32, 128));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(64, 32));
    }

    TEST(CSMMeshImageSizeTest, ddsTruncatedAndZero)
    {
        Bytes shortDds = dds(64, 32);
        shortDds.resize(19);
        EXPECT_FALSE(imageSizeFromHeader(shortDds).has_value());
        EXPECT_FALSE(imageSizeFromHeader(dds(0, 32)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(dds(32, 0)).has_value());
    }

    TEST(CSMMeshImageSizeTest, tgaWidthAndHeight)
    {
        const auto r = imageSizeFromHeader(tga(0, 2, 300, 200));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(300, 200));
    }

    TEST(CSMMeshImageSizeTest, tgaAcceptedImageTypesAndMapTypes)
    {
        for (std::uint8_t type : { 1, 2, 3, 9, 10, 11 })
            for (std::uint8_t map : { 0, 1 })
            {
                const auto r = imageSizeFromHeader(tga(map, type, 16, 8));
                ASSERT_TRUE(r.has_value()) << int(type) << "/" << int(map);
                EXPECT_EQ(*r, Size(16, 8));
            }
    }

    TEST(CSMMeshImageSizeTest, tgaRejectedTypes)
    {
        for (std::uint8_t type : { 0, 4, 5, 8, 12, 32, 255 })
            EXPECT_FALSE(imageSizeFromHeader(tga(0, type, 16, 8)).has_value()) << int(type);
        EXPECT_FALSE(imageSizeFromHeader(tga(2, 2, 16, 8)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(tga(255, 2, 16, 8)).has_value());
    }

    TEST(CSMMeshImageSizeTest, tgaZeroSizeAndTruncated)
    {
        EXPECT_FALSE(imageSizeFromHeader(tga(0, 2, 0, 8)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(tga(0, 2, 8, 0)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(tga(0, 2, 8, 8, 17)).has_value());
    }

    TEST(CSMMeshImageSizeTest, tgaSixteenBitMaximum)
    {
        const auto r = imageSizeFromHeader(tga(0, 2, 65535, 4096));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(65535, 4096));
    }

    TEST(CSMMeshImageSizeTest, bmpWidthAndHeight)
    {
        const auto r = imageSizeFromHeader(bmp(640, 480));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(640, 480));
    }

    TEST(CSMMeshImageSizeTest, bmpNegativeHeightIsAbsolute)
    {
        const auto r = imageSizeFromHeader(bmp(640, -480));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(640, 480));
    }

    TEST(CSMMeshImageSizeTest, bmpTruncatedAndZero)
    {
        EXPECT_FALSE(imageSizeFromHeader(bmp(8, 8, 25)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(bmp(0, 8)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(bmp(8, 0)).has_value());
    }

    TEST(CSMMeshImageSizeTest, pngBigEndianWidthAndHeight)
    {
        const auto r = imageSizeFromHeader(png(1024, 768));
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(*r, Size(1024, 768));
        const auto big = imageSizeFromHeader(png(0x01020304, 0x05060708));
        ASSERT_TRUE(big.has_value());
        EXPECT_EQ(*big, Size(0x01020304, 0x05060708));
    }

    TEST(CSMMeshImageSizeTest, pngBadSignatureTruncatedAndZero)
    {
        Bytes bad = png(10, 10);
        bad[1] = 'X';
        EXPECT_FALSE(imageSizeFromHeader(bad).has_value());
        EXPECT_FALSE(imageSizeFromHeader(png(10, 10, 23)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(png(0, 10)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(png(10, 0)).has_value());
    }

    TEST(CSMMeshImageSizeTest, nonImagesGiveNothing)
    {
        EXPECT_FALSE(imageSizeFromHeader(Bytes{}).has_value());
        EXPECT_FALSE(imageSizeFromHeader(Bytes(64, 0xff)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(Bytes(64, 0)).has_value());
        EXPECT_FALSE(imageSizeFromHeader(Bytes(64, 'a')).has_value());
    }
}
