// Task 0089: CPU resizing of RGBA8 images. Generated data only.

#include <components/vsgutil/rgbaresize.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace
{
    using Image = std::vector<uint8_t>;

    Image solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        Image image(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < image.size(); i += 4)
        {
            image[i] = r;
            image[i + 1] = g;
            image[i + 2] = b;
            image[i + 3] = a;
        }
        return image;
    }

    Image ramp(uint32_t w, uint32_t h)
    {
        Image image(static_cast<size_t>(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                uint8_t* t = &image[(static_cast<size_t>(y) * w + x) * 4];
                t[0] = static_cast<uint8_t>(x * 255 / (w - 1 ? w - 1 : 1));
                t[1] = static_cast<uint8_t>(y * 255 / (h - 1 ? h - 1 : 1));
                t[2] = 7;
                t[3] = 255;
            }
        }
        return image;
    }

    Image resized(const Image& src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh)
    {
        Image dst(static_cast<size_t>(dw) * dh * 4, 0xcd);
        vsgUtil::RgbaResize::resize(src.data(), sw, sh, dst.data(), dw, dh);
        return dst;
    }
}

TEST(RgbaResize, the_same_size_is_a_copy)
{
    const Image src = ramp(13, 7);
    EXPECT_EQ(resized(src, 13, 7, 13, 7), src);
}

TEST(RgbaResize, a_solid_image_stays_solid_at_any_size)
{
    const Image src = solid(6, 10, 200, 100, 50, 25);
    for (auto [w, h] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
             { 1u, 1u }, { 3u, 5u }, { 6u, 20u }, { 12u, 3u }, { 24u, 40u }, { 5u, 11u } })
    {
        const Image out = resized(src, 6, 10, w, h);
        for (size_t i = 0; i < out.size(); i += 4)
        {
            ASSERT_EQ(out[i], 200) << w << "x" << h;
            ASSERT_EQ(out[i + 1], 100);
            ASSERT_EQ(out[i + 2], 50);
            ASSERT_EQ(out[i + 3], 25);
        }
    }
}

TEST(RgbaResize, halving_averages_each_two_by_two_group)
{
    Image src(4 * 2 * 4);
    // Left pair of columns: 0, 100 / 200, 40 in red; right pair all 255.
    const uint8_t red[8] = { 0, 100, 255, 255, 200, 40, 255, 255 };
    for (int i = 0; i < 8; ++i)
    {
        src[i * 4] = red[i];
        src[i * 4 + 1] = 0;
        src[i * 4 + 2] = 0;
        src[i * 4 + 3] = 255;
    }
    const Image out = resized(src, 4, 2, 2, 1);
    EXPECT_EQ(out[0], (0 + 100 + 200 + 40 + 2) / 4);
    EXPECT_EQ(out[4], 255);
}

TEST(RgbaResize, shrinking_by_four_averages_sixteen_texels)
{
    Image src = solid(8, 8, 0, 0, 0, 255);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            src[(static_cast<size_t>(y) * 8 + x) * 4] = static_cast<uint8_t>(16 * (y * 4 + x)); // 0..240, mean 120
        }
    }
    const Image out = resized(src, 8, 8, 2, 2);
    EXPECT_EQ(out[0], 120);
    EXPECT_EQ(out[4], 0);
}

TEST(RgbaResize, enlarging_a_ramp_keeps_it_monotonic_and_inside_the_source_range)
{
    const Image src = ramp(8, 8);
    const Image out = resized(src, 8, 8, 64, 64);
    for (uint32_t y = 0; y < 64; ++y)
    {
        for (uint32_t x = 0; x < 64; ++x)
        {
            const uint8_t* t = &out[(static_cast<size_t>(y) * 64 + x) * 4];
            ASSERT_GE(t[0], x == 0 ? 0 : t[-4]) << x << "," << y;
            ASSERT_LE(t[0], 255);
            ASSERT_EQ(t[2], 7);
        }
    }
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[63 * 4], 255);
}

TEST(RgbaResize, doubling_interpolates_between_texel_centres)
{
    // Two texels 0 and 200 wide: the doubled row is 0, 50, 150, 200 (weights 1/4 and 3/4 between the centres).
    Image src(2 * 4);
    src[0] = 0;
    src[4] = 200;
    for (int i = 0; i < 2; ++i)
    {
        src[i * 4 + 3] = 255;
    }
    const Image out = resized(src, 2, 1, 4, 1);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[4], 50);
    EXPECT_EQ(out[8], 150);
    EXPECT_EQ(out[12], 200);
}

TEST(RgbaResize, a_mixed_resize_shrinks_one_axis_and_enlarges_the_other)
{
    const Image src = solid(8, 2, 90, 91, 92, 93);
    const Image out = resized(src, 8, 2, 2, 6);
    ASSERT_EQ(out.size(), 2u * 6u * 4u);
    for (size_t i = 0; i < out.size(); i += 4)
    {
        EXPECT_EQ(out[i], 90);
        EXPECT_EQ(out[i + 3], 93);
    }
}

TEST(RgbaResize, the_result_does_not_depend_on_the_run)
{
    const Image src = ramp(37, 29);
    EXPECT_EQ(resized(src, 37, 29, 64, 16), resized(src, 37, 29, 64, 16));
}
