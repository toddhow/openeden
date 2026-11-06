#ifndef OPENEDEN_VSGUTIL_BC4_H
#define OPENEDEN_VSGUTIL_BC4_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

/*
 * Task 0089: a BC4 (one channel, UNORM) decoder and encoder, so height layers of another size can join the terrain's
 * height array. Pure functions, no vsg types, no allocation, no shared state; the result depends only on the input.
 *
 * A block is 8 bytes: two 8 bit endpoints a0 and a1 and sixteen 3 bit indices (48 bits, texel p at bit 3p, little
 * endian). With a0 > a1 the palette is a0, a1 and six values between ((7 - k) a0 + k a1) / 7; otherwise a0, a1, four
 * values between (((5 - k) a0 + k a1) / 5), 0 and 255. The in-between values are rounded to the nearest integer (the
 * GPU computes them in floating point and rounds the same way for UNORM).
 *
 * The encoder tries both modes. The eight value mode takes the block's smallest and largest value as endpoints; the six
 * value mode takes those of the texels that are neither 0 nor 255 (the mode's own extra values). Each is refined twice
 * by least squares on its assignment of texels to palette entries; the candidate with the least squared error wins
 * (the eight value mode on a tie).
 */
namespace vsgUtil::Bc4
{
    inline constexpr size_t blockBytes = 8;

    namespace detail
    {
        inline void makePalette(int a0, int a1, uint8_t (&palette)[8])
        {
            palette[0] = static_cast<uint8_t>(a0);
            palette[1] = static_cast<uint8_t>(a1);
            if (a0 > a1)
            {
                for (int k = 1; k <= 6; ++k)
                {
                    palette[k + 1] = static_cast<uint8_t>(((7 - k) * a0 + k * a1 + 3) / 7);
                }
            }
            else
            {
                for (int k = 1; k <= 4; ++k)
                {
                    palette[k + 1] = static_cast<uint8_t>(((5 - k) * a0 + k * a1 + 2) / 5);
                }
                palette[6] = 0;
                palette[7] = 255;
            }
        }

        struct Fit
        {
            int a0 = 0;
            int a1 = 0;
            uint64_t indices = 0;
            uint32_t error = 0xffffffffu;
        };

        // The nearest palette entry for each texel, and the squared error of that assignment.
        inline Fit evaluate(const uint8_t (&texel)[16], int a0, int a1)
        {
            uint8_t palette[8];
            makePalette(a0, a1, palette);
            Fit fit;
            fit.a0 = a0;
            fit.a1 = a1;
            uint32_t error = 0;
            uint64_t indices = 0;
            for (int i = 0; i < 16; ++i)
            {
                int bestIndex = 0;
                int bestDistance = 0x7fffffff;
                for (int k = 0; k < 8; ++k)
                {
                    const int d = static_cast<int>(texel[i]) - static_cast<int>(palette[k]);
                    const int distance = d * d;
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestIndex = k;
                    }
                }
                error += static_cast<uint32_t>(bestDistance);
                indices |= static_cast<uint64_t>(bestIndex) << (3 * i);
            }
            fit.indices = indices;
            fit.error = error;
            return fit;
        }

        // Two rounds of least squares: the endpoints that best reproduce the texels given their current indices (the
        // weights are those of the palette entries the fit's mode builds from the endpoints; 0 and 255 are fixed).
        inline Fit refine(const uint8_t (&texel)[16], Fit best)
        {
            constexpr int rounds = 2;
            for (int round = 0; round < rounds && best.error > 0; ++round)
            {
                const bool eight = best.a0 > best.a1;
                // Weight of a0 in palette entry k (the weight of a1 is one minus it); -1 for the fixed entries.
                float weightOfA0[8];
                weightOfA0[0] = 1.f;
                weightOfA0[1] = 0.f;
                for (int k = 1; k <= (eight ? 6 : 4); ++k)
                {
                    weightOfA0[k + 1] = eight ? static_cast<float>(7 - k) / 7.f : static_cast<float>(5 - k) / 5.f;
                }
                if (!eight)
                {
                    weightOfA0[6] = -1.f;
                    weightOfA0[7] = -1.f;
                }
                float a00 = 0.f;
                float a01 = 0.f;
                float a11 = 0.f;
                float b0 = 0.f;
                float b1 = 0.f;
                for (int i = 0; i < 16; ++i)
                {
                    const float a = weightOfA0[(best.indices >> (3 * i)) & 7];
                    if (a < 0.f)
                    {
                        continue;
                    }
                    const float b = 1.f - a;
                    const float v = static_cast<float>(texel[i]);
                    a00 += a * a;
                    a01 += a * b;
                    a11 += b * b;
                    b0 += a * v;
                    b1 += b * v;
                }
                const float determinant = a00 * a11 - a01 * a01;
                if (determinant < 1e-4f)
                {
                    break;
                }
                const float end0 = std::clamp((b0 * a11 - b1 * a01) / determinant, 0.f, 255.f);
                const float end1 = std::clamp((b1 * a00 - b0 * a01) / determinant, 0.f, 255.f);
                const Fit refined = evaluate(texel, static_cast<int>(end0 + 0.5f), static_cast<int>(end1 + 0.5f));
                if (refined.error >= best.error)
                {
                    break;
                }
                best = refined;
            }
            return best;
        }
    }

    /// Bytes of a BC4 image of width x height texels (4x4 blocks, partial blocks at the edges included).
    inline size_t encodedSize(uint32_t width, uint32_t height)
    {
        return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
    }

    /// Decodes one block to 16 texels (row-major, 4 per row).
    inline void decodeBlock(const uint8_t* block, uint8_t* out)
    {
        uint8_t palette[8];
        detail::makePalette(block[0], block[1], palette);
        uint64_t bits = 0;
        for (int k = 0; k < 6; ++k)
        {
            bits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
        }
        for (int p = 0; p < 16; ++p)
        {
            out[p] = palette[(bits >> (3 * p)) & 7];
        }
    }

    /// Decodes an image of width x height texels (row-major, no padding); partial blocks at the edges are clipped.
    inline void decodeImage(const uint8_t* blocks, uint32_t width, uint32_t height, uint8_t* out)
    {
        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        uint8_t texels[16];
        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                decodeBlock(blocks + (static_cast<size_t>(by) * blocksX + bx) * blockBytes, texels);
                for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
                {
                    for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                    {
                        out[static_cast<size_t>(by * 4 + y) * width + bx * 4 + x] = texels[y * 4 + x];
                    }
                }
            }
        }
    }

    /// Encodes 16 texels (row-major) to one 8 byte block.
    inline void encodeBlock(const uint8_t* texels, uint8_t* block)
    {
        uint8_t texel[16];
        std::memcpy(texel, texels, 16);
        int low = 255;
        int high = 0;
        int innerLow = 255;
        int innerHigh = 0;
        bool anyInner = false;
        for (const uint8_t v : texel)
        {
            low = std::min<int>(low, v);
            high = std::max<int>(high, v);
            if (v != 0 && v != 255)
            {
                anyInner = true;
                innerLow = std::min<int>(innerLow, v);
                innerHigh = std::max<int>(innerHigh, v);
            }
        }

        // The eight value mode: a0 > a1. A block of one value has equal endpoints (the six value mode decodes index 0
        // as a0 as well).
        detail::Fit best = detail::refine(texel, detail::evaluate(texel, high, low));
        if (best.error > 0 && anyInner)
        {
            const detail::Fit six = detail::refine(texel, detail::evaluate(texel, innerLow, innerHigh));
            if (six.error < best.error)
            {
                best = six;
            }
        }

        block[0] = static_cast<uint8_t>(best.a0);
        block[1] = static_cast<uint8_t>(best.a1);
        for (int k = 0; k < 6; ++k)
        {
            block[2 + k] = static_cast<uint8_t>((best.indices >> (8 * k)) & 255);
        }
    }

    /// Encodes a whole image; `out` must have encodedSize() bytes. Texels beyond the edge repeat the edge texel.
    inline void encodeImage(const uint8_t* image, uint32_t width, uint32_t height, uint8_t* out)
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        uint8_t texels[16];
        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                for (uint32_t y = 0; y < 4; ++y)
                {
                    const uint32_t sy = std::min(by * 4 + y, height - 1);
                    for (uint32_t x = 0; x < 4; ++x)
                    {
                        const uint32_t sx = std::min(bx * 4 + x, width - 1);
                        texels[y * 4 + x] = image[static_cast<size_t>(sy) * width + sx];
                    }
                }
                encodeBlock(texels, out + (static_cast<size_t>(by) * blocksX + bx) * blockBytes);
            }
        }
    }
}

#endif
