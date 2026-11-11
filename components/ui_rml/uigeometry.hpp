#ifndef OPENEDEN_COMPONENTS_UI_RML_UIGEOMETRY_H
#define OPENEDEN_COMPONENTS_UI_RML_UIGEOMETRY_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

// The pure part of the VSG render interface (task 0104): what RmlUi hands over, as the bytes the GPU reads, and the
// matrices and rectangles of one draw. No Vulkan or VSG type in here, so the tests need no device.
namespace UiRml
{
    /// One vertex as the shader reads it: position, premultiplied colour, texture coordinate.
    struct GpuVertex
    {
        float mX, mY;
        std::uint8_t mRed, mGreen, mBlue, mAlpha;
        float mU, mV;
    };
    static_assert(sizeof(GpuVertex) == 20);

    /// The push constants of one draw: the column-major clip matrix and the translation RmlUi gives with the geometry,
    /// applied before the matrix (clip = matrix * (position + translation)).
    struct PushConstants
    {
        float mMatrix[16];
        float mTranslation[2];
    };
    static_assert(sizeof(PushConstants) == 72);

    std::vector<GpuVertex> packVertices(std::span<const Rml::Vertex> vertices);

    /// Copies `indices` as 32-bit unsigned values. Returns false (and leaves `out` empty) when the count is not a
    /// multiple of three or any index is negative or not below `vertexCount`: a draw with such an index could read
    /// outside the vertex buffer on the GPU.
    bool packIndices(std::span<const int> indices, std::size_t vertexCount, std::vector<std::uint32_t>& out);

    /// A scissor rectangle in framebuffer pixels (origin top left, as Vulkan's).
    struct ScissorRect
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };

    /// RmlUi's scissor region clamped to the framebuffer: negative origins move to 0 (the size shrinks with them), a
    /// rectangle past the edge is cut, an empty or inverted one has size 0.
    ScissorRect clampScissor(Rml::Rectanglei region, std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);

    /// Column-major matrix taking the RmlUi pixel space (0, 0 top left to width, height) to Vulkan clip space
    /// (-1, -1 top left to 1, 1 bottom right, depth 0).
    void orthographic(std::uint32_t width, std::uint32_t height, float out[16]);

    /// out = a * b, all column-major. `out` may not alias the inputs.
    void multiply(const float a[16], const float b[16], float out[16]);
}

#endif
