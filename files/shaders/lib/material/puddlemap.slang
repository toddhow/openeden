// The Slang twin of lib/material/puddlemap.glsl (task 0095, stage W-d): puddles from the baked puddle map. The C++ maths is
// components/render/puddlemapgpu.hpp, whose comment is the buffer's layout; change all three together. Include after
// lib/view/scene.slang and lib/material/wetness.slang, only where WETNESS_PUDDLES is defined (it needs the binding).
// See the GLSL for what it does.
#ifndef LIB_MATERIAL_PUDDLEMAP_SLANG
#define LIB_MATERIAL_PUDDLEMAP_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

[[vk::binding(VIEW_PUDDLE_BINDING, VIEW_SET)]] StructuredBuffer<uint> puddleData;

static const uint PUDDLE_ENABLED = 0u;
static const uint PUDDLE_DEBUG = 1u;
static const uint PUDDLE_ORIGIN_X = 2u;
static const uint PUDDLE_ORIGIN_Y = 3u;
static const uint PUDDLE_WINDOW_SIDE = 4u;
static const uint PUDDLE_TEXELS = 5u;
static const uint PUDDLE_SPACING = 6u;
static const uint PUDDLE_CELL_SIZE = 7u;
static const uint PUDDLE_SUPPLY = 8u;
static const uint PUDDLE_ABSORPTION = 9u;
static const uint PUDDLE_TABLE_BASE = 16u;

static const float puddleEdgeDepth = 0.3;
static const float puddleRippleFrom = 0.3;
static const float puddleRippleTo = 0.9;
static const float puddleDeepDarkening = 0.8;
static const float puddleDeepDarkeningDepth = 3.0;
static const float puddleMaxCapacity = 15.9375;
static const float puddleDebugDepthScale = 6.0;
static const float puddleDebugLevelScale = 12.0;

struct PuddleShade
{
    bool covered;
    float capacity;
    float basinMax;
    float kappa;
    float level;
    float depth;
    float coverage;
    float rippleGate;
    int2 cell;
};

int puddleFloorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

// The first texel word of cell (cx, cy); 0 when the cell is not in the window or not loaded.
uint puddleCellStart(int cx, int cy)
{
    int sx = cx - int(puddleData[PUDDLE_ORIGIN_X]);
    int sy = cy - int(puddleData[PUDDLE_ORIGIN_Y]);
    int side = int(puddleData[PUDDLE_WINDOW_SIDE]);
    if (sx < 0 || sy < 0 || sx >= side || sy >= side)
        return 0u;
    return puddleData[PUDDLE_TABLE_BASE + uint(sx + side * sy)];
}

// x: 1 when the texel's cell is loaded, y: the texel word.
uint2 puddleTexel(int gx, int gy)
{
    int side = int(puddleData[PUDDLE_TEXELS]);
    int cx = puddleFloorDiv(gx, side);
    int cy = puddleFloorDiv(gy, side);
    uint start = puddleCellStart(cx, cy);
    if (start == 0u)
        return uint2(0u, 0u);
    return uint2(1u, puddleData[start + uint((gy - cy * side) * side + (gx - cx * side))]);
}

float puddleSmoothstep(float a, float b, float x)
{
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

PuddleShade puddleShadeAt(float2 pos, float wet)
{
    PuddleShade s;
    s.covered = false;
    s.capacity = 0.0;
    s.basinMax = 0.0;
    s.kappa = 0.0;
    s.level = 0.0;
    s.depth = 0.0;
    s.coverage = 0.0;
    s.rippleGate = 0.0;
    s.cell = int2(0, 0);
    if (puddleData[PUDDLE_ENABLED] == 0u)
        return s;
    // Dry weather costs nothing unless a debug view asks to see the map.
    if (!(wet > 0.0) && puddleData[PUDDLE_DEBUG] == 0u)
        return s;
    float cellSize = float(puddleData[PUDDLE_CELL_SIZE]);
    float spacing = float(puddleData[PUDDLE_SPACING]);
    if (!(cellSize > 0.0) || !(spacing > 0.0))
        return s;
    s.cell = int2(floor(pos / cellSize));
    s.covered = puddleCellStart(s.cell.x, s.cell.y) != 0u;
    if (!s.covered)
        return s;

    float2 p = pos / spacing;
    float2 base = floor(p);
    float2 t = p - base;
    int2 i0 = int2(base);
    float weights[4] = { (1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y };
    float capacity = 0.0;
    float bestScore = 0.0;
    uint pit = 0u;
    for (int k = 0; k < 4; ++k)
    {
        uint2 texel = puddleTexel(i0.x + (k & 1), i0.y + (k >> 1));
        if (texel.x == 0u)
            continue;
        float c = float(texel.y & 0xFFu) * (1.0 / 16.0);
        capacity += weights[k] * c;
        float score = weights[k] * c;
        if (score > bestScore)
        {
            bestScore = score;
            pit = texel.y;
        }
    }
    s.capacity = capacity;
    if (!(capacity > 0.0))
        return s;
    s.basinMax = float((pit >> 8) & 0xFFu) * (1.0 / 16.0);
    s.kappa = exp2(float((pit >> 16) & 0xFFu) * (1.0 / 15.0));

    float supplied = max(wet, 0.0) * asfloat(puddleData[PUDDLE_SUPPLY]);
    float free = max(supplied - asfloat(puddleData[PUDDLE_ABSORPTION]), 0.0);
    if (s.basinMax > 0.0 && free > 0.0)
        s.level = free >= s.basinMax / s.kappa ? s.basinMax : free * s.kappa;
    s.depth = clamp(s.level - (s.basinMax - capacity), 0.0, capacity);
    float edge = min(s.depth / puddleEdgeDepth, 1.0);
    s.coverage = s.depth > 0.0 ? edge * edge * (3.0 - 2.0 * edge) : 0.0;
    s.rippleGate = puddleSmoothstep(puddleRippleFrom, puddleRippleTo, s.depth);
    return s;
}

// The factor on the ground colour under water: 1 for a film, puddleDeepDarkening for deep water.
float puddleDepthTint(float depth)
{
    return 1.0 - (1.0 - puddleDeepDarkening) * puddleSmoothstep(0.0, puddleDeepDarkeningDepth, depth);
}

uint puddleDebugMode()
{
    return puddleData[PUDDLE_ENABLED] == 0u ? 0u : puddleData[PUDDLE_DEBUG];
}

// 1: capacity, 2: level, 3: cell (see Render::PuddleMap::debugColour).
float3 puddleDebugColour(PuddleShade s)
{
    uint mode = puddleDebugMode();
    if (mode == 1u)
    {
        if (!s.covered)
            return float3(0.25, 0.0, 0.0);
        return float3(min(s.capacity / puddleMaxCapacity, 1.0), 0.5 * min(s.basinMax / puddleMaxCapacity, 1.0), 0.12);
    }
    if (mode == 2u)
    {
        if (!s.covered)
            return float3(0.25, 0.0, 0.0);
        return float3(0.0, min(s.depth / puddleDebugDepthScale, 1.0),
            0.12 + 0.6 * min(s.level / puddleDebugLevelScale, 1.0));
    }
    if (mode == 3u)
    {
        if (!s.covered)
            return float3(0.2, 0.2, 0.2);
        uint h = wetHash(uint(s.cell.x) * 73856093u ^ uint(s.cell.y) * 19349663u);
        return float3(0.25 + 0.7 * float(h & 0xFFu) / 255.0, 0.25 + 0.7 * float((h >> 8) & 0xFFu) / 255.0,
            0.25 + 0.7 * float((h >> 16) & 0xFFu) / 255.0);
    }
    return float3(0.0, 0.0, 0.0);
}

#endif
