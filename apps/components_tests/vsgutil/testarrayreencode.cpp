// Task 0089: re-encoding layers that do not fit an array (BC7, other sizes, uncompressed layers in a BC1 array).
// Generated data only.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/blockconvert.hpp>

#include <vsg/core/Array2D.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    using vsgUtil::ArrayConvertOptions;
    using vsgUtil::ArrayConvertStats;
    constexpr uint8_t sGreyByte = 0x80;

    ArrayConvertOptions reencode(unsigned threads = 0)
    {
        ArrayConvertOptions options;
        options.reencode = true;
        options.threads = threads;
        return options;
    }

    // A BC1 image of width x height texels with `levels` levels, every block the same 8 bytes.
    vsg::ref_ptr<vsg::Data> bc1Of(
        uint32_t width, uint32_t height, uint32_t levels, const std::array<uint8_t, 8>& block, VkFormat format)
    {
        size_t blocks = 0;
        for (uint32_t l = 0; l < levels; ++l)
        {
            blocks += static_cast<size_t>(std::max(1u, (width >> l) / 4)) * std::max(1u, (height >> l) / 4);
        }
        vsg::Data::Properties properties(format);
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        properties.mipLevels = static_cast<uint8_t>(levels);
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        auto* data = new vsg::block64[blocks];
        auto* bytes = reinterpret_cast<uint8_t*>(data);
        for (size_t b = 0; b < blocks; ++b)
        {
            std::memcpy(bytes + b * 8, block.data(), 8);
        }
        return vsg::block64Array2D::create(width / 4, height / 4, data, properties);
    }

    // A colour block of two equal 565 endpoints (a solid colour, index 0 everywhere).
    std::array<uint8_t, 8> solidBc1(uint16_t colour565)
    {
        return { static_cast<uint8_t>(colour565 & 255), static_cast<uint8_t>(colour565 >> 8),
            static_cast<uint8_t>(colour565 & 255), static_cast<uint8_t>(colour565 >> 8), 0, 0, 0, 0 };
    }

    vsg::ref_ptr<vsg::Data> rgbaSolid(
        uint32_t width, uint32_t height, vsg::ubvec4 colour, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM)
    {
        vsg::Data::Properties properties(format);
        auto image = vsg::ubvec4Array2D::create(width, height, colour, properties);
        return image;
    }

    // A mode 6 BC7 block of one colour: both endpoints the same, every index 0. Mode 6 has one p bit for all four
    // channels of an endpoint, so the colour's channels must all have the same lowest bit (the tests use even ones).
    std::array<uint8_t, 16> solidBc7(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        std::array<uint8_t, 16> block{};
        int position = 0;
        auto put = [&](uint32_t value, int bits) {
            for (int i = 0; i < bits; ++i, ++position)
            {
                if ((value >> i) & 1)
                {
                    block[position / 8] = static_cast<uint8_t>(block[position / 8] | (1 << (position % 8)));
                }
            }
        };
        put(1 << 6, 7);
        for (uint8_t c : { r, g, b, a })
        {
            put(c >> 1, 7); // 7 bit endpoint, the p bit below supplies the lowest bit
            put(c >> 1, 7);
        }
        put(r & 1, 1);
        put(r & 1, 1);
        return block;
    }

    vsg::ref_ptr<vsg::Data> bc7Of(uint32_t width, uint32_t height, const std::array<uint8_t, 16>& block)
    {
        vsg::Data::Properties properties(VK_FORMAT_BC7_UNORM_BLOCK);
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        properties.mipLevels = 1;
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        const size_t blocks = static_cast<size_t>(width / 4) * (height / 4);
        auto* data = new vsg::block128[blocks];
        for (size_t b = 0; b < blocks; ++b)
        {
            std::memcpy(reinterpret_cast<uint8_t*>(data) + b * 16, block.data(), 16);
        }
        return vsg::block128Array2D::create(width / 4, height / 4, data, properties);
    }

    const uint8_t* bytesOf(const vsg::Data& data)
    {
        return reinterpret_cast<const uint8_t*>(data.dataPointer());
    }

    // The levels of a BC1 array of `layers` layers: offsets as convertToArrayTexture lays them out.
    size_t bc1Offset(uint32_t width, uint32_t height, uint32_t layers, uint32_t level, uint32_t layer)
    {
        size_t offset = 0;
        for (uint32_t l = 0; l < level; ++l)
        {
            offset
                += static_cast<size_t>(std::max(1u, (width >> l) / 4)) * std::max(1u, (height >> l) / 4) * 8 * layers;
        }
        const size_t levelBytes
            = static_cast<size_t>(std::max(1u, (width >> level) / 4)) * std::max(1u, (height >> level) / 4) * 8;
        return offset + levelBytes * layer;
    }

    // Decodes a single BC1 block (the first texel) the way the engine does.
    vsg::ubvec4 firstTexelOfBc1(const uint8_t* block)
    {
        vsg::Data::Properties properties(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        auto image = vsg::block64Array2D::create(1, 1, properties);
        std::memcpy(image->dataPointer(), block, 8);
        const auto texels = vsgUtil::detail::decodeBlockCompressed(*image);
        const uint32_t t = texels[0];
        return vsg::ubvec4(t & 255, (t >> 8) & 255, (t >> 16) & 255, t >> 24);
    }

    void expectLayerColour(const vsg::Data& array, uint32_t width, uint32_t height, uint32_t layers, uint32_t levels,
        uint32_t layer, int r, int g, int b, int tolerance = 6)
    {
        for (uint32_t level = 0; level < levels; ++level)
        {
            const size_t blocksInLevel
                = static_cast<size_t>(std::max(1u, (width >> level) / 4)) * std::max(1u, (height >> level) / 4);
            const uint8_t* base = bytesOf(array) + bc1Offset(width, height, layers, level, layer);
            for (size_t block = 0; block < blocksInLevel; ++block)
            {
                const auto texel = firstTexelOfBc1(base + block * 8);
                ASSERT_LE(std::abs(int(texel.r) - r), tolerance) << "layer " << layer << " level " << level;
                ASSERT_LE(std::abs(int(texel.g) - g), tolerance) << "layer " << layer << " level " << level;
                ASSERT_LE(std::abs(int(texel.b) - b), tolerance) << "layer " << layer << " level " << level;
            }
        }
    }

    vsg::DataList bc1Reference(int count, uint32_t size = 16, uint32_t levels = 3)
    {
        vsg::DataList list;
        for (int i = 0; i < count; ++i)
        {
            list.push_back(bc1Of(size, size, levels, solidBc1(0xf800), VK_FORMAT_BC1_RGBA_UNORM_BLOCK));
        }
        return list;
    }
}

TEST(ArrayReencode, without_the_option_a_mismatched_layer_is_filled_as_before)
{
    auto layers = bc1Reference(2);
    layers.push_back(rgbaSolid(16, 16, vsg::ubvec4(10, 200, 30, 255)));
    const auto result = vsgUtil::convertToArrayTexture(layers);
    const uint8_t* layer2 = bytesOf(*result) + bc1Offset(16, 16, 3, 0, 2);
    for (size_t k = 0; k < 128; ++k)
    {
        EXPECT_EQ(layer2[k], sGreyByte);
    }
}

TEST(ArrayReencode, an_uncompressed_layer_is_encoded_with_every_mip_level_of_the_array)
{
    auto layers = bc1Reference(2);
    layers.push_back(rgbaSolid(16, 16, vsg::ubvec4(200, 100, 50, 255)));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    expectLayerColour(*result, 16, 16, 3, 3, 2, 200, 100, 50);
    EXPECT_EQ(stats.encoded, 1u);
    EXPECT_EQ(stats.copied, 2u);
    EXPECT_EQ(stats.grey, 0u);
}

TEST(ArrayReencode, an_uncompressed_layer_of_another_size_is_resized_then_encoded)
{
    auto layers = bc1Reference(2);
    layers.push_back(rgbaSolid(64, 64, vsg::ubvec4(40, 220, 90, 255))); // larger
    layers.push_back(rgbaSolid(4, 4, vsg::ubvec4(250, 10, 120, 255))); // smaller
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode());
    expectLayerColour(*result, 16, 16, 4, 3, 2, 40, 220, 90);
    expectLayerColour(*result, 16, 16, 4, 3, 3, 250, 10, 120);
}

TEST(ArrayReencode, a_bgra_layer_keeps_its_colours_in_the_right_channels)
{
    auto layers = bc1Reference(2);
    // B8G8R8A8: the first byte is blue.
    layers.push_back(rgbaSolid(16, 16, vsg::ubvec4(50, 100, 200, 255), VK_FORMAT_B8G8R8A8_UNORM));
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode());
    expectLayerColour(*result, 16, 16, 3, 3, 2, 200, 100, 50);
}

TEST(ArrayReencode, a_bc1_layer_of_another_size_is_decoded_resized_and_encoded)
{
    auto layers = bc1Reference(2);
    layers.push_back(bc1Of(64, 64, 1, solidBc1(0x07e0), VK_FORMAT_BC1_RGBA_UNORM_BLOCK)); // pure green
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode());
    expectLayerColour(*result, 16, 16, 3, 3, 2, 0, 255, 0);
}

TEST(ArrayReencode, a_bc3_layer_of_another_size_is_re_encoded_not_transcoded)
{
    auto layers = bc1Reference(2);
    // BC3 block: 8 bytes of alpha, then a solid blue colour block.
    vsg::Data::Properties properties(VK_FORMAT_BC3_UNORM_BLOCK);
    properties.blockWidth = 4;
    properties.blockHeight = 4;
    properties.mipLevels = 1;
    properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
    auto* data = new vsg::block128[64]; // 32x32 texels
    for (int b = 0; b < 64; ++b)
    {
        uint8_t* block = reinterpret_cast<uint8_t*>(data) + b * 16;
        std::memset(block, 0xff, 8);
        const auto colour = solidBc1(0x001f);
        std::memcpy(block + 8, colour.data(), 8);
    }
    layers.push_back(vsg::block128Array2D::create(8, 8, data, properties));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    expectLayerColour(*result, 16, 16, 3, 3, 2, 0, 0, 255);
    EXPECT_EQ(stats.encoded, 1u);
    EXPECT_EQ(stats.transcoded, 0u);
}

TEST(ArrayReencode, a_bc3_layer_of_the_arrays_size_is_still_transcoded_losslessly)
{
    auto layers = bc1Reference(2);
    vsg::Data::Properties properties(VK_FORMAT_BC3_UNORM_BLOCK);
    properties.blockWidth = 4;
    properties.blockHeight = 4;
    properties.mipLevels = 3;
    properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
    const size_t blocks = 16 + 4 + 1;
    auto* data = new vsg::block128[blocks];
    for (size_t b = 0; b < blocks; ++b)
    {
        uint8_t* block = reinterpret_cast<uint8_t*>(data) + b * 16;
        std::memset(block, 0xff, 8);
        const auto colour = solidBc1(0x07e0);
        std::memcpy(block + 8, colour.data(), 8);
    }
    layers.push_back(vsg::block128Array2D::create(4, 4, data, properties));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    expectLayerColour(*result, 16, 16, 3, 3, 2, 0, 255, 0, 1);
    EXPECT_EQ(stats.transcoded, 1u);
    EXPECT_EQ(stats.encoded, 0u);
}

TEST(ArrayReencode, a_bc7_layer_joins_a_bc1_array)
{
    auto layers = bc1Reference(2);
    layers.push_back(bc7Of(32, 32, solidBc7(240, 60, 20, 255)));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    expectLayerColour(*result, 16, 16, 3, 3, 2, 240, 60, 20);
    EXPECT_EQ(stats.encoded, 1u);
}

TEST(ArrayReencode, a_bc7_layer_joins_an_rgba_array_exactly)
{
    vsg::DataList layers;
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(bc7Of(4, 4, solidBc7(240, 62, 20, 254)));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    const uint8_t* layer = bytesOf(*result) + 2 * 8 * 8 * 4;
    for (int i = 0; i < 64; ++i)
    {
        EXPECT_EQ(layer[i * 4], 240);
        EXPECT_EQ(layer[i * 4 + 1], 62);
        EXPECT_EQ(layer[i * 4 + 2], 20);
        EXPECT_EQ(layer[i * 4 + 3], 254);
    }
    EXPECT_EQ(stats.decoded, 1u);
}

TEST(ArrayReencode, an_rgba_array_resizes_an_uncompressed_layer_and_counts_it)
{
    vsg::DataList layers;
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(rgbaSolid(32, 32, vsg::ubvec4(100, 110, 120, 130)));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    const uint8_t* layer = bytesOf(*result) + 2 * 8 * 8 * 4;
    for (int i = 0; i < 64; ++i)
    {
        EXPECT_EQ(layer[i * 4], 100);
        EXPECT_EQ(layer[i * 4 + 3], 130);
    }
    EXPECT_EQ(stats.resampled, 1u);
}

TEST(ArrayReencode, a_layer_of_an_unknown_format_is_a_grey_block_and_counted)
{
    auto layers = bc1Reference(2);
    vsg::Data::Properties properties(VK_FORMAT_R32_SFLOAT);
    layers.push_back(vsg::floatArray2D::create(16, 16, 1.0f, properties));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    const uint8_t* layer = bytesOf(*result) + bc1Offset(16, 16, 3, 0, 2);
    const auto texel = firstTexelOfBc1(layer);
    EXPECT_NEAR(texel.r, 132, 2); // a real grey, not the red a 0x80 byte fill is
    EXPECT_NEAR(texel.g, 130, 2);
    EXPECT_NEAR(texel.b, 132, 2);
    EXPECT_EQ(stats.grey, 1u);
}

TEST(ArrayReencode, layers_that_already_fit_give_the_same_bytes_with_and_without_the_option)
{
    auto make = [] {
        auto layers = bc1Reference(3);
        vsg::Data::Properties properties(VK_FORMAT_BC3_UNORM_BLOCK);
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        properties.mipLevels = 3;
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        auto* data = new vsg::block128[21];
        for (int b = 0; b < 21; ++b)
        {
            uint8_t* block = reinterpret_cast<uint8_t*>(data) + b * 16;
            std::memset(block, 0x55, 8);
            const auto colour = solidBc1(static_cast<uint16_t>(0x1234 + b));
            std::memcpy(block + 8, colour.data(), 8);
        }
        layers.push_back(vsg::block128Array2D::create(4, 4, data, properties));
        return layers;
    };
    const auto plain = vsgUtil::convertToArrayTexture(make());
    ArrayConvertStats stats;
    const auto re = vsgUtil::convertToArrayTexture(make(), reencode(), &stats);
    ASSERT_EQ(plain->dataSize(), re->dataSize());
    EXPECT_EQ(std::memcmp(plain->dataPointer(), re->dataPointer(), plain->dataSize()), 0);
    EXPECT_EQ(stats.copied, 3u);
    EXPECT_EQ(stats.transcoded, 1u);
    EXPECT_EQ(stats.encoded + stats.grey + stats.decoded + stats.resampled, 0u);
}

TEST(ArrayReencode, the_result_does_not_depend_on_the_thread_count)
{
    auto make = [] {
        auto layers = bc1Reference(2, 32, 4);
        for (int i = 0; i < 6; ++i)
        {
            layers.push_back(rgbaSolid(64, 64, vsg::ubvec4(static_cast<uint8_t>(30 * i), 200, 17, 255)));
        }
        return layers;
    };
    const auto one = vsgUtil::convertToArrayTexture(make(), reencode(1));
    const auto four = vsgUtil::convertToArrayTexture(make(), reencode(4));
    ASSERT_EQ(one->dataSize(), four->dataSize());
    EXPECT_EQ(std::memcmp(one->dataPointer(), four->dataPointer(), one->dataSize()), 0);
}

TEST(ArrayLayerConvertibleReencode, the_policy_admits_what_the_array_can_now_take)
{
    using vsgUtil::LayerShape;
    LayerShape bc1{ VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true, 0, 2048, 2048 };
    LayerShape rgba{ VK_FORMAT_R8G8B8A8_UNORM, false, 4, 2048, 2048 };
    auto block = [](VkFormat f, uint32_t size) { return LayerShape{ f, true, 0, size, size }; };
    auto plain = [](VkFormat f, uint32_t bytes, uint32_t size) { return LayerShape{ f, false, bytes, size, size }; };
    const ArrayConvertOptions on = reencode();
    const ArrayConvertOptions off;

    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 2048), bc1, off));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 2048), bc1, on));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 4096), rgba, on));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 4096), rgba, off));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 256), bc1, on));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 256), bc1, off));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC3_UNORM_BLOCK, 4096), bc1, on));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(plain(VK_FORMAT_R8G8B8A8_UNORM, 4, 4096), bc1, on));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(plain(VK_FORMAT_B8G8R8A8_UNORM, 4, 2048), bc1, on));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(plain(VK_FORMAT_B8G8R8A8_UNORM, 4, 2048), rgba, on));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(plain(VK_FORMAT_R32_SFLOAT, 4, 2048), bc1, on));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC5_UNORM_BLOCK, 2048), bc1, on));
}

// Data arrays (normal, material, height) must not get alpha-carrying layers encoded into a BC1 array.
TEST(ArrayReencode, a_data_array_encodes_only_bc1_layers_into_a_bc1_array)
{
    ArrayConvertOptions data = reencode();
    data.encodeToBc1 = false;

    auto layers = bc1Reference(2);
    layers.push_back(bc1Of(64, 64, 1, solidBc1(0x07e0), VK_FORMAT_BC1_RGBA_UNORM_BLOCK)); // BC1 of another size
    layers.push_back(rgbaSolid(16, 16, vsg::ubvec4(200, 100, 50, 255))); // would lose its alpha
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, data, &stats);
    expectLayerColour(*result, 16, 16, 4, 3, 2, 0, 255, 0);
    EXPECT_EQ(stats.encoded, 1u);
    EXPECT_EQ(stats.grey, 1u);

    using vsgUtil::LayerShape;
    const LayerShape bc1{ VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true, 0, 256, 256 };
    EXPECT_TRUE(
        vsgUtil::arrayLayerConvertible(LayerShape{ VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true, 0, 2048, 2048 }, bc1, data));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(LayerShape{ VK_FORMAT_R8G8B8A8_UNORM, false, 4, 256, 256 }, bc1, data));
    EXPECT_FALSE(
        vsgUtil::arrayLayerConvertible(LayerShape{ VK_FORMAT_BC7_UNORM_BLOCK, true, 0, 2048, 2048 }, bc1, data));
}

TEST(ArrayReencode, a_data_array_in_rgba_still_takes_every_decodable_layer)
{
    ArrayConvertOptions data = reencode();
    data.encodeToBc1 = false;
    vsg::DataList layers;
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(rgbaSolid(8, 8, vsg::ubvec4(1, 2, 3, 4)));
    layers.push_back(bc7Of(16, 16, solidBc7(240, 62, 20, 254)));
    ArrayConvertStats stats;
    const auto result = vsgUtil::convertToArrayTexture(layers, data, &stats);
    EXPECT_EQ(stats.decoded, 1u);
    EXPECT_EQ(bytesOf(*result)[2 * 8 * 8 * 4 + 3], 254); // alpha kept
}
