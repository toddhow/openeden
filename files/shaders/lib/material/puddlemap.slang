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
static const uint PUDDLE_TOPS_BASE = 10u;
static const uint PUDDLE_OPTIONS = 11u;
static const uint PUDDLE_OPTION_KEEP_STATIC_NOISE = 1u;
static const uint PUDDLE_OPTION_TOPOGRAPHIC = 2u;
static const uint PUDDLE_TOPO_STRENGTH = 12u;
static const uint PUDDLE_TABLE_BASE = 16u;
static const uint PUDDLE_TEXEL_STATIC = 0x10u;

static const float puddleEdgeDepth = 0.3;
static const float puddleRippleFrom = 0.3;
static const float puddleRippleTo = 0.9;
static const float puddleDeepDarkening = 0.8;
static const float puddleDeepDarkeningDepth = 3.0;
static const float puddleMaxCapacity = 15.9375;
static const float puddleDebugDepthScale = 6.0;
static const float puddleDebugLevelScale = 12.0;
static const float puddleZTolerance = 2.0;
static const float puddleObjectMinNormalZ = 0.7;
// Stage W-e, the topographic fallback; Render::PuddleMap::topo* (components/render/puddlemapgpu.hpp) documents each.
static const float puddleTopoNoiseCell = 64.0;
static const float puddleTopoMaxCoverage = 0.3;
static const float puddleTopoDipFrom = 0.4;
static const float puddleTopoDipTo = 4.0;
static const float puddleTopoDipBias = 0.35;
static const float puddleTopoEdge = 0.06;
static const float puddleTopoMaxDepth = 0.6;

struct PuddleShade
{
    bool loaded;
    bool covered;
    float top;
    float staticShare;
    float capacity;
    float basinMax;
    float kappa;
    float level;
    float depth;
    float coverage;
    float rippleGate;
    float dip;
    float dipFactor;
    bool topographic;
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

// x: 1 when the buffer has tops and the texel's cell is loaded, y: the world Z of the surface there.
float2 puddleTexelTop(int gx, int gy)
{
    uint topsBase = puddleData[PUDDLE_TOPS_BASE];
    int side = int(puddleData[PUDDLE_TEXELS]);
    if (topsBase == 0u || side <= 0)
        return float2(0.0, 0.0);
    int cx = puddleFloorDiv(gx, side);
    int cy = puddleFloorDiv(gy, side);
    int windowSide = int(puddleData[PUDDLE_WINDOW_SIDE]);
    int sx = cx - int(puddleData[PUDDLE_ORIGIN_X]);
    int sy = cy - int(puddleData[PUDDLE_ORIGIN_Y]);
    if (sx < 0 || sy < 0 || sx >= windowSide || sy >= windowSide)
        return float2(0.0, 0.0);
    int slot = sx + windowSide * sy;
    if (puddleData[PUDDLE_TABLE_BASE + uint(slot)] == 0u)
        return float2(0.0, 0.0);
    uint cells = uint(windowSide * windowSide);
    uint local = uint((gy - cy * side) * side + (gx - cx * side));
    uint word = puddleData[topsBase + cells + uint(slot) * uint(side * side) + local];
    float base = asfloat(puddleData[topsBase + uint(slot)]);
    return float2(1.0, base + float(word & 0xFFFFu) * 0.125);
}

float puddleSmoothstep(float a, float b, float x)
{
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The height of texel (gx, gy), or `fallback` where the cell is not loaded.
float puddleHeightOrElse(int gx, int gy, float fallback)
{
    float2 top = puddleTexelTop(gx, gy);
    return top.x != 0.0 ? top.y : fallback;
}

// The dip of the ground at texel coordinates p, in world units, positive in a hollow (Render::PuddleMap::groundDip):
// the four texels round the point each have the mean of their four neighbours' heights minus their own.
float puddleGroundDip(float2 p)
{
    if (puddleData[PUDDLE_TOPS_BASE] == 0u)
        return 0.0;
    float2 base = floor(p);
    float2 t = p - base;
    int2 i0 = int2(base);
    float weights[4] = { (1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y };
    float dip = 0.0;
    for (int k = 0; k < 4; ++k)
    {
        int gx = i0.x + (k & 1);
        int gy = i0.y + (k >> 1);
        float2 centre = puddleTexelTop(gx, gy);
        if (centre.x == 0.0)
            continue;
        float mean = 0.25 * (puddleHeightOrElse(gx - 1, gy, centre.y) + puddleHeightOrElse(gx + 1, gy, centre.y)
            + puddleHeightOrElse(gx, gy - 1, centre.y) + puddleHeightOrElse(gx, gy + 1, centre.y));
        dip += weights[k] * (mean - centre.y);
    }
    return dip;
}

// How much water the ground has taken up, 0 to 1 (Render::PuddleMap::topoRamp).
float puddleTopoRamp(float wet)
{
    float supply = asfloat(puddleData[PUDDLE_SUPPLY]);
    float absorption = asfloat(puddleData[PUDDLE_ABSORPTION]);
    float full = supply - absorption;
    if (!(full > 0.0))
        return 0.0;
    return clamp((max(wet, 0.0) * supply - absorption) / full, 0.0, 1.0);
}

// The share of the pixel that is puddle in the topographic fallback (Render::PuddleMap::topoCoverage).
float puddleTopoCoverage(float ramp, float dipFactor, float noiseValue, float normalZ, float strength)
{
    float fill = puddleTopoMaxCoverage * clamp(strength, 0.0, 2.0) * clamp(ramp, 0.0, 1.0);
    if (!(fill > 0.0))
        return 0.0;
    float threshold = 1.0 - fill;
    float n = noiseValue + puddleTopoDipBias * dipFactor;
    return wetFlatness(normalZ) * smoothstep(threshold - puddleTopoEdge, threshold + puddleTopoEdge, n);
}

PuddleShade puddleShadeAt(float3 worldPos, float wet, float normalZ, bool objectMode)
{
    float2 pos = worldPos.xy;
    PuddleShade s;
    s.loaded = false;
    s.covered = false;
    s.top = 0.0;
    s.staticShare = 0.0;
    s.capacity = 0.0;
    s.basinMax = 0.0;
    s.kappa = 0.0;
    s.level = 0.0;
    s.depth = 0.0;
    s.coverage = 0.0;
    s.rippleGate = 0.0;
    s.dip = 0.0;
    s.dipFactor = 0.0;
    s.topographic = false;
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
    s.loaded = puddleCellStart(s.cell.x, s.cell.y) != 0u;
    if (!s.loaded)
        return s;
    s.covered = true;

    float2 p = pos / spacing;
    float2 base = floor(p);
    float2 t = p - base;
    int2 i0 = int2(base);
    float weights[4] = { (1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y };
    float capacity = 0.0;
    float bestScore = 0.0;
    uint pit = 0u;
    float topSum = 0.0;
    float topWeight = 0.0;
    float staticWeight = 0.0;
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
        float2 tapTop = puddleTexelTop(i0.x + (k & 1), i0.y + (k >> 1));
        if (tapTop.x != 0.0)
        {
            topSum += weights[k] * tapTop.y;
            topWeight += weights[k];
        }
        if (((texel.y >> 24) & PUDDLE_TEXEL_STATIC) != 0u)
            staticWeight += weights[k];
    }
    if (topWeight > 0.0)
        s.top = topSum / topWeight;
    s.staticShare = staticWeight;
    // The height test (stage W-c). Without tops in the buffer there is nothing to test against.
    if (topWeight > 0.0 && (objectMode || staticWeight > 0.0) && !(abs(worldPos.z - s.top) <= puddleZTolerance))
        s.covered = false;
    if (objectMode && !(normalZ >= puddleObjectMinNormalZ))
        s.covered = false;
    // The bake cannot see the micro relief of paving: a flat static column keeps the noise puddles (the header option).
    if (s.covered && objectMode && (puddleData[PUDDLE_OPTIONS] & PUDDLE_OPTION_KEEP_STATIC_NOISE) != 0u
        && staticWeight >= 0.5 && !(capacity > 0.0))
        s.covered = false;
    if (!s.covered)
        return s;
    s.capacity = capacity;
    if (!(capacity > 0.0))
    {
        // Stage W-e: no pit here (a road, a plateau, a field). Small shallow puddles from the fine shape of the ground.
        if ((puddleData[PUDDLE_OPTIONS] & PUDDLE_OPTION_TOPOGRAPHIC) != 0u)
        {
            s.topographic = true;
            s.dip = puddleGroundDip(p);
            s.dipFactor = puddleSmoothstep(puddleTopoDipFrom, puddleTopoDipTo, s.dip);
            s.coverage = puddleTopoCoverage(puddleTopoRamp(wet), s.dipFactor,
                wetNoise(pos / puddleTopoNoiseCell), normalZ, asfloat(puddleData[PUDDLE_TOPO_STRENGTH]));
            s.depth = puddleTopoMaxDepth * s.coverage;
            s.level = s.depth;
            s.rippleGate = puddleSmoothstep(puddleRippleFrom, puddleRippleTo, s.depth);
        }
        return s;
    }
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

// 1: capacity, 2: level, 3: cell, 4: statics, 5: relief (see Render::PuddleMap::debugColour).
float3 puddleDebugColour(PuddleShade s)
{
    uint mode = puddleDebugMode();
    if (mode == 5u)
    {
        if (!s.covered)
            return float3(0.25, 0.0, 0.0);
        return float3(s.capacity > 0.0 ? 0.6 : 0.0, s.dipFactor, 0.1 + 0.9 * s.coverage);
    }
    if (mode == 4u)
    {
        if (!s.loaded)
            return float3(0.2, 0.2, 0.2);
        if (!s.covered)
            return float3(0.25, 0.0, 0.0);
        return float3(0.1 + 0.9 * s.staticShare, 0.2 * (1.0 - s.staticShare), 0.1 + 0.9 * s.staticShare);
    }
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
