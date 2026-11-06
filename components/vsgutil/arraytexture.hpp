#ifndef VSGOPENMW_VSGUTIL_ARRAYTEXTURE_H
#define VSGOPENMW_VSGUTIL_ARRAYTEXTURE_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <vsg/core/Allocator.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>
#include <vsg/core/MipmapLayout.h>

#include "bc1encode.hpp"
#include "bc7decode.hpp"
#include "blockconvert.hpp"
#include "name.hpp"
#include "rgbaresize.hpp"

namespace vsgUtil
{
    namespace detail
    {
        struct MipLevel
        {
            uint32_t width; // in texels
            uint32_t height; // in texels
            size_t offset; // in bytes, from the start of the image's data
            size_t size; // in bytes
        };

        /*
         * Lists the mip levels stored in a 2D image, from its MipmapLayout if it has one (block compressed images
         * read by vsgXchange) or else from properties.mipLevels.
         */
        inline std::vector<MipLevel> mipLevels(const vsg::Data& data)
        {
            const auto& properties = data.properties;
            auto ceilDiv = [](uint32_t value, uint32_t divisor) -> size_t { return (value + divisor - 1) / divisor; };
            auto levelSize = [&](uint32_t w, uint32_t h) {
                return data.valueSize() * ceilDiv(w, properties.blockWidth) * ceilDiv(h, properties.blockHeight);
            };

            std::vector<MipLevel> levels;
            if (auto layout = data.getMipmapLayout())
            {
                for (const auto& mipmap : *layout)
                    levels.push_back({ mipmap.x, mipmap.y, mipmap.w, levelSize(mipmap.x, mipmap.y) });
                return levels;
            }

            uint32_t w = data.width() * properties.blockWidth;
            uint32_t h = data.height() * properties.blockHeight;
            size_t offset = 0;
            for (uint8_t level = 0; level < std::max<uint8_t>(properties.mipLevels, 1); ++level)
            {
                levels.push_back({ w, h, offset, levelSize(w, h) });
                offset += levels.back().size;
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
            }
            return levels;
        }

        inline bool isBlockCompressed(const vsg::Data& data)
        {
            return data.properties.blockWidth > 1 || data.properties.blockHeight > 1;
        }

        inline bool isRgba8(VkFormat format)
        {
            return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
        }

        // Task 0089: a BC3 layer of the array's size whose levels hold the array's levels (twice the bytes each).
        inline bool bc3FitsBc1Layer(const vsg::Data& image, VkFormat arrayFormat, uint32_t width, uint32_t height,
            const std::vector<MipLevel>& imageLevels, const std::vector<MipLevel>& arrayLevels)
        {
            if (!BlockConvert::isBc1(arrayFormat) || !BlockConvert::isBc3(image.properties.format)
                || image.width() != width || image.height() != height || imageLevels.size() < arrayLevels.size())
                return false;
            for (size_t level = 0; level < arrayLevels.size(); ++level)
                if (imageLevels[level].size != arrayLevels[level].size * 2
                    || imageLevels[level].offset + imageLevels[level].size > image.dataSize())
                    return false;
            return true;
        }

        template <class ArrayType>
        vsg::ref_ptr<vsg::Data> createArray(uint32_t width, uint32_t height, uint32_t depth, size_t totalSize,
            const vsg::Data::Properties& properties, vsg::MipmapLayout* layout)
        {
            using value_type = typename ArrayType::value_type;
            auto data = static_cast<value_type*>(vsg::allocate(totalSize, vsg::ALLOCATOR_AFFINITY_DATA));
            std::memset(static_cast<void*>(data), 0, totalSize);
            return ArrayType::create(width, height, depth, data, properties, layout);
        }

        /*
         * Decodes the top mip level of a BC1, BC2 or BC3 (DXT1/3/5) image to RGBA8, as used by Morrowind's DDS
         * textures. Returns an empty vector for other formats.
         */
        inline std::vector<uint32_t> decodeBlockCompressed(const vsg::Data& image)
        {
            const VkFormat format = image.properties.format;
            const bool bc1 = format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            const bool bc1Opaque = format == VK_FORMAT_BC1_RGB_UNORM_BLOCK || format == VK_FORMAT_BC1_RGB_SRGB_BLOCK;
            const bool bc2 = format == VK_FORMAT_BC2_UNORM_BLOCK || format == VK_FORMAT_BC2_SRGB_BLOCK;
            const bool bc3 = format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK;
            if (!bc1 && !bc2 && !bc3)
                return {};

            const uint32_t blocksX = image.width();
            const uint32_t blocksY = image.height();
            const uint32_t width = blocksX * 4;
            const uint32_t height = blocksY * 4;
            const size_t blockSize = bc1 ? 8 : 16;
            const auto* src = reinterpret_cast<const uint8_t*>(image.dataPointer());
            std::vector<uint32_t> out(static_cast<size_t>(width) * height);

            auto expand565 = [](uint16_t c, uint8_t rgb[3]) {
                rgb[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
                rgb[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
                rgb[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
            };

            for (uint32_t by = 0; by < blocksY; ++by)
            {
                for (uint32_t bx = 0; bx < blocksX; ++bx)
                {
                    const uint8_t* block = src + (static_cast<size_t>(by) * blocksX + bx) * blockSize;
                    const uint8_t* colorBlock = bc1 ? block : block + 8;

                    uint8_t alpha[16];
                    std::fill(std::begin(alpha), std::end(alpha), uint8_t(255));
                    if (bc2)
                    {
                        for (int p = 0; p < 16; ++p)
                            alpha[p] = static_cast<uint8_t>(((block[p / 2] >> ((p % 2) * 4)) & 15) * 17);
                    }
                    else if (bc3)
                    {
                        uint8_t a[8] = { block[0], block[1] };
                        if (a[0] > a[1])
                            for (int k = 1; k < 7; ++k)
                                a[k + 1] = static_cast<uint8_t>(((7 - k) * a[0] + k * a[1]) / 7);
                        else
                        {
                            for (int k = 1; k < 5; ++k)
                                a[k + 1] = static_cast<uint8_t>(((5 - k) * a[0] + k * a[1]) / 5);
                            a[6] = 0;
                            a[7] = 255;
                        }
                        uint64_t bits = 0;
                        for (int k = 0; k < 6; ++k)
                            bits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
                        for (int p = 0; p < 16; ++p)
                            alpha[p] = a[(bits >> (3 * p)) & 7];
                    }

                    const uint16_t c0 = static_cast<uint16_t>(colorBlock[0] | (colorBlock[1] << 8));
                    const uint16_t c1 = static_cast<uint16_t>(colorBlock[2] | (colorBlock[3] << 8));
                    uint8_t palette[4][4];
                    expand565(c0, palette[0]);
                    expand565(c1, palette[1]);
                    palette[0][3] = palette[1][3] = 255;
                    const bool fourColors = !bc1 || c0 > c1;
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        if (fourColors)
                        {
                            palette[2][ch] = static_cast<uint8_t>((2 * palette[0][ch] + palette[1][ch]) / 3);
                            palette[3][ch] = static_cast<uint8_t>((palette[0][ch] + 2 * palette[1][ch]) / 3);
                        }
                        else
                        {
                            palette[2][ch] = static_cast<uint8_t>((palette[0][ch] + palette[1][ch]) / 2);
                            palette[3][ch] = 0;
                        }
                    }
                    palette[2][3] = 255;
                    // BC1 with an alpha bit makes index 3 transparent; the RGB-only BC1 formats keep it opaque black.
                    palette[3][3] = (fourColors || bc1Opaque) ? 255 : 0;

                    const uint32_t indices = static_cast<uint32_t>(colorBlock[4]) | (colorBlock[5] << 8)
                        | (colorBlock[6] << 16) | (static_cast<uint32_t>(colorBlock[7]) << 24);
                    for (int p = 0; p < 16; ++p)
                    {
                        const uint8_t* c = palette[(indices >> (2 * p)) & 3];
                        const uint8_t a = (bc2 || bc3) ? alpha[p] : c[3];
                        const uint32_t x = bx * 4 + p % 4;
                        const uint32_t y = by * 4 + p / 4;
                        out[static_cast<size_t>(y) * width + x] = static_cast<uint32_t>(c[0])
                            | (static_cast<uint32_t>(c[1]) << 8) | (static_cast<uint32_t>(c[2]) << 16)
                            | (static_cast<uint32_t>(a) << 24);
                    }
                }
            }
            return out;
        }

        /*
         * Nearest-neighbour copy of an RGBA8 image into a layer of another size.
         */
        inline void resampleRgba8(
            const uint32_t* src, uint32_t srcWidth, uint32_t srcHeight, uint8_t* dst, uint32_t width, uint32_t height)
        {
            auto* out = reinterpret_cast<uint32_t*>(dst);
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    out[y * width + x] = src[(y * srcHeight / height) * srcWidth + (x * srcWidth / width)];
        }

        /*
         * Nearest-neighbour copy of an uncompressed 4-byte-per-texel image into a layer of another size.
         */
        inline void resampleRgba8(const vsg::Data& image, uint8_t* dst, uint32_t width, uint32_t height)
        {
            const auto* src = reinterpret_cast<const uint32_t*>(image.dataPointer());
            auto* out = reinterpret_cast<uint32_t*>(dst);
            const uint32_t srcWidth = image.width();
            const uint32_t srcHeight = image.height();
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    out[y * width + x] = src[(y * srcHeight / height) * srcWidth + (x * srcWidth / width)];
        }

        inline bool isBc7(VkFormat format)
        {
            return format == VK_FORMAT_BC7_UNORM_BLOCK || format == VK_FORMAT_BC7_SRGB_BLOCK;
        }

        inline bool isBc1Or2Or3(VkFormat format)
        {
            return (format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC3_SRGB_BLOCK);
        }

        // Task 0089: the uncompressed formats read as RGBA8: four bytes a texel in either channel order, or three.
        inline bool isRgbaOrder(VkFormat format)
        {
            return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
        }
        inline bool isBgraOrder(VkFormat format)
        {
            return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
        }
        inline bool isRgb3(VkFormat format)
        {
            return format == VK_FORMAT_R8G8B8_UNORM || format == VK_FORMAT_R8G8B8_SRGB;
        }
        inline bool isBgr3(VkFormat format)
        {
            return format == VK_FORMAT_B8G8R8_UNORM || format == VK_FORMAT_B8G8R8_SRGB;
        }

        // True when decodeToRgba8 can read a layer of this shape (bytes a texel for the uncompressed ones).
        inline bool canDecodeToRgba8(VkFormat format, bool blockCompressed, uint32_t bytesPerTexel)
        {
            if (blockCompressed)
                return isBc1Or2Or3(format) || isBc7(format);
            return ((isRgbaOrder(format) || isBgraOrder(format)) && bytesPerTexel == 4)
                || ((isRgb3(format) || isBgr3(format)) && bytesPerTexel == 3);
        }

        /*
         * Task 0089: the top mip level of a layer as RGBA8 (BC1, BC2, BC3, BC7, and uncompressed RGBA, BGRA, RGB and
         * BGR with 8 bits a channel). Returns false, leaving `out` empty, for anything else or when the layer's data
         * is shorter than its top level.
         */
        inline bool decodeToRgba8(const vsg::Data& image, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height)
        {
            out.clear();
            const VkFormat format = image.properties.format;
            const bool blockCompressed = isBlockCompressed(image);
            const auto levels = mipLevels(image);
            if (levels.empty() || levels[0].offset != 0 || levels[0].size > image.dataSize())
                return false;
            if (!canDecodeToRgba8(
                    format, blockCompressed, blockCompressed ? 0 : static_cast<uint32_t>(image.valueSize())))
                return false;
            const auto* src = reinterpret_cast<const uint8_t*>(image.dataPointer());
            if (blockCompressed)
            {
                width = image.width() * image.properties.blockWidth;
                height = image.height() * image.properties.blockHeight;
                if (isBc7(format))
                {
                    out.resize(static_cast<size_t>(width) * height * 4);
                    const uint32_t blocksX = image.width();
                    const uint32_t blocksY = image.height();
                    uint8_t texels[64];
                    for (uint32_t by = 0; by < blocksY; ++by)
                        for (uint32_t bx = 0; bx < blocksX; ++bx)
                        {
                            Bc7::decodeBlock(src + (static_cast<size_t>(by) * blocksX + bx) * Bc7::blockBytes, texels);
                            for (uint32_t y = 0; y < 4; ++y)
                                std::memcpy(&out[((static_cast<size_t>(by) * 4 + y) * width + bx * 4) * 4],
                                    texels + y * 16, 16);
                        }
                    return true;
                }
                const auto decoded = decodeBlockCompressed(image);
                if (decoded.empty())
                    return false;
                out.resize(decoded.size() * 4);
                std::memcpy(out.data(), decoded.data(), out.size());
                return true;
            }
            width = image.width();
            height = image.height();
            const size_t count = static_cast<size_t>(width) * height;
            out.resize(count * 4);
            if (isRgbaOrder(format))
                std::memcpy(out.data(), src, count * 4);
            else if (isBgraOrder(format))
                for (size_t i = 0; i < count; ++i)
                {
                    out[i * 4] = src[i * 4 + 2];
                    out[i * 4 + 1] = src[i * 4 + 1];
                    out[i * 4 + 2] = src[i * 4];
                    out[i * 4 + 3] = src[i * 4 + 3];
                }
            else
                for (size_t i = 0; i < count; ++i)
                {
                    const bool swap = isBgr3(format);
                    out[i * 4] = src[i * 3 + (swap ? 2 : 0)];
                    out[i * 4 + 1] = src[i * 3 + 1];
                    out[i * 4 + 2] = src[i * 3 + (swap ? 0 : 2)];
                    out[i * 4 + 3] = 255;
                }
            return true;
        }
    }

    /*
     * Task 0089 (re-encoding): with `reencode` set, a layer the array cannot take as it is (another size, another
     * format, BC7, a BC1 array's uncompressed layer) is decoded, resized and, for a BC1 array, encoded again with its
     * own mip chain, instead of being filled grey. Off by default: the callers that have not asked keep the old
     * behaviour byte for byte.
     */
    struct ArrayConvertOptions
    {
        bool reencode = false;
        // A BC1 array has no alpha: layers that carry data in alpha (normal and height maps) must not be encoded into
        // one. When false, only BC1 layers (which have none) are encoded into a BC1 array; the rest of what it took
        // before stays (copies and BC3 transcodes).
        bool encodeToBc1 = true;
        unsigned threads = 0; // 0: the number of hardware threads, at most 8
    };

    /// What convertToArrayTexture did, by layer.
    struct ArrayConvertStats
    {
        size_t copied = 0; // the array's own size and format: bytes copied
        size_t transcoded = 0; // BC3 to BC1, lossless for the colour
        size_t encoded = 0; // decoded, resized and encoded to the array's block format
        size_t decoded = 0; // block compressed layer decoded into an RGBA8 array (resized if needed)
        size_t resampled = 0; // uncompressed layer resized or swizzled into an RGBA8 array
        size_t grey = 0; // could not be converted: grey fill
    };

    /*
     * Task 0089: what an array layer looks like, for deciding whether convertToArrayTexture can put it in an array of
     * another size or format without a grey fill. Texels for width and height, whatever the compression.
     */
    struct LayerShape
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool blockCompressed = false;
        uint32_t bytesPerTexel = 0; // uncompressed images only
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /*
     * True when convertToArrayTexture puts `layer` into an array whose size and format are `reference`'s with its
     * pixels (not grey): the same layer, an uncompressed 4 byte layer of the array's format at another size
     * (resampled), or a BC1, BC2 or BC3 layer in an RGBA8 array (decoded and resampled). Keep in step with that
     * function's branches. A block compressed array takes layers of its own size and format, and BC3 layers of its
     * size in a BC1 array (the colour is kept, the alpha dropped).
     */
    inline bool arrayLayerConvertible(
        const LayerShape& layer, const LayerShape& reference, const ArrayConvertOptions& options = {})
    {
        if (layer.format == reference.format && layer.width == reference.width && layer.height == reference.height)
            return true;
        if (options.reencode)
        {
            // A BC1 array and an RGBA8 array take every layer that can be decoded to RGBA8, whatever its size.
            // Without encodeToBc1 only layers that have no alpha anyway (BC1) are encoded into a BC1 array.
            const bool bc1Array = reference.blockCompressed && BlockConvert::isBc1(reference.format)
                && (options.encodeToBc1 || BlockConvert::isBc1(layer.format));
            const bool rgbaArray
                = !reference.blockCompressed && detail::isRgbaOrder(reference.format) && reference.bytesPerTexel == 4;
            if ((bc1Array || rgbaArray)
                && detail::canDecodeToRgba8(layer.format, layer.blockCompressed, layer.bytesPerTexel))
                return true;
        }
        if (reference.blockCompressed)
        {
            // BC3 in a BC1 array of its size: the colour half is a BC1 block (BlockConvert::bc3BlockToBc1).
            return BlockConvert::isBc1(reference.format) && BlockConvert::isBc3(layer.format)
                && layer.width == reference.width && layer.height == reference.height;
        }
        if (layer.format == reference.format && !layer.blockCompressed && layer.bytesPerTexel == 4)
            return true;
        if (!layer.blockCompressed || !detail::isRgba8(reference.format))
            return false;
        const bool bc1 = layer.format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && layer.format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        const bool bc2 = layer.format == VK_FORMAT_BC2_UNORM_BLOCK || layer.format == VK_FORMAT_BC2_SRGB_BLOCK;
        const bool bc3 = layer.format == VK_FORMAT_BC3_UNORM_BLOCK || layer.format == VK_FORMAT_BC3_SRGB_BLOCK;
        return bc1 || bc2 || bc3;
    }

    inline LayerShape layerShapeOf(const vsg::Data& image)
    {
        LayerShape shape;
        shape.format = image.properties.format;
        shape.blockCompressed = detail::isBlockCompressed(image);
        shape.bytesPerTexel = shape.blockCompressed ? 0 : static_cast<uint32_t>(image.valueSize());
        shape.width = image.width() * image.properties.blockWidth;
        shape.height = image.height() * image.properties.blockHeight;
        return shape;
    }

    /*
     * Converts textures to array texture to circumvent resource limits and descriptor indexing limitations.
     * openeden-vsg: the array takes the most common layer size and format rather than the first layer's (Morrowind's
     * land textures are 256x256 RGBA while OpenMW's _land_default.dds, layer 0, is a 64x64 BC1, which left every
     * other layer black). Uncompressed layers of another size are resampled, and in an uncompressed RGBA array a
     * block-compressed layer is decoded and resampled. Other mismatches are filled grey.
     * Block compressed arrays keep their layers' own mip chains, since VSG 1.1.x generates missing mipmaps by
     * blitting, which compressed formats do not support; uncompressed arrays let VSG generate them. Layers of the
     * array's size and format may have chains of different lengths (Bloodmoon's land textures go down to 1x1,
     * Morrowind's stop at 8x8): the array keeps as many levels as the shortest of them has, and each copies those
     * levels from its own chain. A layer whose levels do not lie within its dataSize() is filled grey instead.
     */
    inline vsg::ref_ptr<vsg::Data> convertToArrayTexture(
        const vsg::DataList& images, const ArrayConvertOptions& options = {}, ArrayConvertStats* stats = nullptr)
    {
        if (images.empty())
            throw std::runtime_error("!convertToArrayTexture(no layers)");
        using Key = std::tuple<uint32_t, uint32_t, VkFormat>;
        auto keyOf = [](const vsg::Data& image) {
            return Key{ image.width(), image.height(), image.properties.format };
        };
        std::map<Key, size_t> keyCounts;
        for (const auto& image : images)
            ++keyCounts[keyOf(*image)];
        const Key reference = std::max_element(keyCounts.begin(), keyCounts.end(), [](auto& a, auto& b) {
            return a.second < b.second;
        })->first;
        const vsg::Data* referenceImage = nullptr;
        for (const auto& image : images)
            if (keyOf(*image) == reference)
            {
                referenceImage = image.get();
                break;
            }

        const auto& first = *referenceImage;
        const auto width = first.width();
        const auto height = first.height();
        const auto depth = static_cast<uint32_t>(images.size());
        const bool compressed = detail::isBlockCompressed(first);
        auto allLevels = detail::mipLevels(first);
        for (const auto& image : images)
            if (keyOf(*image) == reference)
                allLevels.resize(std::min(allLevels.size(), detail::mipLevels(*image).size()));
        const std::vector<detail::MipLevel> levels
            = compressed ? allLevels : std::vector<detail::MipLevel>{ allLevels.front() };

        // The array stores each mip level for all layers in turn, as vsg::TransferTask uploads it.
        vsg::ref_ptr<vsg::MipmapLayout> layout;
        if (compressed)
            layout = vsg::MipmapLayout::create(levels.size());
        std::vector<size_t> levelOffsets;
        size_t totalSize = 0;
        for (size_t level = 0; level < levels.size(); ++level)
        {
            levelOffsets.push_back(totalSize);
            if (layout)
                layout->set(
                    level, vsg::uivec4(levels[level].width, levels[level].height, 1, static_cast<uint32_t>(totalSize)));
            totalSize += levels[level].size * depth;
        }

        auto properties = first.properties;
        properties.imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        properties.mipLevels = compressed ? static_cast<uint8_t>(levels.size()) : 0; // 0: VSG generates mipmaps
        properties.allocatorType
            = vsg::ALLOCATOR_TYPE_VSG_ALLOCATOR; // the layers are copied into vsg::allocate'd memory

        vsg::ref_ptr<vsg::Data> array;
        if (first.valueSize() == 8)
            array = detail::createArray<vsg::block64Array3D>(width, height, depth, totalSize, properties, layout);
        else if (first.valueSize() == 16)
            array = detail::createArray<vsg::block128Array3D>(width, height, depth, totalSize, properties, layout);
        else if (first.valueSize() == 4)
            array = detail::createArray<vsg::ubvec4Array3D>(width, height, depth, totalSize, properties, layout);
        if (!array)
            throw std::runtime_error("!convertToArrayTexture<" + std::string(typeid(first).name())
                + ">(VkFormat=" + std::to_string(properties.format) + ")");

        auto pDst = reinterpret_cast<uint8_t*>(array->dataPointer());
        ArrayConvertStats localStats;
        ArrayConvertStats& counts = stats ? *stats : localStats;
        // Task 0089: the layers that need a decode, a resize and maybe an encode are collected and done afterwards, on
        // several threads (each writes only its own layer's bytes).
        const bool bc1Array = compressed && BlockConvert::isBc1(properties.format);
        const bool rgbaArray = !compressed && first.valueSize() == 4 && detail::isRgbaOrder(properties.format);
        const bool reencodeArray = options.reencode && (bc1Array || rgbaArray);
        std::vector<uint32_t> jobs;
        std::vector<uint32_t> greyLayers;
        for (uint32_t i = 0; i < depth; ++i)
        {
            const auto& image = *images[i];
            // The array's size and format: copy its levels from the layer's own chain, checking each level's size
            // against the array's so a malformed header cannot make the copy read past the layer.
            const auto imageLevels = detail::mipLevels(image);
            bool levelsMatch = keyOf(image) == reference && imageLevels.size() >= levels.size();
            for (size_t level = 0; levelsMatch && level < levels.size(); ++level)
                levelsMatch = imageLevels[level].size == levels[level].size
                    && imageLevels[level].offset + imageLevels[level].size <= image.dataSize();
            if (levelsMatch)
            {
                auto pSrc = reinterpret_cast<const uint8_t*>(image.dataPointer());
                for (size_t level = 0; level < levels.size(); ++level)
                    std::memcpy(pDst + levelOffsets[level] + i * levels[level].size, pSrc + imageLevels[level].offset,
                        levels[level].size);
                ++counts.copied;
            }
            else if (compressed
                && detail::bc3FitsBc1Layer(image, properties.format, width, height, imageLevels, levels))
            {
                auto pSrc = reinterpret_cast<const uint8_t*>(image.dataPointer());
                for (size_t level = 0; level < levels.size(); ++level)
                    BlockConvert::bc3ToBc1(pSrc + imageLevels[level].offset,
                        pDst + levelOffsets[level] + i * levels[level].size,
                        levels[level].size / BlockConvert::bc1BlockBytes);
                ++counts.transcoded;
            }
            else if (reencodeArray && (!bc1Array || options.encodeToBc1 || BlockConvert::isBc1(image.properties.format))
                && detail::canDecodeToRgba8(image.properties.format, detail::isBlockCompressed(image),
                    detail::isBlockCompressed(image) ? 0 : static_cast<uint32_t>(image.valueSize())))
                jobs.push_back(i);
            else if (!reencodeArray && !compressed && image.properties.format == properties.format)
            {
                detail::resampleRgba8(image, pDst + i * levels[0].size, width, height);
                ++counts.resampled;
            }
            else if (const auto decoded = (!reencodeArray && !compressed && detail::isRgba8(properties.format))
                         ? detail::decodeBlockCompressed(image)
                         : std::vector<uint32_t>{};
                     !decoded.empty())
            {
                detail::resampleRgba8(
                    decoded.data(), image.width() * 4, image.height() * 4, pDst + i * levels[0].size, width, height);
                ++counts.decoded;
            }
            else
                greyLayers.push_back(i);
        }

        // One job: decode the layer, resize it to the array's size, and either store it (an RGBA8 array) or encode it
        // with a mip chain of its own (a BC1 array). Returns false when the layer cannot be read.
        auto runJob = [&](uint32_t i) -> bool {
            try
            {
                // The array's size in texels (width and height above count blocks in a block compressed array).
                const uint32_t targetWidth = levels[0].width;
                const uint32_t targetHeight = levels[0].height;
                std::vector<uint8_t> rgba;
                uint32_t sourceWidth = 0;
                uint32_t sourceHeight = 0;
                if (!detail::decodeToRgba8(*images[i], rgba, sourceWidth, sourceHeight))
                    return false;
                if (!bc1Array)
                {
                    uint8_t* dst = pDst + i * levels[0].size;
                    if (sourceWidth == targetWidth && sourceHeight == targetHeight)
                        std::memcpy(dst, rgba.data(), rgba.size());
                    else
                        RgbaResize::resize(rgba.data(), sourceWidth, sourceHeight, dst, targetWidth, targetHeight);
                    return true;
                }
                std::vector<uint8_t> current;
                if (sourceWidth == targetWidth && sourceHeight == targetHeight)
                    current = std::move(rgba);
                else
                {
                    current.resize(static_cast<size_t>(targetWidth) * targetHeight * 4);
                    RgbaResize::resize(
                        rgba.data(), sourceWidth, sourceHeight, current.data(), targetWidth, targetHeight);
                }
                rgba = std::vector<uint8_t>();
                uint32_t currentWidth = targetWidth;
                uint32_t currentHeight = targetHeight;
                std::vector<uint8_t> next;
                for (size_t level = 0; level < levels.size(); ++level)
                {
                    if (level > 0)
                    {
                        next.resize(static_cast<size_t>(levels[level].width) * levels[level].height * 4);
                        RgbaResize::resize(current.data(), currentWidth, currentHeight, next.data(),
                            levels[level].width, levels[level].height);
                        current.swap(next);
                        currentWidth = levels[level].width;
                        currentHeight = levels[level].height;
                    }
                    if (Bc1Encode::encodedSize(currentWidth, currentHeight) != levels[level].size)
                        return false;
                    Bc1Encode::encodeImage(current.data(), currentWidth, currentHeight,
                        pDst + levelOffsets[level] + i * levels[level].size);
                }
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };

        std::vector<char> jobOk(jobs.size(), 0);
        unsigned threadCount = options.threads ? options.threads : std::thread::hardware_concurrency();
        threadCount = std::clamp(threadCount, 1u, 8u);
        threadCount = std::min<unsigned>(threadCount, static_cast<unsigned>(std::max<size_t>(jobs.size(), 1)));
        if (threadCount <= 1)
        {
            for (size_t j = 0; j < jobs.size(); ++j)
                jobOk[j] = runJob(jobs[j]) ? 1 : 0;
        }
        else
        {
            std::atomic<size_t> nextJob{ 0 };
            auto worker = [&] {
                for (size_t j = nextJob++; j < jobs.size(); j = nextJob++)
                    jobOk[j] = runJob(jobs[j]) ? 1 : 0;
            };
            std::vector<std::thread> threads;
            for (unsigned t = 1; t < threadCount; ++t)
                threads.emplace_back(worker);
            worker();
            for (auto& thread : threads)
                thread.join();
        }
        for (size_t j = 0; j < jobs.size(); ++j)
        {
            if (jobOk[j])
            {
                if (bc1Array)
                    ++counts.encoded;
                else if (detail::isBlockCompressed(*images[jobs[j]]))
                    ++counts.decoded;
                else
                    ++counts.resampled;
            }
            else
                greyLayers.push_back(jobs[j]);
        }

        for (const uint32_t i : greyLayers)
        {
            const auto& image = *images[i];
            std::cerr << ("!convertToArrayTexture(i=" + std::to_string(i) + ", name=" + vsgUtil::getName(image)
                + ", format " + std::to_string(static_cast<int>(image.properties.format)) + " "
                + std::to_string(image.width() * image.properties.blockWidth) + "x"
                + std::to_string(image.height() * image.properties.blockHeight)
                + "): this layer's size or format does not match the other layers; using a grey fill.\n");
            for (size_t level = 0; level < levels.size(); ++level)
            {
                uint8_t* dst = pDst + levelOffsets[level] + i * levels[level].size;
                if (options.reencode && bc1Array && options.encodeToBc1)
                    BlockConvert::fillGrey(properties.format, dst, levels[level].size);
                else
                    std::memset(dst, 0x80, levels[level].size);
                // Task 0089: in a BC1 array a 0x80 byte fill is the endpoint (131, 16, 0), a dark red, not a grey.
                // The default keeps the old bytes (three existing tests pin them: expectBc1LayerGrey in
                // testarraytexture.cpp, which needs toddhow's approval to change); the re-encoding callers get a real
                // grey block.
            }
            ++counts.grey;
        }
        return array;
    }
}

#endif
