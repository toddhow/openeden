#ifndef OPENEDEN_VSGUTIL_BLOCKCONVERT_H
#define OPENEDEN_VSGUTIL_BLOCKCONVERT_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan_core.h>

/*
 * Task 0089: byte-level conversions between block compressed layers of terrain arrays. Pure functions on raw blocks;
 * no vsg types.
 */
namespace vsgUtil::BlockConvert
{
    constexpr size_t bc1BlockBytes = 8;
    constexpr size_t bc3BlockBytes = 16;

    inline bool isBc1(VkFormat format)
    {
        return format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    }

    inline bool isBc3(VkFormat format)
    {
        return format == VK_FORMAT_BC3_UNORM_BLOCK || format == VK_FORMAT_BC3_SRGB_BLOCK;
    }

    /*
     * The colour half of a BC3 block (its last 8 bytes) is a BC1 block that is always decoded in four colour mode,
     * while a BC1 block is in four colour mode only when its first endpoint is greater than its second. The block is
     * copied as it is when that holds; when the endpoints are in the other order they are swapped and the indices
     * mapped (0 and 1, 2 and 3 swap places), which gives the same four colours; equal endpoints are one colour, so
     * every index becomes 0. The decoded colour of every texel is exactly the BC3 block's; the alpha half is dropped.
     * `bc3` is 16 bytes, `bc1` 8.
     */
    inline void bc3BlockToBc1(const uint8_t* bc3, uint8_t* bc1)
    {
        const uint8_t* colour = bc3 + 8;
        const uint16_t c0 = static_cast<uint16_t>(colour[0] | (colour[1] << 8));
        const uint16_t c1 = static_cast<uint16_t>(colour[2] | (colour[3] << 8));
        if (c0 > c1)
        {
            std::memcpy(bc1, colour, bc1BlockBytes);
            return;
        }
        if (c0 == c1)
        {
            std::memcpy(bc1, colour, 4);
            std::memset(bc1 + 4, 0, 4);
            return;
        }
        bc1[0] = colour[2];
        bc1[1] = colour[3];
        bc1[2] = colour[0];
        bc1[3] = colour[1];
        for (int row = 0; row < 4; ++row)
        {
            // Two bits a texel, four texels a byte: i xor 1 swaps 0 and 1, and 2 and 3.
            bc1[4 + row] = static_cast<uint8_t>(colour[4 + row] ^ 0x55);
        }
    }

    /// `blockCount` blocks of BC3 to as many of BC1.
    inline void bc3ToBc1(const uint8_t* bc3, uint8_t* bc1, size_t blockCount)
    {
        for (size_t i = 0; i < blockCount; ++i)
            bc3BlockToBc1(bc3 + i * bc3BlockBytes, bc1 + i * bc1BlockBytes);
    }

    /*
     * Fills `byteCount` bytes of blocks of `format` with a mid grey (opaque for the formats with alpha). A byte fill of
     * 0x80, which a grey stands for in an 8 bit image, is a RED in BC1 (the endpoint 0x8080 is r 16, g 4, b 0 in
     * RGB565), which is what the terrain showed. Formats this does not know are filled with 0x80 as before; returns
     * whether the format was known.
     */
    inline bool fillGrey(VkFormat format, uint8_t* dst, size_t byteCount)
    {
        // RGB565 0x8410: r 16, g 32, b 16, which decodes to (132, 130, 132).
        static constexpr uint8_t bc1Grey[bc1BlockBytes] = { 0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0 };
        static constexpr uint8_t opaqueAlphaBc2[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        static constexpr uint8_t opaqueAlphaBc3[8] = { 0xff, 0xff, 0, 0, 0, 0, 0, 0 }; // both endpoints 255
        const bool bc2 = format == VK_FORMAT_BC2_UNORM_BLOCK || format == VK_FORMAT_BC2_SRGB_BLOCK;
        if (!isBc1(format) && !bc2 && !isBc3(format))
        {
            std::memset(dst, 0x80, byteCount);
            return false;
        }
        const size_t blockBytes = isBc1(format) ? bc1BlockBytes : bc3BlockBytes;
        for (size_t at = 0; at + blockBytes <= byteCount; at += blockBytes)
        {
            uint8_t* block = dst + at;
            if (isBc1(format))
            {
                std::memcpy(block, bc1Grey, bc1BlockBytes);
            }
            else
            {
                std::memcpy(block, bc2 ? opaqueAlphaBc2 : opaqueAlphaBc3, 8);
                std::memcpy(block + 8, bc1Grey, bc1BlockBytes);
            }
        }
        return true;
    }
}

#endif
