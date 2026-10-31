#include <components/vsgutil/imageio.hpp>
#include <components/vsgutil/imagepixels.hpp>

#include <gtest/gtest.h>

#include <vsg/core/Array2D.h>
#include <vsg/maths/vec4.h>

#include <vector>

namespace
{
    using namespace testing;

    // Decodes through the function the game uses to read a converted save's images.
    vsg::ref_ptr<vsg::ubvec4Array2D> decode(const std::vector<char>& encoded)
    {
        const auto data = vsgUtil::readImageFromMemory(encoded);
        return vsg::ref_ptr<vsg::ubvec4Array2D>(data ? data->cast<vsg::ubvec4Array2D>() : nullptr);
    }

    TEST(VsgUtilImagePixelsTest, pngOfRgbaKeepsRowOrderAndEveryChannel)
    {
        const std::vector<unsigned char> pixels = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
            20, 21, 22, 23, 24 };
        const auto image = decode(vsgUtil::encodeImage(pixels.data(), 2, 3, 4, ".png"));
        ASSERT_TRUE(image);
        ASSERT_EQ(image->width(), 2u);
        ASSERT_EQ(image->height(), 3u);
        EXPECT_EQ(image->at(0, 0), vsg::ubvec4(1, 2, 3, 4));
        EXPECT_EQ(image->at(1, 0), vsg::ubvec4(5, 6, 7, 8));
        EXPECT_EQ(image->at(0, 2), vsg::ubvec4(17, 18, 19, 20));
        EXPECT_EQ(image->at(1, 2), vsg::ubvec4(21, 22, 23, 24));
    }

    TEST(VsgUtilImagePixelsTest, tgaOfRgbaKeepsRowOrderAndEveryChannel)
    {
        const std::vector<unsigned char> pixels = { 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 10, 20, 30, 40 };
        const auto image = decode(vsgUtil::encodeImage(pixels.data(), 2, 2, 4, ".tga"));
        ASSERT_TRUE(image);
        EXPECT_EQ(image->at(0, 0), vsg::ubvec4(255, 0, 0, 255));
        EXPECT_EQ(image->at(1, 0), vsg::ubvec4(0, 255, 0, 128));
        EXPECT_EQ(image->at(0, 1), vsg::ubvec4(0, 0, 255, 64));
        EXPECT_EQ(image->at(1, 1), vsg::ubvec4(10, 20, 30, 40));
    }

    TEST(VsgUtilImagePixelsTest, jpgOfRgbKeepsRowOrderWithinItsLoss)
    {
        // The top half is red and the bottom half blue, so a flipped image is far from the expected values.
        std::vector<unsigned char> pixels(16 * 16 * 3, 0);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                pixels[(y * 16 + x) * 3 + (y < 8 ? 0 : 2)] = 255;
        const auto image = decode(vsgUtil::encodeImage(pixels.data(), 16, 16, 3, ".jpg"));
        ASSERT_TRUE(image);
        ASSERT_EQ(image->width(), 16u);
        EXPECT_GT(image->at(4, 2).r, 200);
        EXPECT_LT(image->at(4, 2).b, 55);
        EXPECT_GT(image->at(4, 13).b, 200);
        EXPECT_LT(image->at(4, 13).r, 55);
    }

    TEST(VsgUtilImagePixelsTest, encodeImageRefusesOtherChannelCounts)
    {
        const std::vector<unsigned char> pixels = { 1, 2 };
        EXPECT_TRUE(vsgUtil::encodeImage(pixels.data(), 1, 1, 2, ".png").empty());
    }

    TEST(VsgUtilImagePixelsTest, doubleImageSizeWeighsThreeQuartersToOneQuarterAndWraps)
    {
        // The expected values are worked by hand from the box filter in gluScaleImage: the new pixel at i covers the
        // source range (i + 0.5) / 2 +- 0.5, wrapping around.
        const std::vector<unsigned char> pixels = { 0, 16, 32, 48 };
        const std::vector<unsigned char> result = vsgUtil::doubleImageSize(pixels, 2, 1);
        ASSERT_EQ(result.size(), 16u);
        const auto at = [&](int x, int y) -> int { return result[y * 4 + x]; };
        EXPECT_EQ(at(0, 0), 12);
        EXPECT_EQ(at(1, 0), 12);
        EXPECT_EQ(at(2, 0), 20);
        EXPECT_EQ(at(0, 2), 28);
        EXPECT_EQ(at(3, 3), 36);
    }

    TEST(VsgUtilImagePixelsTest, doubleImageSizeRoundsAsGluScaleImagesSixteenBits)
    {
        // The weighted value here is 62.5. In 16 bits that is 16062.5, which rounds to 16063, whose high byte is 62. The
        // first is also what the OSG-era importer wrote for these weights.
        const std::vector<unsigned char> pixels = { 0, 7, 243, 250 };
        EXPECT_EQ(vsgUtil::doubleImageSize(pixels, 2, 1)[0], 62);
    }

    TEST(VsgUtilImagePixelsTest, doubleImageSizeKeepsAFlatImageAndEachChannelApart)
    {
        const std::vector<unsigned char> pixels = { 200, 10, 200, 10, 200, 10, 200, 10 };
        const std::vector<unsigned char> result = vsgUtil::doubleImageSize(pixels, 2, 2);
        ASSERT_EQ(result.size(), 32u);
        for (std::size_t i = 0; i < result.size(); ++i)
            EXPECT_EQ(result[i], i % 2 == 0 ? 200 : 10);
    }

    TEST(VsgUtilImagePixelsTest, bgraToRgbSwapsRedAndBlueAndDropsAlpha)
    {
        const std::vector<unsigned char> pixels = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const std::vector<unsigned char> expected = { 3, 2, 1, 7, 6, 5 };
        EXPECT_EQ(vsgUtil::bgraToRgb(pixels), expected);
    }

    TEST(VsgUtilImagePixelsTest, copyRgbSquareToRgbaTakesAlphaFromTheNextSourceByte)
    {
        // A 2x2 RGB source, whole, into the lower right of a 3x3 RGBA destination.
        const std::vector<unsigned char> source = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        std::vector<unsigned char> destination(3 * 3 * 4, 200);
        vsgUtil::copyRgbSquareToRgba(source, 2, 0, 0, destination, 3, 1, 1, 2);

        const std::vector<unsigned char> expected = { 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, //
            200, 200, 200, 200, 1, 2, 3, 4, 4, 5, 6, 7, //
            200, 200, 200, 200, 7, 8, 9, 10, 10, 11, 12, 0 };
        EXPECT_EQ(destination, expected);
    }
}
