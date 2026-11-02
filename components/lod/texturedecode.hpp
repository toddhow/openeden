#ifndef OPENMW_COMPONENTS_LOD_TEXTUREDECODE_H
#define OPENMW_COMPONENTS_LOD_TEXTUREDECODE_H

// OpenEden world LOD (task 0055, albedo bake). Decoding the first mip level of a land texture to plain RGBA8 on the
// CPU, so that the baker can blend the terrain layers. Pure code. Untrusted input: a size that does not fit the byte
// span is refused, never read past.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Lod
{
    /// Plain 8-bit RGBA, row 0 first (the first row of the file), four bytes a pixel.
    struct Rgba8Image
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint8_t> mPixels;
    };

    enum class SourceFormat : std::uint8_t
    {
        Rgba8, // R, G, B, A bytes
        Bgra8, // B, G, R, A bytes
        Rgb8, // three bytes, alpha 255
        Bgr8, // three bytes, alpha 255
        Grey8, // one byte copied to R, G and B, alpha 255
        Bc1, // DXT1: 8 bytes per 4 x 4 block, 4-colour and 3-colour+transparent modes
        Bc2, // DXT3: 16 bytes per block, explicit 4-bit alpha
        Bc3, // DXT5: 16 bytes per block, interpolated alpha
    };

    /// The bytes the first mip level of this format and size takes (block formats round up to whole 4 x 4 blocks).
    /// 0 for a zero width or height.
    std::uint64_t mipLevelBytes(SourceFormat format, std::uint32_t width, std::uint32_t height) noexcept;

    /// Decodes level 0. `bytes` may be longer than the level (more mip levels follow). Returns false, and leaves `out`
    /// empty, when width or height is 0, either is above 16384, or the span is shorter than mipLevelBytes. The block
    /// formats decode as the Direct3D formats do (BC1: colour 0 > colour 1 gives four colours, otherwise three and a
    /// transparent black; the 565 endpoints expand by bit replication).
    bool decodeTexture(SourceFormat format, std::uint32_t width, std::uint32_t height,
        std::span<const std::uint8_t> bytes, Rgba8Image& out);
}

#endif
