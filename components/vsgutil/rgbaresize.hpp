#ifndef OPENEDEN_VSGUTIL_RGBARESIZE_H
#define OPENEDEN_VSGUTIL_RGBARESIZE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

/*
 * Task 0089: resizing RGBA8 images on the CPU. Pure functions, no vsg types.
 */
namespace vsgUtil::RgbaResize
{
    namespace detail
    {
        // For each destination index of one axis, the source texels that make it: a box (lo to hi, equal weights) when
        // the axis shrinks, two texels and a weight out of 256 when it grows or stays.
        struct AxisTap
        {
            uint32_t first; // first source index
            uint32_t second; // last source index + 1 for a box; the second texel for a linear tap
            uint32_t weight; // linear: weight of `second` out of 256
        };

        inline void planAxis(uint32_t srcSize, uint32_t dstSize, std::vector<AxisTap>& taps)
        {
            taps.resize(dstSize);
            for (uint32_t x = 0; x < dstSize; ++x)
            {
                if (dstSize < srcSize)
                {
                    const uint32_t lo = static_cast<uint32_t>(static_cast<uint64_t>(x) * srcSize / dstSize);
                    uint32_t hi = static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * srcSize / dstSize);
                    if (hi <= lo)
                    {
                        hi = lo + 1;
                    }
                    taps[x] = { lo, hi, 0 };
                }
                else
                {
                    // Texel centres: destination x is at (x + 0.5) * srcSize / dstSize - 0.5 in source texels,
                    // in units of 1 / (2 * dstSize).
                    const int64_t p = static_cast<int64_t>(2 * x + 1) * srcSize - dstSize;
                    uint32_t i0 = 0;
                    uint32_t weight = 0;
                    if (p > 0)
                    {
                        i0 = static_cast<uint32_t>(p / (2 * static_cast<int64_t>(dstSize)));
                        weight = static_cast<uint32_t>(
                            (p % (2 * static_cast<int64_t>(dstSize))) * 256 / (2 * static_cast<int64_t>(dstSize)));
                    }
                    if (i0 >= srcSize - 1)
                    {
                        i0 = srcSize - 1;
                        weight = 0;
                    }
                    taps[x] = { i0, std::min(i0 + 1, srcSize - 1), weight };
                }
            }
        }
    }

    /*
     * Resizes `src` (srcWidth x srcHeight RGBA8) to `dst` (dstWidth x dstHeight). Each axis on its own: an axis that
     * shrinks averages the source texels it covers (a box filter), an axis that grows interpolates linearly between
     * texel centres. Integer arithmetic only, so the result is the same on every platform; the first pass keeps 8
     * fractional bits so the two passes round once.
     */
    inline void resize(
        const uint8_t* src, uint32_t srcWidth, uint32_t srcHeight, uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight)
    {
        if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
        {
            return;
        }
        if (srcWidth == dstWidth && srcHeight == dstHeight)
        {
            std::memcpy(dst, src, static_cast<size_t>(srcWidth) * srcHeight * 4);
            return;
        }
        std::vector<detail::AxisTap> xTaps;
        std::vector<detail::AxisTap> yTaps;
        detail::planAxis(srcWidth, dstWidth, xTaps);
        detail::planAxis(srcHeight, dstHeight, yTaps);

        // Horizontal pass: 8.8 fixed point values, dstWidth x srcHeight.
        std::vector<uint16_t> middle(static_cast<size_t>(dstWidth) * srcHeight * 4);
        for (uint32_t y = 0; y < srcHeight; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * srcWidth * 4;
            uint16_t* out = &middle[static_cast<size_t>(y) * dstWidth * 4];
            for (uint32_t x = 0; x < dstWidth; ++x)
            {
                const detail::AxisTap& tap = xTaps[x];
                for (int c = 0; c < 4; ++c)
                {
                    uint32_t value;
                    if (dstWidth < srcWidth)
                    {
                        uint32_t sum = 0;
                        for (uint32_t i = tap.first; i < tap.second; ++i)
                        {
                            sum += row[i * 4 + c];
                        }
                        const uint32_t count = tap.second - tap.first;
                        value = (sum * 256 + count / 2) / count;
                    }
                    else
                    {
                        value = row[tap.first * 4 + c] * (256 - tap.weight) + row[tap.second * 4 + c] * tap.weight;
                    }
                    out[x * 4 + c] = static_cast<uint16_t>(value);
                }
            }
        }

        // Vertical pass.
        for (uint32_t y = 0; y < dstHeight; ++y)
        {
            const detail::AxisTap& tap = yTaps[y];
            uint8_t* out = dst + static_cast<size_t>(y) * dstWidth * 4;
            for (uint32_t i = 0; i < dstWidth * 4; ++i)
            {
                uint32_t value;
                if (dstHeight < srcHeight)
                {
                    uint32_t sum = 0;
                    for (uint32_t r = tap.first; r < tap.second; ++r)
                    {
                        sum += middle[static_cast<size_t>(r) * dstWidth * 4 + i];
                    }
                    const uint32_t count = tap.second - tap.first;
                    value = (sum + count * 128) / (count * 256);
                }
                else
                {
                    const uint32_t a = middle[static_cast<size_t>(tap.first) * dstWidth * 4 + i];
                    const uint32_t b = middle[static_cast<size_t>(tap.second) * dstWidth * 4 + i];
                    value = ((a * (256 - tap.weight) + b * tap.weight) / 256 + 128) / 256;
                }
                out[i] = static_cast<uint8_t>(std::min<uint32_t>(value, 255));
            }
        }
    }
}

#endif
