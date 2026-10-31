#ifndef OPENEDEN_COMPONENTS_VSGUTIL_IMAGEPIXELS_H
#define OPENEDEN_COMPONENTS_VSGUTIL_IMAGEPIXELS_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsgUtil
{
    // Plain 8-bit pixel work on tightly packed buffers, for the save importer (task 0014, Track V stage V6), which used
    // osg::Image for it. Row 0 of a buffer is the first row in memory, and the top row of an encoded file.

    // Encodes as ".png", ".jpg" or ".tga" through vsgXchange's stbi writer. channels is 3 (RGB) or 4 (RGBA).
    // JPEG quality is the stbi reader-writer's default of 100; osgDB's was 75.
    // Returns an empty vector on failure.
    std::vector<char> encodeImage(
        const unsigned char* pixels, std::uint32_t width, std::uint32_t height, int channels, const char* extension);

    // Doubles the width and height of a square image, as osg::Image::scaleImage did (gluScaleImage): each new pixel is the
    // average of a one-pixel box around its centre, which gives weights of 3/4 and 1/4, and the box wraps at the edges.
    std::vector<unsigned char> doubleImageSize(const std::vector<unsigned char>& pixels, int size, int channels);

    // Drops the alpha channel of BGRA pixels and swaps red and blue, giving RGB.
    std::vector<unsigned char> bgraToRgb(const std::vector<unsigned char>& pixels);

    // Copies a cellSize square from an RGB image to an RGBA one. As the importer always has, this copies four bytes per
    // pixel, so each destination alpha is the red of the next source pixel; alpha is 0 past the end of the source.
    void copyRgbSquareToRgba(const std::vector<unsigned char>& source, int sourceWidth, int sourceLeft, int sourceTop,
        std::vector<unsigned char>& destination, int destinationWidth, int destinationLeft, int destinationTop,
        int cellSize);
}

#endif
