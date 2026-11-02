#include "texturedecode.hpp"

#include <algorithm>
#include <utility>

namespace Lod
{
    namespace
    {
        constexpr std::uint32_t maxTextureSide = 16384;

        struct Colour
        {
            std::uint8_t mR = 0, mG = 0, mB = 0, mA = 255;
        };

        std::uint8_t expand5(std::uint32_t v)
        {
            return static_cast<std::uint8_t>((v << 3) | (v >> 2));
        }

        std::uint8_t expand6(std::uint32_t v)
        {
            return static_cast<std::uint8_t>((v << 2) | (v >> 4));
        }

        Colour decode565(std::uint16_t value)
        {
            Colour colour;
            colour.mR = expand5((value >> 11) & 31u);
            colour.mG = expand6((value >> 5) & 63u);
            colour.mB = expand5(value & 31u);
            return colour;
        }

        std::uint8_t mix(std::uint32_t a, std::uint32_t wa, std::uint32_t b, std::uint32_t wb, std::uint32_t divisor)
        {
            return static_cast<std::uint8_t>((a * wa + b * wb) / divisor);
        }

        // The colour part of a BC1, BC2 or BC3 block: 8 bytes at `block`. `allowThreeColour` is true only for BC1.
        void colourPalette(const std::uint8_t* block, bool allowThreeColour, Colour (&palette)[4])
        {
            const std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
            palette[0] = decode565(c0);
            palette[1] = decode565(c1);
            if (c0 > c1 || !allowThreeColour)
            {
                palette[2] = Colour{ mix(palette[0].mR, 2, palette[1].mR, 1, 3),
                    mix(palette[0].mG, 2, palette[1].mG, 1, 3), mix(palette[0].mB, 2, palette[1].mB, 1, 3), 255 };
                palette[3] = Colour{ mix(palette[0].mR, 1, palette[1].mR, 2, 3),
                    mix(palette[0].mG, 1, palette[1].mG, 2, 3), mix(palette[0].mB, 1, palette[1].mB, 2, 3), 255 };
            }
            else
            {
                palette[2] = Colour{ mix(palette[0].mR, 1, palette[1].mR, 1, 2),
                    mix(palette[0].mG, 1, palette[1].mG, 1, 2), mix(palette[0].mB, 1, palette[1].mB, 1, 2), 255 };
                palette[3] = Colour{ 0, 0, 0, 0 };
            }
        }

        void putPixel(Rgba8Image& image, std::uint32_t x, std::uint32_t y, const Colour& colour, int alpha)
        {
            if (x >= image.mWidth || y >= image.mHeight)
                return;
            std::uint8_t* pixel = &image.mPixels[(std::size_t{ y } * image.mWidth + x) * 4];
            pixel[0] = colour.mR;
            pixel[1] = colour.mG;
            pixel[2] = colour.mB;
            pixel[3] = alpha < 0 ? colour.mA : static_cast<std::uint8_t>(alpha);
        }

        void decodeBlocks(SourceFormat format, std::span<const std::uint8_t> bytes, Rgba8Image& image)
        {
            const std::uint32_t blocksX = (image.mWidth + 3) / 4;
            const std::uint32_t blocksY = (image.mHeight + 3) / 4;
            const std::size_t blockSize = format == SourceFormat::Bc1 ? 8 : 16;
            for (std::uint32_t by = 0; by < blocksY; ++by)
            {
                for (std::uint32_t bx = 0; bx < blocksX; ++bx)
                {
                    const std::uint8_t* block = bytes.data() + (std::size_t{ by } * blocksX + bx) * blockSize;
                    const std::uint8_t* colourBlock = format == SourceFormat::Bc1 ? block : block + 8;
                    Colour palette[4];
                    colourPalette(colourBlock, format == SourceFormat::Bc1, palette);

                    int alphas[16];
                    for (int& alpha : alphas)
                        alpha = -1; // the colour's own alpha (BC1)
                    if (format == SourceFormat::Bc2)
                    {
                        for (int i = 0; i < 16; ++i)
                        {
                            const int nibble = (block[i / 2] >> (4 * (i % 2))) & 15;
                            alphas[i] = nibble * 17;
                        }
                    }
                    else if (format == SourceFormat::Bc3)
                    {
                        const int a0 = block[0];
                        const int a1 = block[1];
                        int table[8];
                        table[0] = a0;
                        table[1] = a1;
                        if (a0 > a1)
                        {
                            for (int i = 2; i < 8; ++i)
                                table[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
                        }
                        else
                        {
                            for (int i = 2; i < 6; ++i)
                                table[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
                            table[6] = 0;
                            table[7] = 255;
                        }
                        std::uint64_t bits = 0;
                        for (int i = 0; i < 6; ++i)
                            bits |= std::uint64_t{ block[2 + i] } << (8 * i);
                        for (int i = 0; i < 16; ++i)
                            alphas[i] = table[(bits >> (3 * i)) & 7u];
                    }

                    for (std::uint32_t py = 0; py < 4; ++py)
                    {
                        const std::uint8_t row = colourBlock[4 + py];
                        for (std::uint32_t px = 0; px < 4; ++px)
                        {
                            const Colour& colour = palette[(row >> (2 * px)) & 3u];
                            putPixel(image, bx * 4 + px, by * 4 + py, colour, alphas[py * 4 + px]);
                        }
                    }
                }
            }
        }
    }

    std::uint64_t mipLevelBytes(SourceFormat format, std::uint32_t width, std::uint32_t height) noexcept
    {
        if (width == 0 || height == 0)
            return 0;
        const std::uint64_t w = width;
        const std::uint64_t h = height;
        switch (format)
        {
            case SourceFormat::Rgba8:
            case SourceFormat::Bgra8:
                return w * h * 4;
            case SourceFormat::Rgb8:
            case SourceFormat::Bgr8:
                return w * h * 3;
            case SourceFormat::Grey8:
                return w * h;
            case SourceFormat::Bc1:
                return ((w + 3) / 4) * ((h + 3) / 4) * 8;
            case SourceFormat::Bc2:
            case SourceFormat::Bc3:
                return ((w + 3) / 4) * ((h + 3) / 4) * 16;
        }
        return 0;
    }

    bool decodeTexture(SourceFormat format, std::uint32_t width, std::uint32_t height,
        std::span<const std::uint8_t> bytes, Rgba8Image& out)
    {
        out = {};
        if (width == 0 || height == 0 || width > maxTextureSide || height > maxTextureSide)
            return false;
        if (bytes.size() < mipLevelBytes(format, width, height))
            return false;
        Rgba8Image image;
        image.mWidth = width;
        image.mHeight = height;
        image.mPixels.assign(std::size_t{ width } * height * 4, 255);
        const std::size_t count = std::size_t{ width } * height;
        switch (format)
        {
            case SourceFormat::Rgba8:
                std::copy_n(bytes.begin(), count * 4, image.mPixels.begin());
                break;
            case SourceFormat::Bgra8:
                for (std::size_t i = 0; i < count; ++i)
                {
                    image.mPixels[4 * i] = bytes[4 * i + 2];
                    image.mPixels[4 * i + 1] = bytes[4 * i + 1];
                    image.mPixels[4 * i + 2] = bytes[4 * i];
                    image.mPixels[4 * i + 3] = bytes[4 * i + 3];
                }
                break;
            case SourceFormat::Rgb8:
                for (std::size_t i = 0; i < count; ++i)
                {
                    image.mPixels[4 * i] = bytes[3 * i];
                    image.mPixels[4 * i + 1] = bytes[3 * i + 1];
                    image.mPixels[4 * i + 2] = bytes[3 * i + 2];
                }
                break;
            case SourceFormat::Bgr8:
                for (std::size_t i = 0; i < count; ++i)
                {
                    image.mPixels[4 * i] = bytes[3 * i + 2];
                    image.mPixels[4 * i + 1] = bytes[3 * i + 1];
                    image.mPixels[4 * i + 2] = bytes[3 * i];
                }
                break;
            case SourceFormat::Grey8:
                for (std::size_t i = 0; i < count; ++i)
                {
                    image.mPixels[4 * i] = bytes[i];
                    image.mPixels[4 * i + 1] = bytes[i];
                    image.mPixels[4 * i + 2] = bytes[i];
                }
                break;
            case SourceFormat::Bc1:
            case SourceFormat::Bc2:
            case SourceFormat::Bc3:
                decodeBlocks(format, bytes, image);
                break;
        }
        out = std::move(image);
        return true;
    }
}
