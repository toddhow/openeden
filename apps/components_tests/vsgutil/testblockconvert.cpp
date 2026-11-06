// Task 0089: the BC3 to BC1 transcode and the grey fill of block compressed array layers. Generated data only.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/blockconvert.hpp>

#include <vsg/core/Array2D.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    namespace BC = vsgUtil::BlockConvert;

    // A small deterministic generator; the tests must not depend on a seed from the platform.
    struct Lcg
    {
        uint32_t state = 12345;
        uint8_t next()
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<uint8_t>(state >> 24);
        }
    };

    vsg::ref_ptr<vsg::Data> imageOfBlocks(VkFormat format, const std::vector<uint8_t>& bytes)
    {
        vsg::Data::Properties properties;
        properties.format = format;
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        if (bytes.size() == 8)
        {
            auto image = vsg::block64Array2D::create(1, 1, properties);
            std::memcpy(image->dataPointer(), bytes.data(), bytes.size());
            return image;
        }
        auto image = vsg::block128Array2D::create(1, 1, properties);
        std::memcpy(image->dataPointer(), bytes.data(), bytes.size());
        return image;
    }

    std::vector<uint32_t> decode(VkFormat format, const std::vector<uint8_t>& bytes)
    {
        const auto image = imageOfBlocks(format, bytes);
        return vsgUtil::detail::decodeBlockCompressed(*image);
    }

    std::vector<uint8_t> colourBlock(uint16_t c0, uint16_t c1, uint8_t indexSeed)
    {
        Lcg random;
        random.state = indexSeed * 7919u + 1;
        std::vector<uint8_t> block{ static_cast<uint8_t>(c0 & 255), static_cast<uint8_t>(c0 >> 8),
            static_cast<uint8_t>(c1 & 255), static_cast<uint8_t>(c1 >> 8), 0, 0, 0, 0 };
        for (int i = 4; i < 8; ++i)
        {
            block[i] = random.next();
        }
        return block;
    }

    std::vector<uint8_t> bc3Of(const std::vector<uint8_t>& colour, uint8_t alphaSeed)
    {
        std::vector<uint8_t> block(16);
        Lcg random;
        random.state = alphaSeed + 99u;
        for (int i = 0; i < 8; ++i)
        {
            block[i] = random.next();
        }
        std::copy(colour.begin(), colour.end(), block.begin() + 8);
        return block;
    }

    TEST(BlockConvert, a_block_with_the_first_endpoint_greater_is_copied_as_it_is)
    {
        const auto colour = colourBlock(0xf81f, 0x07e0, 3);
        const auto bc3 = bc3Of(colour, 1);
        std::vector<uint8_t> bc1(8);
        BC::bc3BlockToBc1(bc3.data(), bc1.data());
        EXPECT_EQ(bc1, colour);
    }

    TEST(BlockConvert, a_block_with_the_endpoints_the_other_way_round_has_them_swapped_and_the_indices_mapped)
    {
        const auto colour = colourBlock(0x07e0, 0xf81f, 5);
        const auto bc3 = bc3Of(colour, 2);
        std::vector<uint8_t> bc1(8);
        BC::bc3BlockToBc1(bc3.data(), bc1.data());
        EXPECT_EQ(bc1[0], colour[2]);
        EXPECT_EQ(bc1[1], colour[3]);
        EXPECT_EQ(bc1[2], colour[0]);
        EXPECT_EQ(bc1[3], colour[1]);
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_EQ(bc1[4 + row], colour[4 + row] ^ 0x55) << row;
        }
    }

    TEST(BlockConvert, equal_endpoints_become_one_colour_with_every_index_zero)
    {
        const auto colour = colourBlock(0x1234, 0x1234, 9);
        const auto bc3 = bc3Of(colour, 3);
        std::vector<uint8_t> bc1(8);
        BC::bc3BlockToBc1(bc3.data(), bc1.data());
        EXPECT_EQ(bc1[0], 0x34);
        EXPECT_EQ(bc1[1], 0x12);
        EXPECT_EQ(bc1[2], 0x34);
        EXPECT_EQ(bc1[3], 0x12);
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_EQ(bc1[4 + row], 0) << row;
        }
    }

    // The point of the transcode: every texel's colour is what the BC3 block decodes to.
    TEST(BlockConvert, the_decoded_colour_of_every_texel_is_unchanged_over_many_blocks)
    {
        Lcg random;
        for (int n = 0; n < 3000; ++n)
        {
            std::vector<uint8_t> bc3(16);
            for (auto& byte : bc3)
            {
                byte = random.next();
            }
            if (n % 7 == 0)
            {
                bc3[10] = bc3[8];
                bc3[11] = bc3[9];
            }
            std::vector<uint8_t> bc1(8);
            BC::bc3BlockToBc1(bc3.data(), bc1.data());
            const auto before = decode(VK_FORMAT_BC3_UNORM_BLOCK, bc3);
            const auto after = decode(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, bc1);
            ASSERT_EQ(before.size(), 16u);
            ASSERT_EQ(after.size(), 16u);
            for (size_t t = 0; t < 16; ++t)
            {
                ASSERT_EQ(before[t] & 0x00ffffffu, after[t] & 0x00ffffffu) << "block " << n << " texel " << t;
            }
        }
    }

    TEST(BlockConvert, many_blocks_are_converted_one_by_one)
    {
        std::vector<uint8_t> bc3;
        for (uint8_t n = 0; n < 5; ++n)
        {
            const auto block = bc3Of(colourBlock(static_cast<uint16_t>(1000 * n + 7), 20000, n), n);
            bc3.insert(bc3.end(), block.begin(), block.end());
        }
        std::vector<uint8_t> bc1(5 * 8);
        BC::bc3ToBc1(bc3.data(), bc1.data(), 5);
        for (size_t n = 0; n < 5; ++n)
        {
            std::vector<uint8_t> one(8);
            BC::bc3BlockToBc1(&bc3[n * 16], one.data());
            EXPECT_TRUE(std::equal(one.begin(), one.end(), bc1.begin() + n * 8)) << n;
        }
    }

    TEST(BlockConvert, the_grey_fill_of_bc1_decodes_to_grey_not_red)
    {
        std::vector<uint8_t> bytes(8 * 3);
        EXPECT_TRUE(BC::fillGrey(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, bytes.data(), bytes.size()));
        for (size_t n = 0; n < 3; ++n)
        {
            const auto texels = decode(
                VK_FORMAT_BC1_RGBA_UNORM_BLOCK, std::vector<uint8_t>(bytes.begin() + n * 8, bytes.begin() + n * 8 + 8));
            for (const uint32_t texel : texels)
            {
                EXPECT_NEAR(static_cast<int>(texel & 255), 128, 6);
                EXPECT_NEAR(static_cast<int>((texel >> 8) & 255), 128, 6);
                EXPECT_NEAR(static_cast<int>((texel >> 16) & 255), 128, 6);
                EXPECT_EQ(texel >> 24, 255u);
            }
        }
    }

    TEST(BlockConvert, the_old_byte_fill_of_bc1_was_red)
    {
        const std::vector<uint8_t> bytes(8, 0x80);
        const auto texels = decode(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, bytes);
        ASSERT_EQ(texels.size(), 16u);
        EXPECT_GT(static_cast<int>(texels[0] & 255), 100);
        EXPECT_LT(static_cast<int>((texels[0] >> 8) & 255), 40);
        EXPECT_LT(static_cast<int>((texels[0] >> 16) & 255), 40);
    }

    TEST(BlockConvert, the_grey_fill_of_bc2_and_bc3_is_opaque_grey)
    {
        for (const VkFormat format : { VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK })
        {
            std::vector<uint8_t> bytes(16);
            EXPECT_TRUE(BC::fillGrey(format, bytes.data(), bytes.size())) << format;
            const auto texels = decode(format, bytes);
            ASSERT_EQ(texels.size(), 16u) << format;
            for (const uint32_t texel : texels)
            {
                EXPECT_NEAR(static_cast<int>(texel & 255), 128, 6) << format;
                EXPECT_NEAR(static_cast<int>((texel >> 8) & 255), 128, 6) << format;
                EXPECT_EQ(texel >> 24, 255u) << format;
            }
        }
    }

    TEST(BlockConvert, an_unknown_format_gets_the_old_byte_fill)
    {
        std::vector<uint8_t> bytes(32, 0);
        EXPECT_FALSE(BC::fillGrey(VK_FORMAT_BC7_UNORM_BLOCK, bytes.data(), bytes.size()));
        for (const uint8_t byte : bytes)
        {
            EXPECT_EQ(byte, 0x80);
        }
    }

    // The array: two BC1 layers decide the format; a BC3 layer of the same size joins with its colours.
    TEST(BlockConvertArray, a_bc3_layer_joins_a_bc1_array)
    {
        vsg::Data::Properties bc1Properties;
        bc1Properties.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        bc1Properties.blockWidth = 4;
        bc1Properties.blockHeight = 4;
        bc1Properties.mipLevels = 1;
        auto makeBc1 = [&] {
            auto image = vsg::block64Array2D::create(1, 1, bc1Properties);
            const auto colour = colourBlock(0xf81f, 0x07e0, 1);
            std::memcpy(image->dataPointer(), colour.data(), 8);
            return image;
        };
        vsg::Data::Properties bc3Properties = bc1Properties;
        bc3Properties.format = VK_FORMAT_BC3_UNORM_BLOCK;
        auto bc3Image = vsg::block128Array2D::create(1, 1, bc3Properties);
        const auto bc3 = bc3Of(colourBlock(0x07e0, 0xf81f, 6), 4);
        std::memcpy(bc3Image->dataPointer(), bc3.data(), 16);
        vsg::Data::Properties bc7Properties = bc3Properties;
        bc7Properties.format = VK_FORMAT_BC7_UNORM_BLOCK;
        auto bc7Image = vsg::block128Array2D::create(1, 1, bc7Properties);

        vsg::DataList layers{ makeBc1(), bc3Image, makeBc1(), bc7Image };
        const auto array = vsgUtil::convertToArrayTexture(layers);
        ASSERT_TRUE(array.valid());
        const auto* bytes = reinterpret_cast<const uint8_t*>(array->dataPointer());
        ASSERT_GE(array->dataSize(), 4u * 8u);
        const std::vector<uint8_t> layer1(bytes + 8, bytes + 16);
        const auto expected = decode(VK_FORMAT_BC3_UNORM_BLOCK, bc3);
        const auto got = decode(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, layer1);
        ASSERT_EQ(got.size(), 16u);
        for (size_t t = 0; t < 16; ++t)
        {
            EXPECT_EQ(got[t] & 0x00ffffffu, expected[t] & 0x00ffffffu) << t;
        }
    }

    // Disabled until toddhow approves replacing the 0x80 byte fill in convertToArrayTexture (it needs a change to the
    // existing helper expectBc1LayerGrey): a layer that cannot be converted is a grey block, not a red one.
    TEST(BlockConvertArray, DISABLED_a_bc7_layer_is_a_grey_block_not_red)
    {
        vsg::Data::Properties bc1Properties;
        bc1Properties.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        bc1Properties.blockWidth = 4;
        bc1Properties.blockHeight = 4;
        bc1Properties.mipLevels = 1;
        auto makeBc1 = [&] {
            auto image = vsg::block64Array2D::create(1, 1, bc1Properties);
            const auto colour = colourBlock(0xf81f, 0x07e0, 1);
            std::memcpy(image->dataPointer(), colour.data(), 8);
            return image;
        };
        vsg::Data::Properties bc7Properties = bc1Properties;
        bc7Properties.format = VK_FORMAT_BC7_UNORM_BLOCK;
        auto bc7Image = vsg::block128Array2D::create(1, 1, bc7Properties);
        vsg::DataList layers{ makeBc1(), makeBc1(), bc7Image };
        const auto array = vsgUtil::convertToArrayTexture(layers);
        ASSERT_TRUE(array.valid());
        const auto* bytes = reinterpret_cast<const uint8_t*>(array->dataPointer());
        const std::vector<uint8_t> layer2(bytes + 16, bytes + 24);
        const auto grey = decode(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, layer2);
        ASSERT_EQ(grey.size(), 16u);
        EXPECT_NEAR(static_cast<int>(grey[0] & 255), 128, 6);
        EXPECT_NEAR(static_cast<int>((grey[0] >> 8) & 255), 128, 6);
    }
}
