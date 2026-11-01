// Tests for vsgUtil::removeSrgb with block-compressed formats and for a generated DX10 DDS (task 0044, D7). Written
// from the spec before the implementation. The DDS bytes are made here; no file is read.

#include <components/vsgutil/readimage.hpp>

#include <vsg/core/Data.h>
#include <vsg/io/Options.h>
#include <vsg/io/mem_stream.h>
#include <vsgXchange/images.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    vsg::ref_ptr<vsg::Data> withFormat(VkFormat format)
    {
        return vsg::ubvec4Array2D::create(4, 4, vsg::Data::Properties{ format });
    }

    TEST(VsgUtilRemoveSrgbTest, block_compressed_srgb_formats_become_their_unorm_twins)
    {
        const std::pair<VkFormat, VkFormat> pairs[] = {
            { VK_FORMAT_BC1_RGB_SRGB_BLOCK, VK_FORMAT_BC1_RGB_UNORM_BLOCK },
            { VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK },
            { VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK },
            { VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK },
            { VK_FORMAT_BC7_SRGB_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK },
        };
        for (const auto& [from, to] : pairs)
        {
            auto data = withFormat(from);
            vsgUtil::removeSrgb(*data);
            EXPECT_EQ(data->properties.format, to) << from;
        }
    }

    TEST(VsgUtilRemoveSrgbTest, other_formats_are_left_alone)
    {
        for (VkFormat f : { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK,
                 VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM })
        {
            auto data = withFormat(f);
            vsgUtil::removeSrgb(*data);
            EXPECT_EQ(data->properties.format, f);
        }
    }

    TEST(VsgUtilRemoveSrgbTest, the_eight_bit_cases_still_work)
    {
        auto data = withFormat(VK_FORMAT_R8G8B8A8_SRGB);
        vsgUtil::removeSrgb(*data);
        EXPECT_EQ(data->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
    }

    // A 4 by 4 DDS with a DX10 header: one BC1 block (8 bytes), one mip level.
    std::vector<uint8_t> makeDx10Bc1(uint32_t dxgiFormat)
    {
        std::vector<uint8_t> bytes(4 + 124 + 20 + 8, 0);
        const auto put = [&](size_t at, uint32_t v) { std::memcpy(&bytes[at], &v, 4); };
        std::memcpy(&bytes[0], "DDS ", 4);
        put(4, 124); // header size
        put(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000); // caps, height, width, pixel format, linear size
        put(12, 4); // height
        put(16, 4); // width
        put(20, 8); // linear size
        put(28, 1); // mip count
        put(76, 32); // pixel format size
        put(80, 0x4); // FOURCC
        std::memcpy(&bytes[84], "DX10", 4);
        put(108, 0x1000); // caps
        put(128, dxgiFormat);
        put(132, 3); // Texture2D
        put(136, 0); // misc
        put(140, 1); // array size
        put(144, 0); // misc2
        return bytes;
    }

    vsg::ref_ptr<vsg::Data> readGenerated(uint32_t dxgiFormat)
    {
        const auto bytes = makeDx10Bc1(dxgiFormat);
        vsg::mem_stream in(bytes.data(), bytes.size());
        auto options = vsg::Options::create();
        options->extensionHint = ".dds";
        vsgXchange::dds reader;
        auto object = reader.read(in, options);
        return vsg::ref_ptr<vsg::Data>(object ? object.cast<vsg::Data>() : nullptr);
    }

    TEST(VsgUtilRemoveSrgbTest, a_generated_dx10_bc1_srgb_file_reads_as_unorm_after_the_strip)
    {
        auto data = readGenerated(72); // DXGI_FORMAT_BC1_UNORM_SRGB
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->properties.format, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        vsgUtil::removeSrgb(*data);
        EXPECT_EQ(data->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    }

    TEST(VsgUtilRemoveSrgbTest, a_generated_dx10_bc1_unorm_file_stays_unorm)
    {
        auto data = readGenerated(71); // DXGI_FORMAT_BC1_UNORM
        ASSERT_TRUE(data.valid());
        vsgUtil::removeSrgb(*data);
        EXPECT_EQ(data->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    }
}
