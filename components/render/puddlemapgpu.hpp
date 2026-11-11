#ifndef OPENEDEN_RENDER_PUDDLEMAPGPU_H
#define OPENEDEN_RENDER_PUDDLEMAPGPU_H

// OpenEden (task 0095, stage W-d): the puddle map as the shader reads it, and the shader's maths as pure functions. The
// GLSL twin is files/shaders/lib/material/puddlemap.glsl and its Slang twin puddlemap.slang; change all three together.
// No game types, no Vulkan, standard library and puddlelevel.hpp only, so it is tested without a device
// (apps/components_tests/render/testpuddlemapgpu.cpp). `components/puddle/puddlegpu.*` fills the buffer.
//
// The buffer (binding VIEW_PUDDLE_BINDING, a read-only storage buffer of 32 bit words; a stand-in of `dataBase` zero
// words says "off"):
//   word 0  enabled (1: a map is loaded; 0: none, the shader draws the old noise puddles)
//   word 1  debug view (DebugMode)
//   word 2  window origin cell x (int32 bits)       word 3  window origin cell y
//   word 4  window side in cells (always `windowSide`)
//   word 5  texels along a cell side                word 6  spacing in world units between texels
//   word 7  cell size in world units
//   word 8  water supplied at wetness 1, in units of depth (float bits)
//   word 9  water the ground absorbs before any stands, same unit (float bits)
//   words tableBase .. tableBase + windowSide^2 - 1  the cell table, slot = (cx - originX) + windowSide * (cy -
//   originY):
//           the word index of the cell's first texel, 0 when the cell is not loaded
//   words dataBase ..  the cells' texels, side * side words each, row-major, the SOUTH row first (the pack's order)
// A texel word, little end first: capacity (1/16 unit), the pit's maximum capacity (1/16 unit), the catchment code
// (log2 kappa in 15ths) and the pack's texel flags. Texel (tx, ty) of cell (cx, cy) lies at world position
// (cx * cellSize + tx * spacing, cy * cellSize + ty * spacing): the baker samples the ground at the texel corner
// (components/puddle/puddlebake.cpp, `terrainHeightAt`), so the map is bilinear between texel positions.
//
// What the shader does with it (research note 0049 section 5.2): the capacity C at the pixel is blended from the four
// nearest texels (a missing cell counts as no capacity); the pit (its maximum Cmax and catchment kappa) is that of the
// tap with the largest weighted capacity; wetness w gives the supply W = w * supply, the free water Wf = max(W -
// absorption, 0), the pit's level L = min(Cmax, Wf * kappa) and the depth d = clamp(L - (Cmax - C), 0, C).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "puddlelevel.hpp"

namespace Render::PuddleMap
{
    inline constexpr std::uint32_t wordEnabled = 0;
    inline constexpr std::uint32_t wordDebug = 1;
    inline constexpr std::uint32_t wordOriginX = 2;
    inline constexpr std::uint32_t wordOriginY = 3;
    inline constexpr std::uint32_t wordWindowSide = 4;
    inline constexpr std::uint32_t wordTexelsPerCell = 5;
    inline constexpr std::uint32_t wordSpacing = 6;
    inline constexpr std::uint32_t wordCellSize = 7;
    inline constexpr std::uint32_t wordSupply = 8;
    inline constexpr std::uint32_t wordAbsorption = 9;
    inline constexpr std::uint32_t tableBase = 16;
    // The table has room for a 9 x 9 window; the game uses 7 x 7 (3 cells round the camera's).
    inline constexpr std::uint32_t maxWindowSide = 9;
    inline constexpr std::uint32_t dataBase = 128;
    inline constexpr std::uint32_t windowRadius = 3;
    inline constexpr std::uint32_t windowSide = 2 * windowRadius + 1;
    // The most texels a cell side may have for the game's fixed buffer (the default spacing of 64 units gives 128).
    inline constexpr std::uint32_t maxTexelsPerSide = 128;

    // Visual constants (by eye, no source; the GLSL and Slang files repeat them under the same names):
    // the depth over which the water's edge fades in, in game units (a film of 0.4 cm at the shore).
    inline constexpr float edgeDepth = 0.3f;
    // Ripples need water at least this deep to start (0.3 units is 4 mm) and this deep to be complete. First values
    // were 0.8 and 2; the frames showed thin puddles with no ripples and darker than the noise ones, so they were
    // lowered.
    inline constexpr float rippleDepthFrom = 0.3f;
    inline constexpr float rippleDepthTo = 0.9f;
    // The ground under deep water is darker: a share of the puddle darkening that grows with depth.
    inline constexpr float deepDarkening = 0.8f;
    inline constexpr float deepDarkeningDepth = 3.f;
    // Debug views scale depth and capacity by these.
    inline constexpr float debugDepthScale = 6.f;
    inline constexpr float debugLevelScale = 12.f;

    enum DebugMode : std::uint32_t
    {
        DebugOff = 0,
        DebugCapacity = 1,
        DebugLevel = 2,
        DebugCell = 3,
    };

    /// The names of `wetness puddle debug`. Exact; on a name it does not know it returns false and leaves `mode`.
    inline bool parseDebugMode(std::string_view text, std::uint32_t& mode)
    {
        if (text == "off")
            mode = DebugOff;
        else if (text == "capacity")
            mode = DebugCapacity;
        else if (text == "level")
            mode = DebugLevel;
        else if (text == "cell")
            mode = DebugCell;
        else
            return false;
        return true;
    }

    inline constexpr std::size_t bufferWords(std::uint32_t windowSideCells, std::uint32_t texelsPerSide)
    {
        return static_cast<std::size_t>(dataBase)
            + static_cast<std::size_t>(windowSideCells) * windowSideCells * texelsPerSide * texelsPerSide;
    }

    inline constexpr std::uint32_t packTexel(
        std::uint8_t capacity, std::uint8_t basinMax, std::uint8_t catchment, std::uint8_t flags)
    {
        return static_cast<std::uint32_t>(capacity) | (static_cast<std::uint32_t>(basinMax) << 8)
            | (static_cast<std::uint32_t>(catchment) << 16) | (static_cast<std::uint32_t>(flags) << 24);
    }

    inline std::uint32_t floatBits(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return bits;
    }

    inline float bitsFloat(std::uint32_t bits)
    {
        float value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    inline std::int32_t floorDiv(std::int32_t a, std::int32_t b)
    {
        std::int32_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0)))
            --q;
        return q;
    }

    // The first texel word of cell (cx, cy), 0 when the cell is not in the window or not loaded.
    inline std::uint32_t cellStart(const std::uint32_t* data, std::int32_t cx, std::int32_t cy)
    {
        const std::int32_t sx = cx - static_cast<std::int32_t>(data[wordOriginX]);
        const std::int32_t sy = cy - static_cast<std::int32_t>(data[wordOriginY]);
        const std::int32_t side = static_cast<std::int32_t>(data[wordWindowSide]);
        if (sx < 0 || sy < 0 || sx >= side || sy >= side)
            return 0;
        return data[tableBase + static_cast<std::uint32_t>(sx + side * sy)];
    }

    // The texel word at global texel coordinates (gx, gy) (the cell's texel grid continues into its neighbours); false
    // when the cell is not loaded.
    inline bool texelWord(const std::uint32_t* data, std::int32_t gx, std::int32_t gy, std::uint32_t& word)
    {
        const std::int32_t side = static_cast<std::int32_t>(data[wordTexelsPerCell]);
        if (side <= 0)
            return false;
        const std::int32_t cx = floorDiv(gx, side);
        const std::int32_t cy = floorDiv(gy, side);
        const std::uint32_t start = cellStart(data, cx, cy);
        if (start == 0)
            return false;
        const std::int32_t lx = gx - cx * side;
        const std::int32_t ly = gy - cy * side;
        word = data[start + static_cast<std::uint32_t>(ly * side + lx)];
        return true;
    }

    struct Shade
    {
        bool covered = false; // the cell under the pixel is loaded
        float capacity = 0.f; // blended, units
        float basinMax = 0.f;
        float kappa = 0.f;
        float level = 0.f; // of the pit's water above its floor
        float depth = 0.f; // of the water over the ground here
        float coverage = 0.f; // 0 dry, 1 under water, with a soft edge
        float rippleGate = 0.f; // 0 shallow, 1 deep enough for the rain's ripples
        std::int32_t cellX = 0;
        std::int32_t cellY = 0;
    };

    inline float smoothstep(float a, float b, float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // The map at world (x, y) for the wetness `wet` (0 dry to 1 soaked, already times the strength and the cover).
    inline Shade shade(const std::uint32_t* data, float x, float y, float wet)
    {
        Shade s;
        if (data[wordEnabled] == 0)
            return s;
        const float cellSize = static_cast<float>(data[wordCellSize]);
        const float spacing = static_cast<float>(data[wordSpacing]);
        if (!(cellSize > 0.f) || !(spacing > 0.f))
            return s;
        s.cellX = static_cast<std::int32_t>(std::floor(x / cellSize));
        s.cellY = static_cast<std::int32_t>(std::floor(y / cellSize));
        s.covered = cellStart(data, s.cellX, s.cellY) != 0;
        if (!s.covered)
            return s;

        const float px = x / spacing;
        const float py = y / spacing;
        const float fx0 = std::floor(px);
        const float fy0 = std::floor(py);
        const std::int32_t i0 = static_cast<std::int32_t>(fx0);
        const std::int32_t j0 = static_cast<std::int32_t>(fy0);
        const float tx = px - fx0;
        const float ty = py - fy0;
        const float weights[4] = { (1.f - tx) * (1.f - ty), tx * (1.f - ty), (1.f - tx) * ty, tx * ty };
        const std::int32_t dx[4] = { 0, 1, 0, 1 };
        const std::int32_t dy[4] = { 0, 0, 1, 1 };

        float capacity = 0.f;
        float bestScore = 0.f;
        std::uint32_t pit = 0;
        for (int k = 0; k < 4; ++k)
        {
            std::uint32_t word = 0;
            if (!texelWord(data, i0 + dx[k], j0 + dy[k], word))
                continue;
            const float c = Puddle::dequantiseCapacity(static_cast<std::uint8_t>(word & 0xFFu));
            capacity += weights[k] * c;
            const float score = weights[k] * c;
            if (score > bestScore)
            {
                bestScore = score;
                pit = word;
            }
        }
        s.capacity = capacity;
        if (!(capacity > 0.f))
            return s;
        s.basinMax = Puddle::dequantiseCapacity(static_cast<std::uint8_t>((pit >> 8) & 0xFFu));
        s.kappa = Puddle::dequantiseKappa(static_cast<std::uint8_t>((pit >> 16) & 0xFFu));

        const float supplied = std::max(wet, 0.f) * bitsFloat(data[wordSupply]);
        const float freeDepth = Puddle::freeWater(supplied, bitsFloat(data[wordAbsorption]));
        s.level = Puddle::levelOf(freeDepth, s.kappa, s.basinMax);
        s.depth = Puddle::depthAt(s.level, s.basinMax, capacity);
        s.coverage = Puddle::coverage(s.depth, edgeDepth);
        s.rippleGate = smoothstep(rippleDepthFrom, rippleDepthTo, s.depth);
        return s;
    }

    // The factor on the ground colour under a puddle's water: 1 for a film, `deepDarkening` for deep water.
    inline float depthTint(float depth)
    {
        return 1.f - (1.f - deepDarkening) * smoothstep(0.f, deepDarkeningDepth, depth);
    }

    struct Rgb
    {
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
    };

    // The same 32 bit hash as lib/material/wetness.glsl (`wetHash`) and Render::Wetness::hash.
    inline std::uint32_t hash(std::uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    // The colour of a debug view. Capacity: red the capacity, green half the pit's maximum, a dark blue where the map
    // covers the pixel at all (dark red where it does not). Level: green the depth of the water, blue the pit's level.
    // Cell: a colour from the cell's coordinates, grey where no cell is loaded.
    inline Rgb debugColour(std::uint32_t mode, const Shade& s)
    {
        Rgb c;
        switch (mode)
        {
            case DebugCapacity:
                if (!s.covered)
                    return { 0.25f, 0.f, 0.f };
                c.r = std::min(s.capacity / Puddle::maxQuantisedCapacity, 1.f);
                c.g = 0.5f * std::min(s.basinMax / Puddle::maxQuantisedCapacity, 1.f);
                c.b = 0.12f;
                return c;
            case DebugLevel:
                if (!s.covered)
                    return { 0.25f, 0.f, 0.f };
                c.g = std::min(s.depth / debugDepthScale, 1.f);
                c.b = 0.12f + 0.6f * std::min(s.level / debugLevelScale, 1.f);
                return c;
            case DebugCell:
            {
                if (!s.covered)
                    return { 0.2f, 0.2f, 0.2f };
                const std::uint32_t h = hash(
                    static_cast<std::uint32_t>(s.cellX) * 73856093u ^ static_cast<std::uint32_t>(s.cellY) * 19349663u);
                c.r = 0.25f + 0.7f * static_cast<float>(h & 0xFFu) / 255.f;
                c.g = 0.25f + 0.7f * static_cast<float>((h >> 8) & 0xFFu) / 255.f;
                c.b = 0.25f + 0.7f * static_cast<float>((h >> 16) & 0xFFu) / 255.f;
                return c;
            }
            default:
                return c;
        }
    }
}

#endif
