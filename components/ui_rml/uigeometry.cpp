#include "uigeometry.hpp"

#include <algorithm>

namespace UiRml
{
    std::vector<GpuVertex> packVertices(std::span<const Rml::Vertex> vertices)
    {
        std::vector<GpuVertex> out;
        out.reserve(vertices.size());
        for (const Rml::Vertex& v : vertices)
            out.push_back(GpuVertex{ v.position.x, v.position.y, v.colour.red, v.colour.green, v.colour.blue,
                v.colour.alpha, v.tex_coord.x, v.tex_coord.y });
        return out;
    }

    bool packIndices(std::span<const int> indices, std::size_t vertexCount, std::vector<std::uint32_t>& out)
    {
        out.clear();
        if (indices.size() % 3 != 0)
            return false;
        out.reserve(indices.size());
        for (const int index : indices)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= vertexCount)
            {
                out.clear();
                return false;
            }
            out.push_back(static_cast<std::uint32_t>(index));
        }
        return true;
    }

    ScissorRect clampScissor(Rml::Rectanglei region, std::uint32_t framebufferWidth, std::uint32_t framebufferHeight)
    {
        const long long left = std::clamp<long long>(region.Left(), 0, framebufferWidth);
        const long long top = std::clamp<long long>(region.Top(), 0, framebufferHeight);
        const long long right = std::clamp<long long>(region.Right(), 0, framebufferWidth);
        const long long bottom = std::clamp<long long>(region.Bottom(), 0, framebufferHeight);
        ScissorRect rect;
        rect.mX = static_cast<std::int32_t>(left);
        rect.mY = static_cast<std::int32_t>(top);
        rect.mWidth = right > left ? static_cast<std::uint32_t>(right - left) : 0u;
        rect.mHeight = bottom > top ? static_cast<std::uint32_t>(bottom - top) : 0u;
        return rect;
    }

    void orthographic(std::uint32_t width, std::uint32_t height, float out[16])
    {
        const float w = static_cast<float>(std::max(width, 1u));
        const float h = static_cast<float>(std::max(height, 1u));
        // x' = 2x / w - 1, y' = 2y / h - 1 (Vulkan's clip space has y down, like RmlUi's pixels), z' = 0, w' = 1.
        const float m[16]
            = { 2.f / w, 0.f, 0.f, 0.f, 0.f, 2.f / h, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, 0.f, 1.f };
        std::copy(std::begin(m), std::end(m), out);
    }

    void multiply(const float a[16], const float b[16], float out[16])
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k)
                    sum += a[k * 4 + row] * b[column * 4 + k];
                out[column * 4 + row] = sum;
            }
        }
    }
}
