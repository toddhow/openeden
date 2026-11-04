// The Slang twin of lib/material/cover.glsl (task 0072): snow and ash cover (task 0039). Include after
// lib/view/scene.slang (and after lib/view/skyvis.slang when GI_SKY is defined). The C++ twin,
// components/render/covermath.hpp, documents each constant; change all three together (a unit test reads the
// constants back from both shader files).
#ifndef LIB_MATERIAL_COVER_SLANG
#define LIB_MATERIAL_COVER_SLANG

#include "lib/material/wetness.slang"

static const float coverUpFrom = 0.5;
static const float coverUpTo = 0.8;
static const float coverEdge = 0.08;
static const float coverNoiseCell = 36.0;
static const float coverSteps = 128.0;
static const float coverSnowR = 0.78;
static const float coverSnowG = 0.8;
static const float coverSnowB = 0.84;
static const float coverAshR = 0.075;
static const float coverAshG = 0.072;
static const float coverAshB = 0.07;
static const float coverSnowRoughness = 0.75;
static const float coverAshRoughness = 0.92;

// kind: 0 bare, 1 snow, 2 ash.
struct CoverSurface
{
    float mask; // the share of the pixel that is covered
    int kind;
    float amount; // how much cover the ground has
    float upwardness;
    float exposure; // 0 under a roof, 1 in the open
};

float coverUpwardness(float normalZ)
{
    return smoothstep(coverUpFrom, coverUpTo, normalZ);
}

float coverMaskOf(float amount, float up, float exposure, float noiseValue)
{
    float strength = clamp(clamp(amount, 0.0, 1.0) * clamp(up, 0.0, 1.0) * clamp(exposure, 0.0, 1.0), 0.0, 1.0);
    float x = strength * (1.0 + 2.0 * coverEdge) - coverEdge;
    return smoothstep(noiseValue - coverEdge, noiseValue + coverEdge, x);
}

float3 coverColourOf(int kind)
{
    if (kind == 1)
        return float3(coverSnowR, coverSnowG, coverSnowB);
    return float3(coverAshR, coverAshG, coverAshB);
}

float coverRoughnessOf(int kind)
{
    return kind == 1 ? coverSnowRoughness : coverAshRoughness;
}

// What the scene's alpha says about the cover (Render::Cover::unpack): the kind in .x (0, 1, 2) and the amount in .y.
float2 coverUnpack(float alpha)
{
    float raw = alpha - 1.0;
    if (!(raw > 0.0))
        return float2(0.0);
    float code = floor(raw * 0.5);
    if (code < 1.0)
        return float2(0.0);
    if (code <= coverSteps)
        return float2(1.0, code / coverSteps);
    return float2(2.0, min(code - coverSteps, coverSteps) / coverSteps);
}

// The cover at one point. `worldNormal` is the surface's world normal (Z up).
CoverSurface coverSurfaceAt(float3 worldPos, float3 worldNormal)
{
    CoverSurface s;
    s.mask = 0.0;
    s.kind = 0;
    s.amount = 0.0;
    s.upwardness = 0.0;
    s.exposure = 1.0;
    float2 coverCode = coverUnpack(scene.ambient.w);
    if (coverCode.x < 0.5)
        return s;
    s.kind = coverCode.x > 1.5 ? 2 : 1;
    s.amount = coverCode.y;
    s.upwardness = coverUpwardness(worldNormal.z);
    if (s.upwardness <= 0.0)
        return s;
#ifdef GI_SKY
    float2 sky = skyVisUp(worldPos);
    s.exposure = wetExposure(sky.x, sky.y);
#endif
    s.mask = coverMaskOf(s.amount, s.upwardness, s.exposure, wetNoise(worldPos.xy / coverNoiseCell));
    return s;
}

#endif
