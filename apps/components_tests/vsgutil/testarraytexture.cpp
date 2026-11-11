#include <gtest/gtest.h>

#if OPENEDEN_BUILD_VSG

#include <components/vsgutil/arraytexture.hpp>

#include <vsg/all.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr uint8_t sGrey = 0x80;
    constexpr size_t sBc1BlockBytes = 8; // one 4x4 BC1 block is 64 bits
    constexpr uint32_t sBc1BlockSize = 4;

    // Bytes in one mip level of a BC1 image whose level 0 is width x height texels.
    size_t bc1LevelBytes(uint32_t width, uint32_t height, uint32_t level)
    {
        const uint32_t w = std::max(1u, width >> level);
        const uint32_t h = std::max(1u, height >> level);
        return sBc1BlockBytes * ((w + sBc1BlockSize - 1) / sBc1BlockSize) * ((h + sBc1BlockSize - 1) / sBc1BlockSize);
    }

    // A distinct byte per (layer id, mip level, byte index); never 0x80, so it can't be mistaken for the grey fill.
    uint8_t patternByte(int id, uint32_t level, size_t index)
    {
        return static_cast<uint8_t>(((id + 1) << 4) | ((level & 7) << 1) | (index & 1));
    }

    // A BC1 image of width x height texels with `levels` mip levels stored one after another (no MipmapLayout),
    // each level filled with patternByte(id, level, ...).
    vsg::ref_ptr<vsg::Data> makeBc1(
        uint32_t width, uint32_t height, uint8_t levels, int id, VkFormat format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
    {
        size_t totalBytes = 0;
        for (uint32_t level = 0; level < std::max<uint32_t>(levels, 1); ++level)
            totalBytes += bc1LevelBytes(width, height, level);

        // Owned by the array created below, which frees it with delete[] (ALLOCATOR_TYPE_NEW_DELETE).
        auto* blocks = new vsg::block64[totalBytes / sBc1BlockBytes];
        auto* bytes = reinterpret_cast<uint8_t*>(blocks);
        size_t offset = 0;
        for (uint32_t level = 0; level < std::max<uint32_t>(levels, 1); ++level)
        {
            const size_t size = bc1LevelBytes(width, height, level);
            for (size_t k = 0; k < size; ++k)
                bytes[offset + k] = patternByte(id, level, k);
            offset += size;
        }

        vsg::Data::Properties properties(format);
        properties.blockWidth = sBc1BlockSize;
        properties.blockHeight = sBc1BlockSize;
        properties.mipLevels = levels;
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        return vsg::block64Array2D::create(width / sBc1BlockSize, height / sBc1BlockSize, blocks, properties);
    }

    // A distinct RGBA texel per (layer id, x, y); no channel is 0x80.
    vsg::ubvec4 rgbaTexel(int id, uint32_t x, uint32_t y)
    {
        return vsg::ubvec4(static_cast<uint8_t>(0x10 * (id + 1) + x), static_cast<uint8_t>(0x20 + y), 0x11, 0xff);
    }

    // A BC1 image like vsgXchange's DDS reader produces: a vsg::MipmapLayout with one
    // uivec4(width in texels, height in texels, 1, byte offset) per level. `padding` bytes (a multiple of the block
    // size, filled with 0xEE) go before level 0 and between levels. With padding 0 the levels are contiguous, as in a
    // DDS file. vsg::Data::dataSize() counts only the levels themselves, so with padding the later levels lie past
    // dataSize(), which convertToArrayTexture treats as invalid (grey).
    vsg::ref_ptr<vsg::Data> makeBc1WithLayout(uint32_t width, uint32_t height, uint8_t levels, int id, size_t padding)
    {
        auto layout = vsg::MipmapLayout::create(levels);
        size_t totalBytes = 0;
        for (uint32_t level = 0; level < levels; ++level)
        {
            totalBytes += padding;
            layout->set(level,
                vsg::uivec4(std::max(1u, width >> level), std::max(1u, height >> level), 1u,
                    static_cast<uint32_t>(totalBytes)));
            totalBytes += bc1LevelBytes(width, height, level);
        }

        // Owned by the array created below, which frees it with delete[] (ALLOCATOR_TYPE_NEW_DELETE).
        auto* blocks = new vsg::block64[totalBytes / sBc1BlockBytes];
        auto* bytes = reinterpret_cast<uint8_t*>(blocks);
        std::fill(bytes, bytes + totalBytes, uint8_t{ 0xEE });
        for (uint32_t level = 0; level < levels; ++level)
        {
            const size_t offset = layout->at(level).w;
            const size_t size = bc1LevelBytes(width, height, level);
            for (size_t k = 0; k < size; ++k)
                bytes[offset + k] = patternByte(id, level, k);
        }

        vsg::Data::Properties properties(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        properties.blockWidth = sBc1BlockSize;
        properties.blockHeight = sBc1BlockSize;
        properties.mipLevels = levels;
        properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
        return vsg::block64Array2D::create(
            width / sBc1BlockSize, height / sBc1BlockSize, blocks, properties, layout.get());
    }

    vsg::ref_ptr<vsg::Data> makeRgba(uint32_t width, uint32_t height, int id)
    {
        auto image = vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties(VK_FORMAT_R8G8B8A8_UNORM));
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                image->set(x, y, rgbaTexel(id, x, y));
        return image;
    }

    // Byte offset of (level, layer) in a block compressed array of `layerCount` layers: each mip level holds all
    // layers in turn, as vsg::TransferTask uploads it.
    size_t bc1ArrayOffset(uint32_t width, uint32_t height, uint32_t layerCount, uint32_t level, uint32_t layer)
    {
        size_t offset = 0;
        for (uint32_t l = 0; l < level; ++l)
            offset += bc1LevelBytes(width, height, l) * layerCount;
        return offset + bc1LevelBytes(width, height, level) * layer;
    }

    size_t bc1ArrayBytes(uint32_t width, uint32_t height, uint32_t layerCount, uint32_t levels)
    {
        return bc1ArrayOffset(width, height, layerCount, levels, 0);
    }

    const uint8_t* bytesOf(const vsg::Data& data)
    {
        return static_cast<const uint8_t*>(data.dataPointer());
    }

    // Expects layer `layer` of a BC1 array to hold source image `id`'s first `levels` levels exactly.
    void expectBc1LayerCopied(const vsg::Data& result, uint32_t width, uint32_t height, uint32_t layerCount,
        uint32_t levels, uint32_t layer, int id)
    {
        const uint8_t* bytes = bytesOf(result);
        for (uint32_t level = 0; level < levels; ++level)
        {
            const size_t offset = bc1ArrayOffset(width, height, layerCount, level, layer);
            const size_t size = bc1LevelBytes(width, height, level);
            size_t mismatches = 0;
            for (size_t k = 0; k < size; ++k)
                if (bytes[offset + k] != patternByte(id, level, k))
                    ++mismatches;
            EXPECT_EQ(mismatches, 0u) << "layer " << layer << " level " << level << " (source " << id << ")";
        }
    }

    void expectBc1LayerGrey(
        const vsg::Data& result, uint32_t width, uint32_t height, uint32_t layerCount, uint32_t levels, uint32_t layer)
    {
        const uint8_t* bytes = bytesOf(result);
        for (uint32_t level = 0; level < levels; ++level)
        {
            const size_t offset = bc1ArrayOffset(width, height, layerCount, level, layer);
            const size_t size = bc1LevelBytes(width, height, level);
            size_t notGrey = 0;
            for (size_t k = 0; k < size; ++k)
                if (bytes[offset + k] != sGrey)
                    ++notGrey;
            EXPECT_EQ(notGrey, 0u) << "layer " << layer << " level " << level;
        }
    }

    vsg::ubvec4 rgbaAt(const vsg::Data& result, uint32_t width, uint32_t height, uint32_t layer, uint32_t x, uint32_t y)
    {
        const uint8_t* texel = bytesOf(result) + (static_cast<size_t>(layer) * width * height + y * width + x) * 4;
        return vsg::ubvec4(texel[0], texel[1], texel[2], texel[3]);
    }

    bool isGrey(const vsg::ubvec4& texel)
    {
        return texel.r == sGrey && texel.g == sGrey && texel.b == sGrey && texel.a == sGrey;
    }

    // --- Input validation ---

    TEST(VsgUtilArrayTextureTest, EmptyInputThrows)
    {
        EXPECT_THROW(static_cast<void>(vsgUtil::convertToArrayTexture(vsg::DataList{})), std::exception);
    }

    // Task 0109: a session with no land textures (a neutral world of another game) reaches the terrain builder with an
    // empty layer list; the builder gives the array one grey layer instead of throwing at shutdown.
    TEST(VsgUtilArrayTextureTest, AnEmptyLayerListGetsOneGreyPlaceholderLayer)
    {
        vsg::DataList images;
        EXPECT_TRUE(vsgUtil::ensureOneLayer(images));
        ASSERT_EQ(images.size(), 1u);
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->depth(), 1u);
        EXPECT_EQ(result->width(), 1u);
        EXPECT_EQ(result->height(), 1u);
    }

    TEST(VsgUtilArrayTextureTest, ALayerListWithLayersIsLeftAlone)
    {
        vsg::DataList images{ makeBc1(8, 8, 4, 0) };
        const vsg::Data* first = images[0].get();
        EXPECT_FALSE(vsgUtil::ensureOneLayer(images));
        ASSERT_EQ(images.size(), 1u);
        EXPECT_EQ(images[0].get(), first);
    }

    // --- Result description ---

    TEST(VsgUtilArrayTextureTest, ResultIs2DArrayWithDepthEqualToLayerCount)
    {
        const vsg::DataList images{ makeBc1(8, 8, 4, 0), makeBc1(8, 8, 4, 1), makeBc1(8, 8, 4, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.imageViewType, static_cast<int>(VK_IMAGE_VIEW_TYPE_2D_ARRAY));
        EXPECT_EQ(result->depth(), 3u);
        EXPECT_EQ(result->width(), images[0]->width());
        EXPECT_EQ(result->height(), images[0]->height());
        EXPECT_EQ(result->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        EXPECT_EQ(result->properties.blockWidth, sBc1BlockSize);
        EXPECT_EQ(result->properties.blockHeight, sBc1BlockSize);
    }

    TEST(VsgUtilArrayTextureTest, UncompressedResultIs2DArrayWithDepthEqualToLayerCount)
    {
        const vsg::DataList images{ makeRgba(4, 4, 0), makeRgba(4, 4, 1) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.imageViewType, static_cast<int>(VK_IMAGE_VIEW_TYPE_2D_ARRAY));
        EXPECT_EQ(result->depth(), 2u);
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 4u);
        EXPECT_EQ(result->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        // Uncompressed arrays let VSG generate their mipmaps, so they carry a single stored level.
        EXPECT_LE(result->properties.mipLevels, 1);
    }

    TEST(VsgUtilArrayTextureTest, CompressedArrayKeepsLayersMipChain)
    {
        const vsg::DataList images{ makeBc1(8, 8, 4, 0), makeBc1(8, 8, 4, 1) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.mipLevels, 4);
        EXPECT_EQ(result->dataSize(), bc1ArrayBytes(8, 8, 2, 4));
    }

    // --- Matching layers ---

    TEST(VsgUtilArrayTextureTest, MatchingCompressedLayersAreCopiedExactlyPerLevel)
    {
        constexpr uint32_t size = 16;
        constexpr uint32_t levels = 5;
        constexpr uint32_t layers = 3;
        const vsg::DataList images{ makeBc1(size, size, levels, 0), makeBc1(size, size, levels, 1),
            makeBc1(size, size, levels, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, MatchingCompressedLayersWithTruncatedChainAreCopiedExactly)
    {
        // Morrowind's land textures stop short of 1x1.
        constexpr uint32_t size = 16;
        constexpr uint32_t levels = 2;
        constexpr uint32_t layers = 2;
        const vsg::DataList images{ makeBc1(size, size, levels, 0), makeBc1(size, size, levels, 1) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->properties.mipLevels, levels);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, MatchingUncompressedLayersAreCopiedExactly)
    {
        constexpr uint32_t size = 4;
        const vsg::DataList images{ makeRgba(size, size, 0), makeRgba(size, size, 1), makeRgba(size, size, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        ASSERT_GE(result->dataSize(), size_t{ 3 } * size * size * 4);

        for (uint32_t layer = 0; layer < 3; ++layer)
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                    EXPECT_EQ(rgbaAt(*result, size, size, layer, x, y), rgbaTexel(static_cast<int>(layer), x, y))
                        << "layer " << layer << " x " << x << " y " << y;
    }

    // --- The most common size, format and mip count is the array's ---

    TEST(VsgUtilArrayTextureTest, MostCommonSizeWinsOverFirstLayer)
    {
        // Like OpenMW's _land_default.dds: layer 0 is smaller than the rest.
        const vsg::DataList images{ makeBc1(16, 16, 1, 0), makeBc1(8, 8, 1, 1), makeBc1(8, 8, 1, 2),
            makeBc1(8, 8, 1, 3) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->width(), 2u);
        EXPECT_EQ(result->height(), 2u);
        EXPECT_EQ(result->depth(), 4u);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(8, 8, 4, 1));

        // A compressed layer of another size can't be resampled, so it is grey.
        expectBc1LayerGrey(*result, 8, 8, 4, 1, 0);
        for (uint32_t layer = 1; layer < 4; ++layer)
            expectBc1LayerCopied(*result, 8, 8, 4, 1, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, MostCommonFormatWinsOverFirstLayer)
    {
        // Morrowind's land textures are RGBA while layer 0 is a BC1.
        const vsg::DataList images{ makeBc1(4, 4, 1, 0), makeRgba(4, 4, 1), makeRgba(4, 4, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
        EXPECT_EQ(result->properties.blockWidth, 1);
        EXPECT_EQ(result->properties.blockHeight, 1);
        EXPECT_EQ(result->width(), 4u);
        EXPECT_EQ(result->height(), 4u);
        EXPECT_EQ(result->depth(), 3u);
        ASSERT_GE(result->dataSize(), size_t{ 3 } * 4 * 4 * 4);

        // In an uncompressed RGBA array a block-compressed layer is decoded and resampled, not filled grey. The BC1
        // pattern bytes are arbitrary, so only check that layer 0 is not the grey fill.
        const uint8_t* layer0 = bytesOf(*result);
        EXPECT_FALSE(std::all_of(layer0, layer0 + 4 * 4 * 4, [](uint8_t b) { return b == sGrey; }));

        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
            {
                EXPECT_EQ(rgbaAt(*result, 4, 4, 1, x, y), rgbaTexel(1, x, y));
                EXPECT_EQ(rgbaAt(*result, 4, 4, 2, x, y), rgbaTexel(2, x, y));
            }
    }

    TEST(VsgUtilArrayTextureTest, MostCommonCompressedFormatWins)
    {
        // Same size, but layer 0 is the RGB-only BC1 variant.
        const vsg::DataList images{ makeBc1(8, 8, 1, 0, VK_FORMAT_BC1_RGB_UNORM_BLOCK), makeBc1(8, 8, 1, 1),
            makeBc1(8, 8, 1, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(8, 8, 3, 1));
        expectBc1LayerGrey(*result, 8, 8, 3, 1, 0);
        expectBc1LayerCopied(*result, 8, 8, 3, 1, 1, 1);
        expectBc1LayerCopied(*result, 8, 8, 3, 1, 2, 2);
    }

    TEST(VsgUtilArrayTextureTest, ShortestMipChainSetsArrayLevelsEvenInMinority)
    {
        // Same size and format; mip count is not part of the most-common choice. Three layers with a full chain of 5
        // and one with 3: the array keeps 3 levels, and every layer copies its first 3.
        constexpr uint32_t size = 16;
        constexpr uint32_t levels = 3;
        constexpr uint32_t layers = 4;
        const vsg::DataList images{ makeBc1(size, size, 5, 0), makeBc1(size, size, levels, 1),
            makeBc1(size, size, 5, 2), makeBc1(size, size, 5, 3) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);

        EXPECT_EQ(result->properties.mipLevels, levels);
        EXPECT_EQ(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));
        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    // --- Same size and format, different mip chain ---

    TEST(VsgUtilArrayTextureTest, LayerWithLongerMipChainCopiesArraysLevels)
    {
        // Bloodmoon's land textures go down to 1x1 while Morrowind's stop early.
        constexpr uint32_t size = 16;
        constexpr uint32_t levels = 3;
        constexpr uint32_t layers = 4;
        const vsg::DataList images{ makeBc1(size, size, levels, 0), makeBc1(size, size, 5, 1),
            makeBc1(size, size, levels, 2), makeBc1(size, size, levels, 3) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->properties.mipLevels, levels);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, LayerWithLongerMipChainIsNotGrey)
    {
        constexpr uint32_t size = 8;
        const vsg::DataList images{ makeBc1(size, size, 2, 0), makeBc1(size, size, 4, 1), makeBc1(size, size, 2, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, 3, 2));

        const uint8_t* bytes = bytesOf(*result);
        for (uint32_t level = 0; level < 2; ++level)
        {
            const size_t offset = bc1ArrayOffset(size, size, 3, level, 1);
            const size_t levelBytes = bc1LevelBytes(size, size, level);
            EXPECT_FALSE(std::all_of(bytes + offset, bytes + offset + levelBytes, [](uint8_t b) { return b == sGrey; }))
                << "level " << level;
        }
    }

    TEST(VsgUtilArrayTextureTest, LayerWithShorterMipChainShortensArray)
    {
        // A shorter chain is not a mismatch: it shortens the array, and no layer is grey.
        constexpr uint32_t size = 16;
        constexpr uint32_t shortest = 2;
        constexpr uint32_t layers = 4;
        const vsg::DataList images{ makeBc1(size, size, 4, 0), makeBc1(size, size, 4, 1),
            makeBc1(size, size, shortest, 2), makeBc1(size, size, 5, 3) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->properties.mipLevels, shortest);
        EXPECT_EQ(result->dataSize(), bc1ArrayBytes(size, size, layers, shortest));

        const uint8_t* bytes = bytesOf(*result);
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            expectBc1LayerCopied(*result, size, size, layers, shortest, layer, static_cast<int>(layer));
            for (uint32_t level = 0; level < shortest; ++level)
            {
                const size_t offset = bc1ArrayOffset(size, size, layers, level, layer);
                const size_t levelBytes = bc1LevelBytes(size, size, level);
                EXPECT_FALSE(
                    std::all_of(bytes + offset, bytes + offset + levelBytes, [](uint8_t b) { return b == sGrey; }))
                    << "layer " << layer << " level " << level;
            }
        }
    }

    // --- Layers with a vsg::MipmapLayout ---

    TEST(VsgUtilArrayTextureTest, LayersWithMipmapLayoutCopyFromLayoutOffsets)
    {
        // Contiguous DDS-style layouts: level offsets are non-zero from level 1 on.
        constexpr uint32_t size = 16;
        constexpr uint8_t levels = 5;
        constexpr uint32_t layers = 3;
        const vsg::DataList images{ makeBc1WithLayout(size, size, levels, 0, 0),
            makeBc1WithLayout(size, size, levels, 1, 0), makeBc1WithLayout(size, size, levels, 2, 0) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->properties.mipLevels, levels);
        EXPECT_EQ(result->depth(), layers);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, LayersWithAndWithoutMipmapLayoutMix)
    {
        // A DDS-style layer (contiguous levels described by a MipmapLayout) beside layers described only by
        // properties.mipLevels, with a shorter layout chain setting the array's level count.
        constexpr uint32_t size = 16;
        constexpr uint32_t levels = 3;
        constexpr uint32_t layers = 3;
        const vsg::DataList images{ makeBc1(size, size, 5, 0), makeBc1WithLayout(size, size, levels, 1, 0),
            makeBc1WithLayout(size, size, 5, 2, 0) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->properties.mipLevels, levels);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        for (uint32_t layer = 0; layer < layers; ++layer)
            expectBc1LayerCopied(*result, size, size, layers, levels, layer, static_cast<int>(layer));
    }

    TEST(VsgUtilArrayTextureTest, LayerWhoseLayoutPointsPastDataSizeIsGrey)
    {
        // Padding shifts the layer's levels past its dataSize(), which counts only the levels themselves.
        constexpr uint32_t size = 16;
        constexpr uint8_t levels = 3;
        constexpr uint32_t layers = 3;
        const vsg::DataList images{ makeBc1WithLayout(size, size, levels, 0, 0),
            makeBc1WithLayout(size, size, levels, 1, sBc1BlockBytes), makeBc1WithLayout(size, size, levels, 2, 0) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->depth(), layers);
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(size, size, layers, levels));

        expectBc1LayerCopied(*result, size, size, layers, levels, 0, 0);
        expectBc1LayerGrey(*result, size, size, layers, levels, 1);
        expectBc1LayerCopied(*result, size, size, layers, levels, 2, 2);
    }

    // --- Uncompressed layers of another size are resampled ---

    TEST(VsgUtilArrayTextureTest, SmallerUncompressedLayerIsResampledNotGrey)
    {
        constexpr uint32_t size = 4;
        const vsg::DataList images{ makeRgba(size, size, 0), makeRgba(2, 2, 1), makeRgba(size, size, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->width(), size);
        EXPECT_EQ(result->height(), size);
        ASSERT_GE(result->dataSize(), size_t{ 3 } * size * size * 4);

        std::set<std::vector<uint8_t>> source;
        for (uint32_t y = 0; y < 2; ++y)
            for (uint32_t x = 0; x < 2; ++x)
            {
                const auto t = rgbaTexel(1, x, y);
                source.insert({ t.r, t.g, t.b, t.a });
            }

        std::set<std::vector<uint8_t>> seen;
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const auto t = rgbaAt(*result, size, size, 1, x, y);
                EXPECT_FALSE(isGrey(t)) << "x " << x << " y " << y;
                const std::vector<uint8_t> bytes{ t.r, t.g, t.b, t.a };
                EXPECT_TRUE(source.count(bytes)) << "x " << x << " y " << y << " is not a source texel";
                seen.insert(bytes);
            }
        // Doubling the size keeps every source texel.
        EXPECT_EQ(seen, source);
        EXPECT_EQ(rgbaAt(*result, size, size, 1, 0, 0), rgbaTexel(1, 0, 0));

        // The neighbouring layers are untouched.
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                EXPECT_EQ(rgbaAt(*result, size, size, 0, x, y), rgbaTexel(0, x, y));
                EXPECT_EQ(rgbaAt(*result, size, size, 2, x, y), rgbaTexel(2, x, y));
            }
    }

    TEST(VsgUtilArrayTextureTest, LargerUncompressedLayerIsResampledNotGrey)
    {
        constexpr uint32_t size = 2;
        const vsg::DataList images{ makeRgba(8, 8, 0), makeRgba(size, size, 1), makeRgba(size, size, 2) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->width(), size);
        EXPECT_EQ(result->height(), size);
        ASSERT_GE(result->dataSize(), size_t{ 3 } * size * size * 4);

        std::set<std::vector<uint8_t>> source;
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x)
            {
                const auto t = rgbaTexel(0, x, y);
                source.insert({ t.r, t.g, t.b, t.a });
            }

        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const auto t = rgbaAt(*result, size, size, 0, x, y);
                EXPECT_FALSE(isGrey(t)) << "x " << x << " y " << y;
                EXPECT_TRUE(source.count({ t.r, t.g, t.b, t.a }))
                    << "x " << x << " y " << y << " is not a source texel";
            }
        EXPECT_EQ(rgbaAt(*result, size, size, 0, 0, 0), rgbaTexel(0, 0, 0));
    }

    TEST(VsgUtilArrayTextureTest, SingleLayerIsCopied)
    {
        const vsg::DataList images{ makeBc1(8, 8, 4, 5) };
        auto result = vsgUtil::convertToArrayTexture(images);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result->depth(), 1u);
        EXPECT_EQ(result->properties.imageViewType, static_cast<int>(VK_IMAGE_VIEW_TYPE_2D_ARRAY));
        ASSERT_GE(result->dataSize(), bc1ArrayBytes(8, 8, 1, 4));
        expectBc1LayerCopied(*result, 8, 8, 1, 4, 0, 5);
    }
}

#endif
