#ifndef OPENEDEN_COMPONENTS_TESTS_TEXTURETESTUTIL_H
#define OPENEDEN_COMPONENTS_TESTS_TEXTURETESTUTIL_H

// Task 0049: generated images for the texture downscaler's tests. Nothing here reads a file or needs a device.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <vsg/core/Allocator.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Array3D.h>
#include <vsg/core/MipmapLayout.h>
#include <vsg/io/Options.h>
#include <vsg/io/mem_stream.h>
#include <vsgXchange/images.h>

namespace TextureTest
{
    enum class DdsFormat
    {
        Bc1Legacy, // FOURCC "DXT1"
        Bc7Dx10, // DX10 header, BC7_UNORM
        Rgba8Legacy, // 32 bit RGBA masks
    };

    inline uint32_t blocks(uint32_t texels)
    {
        return std::max(1u, (texels + 3) / 4);
    }

    // The bytes of one level of a DDS made by makeDds.
    inline size_t ddsLevelBytes(DdsFormat format, uint32_t w, uint32_t h)
    {
        switch (format)
        {
            case DdsFormat::Bc1Legacy:
                return size_t(blocks(w)) * blocks(h) * 8;
            case DdsFormat::Bc7Dx10:
                return size_t(blocks(w)) * blocks(h) * 16;
            case DdsFormat::Rgba8Legacy:
                return size_t(w) * h * 4;
        }
        return 0;
    }

    // The byte at position `i` of level `level`: different in every level, so a wrong copy shows.
    inline uint8_t ddsPattern(int level, size_t i)
    {
        return static_cast<uint8_t>((level + 1) * 29 + i * 7);
    }

    // A DDS file in memory with `mips` levels (level 0 is w by h; sizes follow max(1, size / 2)).
    inline std::vector<uint8_t> makeDds(DdsFormat format, uint32_t w, uint32_t h, uint32_t mips)
    {
        const bool dx10 = format == DdsFormat::Bc7Dx10;
        std::vector<uint8_t> bytes(4 + 124 + (dx10 ? 20 : 0), 0);
        const auto put = [&](size_t at, uint32_t v) { std::memcpy(&bytes[at], &v, 4); };
        std::memcpy(&bytes[0], "DDS ", 4);
        put(4, 124);
        const bool compressed = format != DdsFormat::Rgba8Legacy;
        put(8, 0x1 | 0x2 | 0x4 | 0x1000 | (mips > 1 ? 0x20000 : 0) | (compressed ? 0x80000 : 0x8));
        put(12, h);
        put(16, w);
        put(20, compressed ? static_cast<uint32_t>(ddsLevelBytes(format, w, h)) : w * 4);
        put(28, mips);
        put(76, 32);
        if (format == DdsFormat::Bc1Legacy)
        {
            put(80, 0x4);
            std::memcpy(&bytes[84], "DXT1", 4);
        }
        else if (format == DdsFormat::Bc7Dx10)
        {
            put(80, 0x4);
            std::memcpy(&bytes[84], "DX10", 4);
        }
        else
        {
            put(80, 0x41); // RGB and ALPHAPIXELS
            put(88, 32);
            put(92, 0x000000ff);
            put(96, 0x0000ff00);
            put(100, 0x00ff0000);
            put(104, 0xff000000);
        }
        put(108, 0x1000 | (mips > 1 ? 0x400008 : 0));
        if (dx10)
        {
            put(128, 98); // DXGI_FORMAT_BC7_UNORM
            put(132, 3); // Texture2D
            put(140, 1);
        }
        uint32_t lw = w;
        uint32_t lh = h;
        for (uint32_t level = 0; level < std::max(mips, 1u); ++level)
        {
            const size_t size = ddsLevelBytes(format, lw, lh);
            for (size_t i = 0; i < size; ++i)
                bytes.push_back(ddsPattern(static_cast<int>(level), i));
            lw = std::max(1u, lw / 2);
            lh = std::max(1u, lh / 2);
        }
        return bytes;
    }

    inline vsg::ref_ptr<vsg::Data> readDds(const std::vector<uint8_t>& bytes)
    {
        vsg::mem_stream in(bytes.data(), bytes.size());
        auto options = vsg::Options::create();
        options->extensionHint = ".dds";
        vsgXchange::dds reader;
        auto object = reader.read(in, options);
        return vsg::ref_ptr<vsg::Data>(object ? object.cast<vsg::Data>() : nullptr);
    }

    // A single-level or layout-less image with `levels` contiguous levels, filled with ddsPattern. `T` is the
    // Array2D's element type, so the element size is sizeof(T); blockWidth/blockHeight are 1 for uncompressed.
    template <class ArrayType>
    vsg::ref_ptr<vsg::Data> makeChain(
        uint32_t w, uint32_t h, int levels, VkFormat format, uint8_t blockSize = 1, bool pattern = true)
    {
        using Value = typename ArrayType::value_type;
        size_t total = 0;
        uint32_t lw = w;
        uint32_t lh = h;
        for (int level = 0; level < std::max(levels, 1); ++level)
        {
            total += size_t((lw + blockSize - 1) / blockSize) * ((lh + blockSize - 1) / blockSize) * sizeof(Value);
            lw = std::max(1u, lw / 2);
            lh = std::max(1u, lh / 2);
        }
        auto* memory = static_cast<uint8_t*>(vsg::allocate(total, vsg::ALLOCATOR_AFFINITY_DATA));
        size_t at = 0;
        lw = w;
        lh = h;
        for (int level = 0; level < std::max(levels, 1); ++level)
        {
            const size_t size
                = size_t((lw + blockSize - 1) / blockSize) * ((lh + blockSize - 1) / blockSize) * sizeof(Value);
            for (size_t i = 0; i < size; ++i)
                memory[at + i] = pattern ? ddsPattern(level, i) : 0;
            at += size;
            lw = std::max(1u, lw / 2);
            lh = std::max(1u, lh / 2);
        }
        vsg::Data::Properties properties(format);
        properties.mipLevels = static_cast<uint8_t>(levels);
        properties.blockWidth = blockSize;
        properties.blockHeight = blockSize;
        return ArrayType::create((w + blockSize - 1) / blockSize, (h + blockSize - 1) / blockSize,
            reinterpret_cast<Value*>(memory), properties);
    }

    // The bytes of an image, from `offset`.
    inline std::vector<uint8_t> bytesFrom(const vsg::Data& data, size_t offset)
    {
        const auto* p = static_cast<const uint8_t*>(data.dataPointer());
        return std::vector<uint8_t>(p + offset, p + data.dataSize());
    }
}

#endif
