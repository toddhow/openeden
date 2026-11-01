#include "internaltexture.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
// Visitor.h and ConstVisitor.h define Array2D<T>::accept.
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Visitor.h>

#include <components/nif/data.hpp>

namespace vsgAdapters
{
    namespace
    {
        using Format = Nif::NiPixelFormat::Format;

        std::size_t blockSize(Format format)
        {
            return format == Format::DXT1 ? 8 : 16;
        }

        /// Bytes of the first level as stored in the NIF, or 0 for an unsupported format.
        std::size_t levelSize(Format format, uint32_t width, uint32_t height)
        {
            switch (format)
            {
                case Format::RGB:
                case Format::BGR:
                    return std::size_t(width) * height * 3;
                case Format::RGBA:
                case Format::BGRA:
                    return std::size_t(width) * height * 4;
                case Format::Palette:
                case Format::PaletteAlpha:
                    return std::size_t(width) * height;
                case Format::DXT1:
                case Format::DXT3:
                case Format::DXT5:
                    return std::size_t((width + 3) / 4) * ((height + 3) / 4) * blockSize(format);
            }
            return 0;
        }

        /// BC blocks of every NIF level VSG can lay out (the renderer cannot generate levels of compressed formats):
        /// levels are kept while each has the size VSG expects, half the previous, and lies inside the pixel data.
        template <class ArrayT>
        vsg::ref_ptr<vsg::Data> readCompressed(const Nif::NiPixelData& pixelData, Format format, VkFormat vkFormat)
        {
            const auto& levels = pixelData.mMipmaps;
            const uint32_t blocksWide = (levels.front().mWidth + 3) / 4;
            const uint32_t blocksHigh = (levels.front().mHeight + 3) / 4;
            const std::size_t bytesPerBlock = blockSize(format);

            std::vector<std::pair<std::size_t, std::size_t>> copies; // offset, bytes
            std::size_t total = 0;
            for (std::size_t i = 0; i < levels.size() && i < 32; ++i)
            {
                const uint32_t expectedWide = (std::max(1u, (blocksWide * 4) >> i) + 3) / 4;
                const uint32_t expectedHigh = (std::max(1u, (blocksHigh * 4) >> i) + 3) / 4;
                if ((levels[i].mWidth + 3) / 4 != expectedWide || (levels[i].mHeight + 3) / 4 != expectedHigh)
                    break;
                const std::size_t bytes = std::size_t(expectedWide) * expectedHigh * bytesPerBlock;
                if (std::size_t(levels[i].mOffset) + bytes > pixelData.mData.size())
                    break;
                copies.emplace_back(levels[i].mOffset, bytes);
                total += bytes;
            }

            auto storage = vsg::ubyteArray::create(total);
            std::size_t position = 0;
            for (const auto& [offset, bytes] : copies)
            {
                std::memcpy(storage->data() + position, pixelData.mData.data() + offset, bytes);
                position += bytes;
            }

            vsg::Data::Properties properties{ vkFormat };
            properties.blockWidth = 4;
            properties.blockHeight = 4;
            properties.mipLevels = static_cast<uint8_t>(copies.size());
            return ArrayT::create(storage, 0, static_cast<uint32_t>(bytesPerBlock), blocksWide, blocksHigh, properties);
        }
    }

    vsg::ref_ptr<vsg::Data> readInternalTexture(const Nif::NiPixelData& pixelData, std::string& error)
    {
        if (pixelData.mMipmaps.empty())
        {
            error = "internal texture has no mipmap levels";
            return {};
        }
        const Format format = pixelData.mPixelFormat.mFormat;
        const Nif::NiPixelData::Mipmap& level = pixelData.mMipmaps.front();
        const uint32_t width = level.mWidth;
        const uint32_t height = level.mHeight;
        if (width == 0 || height == 0)
        {
            error = "internal texture width and height must be non-zero";
            return {};
        }
        const std::size_t size = levelSize(format, width, height);
        if (size == 0)
        {
            error = "unhandled internal pixel format " + std::to_string(static_cast<uint32_t>(format));
            return {};
        }
        if (std::size_t(level.mOffset) + size > pixelData.mData.size())
        {
            error = "internal texture's mipmap data out of bounds";
            return {};
        }
        const uint8_t* source = pixelData.mData.data() + level.mOffset;

        switch (format)
        {
            case Format::RGBA:
            case Format::BGRA:
            {
                auto image = vsg::ubvec4Array2D::create(width, height,
                    vsg::Data::Properties{
                        format == Format::RGBA ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM });
                std::memcpy(static_cast<void*>(image->data()), source, size);
                return image;
            }
            case Format::RGB:
            case Format::BGR:
            {
                auto image = vsg::ubvec4Array2D::create(width, height,
                    vsg::Data::Properties{
                        format == Format::RGB ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM });
                for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
                    image->at(i) = vsg::ubvec4(source[i * 3], source[i * 3 + 1], source[i * 3 + 2], 255);
                return image;
            }
            case Format::Palette:
            case Format::PaletteAlpha:
            {
                if (pixelData.mPalette.empty() || pixelData.mPixelFormat.mBitsPerPixel != 8)
                {
                    error = "palettized internal texture is invalid";
                    return {};
                }
                const auto& colors = pixelData.mPalette->mColors;
                const bool alpha = format == Format::PaletteAlpha;
                auto image
                    = vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
                for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
                {
                    // Palette colours are stored as bytes R, G, B, A.
                    uint8_t rgba[4];
                    std::memcpy(rgba, &colors[source[i]], sizeof(rgba));
                    image->at(i) = vsg::ubvec4(rgba[0], rgba[1], rgba[2], alpha ? rgba[3] : uint8_t(255));
                }
                return image;
            }
            case Format::DXT1:
                return readCompressed<vsg::block64Array2D>(pixelData, format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
            case Format::DXT3:
                return readCompressed<vsg::block128Array2D>(pixelData, format, VK_FORMAT_BC2_UNORM_BLOCK);
            case Format::DXT5:
                return readCompressed<vsg::block128Array2D>(pixelData, format, VK_FORMAT_BC3_UNORM_BLOCK);
        }
        return {};
    }
}
