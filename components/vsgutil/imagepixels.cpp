#include "imagepixels.hpp"

#include "imageio.hpp"

#include <algorithm>
#include <cstring>

#include <vsg/core/Array2D.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace vsgUtil
{
    std::vector<char> encodeImage(
        const unsigned char* pixels, std::uint32_t width, std::uint32_t height, int channels, const char* extension)
    {
        const std::size_t byteCount = std::size_t(width) * height * channels;
        if (channels == 3)
        {
            auto data = vsg::ubvec3Array2D::create(width, height, vsg::Data::Properties{ VK_FORMAT_R8G8B8_UNORM });
            std::memcpy(data->dataPointer(), pixels, byteCount);
            return writeImageToMemory(data, extension);
        }
        if (channels == 4)
        {
            auto data = vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
            std::memcpy(data->dataPointer(), pixels, byteCount);
            return writeImageToMemory(data, extension);
        }
        return {};
    }

    std::vector<unsigned char> doubleImageSize(const std::vector<unsigned char>& pixels, int size, int channels)
    {
        std::vector<unsigned char> result(std::size_t(size) * size * 4 * channels);
        // The nearer source pixel of a new pixel counts 3 times, the other once; the weights are in quarters per axis.
        const auto nearer = [](int i) { return i / 2; };
        const auto farther = [size](int i) { return (i / 2 + (i % 2 == 0 ? size - 1 : 1)) % size; };
        const auto at = [&](int x, int y, int c) -> int { return pixels[(std::size_t(y) * size + x) * channels + c]; };
        std::size_t out = 0;
        for (int y = 0; y < size * 2; ++y)
            for (int x = 0; x < size * 2; ++x)
                for (int c = 0; c < channels; ++c)
                {
                    const int sum = 9 * at(nearer(x), nearer(y), c) + 3 * at(farther(x), nearer(y), c)
                        + 3 * at(nearer(x), farther(y), c) + at(farther(x), farther(y), c);
                    // gluScaleImage worked in 16 bits: a byte became v * 257, it rounded there, and kept the high byte.
                    result[out++] = static_cast<unsigned char>(((sum * 257 + 8) / 16) >> 8);
                }
        return result;
    }

    std::vector<unsigned char> bgraToRgb(const std::vector<unsigned char>& pixels)
    {
        const std::size_t count = pixels.size() / 4;
        std::vector<unsigned char> result(count * 3);
        for (std::size_t i = 0; i < count; ++i)
        {
            result[i * 3] = pixels[i * 4 + 2];
            result[i * 3 + 1] = pixels[i * 4 + 1];
            result[i * 3 + 2] = pixels[i * 4];
        }
        return result;
    }

    void copyRgbSquareToRgba(const std::vector<unsigned char>& source, int sourceWidth, int sourceLeft, int sourceTop,
        std::vector<unsigned char>& destination, int destinationWidth, int destinationLeft, int destinationTop,
        int cellSize)
    {
        for (int x = 0; x < cellSize; ++x)
            for (int y = 0; y < cellSize; ++y)
            {
                const std::size_t in = (std::size_t(sourceTop + y) * sourceWidth + (sourceLeft + x)) * 3;
                const std::size_t out
                    = (std::size_t(destinationTop + y) * destinationWidth + (destinationLeft + x)) * 4;
                for (std::size_t c = 0; c < 4; ++c)
                    destination[out + c] = in + c < source.size() ? source[in + c] : 0;
            }
    }
}
