// Tests for the array versions of the mip operations (task 0080, terrain layer arrays): dropArrayLevels and
// shrinkArray of vsgUtil::TextureResize, on arrays made by vsgUtil::convertToArrayTexture exactly as the terrain
// builder makes them. Written from the layout rules of convertToArrayTexture (each mip level of a block compressed
// array is stored for all layers in turn; an uncompressed array stores one level and the GPU makes the chain) before
// the code. No device is needed.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/textureresize.hpp>

#include "texturetestutil.hpp"

#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>
#include <vsg/core/MipmapLayout.h>

#include <gtest/gtest.h>

#include <cstring>

namespace
{
    using namespace TextureTest;
    using vsgUtil::TextureResize::Kind;

    // A BC1 image of 64 x 64 with 7 levels whose bytes differ from layer to layer.
    vsg::ref_ptr<vsg::Data> bc1Layer(uint8_t salt)
    {
        auto image = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 64, 7));
        auto* bytes = static_cast<uint8_t*>(image->dataPointer());
        for (size_t i = 0; i < image->dataSize(); ++i)
            bytes[i] = static_cast<uint8_t>(bytes[i] ^ salt);
        return image;
    }

    vsg::ref_ptr<vsg::Data> bc1Array(uint32_t layers)
    {
        vsg::DataList images;
        for (uint32_t i = 0; i < layers; ++i)
            images.push_back(bc1Layer(static_cast<uint8_t>(i * 37 + 1)));
        return vsgUtil::convertToArrayTexture(images);
    }

    // An uncompressed RGBA8 layer of size x size texels, every texel `colour`.
    vsg::ref_ptr<vsg::Data> rgbaLayer(uint32_t size, vsg::ubvec4 colour)
    {
        auto image = vsg::ubvec4Array2D::create(size, size, colour, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        return image;
    }

    vsg::ref_ptr<vsg::Data> rgbaArray(uint32_t size, uint32_t layers)
    {
        vsg::DataList images;
        for (uint32_t i = 0; i < layers; ++i)
            images.push_back(rgbaLayer(size, vsg::ubvec4(static_cast<uint8_t>(10 + i * 40), 20, 30, 255)));
        return vsgUtil::convertToArrayTexture(images);
    }

    // The bytes of mip level `level` of layer `layer` in a block compressed array, by its layout.
    std::vector<uint8_t> layerLevelBytes(const vsg::Data& array, size_t level, uint32_t layer)
    {
        const auto* layout = array.getMipmapLayout();
        const size_t blocks = size_t((layout->at(level).x + 3) / 4) * ((layout->at(level).y + 3) / 4);
        const size_t layerBytes = blocks * array.valueSize();
        const auto* base = static_cast<const uint8_t*>(array.dataPointer()) + layout->at(level).w + layer * layerBytes;
        return std::vector<uint8_t>(base, base + layerBytes);
    }

    TEST(TextureArrayDrop, the_test_array_is_what_the_builder_makes)
    {
        auto array = bc1Array(5);
        ASSERT_TRUE(array.valid());
        EXPECT_EQ(array->depth(), 5u);
        ASSERT_NE(array->getMipmapLayout(), nullptr);
        EXPECT_EQ(array->getMipmapLayout()->size(), 7u);
        EXPECT_EQ(array->properties.imageViewType, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    }

    TEST(TextureArrayDrop, zero_levels_returns_the_same_object)
    {
        auto array = bc1Array(3);
        EXPECT_EQ(vsgUtil::TextureResize::dropArrayLevels(*array, 0).get(), array.get());
        EXPECT_EQ(vsgUtil::TextureResize::dropArrayLevels(*array, -2).get(), array.get());
    }

    TEST(TextureArrayDrop, one_level_less_halves_the_size_and_keeps_every_layer)
    {
        auto array = bc1Array(4);
        auto result = vsgUtil::TextureResize::dropArrayLevels(*array, 1);
        ASSERT_TRUE(result.valid());
        EXPECT_NE(result.get(), array.get());
        EXPECT_EQ(result->depth(), 4u);
        EXPECT_EQ(result->width(), 8u); // 32 texels, in blocks
        EXPECT_EQ(result->height(), 8u);
        EXPECT_EQ(result->properties.format, array->properties.format);
        EXPECT_EQ(result->properties.blockWidth, 4);
        EXPECT_EQ(result->properties.mipLevels, 6);
        EXPECT_EQ(result->properties.imageViewType, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        ASSERT_NE(result->getMipmapLayout(), nullptr);
        EXPECT_EQ(result->getMipmapLayout()->size(), 6u);
    }

    TEST(TextureArrayDrop, the_layout_starts_again_at_zero_with_the_new_top_size)
    {
        auto array = bc1Array(3);
        auto result = vsgUtil::TextureResize::dropArrayLevels(*array, 2);
        ASSERT_TRUE(result.valid());
        const auto* layout = result->getMipmapLayout();
        ASSERT_NE(layout, nullptr);
        const uint32_t sizes[] = { 16, 8, 4, 2, 1 };
        size_t offset = 0;
        for (size_t level = 0; level < 5; ++level)
        {
            EXPECT_EQ(layout->at(level).x, sizes[level]) << level;
            EXPECT_EQ(layout->at(level).y, sizes[level]) << level;
            EXPECT_EQ(layout->at(level).w, offset) << level;
            offset += size_t((sizes[level] + 3) / 4) * ((sizes[level] + 3) / 4) * 8 * 3;
        }
        EXPECT_EQ(result->dataSize(), offset);
    }

    TEST(TextureArrayDrop, every_kept_level_of_every_layer_keeps_its_bytes)
    {
        auto array = bc1Array(4);
        auto result = vsgUtil::TextureResize::dropArrayLevels(*array, 2);
        ASSERT_TRUE(result.valid());
        for (size_t level = 0; level < 5; ++level)
            for (uint32_t layer = 0; layer < 4; ++layer)
                EXPECT_EQ(layerLevelBytes(*result, level, layer), layerLevelBytes(*array, level + 2, layer))
                    << "level " << level << " layer " << layer;
    }

    TEST(TextureArrayDrop, a_bc3_array_keeps_its_larger_blocks)
    {
        vsg::DataList images;
        for (int i = 0; i < 2; ++i)
            images.push_back(readDds(makeDds(DdsFormat::Bc7Dx10, 32, 32, 6)));
        auto array = vsgUtil::convertToArrayTexture(images);
        ASSERT_TRUE(array.valid());
        auto result = vsgUtil::TextureResize::dropArrayLevels(*array, 1);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->valueSize(), 16u);
        EXPECT_EQ(result->depth(), 2u);
        EXPECT_EQ(layerLevelBytes(*result, 0, 1), layerLevelBytes(*array, 1, 1));
    }

    TEST(TextureArrayDrop, dropping_all_levels_or_more_is_refused)
    {
        auto array = bc1Array(2);
        EXPECT_FALSE(vsgUtil::TextureResize::dropArrayLevels(*array, 7).valid());
        EXPECT_FALSE(vsgUtil::TextureResize::dropArrayLevels(*array, 20).valid());
        EXPECT_TRUE(vsgUtil::TextureResize::dropArrayLevels(*array, 6).valid()); // one level is left
    }

    TEST(TextureArrayDrop, a_plain_2d_image_is_refused)
    {
        auto image = bc1Layer(1);
        EXPECT_FALSE(vsgUtil::TextureResize::dropArrayLevels(*image, 1).valid());
    }

    TEST(TextureArrayDrop, an_uncompressed_array_is_refused)
    {
        auto array = rgbaArray(16, 3);
        EXPECT_FALSE(vsgUtil::TextureResize::dropArrayLevels(*array, 1).valid());
    }

    TEST(TextureArrayDrop, the_source_is_not_changed)
    {
        auto array = bc1Array(3);
        const auto before = bytesFrom(*array, 0);
        vsgUtil::TextureResize::dropArrayLevels(*array, 3);
        EXPECT_EQ(bytesFrom(*array, 0), before);
        EXPECT_EQ(array->getMipmapLayout()->size(), 7u);
    }

    TEST(TextureArrayShrink, zero_levels_returns_the_same_object)
    {
        auto array = rgbaArray(8, 2);
        EXPECT_EQ(vsgUtil::TextureResize::shrinkArray(*array, 0, Kind::Colour).get(), array.get());
    }

    TEST(TextureArrayShrink, a_level_halves_every_layer_and_keeps_their_colours)
    {
        auto array = rgbaArray(8, 3);
        auto result = vsgUtil::TextureResize::shrinkArray(*array, 1, Kind::Colour);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 4u);
        EXPECT_EQ(result->depth(), 3u);
        EXPECT_EQ(result->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(result->properties.imageViewType, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
        EXPECT_EQ(result->properties.mipLevels, 0); // the GPU makes the chain
        EXPECT_EQ(result->getMipmapLayout(), nullptr);
        EXPECT_EQ(result->dataSize(), size_t(4 * 4 * 4 * 3));
        const auto* bytes = static_cast<const uint8_t*>(result->dataPointer());
        for (uint32_t layer = 0; layer < 3; ++layer)
            for (uint32_t texel = 0; texel < 16; ++texel)
            {
                const uint8_t* p = bytes + (size_t(layer) * 16 + texel) * 4;
                EXPECT_EQ(p[0], 10 + layer * 40) << layer;
                EXPECT_EQ(p[1], 20);
                EXPECT_EQ(p[2], 30);
                EXPECT_EQ(p[3], 255);
            }
    }

    TEST(TextureArrayShrink, a_texel_is_the_mean_of_its_block)
    {
        vsg::DataList images;
        auto layer = vsg::ubvec4Array2D::create(2, 2, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        layer->set(0, 0, vsg::ubvec4(0, 0, 0, 255));
        layer->set(1, 0, vsg::ubvec4(100, 0, 0, 255));
        layer->set(0, 1, vsg::ubvec4(200, 0, 0, 255));
        layer->set(1, 1, vsg::ubvec4(100, 40, 0, 255));
        images.push_back(layer);
        images.push_back(layer);
        auto array = vsgUtil::convertToArrayTexture(images);
        auto result = vsgUtil::TextureResize::shrinkArray(*array, 1, Kind::Colour);
        ASSERT_TRUE(result.valid());
        const auto* bytes = static_cast<const uint8_t*>(result->dataPointer());
        EXPECT_EQ(bytes[0], 100); // (0 + 100 + 200 + 100 + 2) / 4
        EXPECT_EQ(bytes[1], 10);
        EXPECT_EQ(bytes[4], 100); // the second layer
    }

    TEST(TextureArrayShrink, two_levels_make_quarter_size)
    {
        auto array = rgbaArray(8, 2);
        auto result = vsgUtil::TextureResize::shrinkArray(*array, 2, Kind::Colour);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result->width(), 2u);
        EXPECT_EQ(result->height(), 2u);
        EXPECT_EQ(result->depth(), 2u);
    }

    TEST(TextureArrayShrink, a_normal_array_is_renormalised)
    {
        vsg::DataList images;
        for (int i = 0; i < 2; ++i)
            images.push_back(rgbaLayer(4, vsg::ubvec4(200, 128, 128, 77))); // not of length one
        auto array = vsgUtil::convertToArrayTexture(images);
        auto result = vsgUtil::TextureResize::shrinkArray(*array, 1, Kind::Normal);
        ASSERT_TRUE(result.valid());
        const auto* bytes = static_cast<const uint8_t*>(result->dataPointer());
        EXPECT_EQ(bytes[0], 255); // (0.57, 0.004, 0.004) as a unit vector is (1, 0, 0)
        EXPECT_NEAR(bytes[1], 128, 1);
        EXPECT_EQ(bytes[3], 77); // alpha (the height of an _nh map) is only averaged
    }

    TEST(TextureArrayShrink, shrinking_past_one_texel_is_refused)
    {
        auto array = rgbaArray(4, 2);
        EXPECT_TRUE(vsgUtil::TextureResize::shrinkArray(*array, 2, Kind::Colour).valid());
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkArray(*array, 3, Kind::Colour).valid());
    }

    TEST(TextureArrayShrink, a_block_compressed_array_is_refused)
    {
        auto array = bc1Array(2);
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkArray(*array, 1, Kind::Colour).valid());
    }

    TEST(TextureArrayShrink, a_plain_2d_image_is_refused)
    {
        auto image = rgbaLayer(8, vsg::ubvec4(1, 2, 3, 4));
        EXPECT_FALSE(vsgUtil::TextureResize::shrinkArray(*image, 1, Kind::Colour).valid());
    }

    TEST(TextureArrayShrink, two_runs_give_the_same_bytes)
    {
        auto array = rgbaArray(16, 4);
        auto first = vsgUtil::TextureResize::shrinkArray(*array, 2, Kind::Normal);
        auto second = vsgUtil::TextureResize::shrinkArray(*array, 2, Kind::Normal);
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());
        EXPECT_EQ(bytesFrom(*first, 0), bytesFrom(*second, 0));
    }

    TEST(TextureArrayShrink, the_source_is_not_changed)
    {
        auto array = rgbaArray(8, 2);
        const auto before = bytesFrom(*array, 0);
        vsgUtil::TextureResize::shrinkArray(*array, 1, Kind::Colour);
        EXPECT_EQ(bytesFrom(*array, 0), before);
    }
}
