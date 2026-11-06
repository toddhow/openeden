#ifndef OPENEDEN_VSGUTIL_R8RESIZE_H
#define OPENEDEN_VSGUTIL_R8RESIZE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "rgbaresize.hpp"

/*
 * Task 0089: resizing one channel images (R8, for example the terrain's BC4 height layers) on the CPU. The same
 * filter as vsgUtil::RgbaResize, one channel: the result equals that function's on four equal channels (a test).
 */
namespace vsgUtil::R8Resize
{
    inline void resize(
        const uint8_t* src, uint32_t srcWidth, uint32_t srcHeight, uint8_t* dst, uint32_t dstWidth, uint32_t dstHeight)
    {
        if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
        {
            return;
        }
        if (srcWidth == dstWidth && srcHeight == dstHeight)
        {
            std::memcpy(dst, src, static_cast<size_t>(srcWidth) * srcHeight);
            return;
        }
        std::vector<RgbaResize::detail::AxisTap> xTaps;
        std::vector<RgbaResize::detail::AxisTap> yTaps;
        RgbaResize::detail::planAxis(srcWidth, dstWidth, xTaps);
        RgbaResize::detail::planAxis(srcHeight, dstHeight, yTaps);

        // Horizontal pass: 8.8 fixed point values, dstWidth x srcHeight.
        std::vector<uint16_t> middle(static_cast<size_t>(dstWidth) * srcHeight);
        for (uint32_t y = 0; y < srcHeight; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * srcWidth;
            uint16_t* out = &middle[static_cast<size_t>(y) * dstWidth];
            for (uint32_t x = 0; x < dstWidth; ++x)
            {
                const RgbaResize::detail::AxisTap& tap = xTaps[x];
                uint32_t value;
                if (dstWidth < srcWidth)
                {
                    uint32_t sum = 0;
                    for (uint32_t i = tap.first; i < tap.second; ++i)
                    {
                        sum += row[i];
                    }
                    const uint32_t count = tap.second - tap.first;
                    value = (sum * 256 + count / 2) / count;
                }
                else
                {
                    value = row[tap.first] * (256 - tap.weight) + row[tap.second] * tap.weight;
                }
                out[x] = static_cast<uint16_t>(value);
            }
        }

        // Vertical pass.
        for (uint32_t y = 0; y < dstHeight; ++y)
        {
            const RgbaResize::detail::AxisTap& tap = yTaps[y];
            uint8_t* out = dst + static_cast<size_t>(y) * dstWidth;
            for (uint32_t i = 0; i < dstWidth; ++i)
            {
                uint32_t value;
                if (dstHeight < srcHeight)
                {
                    uint32_t sum = 0;
                    for (uint32_t r = tap.first; r < tap.second; ++r)
                    {
                        sum += middle[static_cast<size_t>(r) * dstWidth + i];
                    }
                    const uint32_t count = tap.second - tap.first;
                    value = (sum + count * 128) / (count * 256);
                }
                else
                {
                    const uint32_t a = middle[static_cast<size_t>(tap.first) * dstWidth + i];
                    const uint32_t b = middle[static_cast<size_t>(tap.second) * dstWidth + i];
                    value = ((a * (256 - tap.weight) + b * tap.weight) / 256 + 128) / 256;
                }
                out[i] = static_cast<uint8_t>(std::min<uint32_t>(value, 255));
            }
        }
    }
}

#endif
