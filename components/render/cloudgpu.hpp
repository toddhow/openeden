#ifndef OPENEDEN_RENDER_CLOUDGPU_H
#define OPENEDEN_RENDER_CLOUDGPU_H

// OpenEden (task 0038, stage c): how the cloud shadow data reaches the GPU. It rides at the end of the exterior
// sky-visibility header buffer (binding VIEW_SKYVIS_SKY_BINDING, see skyvisgpu.hpp), after the sky's own entries, so the
// view descriptor set has no new binding (the editor's, preview's and map's views already hold a stand-in of that
// binding). The shader twin is files/shaders/lib/view/cloudshadow.glsl; change both together. All vec4, at `base` =
// Gi::skyHeaderVec4s:
//   [base + 0]  = (strength, layer blend, scroll, 0): strength is the setting times the weather's share (0 = no
//                 shadows); blend is the weight of the second layer (the first has 1 - blend); scroll is the weather's
//                 growing offset of the clouds' texture coordinate v (`Sky::update`).
//   [base + 1..3] = the first layer's rotation (dome space to world), three columns (x, y, z, 0)
//   [base + 4..6] = the second layer's
//   [base + 7]  = (has first layer, has second layer, 0, 0)
//   [base + 8 ..]                      the first layer's grid: gridSize * gridSize floats, row 0 first
//   [base + 8 + gridVec4s ..]          the second layer's
// No game types.

#include <array>
#include <cstddef>

#include <vsg/maths/vec4.h>

#include "cloudmath.hpp"
#include "skyvisgpu.hpp"

namespace Render::Cloud
{
    constexpr std::size_t gridVec4s = gridSize * gridSize / 4;
    constexpr std::size_t paramsOffset = 0;
    constexpr std::size_t rotationOffset = 1; // first layer; the second is three further on
    constexpr std::size_t flagsOffset = 7;
    constexpr std::size_t gridOffset = 8;
    // What the cloud data adds to the header, and the header's size with it.
    constexpr std::size_t headerExtraVec4s = gridOffset + 2 * gridVec4s;
    constexpr std::size_t headerTotalVec4s = Gi::skyHeaderVec4s + headerExtraVec4s;

    // A rotation as three columns of three, column-major (the same order as a vsg::mat4's first three columns).
    using Rotation = std::array<float, 9>;
    constexpr Rotation identityRotation{ 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };

    struct Params
    {
        float strength = 0.f;
        float blend = 0.f;
        float scroll = 0.f;
        Rotation rotation[2] = { identityRotation, identityRotation };
        bool hasLayer[2] = { false, false };
    };

    // `header` is the whole header buffer (headerTotalVec4s long).
    inline void writeParams(vsg::vec4* header, const Params& p)
    {
        vsg::vec4* base = header + Gi::skyHeaderVec4s;
        base[paramsOffset] = vsg::vec4(p.strength, p.blend, p.scroll, 0.f);
        for (std::size_t layer = 0; layer < 2; ++layer)
            for (std::size_t column = 0; column < 3; ++column)
            {
                const Rotation& r = p.rotation[layer];
                base[rotationOffset + layer * 3 + column]
                    = vsg::vec4(r[column * 3], r[column * 3 + 1], r[column * 3 + 2], 0.f);
            }
        base[flagsOffset] = vsg::vec4(p.hasLayer[0] ? 1.f : 0.f, p.hasLayer[1] ? 1.f : 0.f, 0.f, 0.f);
    }

    // `grid` holds gridSize * gridSize floats; null clears the layer's grid to no cloud.
    inline void writeGrid(vsg::vec4* header, std::size_t layer, const float* grid)
    {
        vsg::vec4* base = header + Gi::skyHeaderVec4s + gridOffset + layer * gridVec4s;
        for (std::size_t i = 0; i < gridVec4s; ++i)
            base[i] = grid != nullptr ? vsg::vec4(grid[i * 4], grid[i * 4 + 1], grid[i * 4 + 2], grid[i * 4 + 3])
                                      : vsg::vec4(0.f, 0.f, 0.f, 0.f);
    }

    // The twin of the shader's read of one grid cell.
    inline float readGridCell(const vsg::vec4* header, std::size_t layer, std::size_t index)
    {
        const vsg::vec4& v = header[Gi::skyHeaderVec4s + gridOffset + layer * gridVec4s + index / 4];
        return v[static_cast<unsigned>(index % 4)];
    }
}

#endif
