#include <components/nif/data.hpp>
#include <components/vsgadapters/nif/internaltexture.hpp>

#include <vsg/core/Array2D.h>
#include <vsg/core/Data.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using Format = Nif::NiPixelFormat::Format;

    // Builds a NiPixelData with one mipmap level at the given offset.
    Nif::NiPixelData makePixelData(
        Format format, uint32_t bitsPerPixel, uint32_t width, uint32_t height, std::vector<uint8_t> data, uint32_t offset = 0)
    {
        Nif::NiPixelData pixelData{}; // value-initialised: NiPixelFormat has members without defaults
        pixelData.mPixelFormat.mFormat = format;
        pixelData.mPixelFormat.mBitsPerPixel = bitsPerPixel;
        pixelData.mPalette = Nif::NiPalettePtr(nullptr);
        pixelData.mBytesPerPixel = bitsPerPixel / 8;
        Nif::NiPixelData::Mipmap level;
        level.mWidth = width;
        level.mHeight = height;
        level.mOffset = offset;
        pixelData.mMipmaps.push_back(level);
        pixelData.mData = std::move(data);
        return pixelData;
    }

    std::vector<uint8_t> sequence(size_t size, uint8_t start = 1)
    {
        std::vector<uint8_t> bytes(size);
        for (size_t i = 0; i < size; ++i)
            bytes[i] = static_cast<uint8_t>(start + i);
        return bytes;
    }

    std::vector<uint8_t> bytesOf(const vsg::Data& data)
    {
        const auto* begin = static_cast<const uint8_t*>(data.dataPointer());
        return std::vector<uint8_t>(begin, begin + data.dataSize());
    }

    // A palette colour whose bytes in memory are R, G, B, A.
    uint32_t paletteColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const std::array<uint8_t, 4> bytes{ r, g, b, a };
        uint32_t value = 0;
        std::memcpy(&value, bytes.data(), sizeof(value));
        return value;
    }

    vsg::ref_ptr<vsg::Data> read(const Nif::NiPixelData& pixelData, std::string& error)
    {
        error.clear();
        return vsgAdapters::readInternalTexture(pixelData, error);
    }

    void expectFailure(const Nif::NiPixelData& pixelData)
    {
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        EXPECT_FALSE(result.valid());
        EXPECT_FALSE(error.empty());
    }

    // ---- Uncompressed 4-byte formats ------------------------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, rgbaKeepsBytesAndUsesR8G8B8A8)
    {
        const std::vector<uint8_t> pixels = sequence(3 * 2 * 4);
        const Nif::NiPixelData pixelData = makePixelData(Format::RGBA, 32, 3, 2, pixels);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 3u);
        EXPECT_EQ(image->height(), 2u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(bytesOf(*image), pixels);
    }

    TEST(VsgAdaptersInternalTextureTest, bgraKeepsBytesAndUsesB8G8R8A8)
    {
        const std::vector<uint8_t> pixels = sequence(2 * 2 * 4, 10);
        const Nif::NiPixelData pixelData = makePixelData(Format::BGRA, 32, 2, 2, pixels);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 2u);
        EXPECT_EQ(image->height(), 2u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_B8G8R8A8_UNORM);
        EXPECT_EQ(bytesOf(*image), pixels);
    }

    // ---- Uncompressed 3-byte formats gain opaque alpha ------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, rgbGainsOpaqueAlpha)
    {
        const std::vector<uint8_t> pixels{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        const Nif::NiPixelData pixelData = makePixelData(Format::RGB, 24, 2, 2, pixels);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 2u);
        EXPECT_EQ(image->height(), 2u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        const std::vector<uint8_t> expected{ 1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255 };
        EXPECT_EQ(bytesOf(*image), expected);
    }

    TEST(VsgAdaptersInternalTextureTest, bgrKeepsByteOrderGainsOpaqueAlphaAndUsesB8G8R8A8)
    {
        const std::vector<uint8_t> pixels{ 30, 20, 10, 60, 50, 40, 90, 80, 70 };
        const Nif::NiPixelData pixelData = makePixelData(Format::BGR, 24, 3, 1, pixels);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 3u);
        EXPECT_EQ(image->height(), 1u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_B8G8R8A8_UNORM);
        const std::vector<uint8_t> expected{ 30, 20, 10, 255, 60, 50, 40, 255, 90, 80, 70, 255 };
        EXPECT_EQ(bytesOf(*image), expected);
    }

    // ---- Level offset and first level only -----------------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, readsFromTheLevelOffset)
    {
        std::vector<uint8_t> data{ 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
        const std::vector<uint8_t> pixels{ 1, 2, 3, 4, 5, 6 };
        data.insert(data.end(), pixels.begin(), pixels.end());
        const Nif::NiPixelData pixelData = makePixelData(Format::RGB, 24, 2, 1, data, 5);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const std::vector<uint8_t> expected{ 1, 2, 3, 255, 4, 5, 6, 255 };
        EXPECT_EQ(bytesOf(*result), expected);
    }

    TEST(VsgAdaptersInternalTextureTest, usesOnlyTheFirstMipmapLevel)
    {
        // Level 0: 2x2 RGBA (16 bytes), level 1: 1x1 RGBA (4 bytes) after it.
        const std::vector<uint8_t> data = sequence(20);
        Nif::NiPixelData pixelData = makePixelData(Format::RGBA, 32, 2, 2, data);
        Nif::NiPixelData::Mipmap level1;
        level1.mWidth = 1;
        level1.mHeight = 1;
        level1.mOffset = 16;
        pixelData.mMipmaps.push_back(level1);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        EXPECT_EQ(result->width(), 2u);
        EXPECT_EQ(result->height(), 2u);
        EXPECT_EQ(bytesOf(*result), std::vector<uint8_t>(data.begin(), data.begin() + 16));
    }

    // ---- Palettes ------------------------------------------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, paletteExpandsThroughThePaletteWithOpaqueAlpha)
    {
        Nif::NiPalette palette;
        palette.mColors[0] = paletteColor(10, 20, 30, 40);
        palette.mColors[1] = paletteColor(50, 60, 70, 0);
        palette.mColors[255] = paletteColor(200, 210, 220, 128);
        Nif::NiPixelData pixelData = makePixelData(Format::Palette, 8, 2, 2, { 0, 1, 255, 1 });
        pixelData.mPalette = Nif::NiPalettePtr(&palette);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 2u);
        EXPECT_EQ(image->height(), 2u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        const std::vector<uint8_t> expected{ 10, 20, 30, 255, 50, 60, 70, 255, 200, 210, 220, 255, 50, 60, 70, 255 };
        EXPECT_EQ(bytesOf(*image), expected);
    }

    TEST(VsgAdaptersInternalTextureTest, paletteAlphaKeepsThePaletteAlpha)
    {
        Nif::NiPalette palette;
        palette.mColors[0] = paletteColor(10, 20, 30, 40);
        palette.mColors[1] = paletteColor(50, 60, 70, 0);
        palette.mColors[255] = paletteColor(200, 210, 220, 128);
        Nif::NiPixelData pixelData = makePixelData(Format::PaletteAlpha, 8, 2, 2, { 0, 1, 255, 1 });
        pixelData.mPalette = Nif::NiPalettePtr(&palette);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::ubvec4Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        const std::vector<uint8_t> expected{ 10, 20, 30, 40, 50, 60, 70, 0, 200, 210, 220, 128, 50, 60, 70, 0 };
        EXPECT_EQ(bytesOf(*image), expected);
    }

    TEST(VsgAdaptersInternalTextureTest, paletteReadsIndicesFromTheLevelOffset)
    {
        Nif::NiPalette palette;
        palette.mColors[3] = paletteColor(1, 2, 3, 4);
        palette.mColors[7] = paletteColor(5, 6, 7, 8);
        Nif::NiPixelData pixelData = makePixelData(Format::PaletteAlpha, 8, 2, 1, { 0, 0, 3, 7 }, 2);
        pixelData.mPalette = Nif::NiPalettePtr(&palette);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const std::vector<uint8_t> expected{ 1, 2, 3, 4, 5, 6, 7, 8 };
        EXPECT_EQ(bytesOf(*result), expected);
    }

    // ---- Block compressed ----------------------------------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, dxt1BecomesBc1Blocks)
    {
        // 8x4 pixels: 2x1 blocks of 8 bytes.
        const std::vector<uint8_t> blocks = sequence(2 * 8);
        const Nif::NiPixelData pixelData = makePixelData(Format::DXT1, 4, 8, 4, blocks);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::block64Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 2u);
        EXPECT_EQ(image->height(), 1u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        EXPECT_EQ(image->properties.blockWidth, 4);
        EXPECT_EQ(image->properties.blockHeight, 4);
        EXPECT_EQ(bytesOf(*image), blocks);
    }

    TEST(VsgAdaptersInternalTextureTest, dxt1RoundsPartialBlocksUp)
    {
        // 5x3 pixels: ceil(5/4) x ceil(3/4) = 2x1 blocks.
        const std::vector<uint8_t> blocks = sequence(2 * 8, 40);
        const Nif::NiPixelData pixelData = makePixelData(Format::DXT1, 4, 5, 3, blocks);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::block64Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 2u);
        EXPECT_EQ(image->height(), 1u);
        EXPECT_EQ(bytesOf(*image), blocks);
    }

    TEST(VsgAdaptersInternalTextureTest, dxt3BecomesBc2Blocks)
    {
        // 4x8 pixels: 1x2 blocks of 16 bytes.
        const std::vector<uint8_t> blocks = sequence(2 * 16);
        const Nif::NiPixelData pixelData = makePixelData(Format::DXT3, 8, 4, 8, blocks);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::block128Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 1u);
        EXPECT_EQ(image->height(), 2u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_BC2_UNORM_BLOCK);
        EXPECT_EQ(image->properties.blockWidth, 4);
        EXPECT_EQ(image->properties.blockHeight, 4);
        EXPECT_EQ(bytesOf(*image), blocks);
    }

    TEST(VsgAdaptersInternalTextureTest, dxt5BecomesBc3Blocks)
    {
        // 2x2 pixels: one partial block of 16 bytes, read from offset 3.
        std::vector<uint8_t> data{ 0, 0, 0 };
        const std::vector<uint8_t> blocks = sequence(16, 100);
        data.insert(data.end(), blocks.begin(), blocks.end());
        const Nif::NiPixelData pixelData = makePixelData(Format::DXT5, 8, 2, 2, data, 3);
        std::string error;
        const vsg::ref_ptr<vsg::Data> result = read(pixelData, error);
        ASSERT_TRUE(result.valid()) << error;
        const auto* image = result->cast<vsg::block128Array2D>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 1u);
        EXPECT_EQ(image->height(), 1u);
        EXPECT_EQ(image->properties.format, VK_FORMAT_BC3_UNORM_BLOCK);
        EXPECT_EQ(image->properties.blockWidth, 4);
        EXPECT_EQ(image->properties.blockHeight, 4);
        EXPECT_EQ(bytesOf(*image), blocks);
    }

    // ---- Errors --------------------------------------------------------------------------------

    TEST(VsgAdaptersInternalTextureTest, failsWithoutMipmaps)
    {
        Nif::NiPixelData pixelData = makePixelData(Format::RGBA, 32, 1, 1, sequence(4));
        pixelData.mMipmaps.clear();
        expectFailure(pixelData);
    }

    TEST(VsgAdaptersInternalTextureTest, failsWithZeroWidth)
    {
        expectFailure(makePixelData(Format::RGBA, 32, 0, 2, sequence(16)));
    }

    TEST(VsgAdaptersInternalTextureTest, failsWithZeroHeight)
    {
        expectFailure(makePixelData(Format::RGBA, 32, 2, 0, sequence(16)));
    }

    TEST(VsgAdaptersInternalTextureTest, failsWhenUncompressedLevelRunsPastTheData)
    {
        // 2x2 RGB needs 12 bytes.
        expectFailure(makePixelData(Format::RGB, 24, 2, 2, sequence(11)));
        expectFailure(makePixelData(Format::RGBA, 32, 2, 2, sequence(15)));
        expectFailure(makePixelData(Format::BGRA, 32, 2, 2, sequence(15)));
        expectFailure(makePixelData(Format::BGR, 24, 2, 2, sequence(11)));
    }

    TEST(VsgAdaptersInternalTextureTest, failsWhenOffsetPushesTheLevelPastTheData)
    {
        // 16 bytes of data hold a 2x2 RGBA level only at offset 0.
        expectFailure(makePixelData(Format::RGBA, 32, 2, 2, sequence(16), 1));
        expectFailure(makePixelData(Format::RGBA, 32, 1, 1, sequence(16), 16));
    }

    TEST(VsgAdaptersInternalTextureTest, failsWhenCompressedLevelRunsPastTheData)
    {
        // 5x5 DXT1 is 2x2 blocks = 32 bytes; DXT3/DXT5 = 64 bytes.
        expectFailure(makePixelData(Format::DXT1, 4, 5, 5, sequence(31)));
        expectFailure(makePixelData(Format::DXT3, 8, 5, 5, sequence(63)));
        expectFailure(makePixelData(Format::DXT5, 8, 5, 5, sequence(63)));
    }

    TEST(VsgAdaptersInternalTextureTest, failsWhenPaletteLevelRunsPastTheData)
    {
        Nif::NiPalette palette;
        Nif::NiPixelData pixelData = makePixelData(Format::Palette, 8, 2, 2, { 0, 0, 0 });
        pixelData.mPalette = Nif::NiPalettePtr(&palette);
        expectFailure(pixelData);
    }

    TEST(VsgAdaptersInternalTextureTest, failsForPaletteWithoutPalette)
    {
        expectFailure(makePixelData(Format::Palette, 8, 2, 2, { 0, 0, 0, 0 }));
        expectFailure(makePixelData(Format::PaletteAlpha, 8, 2, 2, { 0, 0, 0, 0 }));
    }

    TEST(VsgAdaptersInternalTextureTest, failsForPaletteThatIsNotEightBit)
    {
        Nif::NiPalette palette;
        Nif::NiPixelData pixelData = makePixelData(Format::Palette, 4, 2, 2, { 0, 0, 0, 0 });
        pixelData.mPalette = Nif::NiPalettePtr(&palette);
        expectFailure(pixelData);

        Nif::NiPixelData alphaData = makePixelData(Format::PaletteAlpha, 16, 2, 2, sequence(8));
        alphaData.mPalette = Nif::NiPalettePtr(&palette);
        expectFailure(alphaData);
    }

    TEST(VsgAdaptersInternalTextureTest, failsForUnknownFormat)
    {
        expectFailure(makePixelData(static_cast<Format>(99), 32, 1, 1, sequence(64)));
    }
}
