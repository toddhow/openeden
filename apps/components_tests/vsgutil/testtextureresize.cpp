// Tests for the runtime texture downscaler's data operations (task 0049, stage a): vsgUtil::TextureResize. Every
// image is generated here (DDS bytes in memory, arrays filled by hand); nothing needs a device, a window or a file.
// Written from the spec before the implementation.

#include <components/vsgutil/textureresize.hpp>

#include "texturetestutil.hpp"

#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>
#include <vsg/core/MipmapLayout.h>

#include <gtest/gtest.h>

#include <cmath>
#include <thread>
#include <vector>

namespace
{
    using namespace TextureTest;
    using vsgUtil::TextureResize::Kind;

    // dropMipLevels

    TEST(TextureResizeDrop, zero_levels_returns_the_same_object)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 64, 7));
        ASSERT_TRUE(source.valid());
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 0);
        EXPECT_EQ(result.get(), source.get());
        EXPECT_EQ(vsgUtil::TextureResize::dropMipLevels(*source, -1).get(), source.get());
    }

    TEST(TextureResizeDrop, a_bc1_chain_loses_its_top_levels_and_keeps_the_rest_exactly)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 64, 7));
        ASSERT_TRUE(source.valid());
        ASSERT_NE(source->getMipmapLayout(), nullptr);
        ASSERT_EQ(source->getMipmapLayout()->size(), 7u);

        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 2);
        ASSERT_TRUE(result.valid());
        EXPECT_NE(result.get(), source.get());
        EXPECT_EQ(result->properties.format, source->properties.format);
        EXPECT_EQ(result->properties.blockWidth, 4);
        EXPECT_EQ(result->properties.blockHeight, 4);
        EXPECT_EQ(result->width(), 4u); // 16 texels, in blocks
        EXPECT_EQ(result->height(), 4u);
        EXPECT_EQ(result->properties.mipLevels, 5);

        const vsg::MipmapLayout* layout = result->getMipmapLayout();
        ASSERT_NE(layout, nullptr);
        ASSERT_EQ(layout->size(), 5u);
        // Sizes 16, 8, 4, 2, 1; the offsets start again at zero.
        const uint32_t sizes[] = { 16, 8, 4, 2, 1 };
        size_t offset = 0;
        for (size_t level = 0; level < 5; ++level)
        {
            EXPECT_EQ(layout->at(level).x, sizes[level]) << level;
            EXPECT_EQ(layout->at(level).y, sizes[level]) << level;
            EXPECT_EQ(layout->at(level).w, offset) << level;
            offset += ddsLevelBytes(DdsFormat::Bc1Legacy, sizes[level], sizes[level]);
        }
        EXPECT_EQ(result->dataSize(), offset);
        EXPECT_EQ(bytesFrom(*result, 0), bytesFrom(*source, source->getMipmapLayout()->at(2).w));
    }

    TEST(TextureResizeDrop, a_bc7_dx10_chain_works_the_same_way)
    {
        auto source = readDds(makeDds(DdsFormat::Bc7Dx10, 32, 32, 6));
        ASSERT_TRUE(source.valid());
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 3);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->properties.format, source->properties.format);
        EXPECT_EQ(result->width(), 1u); // 4 texels is one block
        EXPECT_EQ(result->properties.mipLevels, 3);
        ASSERT_NE(result->getMipmapLayout(), nullptr);
        EXPECT_EQ(result->getMipmapLayout()->at(0).x, 4u);
        EXPECT_EQ(bytesFrom(*result, 0), bytesFrom(*source, source->getMipmapLayout()->at(3).w));
        EXPECT_EQ(result->dataSize(), source->dataSize() - source->getMipmapLayout()->at(3).w);
    }

    TEST(TextureResizeDrop, an_uncompressed_rgba8_chain_with_a_layout)
    {
        auto source = readDds(makeDds(DdsFormat::Rgba8Legacy, 8, 4, 4));
        ASSERT_TRUE(source.valid());
        ASSERT_EQ(source->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 1);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 2u);
        EXPECT_EQ(result->properties.mipLevels, 3);
        EXPECT_EQ(bytesFrom(*result, 0), bytesFrom(*source, source->getMipmapLayout()->at(1).w));
    }

    TEST(TextureResizeDrop, a_chain_without_a_layout_uses_the_mip_level_count)
    {
        auto source = makeChain<vsg::ubvec4Array2D>(16, 16, 5, VK_FORMAT_R8G8B8A8_UNORM);
        ASSERT_EQ(source->getMipmapLayout(), nullptr);
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 1);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 8u);
        EXPECT_EQ(result->height(), 8u);
        EXPECT_EQ(result->properties.mipLevels, 4);
        EXPECT_EQ(result->dataSize(), size_t(8 * 8 + 4 * 4 + 2 * 2 + 1) * 4);
        EXPECT_EQ(bytesFrom(*result, 0), bytesFrom(*source, size_t(16) * 16 * 4));
    }

    TEST(TextureResizeDrop, non_power_of_two_levels_follow_floor_halving)
    {
        auto source = makeChain<vsg::ubvec4Array2D>(10, 6, 4, VK_FORMAT_R8G8B8A8_UNORM); // 10x6, 5x3, 2x1, 1x1
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 1);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 5u);
        EXPECT_EQ(result->height(), 3u);
        EXPECT_EQ(result->dataSize(), size_t(5 * 3 + 2 * 1 + 1) * 4);
    }

    TEST(TextureResizeDrop, more_levels_than_the_chain_has_cannot_be_dropped)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 16, 16, 5));
        ASSERT_TRUE(source.valid());
        EXPECT_FALSE(vsgUtil::TextureResize::dropMipLevels(*source, 5).valid());
        EXPECT_FALSE(vsgUtil::TextureResize::dropMipLevels(*source, 9).valid());
        EXPECT_TRUE(vsgUtil::TextureResize::dropMipLevels(*source, 4).valid());
    }

    TEST(TextureResizeDrop, a_single_level_image_cannot_lose_a_level)
    {
        auto source = vsg::ubvec4Array2D::create(8, 8, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        EXPECT_FALSE(vsgUtil::TextureResize::dropMipLevels(*source, 1).valid());
    }

    TEST(TextureResizeDrop, a_3d_image_is_refused)
    {
        auto source = vsg::ubvec4Array3D::create(4, 4, 4, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        EXPECT_FALSE(vsgUtil::TextureResize::dropMipLevels(*source, 1).valid());
    }

    TEST(TextureResizeDrop, the_source_is_not_changed)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 32, 32, 6));
        const auto before = bytesFrom(*source, 0);
        const uint32_t width = source->width();
        auto result = vsgUtil::TextureResize::dropMipLevels(*source, 2);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(bytesFrom(*source, 0), before);
        EXPECT_EQ(source->width(), width);
        EXPECT_EQ(source->properties.mipLevels, 6);
    }

    // shrinkUncompressed

    vsg::ref_ptr<vsg::ubvec4Array2D> rgba(uint32_t w, uint32_t h, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM)
    {
        return vsg::ubvec4Array2D::create(w, h, vsg::Data::Properties{ format });
    }

    TEST(TextureResizeShrink, zero_levels_returns_the_same_object)
    {
        auto source = rgba(4, 4);
        EXPECT_EQ(vsgUtil::TextureResize::shrinkUncompressed(*source, 0, Kind::Colour, -1.f).get(), source.get());
    }

    TEST(TextureResizeShrink, a_box_average_of_two_by_two_texels)
    {
        auto source = rgba(4, 4);
        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
            {
                const uint8_t v = static_cast<uint8_t>(4 * (x + 4 * y)); // 0, 4, 8, ... 60
                source->set(x, y, vsg::ubvec4(v, static_cast<uint8_t>(255 - v), 10, 200));
            }
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        auto* out = dynamic_cast<vsg::ubvec4Array2D*>(result.get());
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->width(), 2u);
        EXPECT_EQ(out->height(), 2u);
        EXPECT_EQ(out->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(out->properties.mipLevels, 0);
        // Top left block: texels 0, 4, 16, 20 -> sum 40, (40 + 2) / 4 = 10.
        EXPECT_EQ(out->at(0, 0).r, 10);
        EXPECT_EQ(out->at(0, 0).g, 245);
        EXPECT_EQ(out->at(0, 0).b, 10);
        EXPECT_EQ(out->at(0, 0).a, 200);
        // Top right block: texels 8, 12, 24, 28 -> 72 / 4 = 18.
        EXPECT_EQ(out->at(1, 0).r, 18);
        // Bottom left: 32, 36, 48, 52 -> 168 / 4 = 42; bottom right: 40, 44, 56, 60 -> 200 / 4 = 50.
        EXPECT_EQ(out->at(0, 1).r, 42);
        EXPECT_EQ(out->at(1, 1).r, 50);
    }

    TEST(TextureResizeShrink, the_average_rounds_to_nearest)
    {
        auto source = rgba(2, 2);
        source->set(0, 0, vsg::ubvec4(0, 0, 0, 255));
        source->set(1, 0, vsg::ubvec4(0, 0, 0, 255));
        source->set(0, 1, vsg::ubvec4(1, 2, 3, 255));
        source->set(1, 1, vsg::ubvec4(1, 2, 3, 255));
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        auto* out = dynamic_cast<vsg::ubvec4Array2D*>(result.get());
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->at(0, 0).r, 1); // 2 / 4 = 0.5 -> 1
        EXPECT_EQ(out->at(0, 0).g, 1); // 4 / 4
        EXPECT_EQ(out->at(0, 0).b, 2); // 6 / 4 = 1.5 -> 2
    }

    TEST(TextureResizeShrink, a_uniform_image_stays_uniform_through_many_levels)
    {
        auto source = rgba(16, 16);
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x)
                source->set(x, y, vsg::ubvec4(10, 20, 30, 40));
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 4, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 1u);
        EXPECT_EQ(result->height(), 1u);
        auto* out = dynamic_cast<vsg::ubvec4Array2D*>(result.get());
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->at(0, 0), vsg::ubvec4(10, 20, 30, 40));
    }

    TEST(TextureResizeShrink, several_levels_give_the_expected_size)
    {
        auto source = rgba(32, 8);
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 2, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 8u);
        EXPECT_EQ(result->height(), 2u);
    }

    TEST(TextureResizeShrink, sides_stop_at_one_texel)
    {
        auto source = rgba(8, 2);
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 3, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 1u);
        EXPECT_EQ(result->height(), 1u);
    }

    TEST(TextureResizeShrink, an_odd_size_halves_with_floor)
    {
        auto source = rgba(5, 3);
        for (uint32_t y = 0; y < 3; ++y)
            for (uint32_t x = 0; x < 5; ++x)
                source->set(x, y, vsg::ubvec4(100, 100, 100, 255));
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 2u);
        EXPECT_EQ(result->height(), 1u);
        auto* out = dynamic_cast<vsg::ubvec4Array2D*>(result.get());
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->at(0, 0), vsg::ubvec4(100, 100, 100, 255));
        EXPECT_EQ(out->at(1, 0), vsg::ubvec4(100, 100, 100, 255));
    }

    TEST(TextureResizeShrink, the_source_is_not_changed)
    {
        auto source = rgba(4, 4);
        for (uint32_t i = 0; i < 16; ++i)
            source->set(i % 4, i / 4, vsg::ubvec4(static_cast<uint8_t>(i), 0, 0, 255));
        const auto before = bytesFrom(*source, 0);
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(bytesFrom(*source, 0), before);
        EXPECT_EQ(source->width(), 4u);
    }

    TEST(TextureResizeShrink, the_result_is_the_same_every_time)
    {
        auto source = rgba(16, 16);
        for (uint32_t i = 0; i < 256; ++i)
            source->set(i % 16, i / 16, vsg::ubvec4(static_cast<uint8_t>(i * 7), static_cast<uint8_t>(i * 13), 5, 255));
        auto a = vsgUtil::TextureResize::shrinkUncompressed(*source, 2, Kind::Colour, -1.f);
        auto b = vsgUtil::TextureResize::shrinkUncompressed(*source, 2, Kind::Colour, -1.f);
        ASSERT_TRUE(a.valid() && b.valid());
        EXPECT_EQ(bytesFrom(*a, 0), bytesFrom(*b, 0));
    }

    TEST(TextureResizeShrink, other_channel_layouts_keep_their_format)
    {
        {
            auto source = vsg::ubyteArray2D::create(4, 4, vsg::Data::Properties{ VK_FORMAT_R8_UNORM });
            for (uint32_t i = 0; i < 16; ++i)
                source->set(i % 4, i / 4, 100);
            auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Data, -1.f);
            ASSERT_TRUE(result.valid());
            EXPECT_EQ(result->properties.format, VK_FORMAT_R8_UNORM);
            EXPECT_EQ(result->width(), 2u);
            EXPECT_EQ(static_cast<vsg::ubyteArray2D&>(*result).at(1, 1), 100);
        }
        {
            auto source = vsg::ubvec2Array2D::create(2, 2, vsg::Data::Properties{ VK_FORMAT_R8G8_UNORM });
            source->set(0, 0, vsg::ubvec2(0, 100));
            source->set(1, 0, vsg::ubvec2(4, 100));
            source->set(0, 1, vsg::ubvec2(8, 100));
            source->set(1, 1, vsg::ubvec2(12, 100));
            auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Data, -1.f);
            ASSERT_TRUE(result.valid());
            EXPECT_EQ(result->properties.format, VK_FORMAT_R8G8_UNORM);
            EXPECT_EQ(static_cast<vsg::ubvec2Array2D&>(*result).at(0, 0), vsg::ubvec2(6, 100));
        }
        for (VkFormat format : { VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_B8G8R8_UNORM })
        {
            auto source = vsg::ubvec3Array2D::create(2, 2, vsg::Data::Properties{ format });
            source->set(0, 0, vsg::ubvec3(0, 100, 200));
            source->set(1, 0, vsg::ubvec3(4, 100, 200));
            source->set(0, 1, vsg::ubvec3(8, 100, 200));
            source->set(1, 1, vsg::ubvec3(12, 100, 200));
            auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
            ASSERT_TRUE(result.valid()) << format;
            EXPECT_EQ(result->properties.format, format);
            EXPECT_EQ(static_cast<vsg::ubvec3Array2D&>(*result).at(0, 0), vsg::ubvec3(6, 100, 200));
        }
        {
            auto source = rgba(2, 2, VK_FORMAT_B8G8R8A8_UNORM);
            auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
            ASSERT_TRUE(result.valid());
            EXPECT_EQ(result->properties.format, VK_FORMAT_B8G8R8A8_UNORM);
        }
    }

    // Normal maps: the unit vector (0.6, 0, 0.8) tilted one way and the other, as stored bytes.
    constexpr uint8_t tiltedRight = 204; // (0.6 * 0.5 + 0.5) * 255
    constexpr uint8_t tiltedLeft = 51; // (-0.6 * 0.5 + 0.5) * 255
    constexpr uint8_t flatY = 128;
    constexpr uint8_t upZ = 230; // (0.8 * 0.5 + 0.5) * 255, rounded

    TEST(TextureResizeShrink, a_normal_map_is_renormalised_after_averaging)
    {
        auto source = rgba(2, 2);
        source->set(0, 0, vsg::ubvec4(tiltedRight, flatY, upZ, 0));
        source->set(1, 0, vsg::ubvec4(tiltedLeft, flatY, upZ, 100));
        source->set(0, 1, vsg::ubvec4(tiltedRight, flatY, upZ, 200));
        source->set(1, 1, vsg::ubvec4(tiltedLeft, flatY, upZ, 100));

        // Plain averaging shortens the vector: z stays 230 while x and y fall to the middle (length about 0.8).
        auto plain = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(plain.valid());
        const vsg::ubvec4 plainTexel = static_cast<vsg::ubvec4Array2D&>(*plain).at(0, 0);
        EXPECT_EQ(plainTexel.b, upZ);

        auto normal = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Normal, -1.f);
        ASSERT_TRUE(normal.valid());
        const vsg::ubvec4 texel = static_cast<vsg::ubvec4Array2D&>(*normal).at(0, 0);
        const double x = texel.r / 255.0 * 2 - 1;
        const double y = texel.g / 255.0 * 2 - 1;
        const double z = texel.b / 255.0 * 2 - 1;
        EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 1.0, 0.02);
        EXPECT_GE(texel.b, 254);
        EXPECT_EQ(texel.a, 100); // alpha (height or specular) is averaged plainly: (0 + 100 + 200 + 100 + 2) / 4
    }

    TEST(TextureResizeShrink, a_normal_map_in_bgra_order_is_read_by_position)
    {
        auto source = rgba(2, 2, VK_FORMAT_B8G8R8A8_UNORM);
        // B, G, R, A in memory: z, y, x.
        source->set(0, 0, vsg::ubvec4(upZ, flatY, tiltedRight, 255));
        source->set(1, 0, vsg::ubvec4(upZ, flatY, tiltedLeft, 255));
        source->set(0, 1, vsg::ubvec4(upZ, flatY, tiltedRight, 255));
        source->set(1, 1, vsg::ubvec4(upZ, flatY, tiltedLeft, 255));
        auto normal = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Normal, -1.f);
        ASSERT_TRUE(normal.valid());
        const vsg::ubvec4 texel = static_cast<vsg::ubvec4Array2D&>(*normal).at(0, 0);
        EXPECT_GE(texel.r, 254); // blue (z) in the first byte
        EXPECT_NEAR(texel.g, 128, 2);
        EXPECT_NEAR(texel.b, 128, 2);
    }

    TEST(TextureResizeShrink, a_normal_map_that_already_has_unit_length_is_barely_changed)
    {
        auto source = rgba(2, 2);
        for (uint32_t i = 0; i < 4; ++i)
            source->set(i % 2, i / 2, vsg::ubvec4(tiltedRight, flatY, upZ, 255));
        auto normal = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Normal, -1.f);
        ASSERT_TRUE(normal.valid());
        const vsg::ubvec4 texel = static_cast<vsg::ubvec4Array2D&>(*normal).at(0, 0);
        EXPECT_NEAR(texel.r, tiltedRight, 2);
        EXPECT_NEAR(texel.g, flatY, 2);
        EXPECT_NEAR(texel.b, upZ, 2);
    }

    // Coverage preserving alpha: blocks with two opaque texels and two clear ones, of 16 different opacities.
    vsg::ref_ptr<vsg::ubvec4Array2D> leafLike()
    {
        auto source = rgba(8, 8);
        for (uint32_t block = 0; block < 16; ++block)
        {
            const uint32_t bx = block % 4;
            const uint32_t by = block / 4;
            const uint8_t v = static_cast<uint8_t>(16 * block + 15);
            source->set(2 * bx, 2 * by, vsg::ubvec4(255, 255, 255, v));
            source->set(2 * bx + 1, 2 * by, vsg::ubvec4(255, 255, 255, v));
            source->set(2 * bx, 2 * by + 1, vsg::ubvec4(255, 255, 255, 0));
            source->set(2 * bx + 1, 2 * by + 1, vsg::ubvec4(255, 255, 255, 0));
        }
        return source;
    }

    int texelsAtOrAbove(const vsg::Data& data, int threshold)
    {
        int count = 0;
        const auto& array = static_cast<const vsg::ubvec4Array2D&>(data);
        for (uint32_t y = 0; y < array.height(); ++y)
            for (uint32_t x = 0; x < array.width(); ++x)
                if (array.at(x, y).a >= threshold)
                    ++count;
        return count;
    }

    TEST(TextureResizeShrink, plain_averaging_thins_out_a_cutout)
    {
        auto source = leafLike();
        // 8 blocks have v >= 128: 16 of 64 texels (25 percent) pass an alpha test at 0.5.
        EXPECT_EQ(texelsAtOrAbove(*source, 128), 16);
        auto plain = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        ASSERT_TRUE(plain.valid());
        EXPECT_EQ(texelsAtOrAbove(*plain, 128), 1); // only the most opaque block survives
    }

    TEST(TextureResizeShrink, coverage_preserving_alpha_keeps_the_share_that_passes_the_test)
    {
        auto source = leafLike();
        auto kept = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, 0.5f);
        ASSERT_TRUE(kept.valid());
        // 25 percent of 16 texels: four.
        EXPECT_EQ(texelsAtOrAbove(*kept, 128), 4);
        // Colour is untouched by the alpha rescale.
        EXPECT_EQ(static_cast<vsg::ubvec4Array2D&>(*kept).at(0, 0).r, 255);
    }

    TEST(TextureResizeShrink, a_negative_cutoff_or_a_format_without_alpha_skips_the_coverage_rule)
    {
        auto source = leafLike();
        auto plain = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -1.f);
        auto again = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, -0.5f);
        ASSERT_TRUE(plain.valid() && again.valid());
        EXPECT_EQ(bytesFrom(*plain, 0), bytesFrom(*again, 0));

        auto noAlpha = vsg::ubvec3Array2D::create(2, 2, vsg::Data::Properties{ VK_FORMAT_R8G8B8_UNORM });
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*noAlpha, 1, Kind::Colour, 0.5f);
        EXPECT_TRUE(result.valid());
    }

    TEST(TextureResizeShrink, an_opaque_image_is_not_changed_by_the_coverage_rule)
    {
        auto source = rgba(4, 4);
        for (uint32_t i = 0; i < 16; ++i)
            source->set(i % 4, i / 4, vsg::ubvec4(1, 2, 3, 255));
        auto result = vsgUtil::TextureResize::shrinkUncompressed(*source, 1, Kind::Colour, 0.5f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(static_cast<vsg::ubvec4Array2D&>(*result).at(0, 0), vsg::ubvec4(1, 2, 3, 255));
    }

    TEST(TextureResizeShrink, formats_that_cannot_be_shrunk_return_null)
    {
        // Still sRGB: the shrink would average in the wrong space, so it refuses (spec, D7).
        auto srgb = rgba(4, 4, VK_FORMAT_R8G8B8A8_SRGB);
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkUncompressed(*srgb, 1, Kind::Colour, -1.f).valid());
        // 16 bit float.
        auto half = vsg::usvec4Array2D::create(4, 4, vsg::Data::Properties{ VK_FORMAT_R16G16B16A16_SFLOAT });
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkUncompressed(*half, 1, Kind::Colour, -1.f).valid());
        // Block compressed.
        auto bc1 = readDds(makeDds(DdsFormat::Bc1Legacy, 16, 16, 1));
        ASSERT_TRUE(bc1.valid());
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkUncompressed(*bc1, 1, Kind::Colour, -1.f).valid());
        // 3D.
        auto volume = vsg::ubvec4Array3D::create(4, 4, 4, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkUncompressed(*volume, 1, Kind::Colour, -1.f).valid());
    }

    // applyCap and the report

    class TextureResizeApply : public ::testing::Test
    {
    protected:
        void SetUp() override { vsgUtil::TextureResize::report().reset(); }
        void TearDown() override { vsgUtil::TextureResize::report().reset(); }
    };

    TEST_F(TextureResizeApply, no_cap_or_a_big_enough_cap_returns_the_same_object_and_counts_nothing)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 64, 7));
        EXPECT_EQ(vsgUtil::TextureResize::applyCap(source, 0, Kind::Colour, -1.f).get(), source.get());
        EXPECT_EQ(vsgUtil::TextureResize::applyCap(source, 64, Kind::Colour, -1.f).get(), source.get());
        EXPECT_EQ(vsgUtil::TextureResize::applyCap(source, 128, Kind::Colour, -1.f).get(), source.get());
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.dropped + report.shrunk + report.leftBig, 0u);
        EXPECT_EQ(report.bytesBefore, 0u);
    }

    TEST_F(TextureResizeApply, a_null_image_stays_null)
    {
        EXPECT_FALSE(vsgUtil::TextureResize::applyCap({}, 32, Kind::Colour, -1.f).valid());
    }

    TEST_F(TextureResizeApply, a_chain_is_cut_by_dropping_levels)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 64, 7));
        const size_t before = source->dataSize();
        auto result = vsgUtil::TextureResize::applyCap(source, 16, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_NE(result.get(), source.get());
        EXPECT_EQ(result->width(), 4u);
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.dropped, 1u);
        EXPECT_EQ(report.shrunk, 0u);
        EXPECT_EQ(report.leftBig, 0u);
        EXPECT_EQ(report.bytesBefore, before);
        EXPECT_EQ(report.bytesAfter, result->dataSize());
        EXPECT_LT(report.bytesAfter, report.bytesBefore);
    }

    TEST_F(TextureResizeApply, a_single_level_uncompressed_image_is_shrunk)
    {
        auto source = rgba(16, 16);
        auto result = vsgUtil::TextureResize::applyCap(source, 4, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 4u);
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.shrunk, 1u);
        EXPECT_EQ(report.dropped, 0u);
        EXPECT_EQ(report.bytesBefore, 16u * 16 * 4);
        EXPECT_EQ(report.bytesAfter, 4u * 4 * 4);
    }

    TEST_F(TextureResizeApply, a_short_uncompressed_chain_is_dropped_then_shrunk)
    {
        auto source = makeChain<vsg::ubvec4Array2D>(16, 16, 2, VK_FORMAT_R8G8B8A8_UNORM);
        auto result = vsgUtil::TextureResize::applyCap(source, 4, Kind::Colour, -1.f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 4u);
        EXPECT_EQ(vsgUtil::TextureResize::report().snapshot().shrunk, 1u);
    }

    TEST_F(TextureResizeApply, a_single_level_block_compressed_image_stays_full_size_and_is_reported)
    {
        auto source = readDds(makeDds(DdsFormat::Bc1Legacy, 16, 16, 1));
        const size_t bytes = source->dataSize();
        auto result = vsgUtil::TextureResize::applyCap(source, 4, Kind::Colour, -1.f);
        EXPECT_EQ(result.get(), source.get());
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.leftBig, 1u);
        EXPECT_EQ(report.dropped + report.shrunk, 0u);
        EXPECT_EQ(report.bytesBefore, bytes);
        EXPECT_EQ(report.bytesAfter, bytes);
    }

    TEST_F(TextureResizeApply, an_image_that_cannot_be_shrunk_is_reported_as_left_big)
    {
        auto source = rgba(16, 16, VK_FORMAT_R8G8B8A8_SRGB);
        auto result = vsgUtil::TextureResize::applyCap(source, 4, Kind::Colour, -1.f);
        EXPECT_EQ(result.get(), source.get());
        EXPECT_EQ(vsgUtil::TextureResize::report().snapshot().leftBig, 1u);
    }

    TEST_F(TextureResizeApply, the_kind_and_cutoff_reach_the_shrink)
    {
        auto source = leafLike();
        auto result = vsgUtil::TextureResize::applyCap(source, 4, Kind::Colour, 0.5f);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(texelsAtOrAbove(*result, 128), 4);
    }

    TEST(TextureResizeReport, the_accumulator_adds_up_and_resets)
    {
        vsgUtil::TextureResize::ReportAccumulator accumulator;
        accumulator.addDropped(100, 25);
        accumulator.addShrunk(64, 16);
        accumulator.addLeftBig(40);
        auto report = accumulator.snapshot();
        EXPECT_EQ(report.dropped, 1u);
        EXPECT_EQ(report.shrunk, 1u);
        EXPECT_EQ(report.leftBig, 1u);
        EXPECT_EQ(report.bytesBefore, 100u + 64u + 40u);
        EXPECT_EQ(report.bytesAfter, 25u + 16u + 40u);
        accumulator.reset();
        report = accumulator.snapshot();
        EXPECT_EQ(report.dropped + report.shrunk + report.leftBig, 0u);
        EXPECT_EQ(report.bytesBefore + report.bytesAfter, 0u);
    }

    TEST(TextureResizeReport, loader_threads_can_add_at_the_same_time)
    {
        vsgUtil::TextureResize::ReportAccumulator accumulator;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&accumulator] {
                for (int i = 0; i < 1000; ++i)
                {
                    accumulator.addDropped(10, 2);
                    accumulator.addLeftBig(1);
                }
            });
        for (auto& thread : threads)
            thread.join();
        const auto report = accumulator.snapshot();
        EXPECT_EQ(report.dropped, 4000u);
        EXPECT_EQ(report.leftBig, 4000u);
        EXPECT_EQ(report.bytesBefore, 4000u * 10 + 4000u);
        EXPECT_EQ(report.bytesAfter, 4000u * 2 + 4000u);
    }

    TEST(TextureResizeReport, describe_names_the_counts)
    {
        vsgUtil::TextureResize::Report report;
        report.dropped = 12;
        report.shrunk = 3;
        report.leftBig = 7;
        report.bytesBefore = 100 * 1024 * 1024;
        report.bytesAfter = 25 * 1024 * 1024;
        const std::string text = vsgUtil::TextureResize::describe(report);
        EXPECT_NE(text.find("12"), std::string::npos);
        EXPECT_NE(text.find("3"), std::string::npos);
        EXPECT_NE(text.find("7"), std::string::npos);
        EXPECT_NE(text.find("100"), std::string::npos);
        EXPECT_NE(text.find("25"), std::string::npos);
    }
}
