// OpenEden (task 0039): snow and ash cover. The C++ twin is components/render/covermath.hpp (Render::Cover), which
// documents each constant; change both together (a unit test reads the constants below back from this file). Include
// after lib/view/scene.glsl (and after lib/view/skyvis.glsl when GI_SKY is defined, which gives the shelter test).
//
// The cover rides in scene.ambient.w with the wetness of task 0037: 1 + wetness + 2 * code, the code being 0 (bare), 1
// to coverSteps (snow, in steps) or coverSteps + 1 to 2 * coverSteps (ash). Without cover that is the old 1 + wetness.
#ifndef LIB_MATERIAL_COVER
#define LIB_MATERIAL_COVER

#include "lib/material/wetness.glsl"

const float coverUpFrom = 0.5;
const float coverUpTo = 0.8;
const float coverEdge = 0.08;
const float coverNoiseCell = 36.0;
const float coverSteps = 128.0;
const float coverSnowR = 0.78;
const float coverSnowG = 0.8;
const float coverSnowB = 0.84;
const float coverAshR = 0.075;
const float coverAshG = 0.072;
const float coverAshB = 0.07;
const float coverSnowRoughness = 0.75;
const float coverAshRoughness = 0.92;

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

vec3 coverColourOf(int kind)
{
    if (kind == 1)
        return vec3(coverSnowR, coverSnowG, coverSnowB);
    return vec3(coverAshR, coverAshG, coverAshB);
}

float coverRoughnessOf(int kind)
{
    return kind == 1 ? coverSnowRoughness : coverAshRoughness;
}

// What the scene's alpha says about the cover (Render::Cover::unpack): the kind in .x (0, 1, 2) and the amount in .y.
vec2 coverUnpack(float alpha)
{
    float raw = alpha - 1.0;
    if (!(raw > 0.0))
        return vec2(0.0);
    float code = floor(raw * 0.5);
    if (code < 1.0)
        return vec2(0.0);
    if (code <= coverSteps)
        return vec2(1.0, code / coverSteps);
    return vec2(2.0, min(code - coverSteps, coverSteps) / coverSteps);
}

// The cover at one point. `worldNormal` is the surface's world normal (Z up).
CoverSurface coverSurfaceAt(vec3 worldPos, vec3 worldNormal)
{
    CoverSurface s;
    s.mask = 0.0;
    s.kind = 0;
    s.amount = 0.0;
    s.upwardness = 0.0;
    s.exposure = 1.0;
    vec2 coverCode = coverUnpack(scene.ambient.w);
    if (coverCode.x < 0.5)
        return s;
    s.kind = coverCode.x > 1.5 ? 2 : 1;
    s.amount = coverCode.y;
    s.upwardness = coverUpwardness(worldNormal.z);
    if (s.upwardness <= 0.0)
        return s;
#ifdef GI_SKY
    vec2 sky = skyVisUp(worldPos);
    s.exposure = wetExposure(sky.x, sky.y);
#endif
    s.mask = coverMaskOf(s.amount, s.upwardness, s.exposure, wetNoise(worldPos.xy / coverNoiseCell));
    return s;
}

#endif
