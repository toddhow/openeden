#ifndef OPENEDEN_RENDER_SKYVISGPU_H
#define OPENEDEN_RENDER_SKYVISGPU_H

// OpenEden (task 0036, stage c): how the sky-visibility field of the loaded exterior cells is laid out for the GPU, and
// the C++ twin of the shader that reads it (files/shaders/lib/view/skyvis.glsl; change both together).
//
// Two buffers, both arrays of vec4 (std430):
//  - the header (binding VIEW_SKYVIS_SKY_BINDING, small, rewritten when the sky changes):
//      [0]      = (enabled, strength, window west cell, window south cell)
//      [1]      = (spacing, level spacing, levels, columns per side)
//      [2 + i]  = the sky's SH coefficient i, (r, g, b, 0), i in 0..8
//      [11]     = (r, g, b, 0) of skyDot(sky, hemisphereVisibility()), what open flat ground gets
//      [12 + s] = slot s of the window of skyWindow by skyWindow cells, s = (x - west) + skyWindow * (y - south):
//                 (loaded, offset of the cell's data in floats, 0, 0)
//  - the pool (binding VIEW_SKYVIS_BINDING, large, rewritten when the set of loaded cells changes): the cells' data as
//    plain floats (float i is component i % 4 of vec4 i / 4). A cell's data is its columns in order, each `1 + 9 *
//    levels` floats: the ground height, then the nine coefficients of each level.
// No game types.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include "skyvismath.hpp"

namespace Render::Gi
{
    // Cells per side of the window the buffers can hold (the loaded exterior cells are within it).
    constexpr int skyWindow = 7;
    constexpr std::size_t skySlots = static_cast<std::size_t>(skyWindow) * skyWindow;
    constexpr std::size_t skyHeaderFixed = 12;
    constexpr std::size_t skyHeaderVec4s = skyHeaderFixed + skySlots;
    // The pool: 2.16 million floats, 27 cells of the default size (a 5 by 5 grid of exterior cells; 8.6 MB).
    constexpr std::size_t skyPoolVec4s = 540000;

    // Floats in one cell's data.
    inline std::size_t skyCellFloats(const SkyVisField& f)
    {
        const std::size_t side = skyVisColumnsPerSide(f.spacing);
        return side * side * (1 + static_cast<std::size_t>(f.levels) * shCoefficients);
    }

    struct SkyPackResult
    {
        std::size_t packedCells = 0;
        std::size_t skipped = 0;
    };

    namespace detail
    {
        inline void setFloat(vsg::vec4* pool, std::size_t index, float value)
        {
            vsg::vec4& v = pool[index / 4];
            switch (index % 4)
            {
                case 0:
                    v.x = value;
                    break;
                case 1:
                    v.y = value;
                    break;
                case 2:
                    v.z = value;
                    break;
                default:
                    v.w = value;
                    break;
            }
        }

        inline float getFloat(const vsg::vec4* pool, std::size_t index)
        {
            const vsg::vec4& v = pool[index / 4];
            switch (index % 4)
            {
                case 0:
                    return v.x;
                case 1:
                    return v.y;
                case 2:
                    return v.z;
                default:
                    return v.w;
            }
        }

        inline int floorDiv(int a, int b)
        {
            int q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                --q;
            return q;
        }
    }

    // Lays out `cells` (in the order given) in the pool, and writes the shape and the slots into the header (entries 1
    // and 12 on; writeSkyHeader does the rest). Cells outside the window that starts at the smallest cell given, of
    // another shape than the first, or that do not fit in `poolVec4s` are skipped. The header must have skyHeaderVec4s
    // entries.
    inline SkyPackResult packSkyVisibility(
        const std::vector<const SkyVisField*>& cells, vsg::vec4* header, vsg::vec4* pool, std::size_t poolVec4s)
    {
        SkyPackResult result;
        for (std::size_t s = 0; s < skySlots; ++s)
            header[skyHeaderFixed + s] = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        header[1] = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        header[0].z = header[0].w = 0.f;
        if (cells.empty())
            return result;
        int west = cells.front()->cellX;
        int south = cells.front()->cellY;
        for (const SkyVisField* c : cells)
        {
            west = std::min(west, c->cellX);
            south = std::min(south, c->cellY);
        }
        const SkyVisField& first = *cells.front();
        std::size_t used = 0;
        const std::size_t capacity = poolVec4s * 4;
        for (const SkyVisField* c : cells)
        {
            const int sx = c->cellX - west;
            const int sy = c->cellY - south;
            const std::size_t floats = skyCellFloats(*c);
            if (sx >= skyWindow || sy >= skyWindow || c->spacing != first.spacing
                || c->levelSpacing != first.levelSpacing || c->levels != first.levels || used + floats > capacity)
            {
                ++result.skipped;
                continue;
            }
            const std::size_t levels = c->levels;
            std::size_t at = used;
            for (std::size_t column = 0; column < c->base.size(); ++column)
            {
                detail::setFloat(pool, at++, c->base[column]);
                for (std::size_t l = 0; l < levels; ++l)
                    for (int k = 0; k < shCoefficients; ++k)
                        detail::setFloat(pool, at++, c->probes[column * levels + l][k]);
            }
            header[skyHeaderFixed + static_cast<std::size_t>(sx + skyWindow * sy)]
                = vsg::vec4(1.f, static_cast<float>(used), 0.f, 0.f);
            used += floats;
            ++result.packedCells;
        }
        if (result.packedCells == 0)
            return result;
        header[0].z = static_cast<float>(west);
        header[0].w = static_cast<float>(south);
        header[1] = vsg::vec4(first.spacing, first.levelSpacing, static_cast<float>(first.levels),
            static_cast<float>(skyVisColumnsPerSide(first.spacing)));
        return result;
    }

    // The part of the header that changes with the sky: whether the field is used, its strength, the sky's SH and what
    // open flat ground gets from it. With no cells packed the field stays off whatever `enabled` says.
    inline void writeSkyHeader(vsg::vec4* header, bool enabled, float strength, const Sh9& sky)
    {
        header[0].x = enabled && header[1].w > 0.f ? 1.f : 0.f;
        header[0].y = strength;
        for (int i = 0; i < shCoefficients; ++i)
            header[2 + i] = vsg::vec4(sky.c[i].x, sky.c[i].y, sky.c[i].z, 0.f);
        const vsg::vec3 ref = skyDot(sky, hemisphereVisibility());
        header[11] = vsg::vec4(ref.x, ref.y, ref.z, 0.f);
    }

    // The ambient light at a point: the flat ambient scaled by the share of it the place keeps, from the field of the
    // loaded cells and the sky. The C++ twin of skyVisAmbient in skyvis.glsl.
    inline vsg::vec3 skyVisAmbient(const vsg::vec4* header, const vsg::vec4* pool, const vsg::vec3& worldPos,
        const vsg::vec3& worldNormal, const vsg::vec3& flatAmbient)
    {
        if (header[0].x < 0.5f)
            return flatAmbient;
        const float spacing = header[1].x;
        const float levelSpacing = header[1].y;
        const int levels = static_cast<int>(header[1].z);
        const int side = static_cast<int>(header[1].w);
        const int divisions = side - 1;
        const int stride = 1 + levels * shCoefficients;
        const int west = static_cast<int>(header[0].z);
        const int south = static_cast<int>(header[0].w);

        vsg::vec3 p = worldPos;
        const float nl
            = std::sqrt(worldNormal.x * worldNormal.x + worldNormal.y * worldNormal.y + worldNormal.z * worldNormal.z);
        if (nl > 1e-10f)
            p += worldNormal * (normalBiasFraction * spacing / nl);

        const float gx = p.x / spacing;
        const float gy = p.y / spacing;
        const int gx0 = static_cast<int>(std::floor(gx));
        const int gy0 = static_cast<int>(std::floor(gy));
        const float fx = gx - static_cast<float>(gx0);
        const float fy = gy - static_cast<float>(gy0);

        // The float offset of the data of the column at global lattice place (cx, cy), or -1 when no loaded cell has
        // it.
        const auto columnOffset = [&](int cx, int cy) -> int {
            const int cellX = detail::floorDiv(cx, divisions);
            const int cellY = detail::floorDiv(cy, divisions);
            const int ix = cx - cellX * divisions;
            const int iy = cy - cellY * divisions;
            // A border column belongs to two cells; either one's data will do.
            for (int ax = 0; ax < 2; ++ax)
                for (int ay = 0; ay < 2; ++ay)
                {
                    if ((ax == 1 && ix != 0) || (ay == 1 && iy != 0))
                        continue;
                    const int sx = cellX - ax - west;
                    const int sy = cellY - ay - south;
                    if (sx < 0 || sy < 0 || sx >= skyWindow || sy >= skyWindow)
                        continue;
                    const vsg::vec4& slot = header[skyHeaderFixed + static_cast<std::size_t>(sx + skyWindow * sy)];
                    if (slot.x < 0.5f)
                        continue;
                    const int cix = ax == 1 ? divisions : ix;
                    const int ciy = ay == 1 ? divisions : iy;
                    return static_cast<int>(slot.y) + (cix + side * ciy) * stride;
                }
            return -1;
        };

        float weightSum = 0.f;
        VisSh sum{};
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
            {
                const float w = (dx == 1 ? fx : 1.f - fx) * (dy == 1 ? fy : 1.f - fy);
                if (w <= 0.f)
                    continue;
                const int offset = columnOffset(gx0 + dx, gy0 + dy);
                if (offset < 0)
                    continue;
                const float base = detail::getFloat(pool, static_cast<std::size_t>(offset));
                const float level = std::clamp((p.z - base) / levelSpacing - 0.5f, 0.f, static_cast<float>(levels - 1));
                const int l0 = static_cast<int>(std::floor(level));
                const int l1 = std::min(l0 + 1, levels - 1);
                const float t = level - static_cast<float>(l0);
                for (int k = 0; k < shCoefficients; ++k)
                {
                    const float a
                        = detail::getFloat(pool, static_cast<std::size_t>(offset + 1 + l0 * shCoefficients + k));
                    const float b
                        = detail::getFloat(pool, static_cast<std::size_t>(offset + 1 + l1 * shCoefficients + k));
                    sum[k] += (a * (1.f - t) + b * t) * w;
                }
                weightSum += w;
            }
        if (weightSum <= 1e-4f)
            return flatAmbient;
        for (float& v : sum)
            v /= weightSum;
        Sh9 sky;
        for (int k = 0; k < shCoefficients; ++k)
            sky.c[k] = vsg::vec3(header[2 + k].x, header[2 + k].y, header[2 + k].z);
        const vsg::vec3 occlusion = skyOcclusionNormal(sky, sum, worldNormal);
        const float strength = header[0].y;
        const auto channel = [&](float occ, float flat) {
            const float kept = std::max(1.f - strength * (1.f - occ), 0.f);
            return flat * (1.f - weightSum) + flat * kept * weightSum;
        };
        return vsg::vec3(channel(occlusion.x, flatAmbient.x), channel(occlusion.y, flatAmbient.y),
            channel(occlusion.z, flatAmbient.z));
    }
}

#endif
