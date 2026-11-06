#ifndef OPENEDEN_VSGUTIL_BC1ENCODE_H
#define OPENEDEN_VSGUTIL_BC1ENCODE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

/*
 * Task 0089: a BC1 (DXT1) encoder for RGBA8 images, so layers that are not BC1 can join a BC1 terrain array. Pure
 * functions, no vsg types, no allocation, no shared state; the result depends only on the input.
 *
 * Per 4x4 block: the colour range's bounding box (pulled in a little, and turned to follow the colours' correlation)
 * gives two first endpoints; each round assigns every texel to the nearest of the four palette colours and refits the
 * two endpoints to those assignments by least squares (quantising to RGB565 each time); the round with the least
 * squared error wins. The block is always in four colour mode (first endpoint greater than the second) unless it is a
 * single colour, which has equal endpoints and index 0 everywhere, so the transparent index is never used and alpha
 * is ignored.
 */
namespace vsgUtil::Bc1Encode
{
    namespace detail
    {
        // The decoder's expansion of 5 and 6 bit endpoints (vsgUtil::detail::decodeBlockCompressed).
        inline constexpr uint8_t expand5(int v)
        {
            return static_cast<uint8_t>(v * 255 / 31);
        }
        inline constexpr uint8_t expand6(int v)
        {
            return static_cast<uint8_t>(v * 255 / 63);
        }

        struct Palette
        {
            int colour[4][3];
        };

        inline void makePalette(uint16_t c0, uint16_t c1, Palette& palette)
        {
            const int r0 = expand5(c0 >> 11), g0 = expand6((c0 >> 5) & 63), b0 = expand5(c0 & 31);
            const int r1 = expand5(c1 >> 11), g1 = expand6((c1 >> 5) & 63), b1 = expand5(c1 & 31);
            palette.colour[0][0] = r0;
            palette.colour[0][1] = g0;
            palette.colour[0][2] = b0;
            palette.colour[1][0] = r1;
            palette.colour[1][1] = g1;
            palette.colour[1][2] = b1;
            palette.colour[2][0] = (2 * r0 + r1) / 3;
            palette.colour[2][1] = (2 * g0 + g1) / 3;
            palette.colour[2][2] = (2 * b0 + b1) / 3;
            palette.colour[3][0] = (r0 + 2 * r1) / 3;
            palette.colour[3][1] = (g0 + 2 * g1) / 3;
            palette.colour[3][2] = (b0 + 2 * b1) / 3;
        }

        struct Fit
        {
            uint16_t c0 = 0;
            uint16_t c1 = 0;
            uint32_t indices = 0;
            uint32_t error = 0xffffffffu;
        };

        // The best indices for the endpoints (put in the order that gives four colour mode) and their squared error.
        inline Fit evaluate(const int (&texel)[16][3], uint16_t a, uint16_t b)
        {
            if (a < b)
            {
                std::swap(a, b);
            }
            Fit fit;
            fit.c0 = a;
            fit.c1 = b;
            Palette palette;
            makePalette(a, b, palette);
            uint32_t error = 0;
            uint32_t indices = 0;
            const int choices = a == b ? 1 : 4;
            for (int i = 0; i < 16; ++i)
            {
                int bestIndex = 0;
                int bestDistance = 0x7fffffff;
                for (int k = 0; k < choices; ++k)
                {
                    const int dr = texel[i][0] - palette.colour[k][0];
                    const int dg = texel[i][1] - palette.colour[k][1];
                    const int db = texel[i][2] - palette.colour[k][2];
                    const int distance = dr * dr + dg * dg + db * db;
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestIndex = k;
                    }
                }
                error += static_cast<uint32_t>(bestDistance);
                indices |= static_cast<uint32_t>(bestIndex) << (2 * i);
            }
            fit.indices = indices;
            fit.error = error;
            return fit;
        }

        inline int quantise(float value, int levels)
        {
            const int q = static_cast<int>(value * static_cast<float>(levels) / 255.f + 0.5f);
            return std::clamp(q, 0, levels);
        }

        inline uint16_t pack(float r, float g, float b)
        {
            return static_cast<uint16_t>((quantise(r, 31) << 11) | (quantise(g, 63) << 5) | quantise(b, 31));
        }

        // The endpoints widened to the next quantisation step on each side (the one at `a` up when it is the larger
        // of the two in a channel, down when it is the smaller), so that a narrow range does not collapse to one
        // colour.
        inline void packWidened(const float (&a)[3], const float (&b)[3], uint16_t& packedA, uint16_t& packedB)
        {
            static constexpr int levels[3] = { 31, 63, 31 };
            int qa[3];
            int qb[3];
            for (int c = 0; c < 3; ++c)
            {
                const float sa = a[c] * static_cast<float>(levels[c]) / 255.f;
                const float sb = b[c] * static_cast<float>(levels[c]) / 255.f;
                const bool aHigher = a[c] >= b[c];
                qa[c] = std::clamp(static_cast<int>(aHigher ? std::ceil(sa) : std::floor(sa)), 0, levels[c]);
                qb[c] = std::clamp(static_cast<int>(aHigher ? std::floor(sb) : std::ceil(sb)), 0, levels[c]);
            }
            packedA = static_cast<uint16_t>((qa[0] << 11) | (qa[1] << 5) | qa[2]);
            packedB = static_cast<uint16_t>((qb[0] << 11) | (qb[1] << 5) | qb[2]);
        }
    }

    /// Bytes of a BC1 image of width x height texels (4x4 blocks, partial blocks at the edges included).
    inline size_t encodedSize(uint32_t width, uint32_t height)
    {
        return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4) * 8;
    }

    /// Encodes 16 texels (RGBA8, row-major) to one 8 byte block. Alpha is ignored; the block is always opaque.
    inline void encodeBlock(const uint8_t* rgba, uint8_t* out)
    {
        int texel[16][3];
        int low[3] = { 255, 255, 255 };
        int high[3] = { 0, 0, 0 };
        for (int i = 0; i < 16; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                const int v = rgba[i * 4 + c];
                texel[i][c] = v;
                low[c] = std::min(low[c], v);
                high[c] = std::max(high[c], v);
            }
        }

        // First endpoints: the bounding box pulled in by a sixteenth of its size, along the colours' diagonal.
        float lo[3];
        float hi[3];
        for (int c = 0; c < 3; ++c)
        {
            const int inset = (high[c] - low[c]) >> 4;
            lo[c] = static_cast<float>(low[c] + inset);
            hi[c] = static_cast<float>(high[c] - inset);
        }
        float midpoint[3];
        for (int c = 0; c < 3; ++c)
        {
            midpoint[c] = 0.5f * static_cast<float>(low[c] + high[c]);
        }
        float covarianceRG = 0.f;
        float covarianceRB = 0.f;
        for (int i = 0; i < 16; ++i)
        {
            const float r = static_cast<float>(texel[i][0]) - midpoint[0];
            covarianceRG += r * (static_cast<float>(texel[i][1]) - midpoint[1]);
            covarianceRB += r * (static_cast<float>(texel[i][2]) - midpoint[2]);
        }
        if (covarianceRG < 0.f)
        {
            std::swap(lo[1], hi[1]);
        }
        if (covarianceRB < 0.f)
        {
            std::swap(lo[2], hi[2]);
        }

        detail::Fit best
            = detail::evaluate(texel, detail::pack(hi[0], hi[1], hi[2]), detail::pack(lo[0], lo[1], lo[2]));
        if (best.error > 0)
        {
            uint16_t wideA;
            uint16_t wideB;
            detail::packWidened(hi, lo, wideA, wideB);
            const detail::Fit spread = detail::evaluate(texel, wideA, wideB);
            if (spread.error < best.error)
            {
                best = spread;
            }
        }
        constexpr int refinements = 2;
        for (int round = 0; round < refinements && best.error > 0 && best.c0 != best.c1; ++round)
        {
            // Least squares for the two endpoints given the assignment: texel i is a * end0 + (1 - a) * end1 with
            // a = 1, 0, 2/3, 1/3 for the indices 0 to 3.
            static constexpr float weightOfEnd0[4] = { 1.f, 0.f, 2.f / 3.f, 1.f / 3.f };
            float a00 = 0.f;
            float a01 = 0.f;
            float a11 = 0.f;
            float b0[3] = { 0.f, 0.f, 0.f };
            float b1[3] = { 0.f, 0.f, 0.f };
            for (int i = 0; i < 16; ++i)
            {
                const float a = weightOfEnd0[(best.indices >> (2 * i)) & 3];
                const float b = 1.f - a;
                a00 += a * a;
                a01 += a * b;
                a11 += b * b;
                for (int c = 0; c < 3; ++c)
                {
                    b0[c] += a * static_cast<float>(texel[i][c]);
                    b1[c] += b * static_cast<float>(texel[i][c]);
                }
            }
            const float determinant = a00 * a11 - a01 * a01;
            if (determinant < 1e-4f)
            {
                break;
            }
            float end0[3];
            float end1[3];
            for (int c = 0; c < 3; ++c)
            {
                end0[c] = std::clamp((b0[c] * a11 - b1[c] * a01) / determinant, 0.f, 255.f);
                end1[c] = std::clamp((b1[c] * a00 - b0[c] * a01) / determinant, 0.f, 255.f);
            }
            const detail::Fit refined = detail::evaluate(
                texel, detail::pack(end0[0], end0[1], end0[2]), detail::pack(end1[0], end1[1], end1[2]));
            if (refined.error >= best.error)
            {
                break;
            }
            best = refined;
        }

        out[0] = static_cast<uint8_t>(best.c0 & 255);
        out[1] = static_cast<uint8_t>(best.c0 >> 8);
        out[2] = static_cast<uint8_t>(best.c1 & 255);
        out[3] = static_cast<uint8_t>(best.c1 >> 8);
        out[4] = static_cast<uint8_t>(best.indices & 255);
        out[5] = static_cast<uint8_t>((best.indices >> 8) & 255);
        out[6] = static_cast<uint8_t>((best.indices >> 16) & 255);
        out[7] = static_cast<uint8_t>(best.indices >> 24);
    }

    /// Encodes a whole image; `out` must have encodedSize() bytes. Texels beyond the edge repeat the edge texel.
    inline void encodeImage(const uint8_t* rgba, uint32_t width, uint32_t height, uint8_t* out)
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        uint8_t texels[64];
        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                for (uint32_t y = 0; y < 4; ++y)
                {
                    const uint32_t sy = std::min(by * 4 + y, height - 1);
                    if (bx * 4 + 4 <= width)
                    {
                        std::memcpy(texels + y * 16, rgba + (static_cast<size_t>(sy) * width + bx * 4) * 4, 16);
                    }
                    else
                    {
                        for (uint32_t x = 0; x < 4; ++x)
                        {
                            const uint32_t sx = std::min(bx * 4 + x, width - 1);
                            std::memcpy(texels + (y * 4 + x) * 4, rgba + (static_cast<size_t>(sy) * width + sx) * 4, 4);
                        }
                    }
                }
                encodeBlock(texels, out + (static_cast<size_t>(by) * blocksX + bx) * 8);
            }
        }
    }
}

#endif
