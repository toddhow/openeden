// Task 0089: BC4 layers of another size join a BC4 array (the terrain height array). Generated data only.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/bc4.hpp>
#include <components/vsgutil/r8resize.hpp>

#include <vsg/core/Array2D.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    using vsgUtil::ArrayConvertOptions;
    using vsgUtil::ArrayConvertStats;
    using Gray = std::vector<uint8_t>;

    ArrayConvertOptions reencode(unsigned threads = 0)
    {
        ArrayConvertOptions options;
        options.reencode = true;
        options.encodeToBc1 = false; // as the terrain's data arrays
        options.threads = threads;
        return options;
    }

    // A BC4 image of width x height texels with `levels` levels (every level a box average of the one before),
    // built from `top` with the encoder's own inverse, so the test does not depend on how it rounds.
    vsg::ref_ptr<vsg::Data> bc4Of(uint32_t width, uint32_t height, uint32_t levels, const Gray& top)
    {
        std::vector<uint8_t> bytes;
        Gray current = top;
        uint32_t w = width;
        uint32_t h = height;
        for (uint32_t l = 0; l < levels; ++l)
        {
            const size_t offset = bytes.size();
            bytes.resize(offset + vsgUtil::Bc4::encodedSize(w, h));
            vsgUtil::Bc4::encodeImage(current.data(), w, h, bytes.data() + offset);
            const uint32_t nw = std::max(1u, w / 2);
            const uint32_t nh = std::max(1u, h / 2);
            Gray next(static_cast<size_t>(nw) * nh);
            vsgUtil::R8Resize::resize(current.data(), w, h, next.data(), nw, nh);
            current = next;
            w = nw;
            h = nh;
        }
        vsg::Data::Properties properties(VK_FORMAT_BC4_UNORM_BLOCK);
        properties.blockWidth = 4;
        properties.blockHeight = 4;
        properties.mipLevels = static_cast<uint8_t>(levels);
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        auto* data = new vsg::block64[bytes.size() / 8];
        std::memcpy(static_cast<void*>(data), bytes.data(), bytes.size());
        return vsg::block64Array2D::create(width / 4, height / 4, data, properties);
    }

    Gray ramp(uint32_t w, uint32_t h)
    {
        Gray image(static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                image[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(40 + x * 160 / (w - 1) + y * 20 / (h - 1));
            }
        }
        return image;
    }

    // The array's layer `layer` at mip level `level` as bytes of 8 per block (levels[0] at the front).
    Gray decodeLayerLevel(const vsg::Data& array, uint32_t layer, uint32_t level)
    {
        const auto levels = vsgUtil::detail::mipLevels(array);
        const auto* bytes = reinterpret_cast<const uint8_t*>(array.dataPointer());
        const auto& l = levels[level];
        // Each level holds all layers in turn: layer * (one layer's size) within it.
        const size_t layerSize = vsgUtil::Bc4::encodedSize(l.width, l.height);
        Gray out(static_cast<size_t>(l.width) * l.height);
        vsgUtil::Bc4::decodeImage(bytes + l.offset + layer * layerSize, l.width, l.height, out.data());
        return out;
    }

    double psnr(const Gray& a, const Gray& b)
    {
        double sum = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = double(a[i]) - double(b[i]);
            sum += d * d;
        }
        return sum == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / (sum / double(a.size())));
    }

    vsgUtil::LayerShape bc4Shape(uint32_t w, uint32_t h)
    {
        vsgUtil::LayerShape shape;
        shape.format = VK_FORMAT_BC4_UNORM_BLOCK;
        shape.blockCompressed = true;
        shape.width = w;
        shape.height = h;
        return shape;
    }
}

TEST(Bc4ArrayConvertible, a_bc4_layer_of_another_size_fits_a_bc4_array_with_reencoding_only)
{
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(bc4Shape(512, 512), bc4Shape(256, 256), reencode()));
    EXPECT_TRUE(vsgUtil::arrayLayerConvertible(bc4Shape(64, 64), bc4Shape(256, 256), reencode()));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(bc4Shape(512, 512), bc4Shape(256, 256)));
}

TEST(Bc4ArrayConvertible, other_formats_do_not_fit_a_bc4_array_and_bc4_does_not_fit_other_arrays)
{
    vsgUtil::LayerShape bc1 = bc4Shape(256, 256);
    bc1.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(bc1, bc4Shape(512, 512), reencode()));
    vsgUtil::LayerShape rgba;
    rgba.format = VK_FORMAT_R8G8B8A8_UNORM;
    rgba.bytesPerTexel = 4;
    rgba.width = 512;
    rgba.height = 512;
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(rgba, bc4Shape(256, 256), reencode()));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(bc4Shape(512, 512), bc1, reencode()));
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(bc4Shape(512, 512), rgba, reencode()));
    // Even a diffuse-style option set (encodeToBc1) leaves BC4 out of a BC1 array.
    ArrayConvertOptions diffuse;
    diffuse.reencode = true;
    EXPECT_FALSE(vsgUtil::arrayLayerConvertible(bc4Shape(512, 512), bc1, diffuse));
}

TEST(Bc4Array, a_larger_layer_is_resized_and_encoded_with_its_own_mip_chain)
{
    // Three layers of 16x16 (the array) and one of 64x64: the array keeps 16x16 and 3 levels.
    const Gray small = ramp(16, 16);
    const Gray big = ramp(64, 64);
    vsg::DataList layers
        = { bc4Of(16, 16, 3, small), bc4Of(64, 64, 4, big), bc4Of(16, 16, 3, small), bc4Of(16, 16, 3, small) };
    ArrayConvertStats stats;
    const auto array = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    EXPECT_EQ(stats.copied, 3u);
    EXPECT_EQ(stats.encoded, 1u);
    EXPECT_EQ(stats.grey, 0u);
    ASSERT_EQ(array->properties.format, VK_FORMAT_BC4_UNORM_BLOCK);
    ASSERT_EQ(vsgUtil::detail::mipLevels(*array).size(), 3u);

    // Level 0 of layer 1: the big layer decoded, box resized to 16x16, then only the encoder's error.
    Gray bigDecoded(64 * 64);
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(layers[1]->dataPointer());
        vsgUtil::Bc4::decodeImage(bytes, 64, 64, bigDecoded.data());
    }
    Gray expected0(16 * 16);
    vsgUtil::R8Resize::resize(bigDecoded.data(), 64, 64, expected0.data(), 16, 16);
    EXPECT_GT(psnr(expected0, decodeLayerLevel(*array, 1, 0)), 40.0);
    // Level 1 and 2 follow the chain (8x8, 4x4).
    Gray expected1(8 * 8);
    vsgUtil::R8Resize::resize(expected0.data(), 16, 16, expected1.data(), 8, 8);
    EXPECT_GT(psnr(expected1, decodeLayerLevel(*array, 1, 1)), 38.0);
    EXPECT_EQ(decodeLayerLevel(*array, 1, 2).size(), 16u);
    // The layers that fit are untouched: their level 0 equals their own source's decode.
    EXPECT_EQ(decodeLayerLevel(*array, 0, 0), decodeLayerLevel(*array, 2, 0));
}

TEST(Bc4Array, a_smaller_layer_is_enlarged)
{
    const Gray small = ramp(16, 16);
    const Gray tiny = ramp(8, 8);
    vsg::DataList layers
        = { bc4Of(16, 16, 3, small), bc4Of(8, 8, 2, tiny), bc4Of(16, 16, 3, small), bc4Of(16, 16, 3, small) };
    ArrayConvertStats stats;
    const auto array = vsgUtil::convertToArrayTexture(layers, reencode(), &stats);
    EXPECT_EQ(stats.encoded, 1u);
    EXPECT_EQ(stats.grey, 0u);
    Gray tinyDecoded(8 * 8);
    vsgUtil::Bc4::decodeImage(reinterpret_cast<const uint8_t*>(layers[1]->dataPointer()), 8, 8, tinyDecoded.data());
    Gray expected(16 * 16);
    vsgUtil::R8Resize::resize(tinyDecoded.data(), 8, 8, expected.data(), 16, 16);
    EXPECT_GT(psnr(expected, decodeLayerLevel(*array, 1, 0)), 40.0);
}

TEST(Bc4Array, a_solid_layer_stays_solid_at_every_level)
{
    vsg::DataList layers
        = { bc4Of(16, 16, 3, Gray(256, 100)), bc4Of(32, 32, 4, Gray(1024, 90)), bc4Of(16, 16, 3, Gray(256, 100)) };
    const auto array = vsgUtil::convertToArrayTexture(layers, reencode());
    for (uint32_t level = 0; level < 3; ++level)
    {
        for (const uint8_t v : decodeLayerLevel(*array, 1, level))
        {
            EXPECT_EQ(v, 90) << "level " << level;
        }
    }
}

TEST(Bc4Array, without_the_option_the_old_grey_fill_stays_byte_for_byte)
{
    vsg::DataList layers
        = { bc4Of(16, 16, 3, ramp(16, 16)), bc4Of(32, 32, 4, ramp(32, 32)), bc4Of(16, 16, 3, ramp(16, 16)) };
    ArrayConvertStats stats;
    const auto array = vsgUtil::convertToArrayTexture(layers, ArrayConvertOptions{}, &stats);
    EXPECT_EQ(stats.grey, 1u);
    EXPECT_EQ(stats.encoded, 0u);
    const auto levels = vsgUtil::detail::mipLevels(*array);
    const auto* bytes = reinterpret_cast<const uint8_t*>(array->dataPointer());
    for (size_t i = 0; i < levels[0].size; ++i)
    {
        EXPECT_EQ(bytes[levels[0].offset + levels[0].size + i], 0x80) << i;
    }
}

TEST(Bc4Array, layers_that_already_fit_give_the_same_bytes_with_and_without_the_option)
{
    vsg::DataList layers
        = { bc4Of(16, 16, 3, ramp(16, 16)), bc4Of(16, 16, 3, Gray(256, 33)), bc4Of(16, 16, 3, ramp(16, 16)) };
    const auto plain = vsgUtil::convertToArrayTexture(layers, ArrayConvertOptions{});
    const auto reencoded = vsgUtil::convertToArrayTexture(layers, reencode());
    ASSERT_EQ(plain->dataSize(), reencoded->dataSize());
    EXPECT_EQ(std::memcmp(plain->dataPointer(), reencoded->dataPointer(), plain->dataSize()), 0);
}

TEST(Bc4Array, the_bytes_do_not_depend_on_the_thread_count)
{
    vsg::DataList layers;
    for (int i = 0; i < 6; ++i)
    {
        layers.push_back(bc4Of(16, 16, 3, ramp(16, 16)));
    }
    for (int i = 0; i < 5; ++i)
    {
        layers.push_back(bc4Of(32, 32, 4, ramp(32, 32)));
    }
    const auto one = vsgUtil::convertToArrayTexture(layers, reencode(1));
    const auto four = vsgUtil::convertToArrayTexture(layers, reencode(4));
    ASSERT_EQ(one->dataSize(), four->dataSize());
    EXPECT_EQ(std::memcmp(one->dataPointer(), four->dataPointer(), one->dataSize()), 0);
}

TEST(Bc4Array, other_arrays_are_unchanged_by_the_new_path)
{
    // A BC1 array with a BC4 layer: the BC4 layer cannot be decoded as colour, so it is grey as before (a real grey
    // block with the option, which the BC1 path already gave).
    vsg::Data::Properties bc1Properties(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    bc1Properties.blockWidth = 4;
    bc1Properties.blockHeight = 4;
    bc1Properties.mipLevels = 1;
    bc1Properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
    auto makeBc1 = [&] {
        auto* data = new vsg::block64[4];
        std::memset(static_cast<void*>(data), 0, 4 * 8);
        return vsg::block64Array2D::create(2, 2, data, bc1Properties);
    };
    vsg::DataList layers = { makeBc1(), makeBc1(), bc4Of(16, 16, 1, ramp(16, 16)) };
    ArrayConvertOptions options;
    options.reencode = true;
    ArrayConvertStats stats;
    const auto array = vsgUtil::convertToArrayTexture(layers, options, &stats);
    EXPECT_EQ(array->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    EXPECT_EQ(stats.grey, 1u);
    EXPECT_EQ(stats.encoded, 0u);
}
