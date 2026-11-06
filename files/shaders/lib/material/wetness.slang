// The Slang twin of lib/material/wetness.glsl (task 0072): rain wetness and puddles (task 0037). Include after
// lib/view/scene.slang (and after lib/view/skyvis.slang when GI_SKY is defined). The C++ twin,
// components/render/wetnessmath.hpp, documents each constant; change all three together.
#ifndef LIB_MATERIAL_WETNESS_SLANG
#define LIB_MATERIAL_WETNESS_SLANG

#ifdef GI_SKY
import lib.view.reflection;
#endif

static const float wetMaxDarkening = 0.45;
static const float wetSmoothDarkeningShare = 0.4;
static const float wetWetRoughness = 0.18;
static const float wetPuddleRoughness = 0.03;
static const float wetPuddleDarkening = 0.7;
static const float wetWaterF0 = 0.02;
static const float wetPuddleStart = 0.5;
static const float wetPuddleReflection = 0.5;
static const float wetPuddleMaxCoverage = 0.4;
static const float wetPuddleEdge = 0.06;
static const float wetFlatFrom = 0.94;
static const float wetFlatTo = 0.99;
static const float wetPuddleNoiseCell = 120.0;
static const float wetCoverBelow = 0.2;
static const float wetCoverAbove = 0.6;
static const float wetRippleCell = 48.0;
static const float wetRippleMaxRadius = 16.0;
static const float wetRipplePeriod = 1.1;
static const float wetRippleWidth = 3.0;
static const float wetRippleWavelength = 6.0;
static const float wetRippleMaxSlope = 0.22;
static const float wetTwoPi = 6.28318530717959;

float wetDarkening(float wet, float surfaceRoughness)
{
    float rough = clamp(surfaceRoughness, 0.0, 1.0);
    return 1.0
        - wetMaxDarkening * clamp(wet, 0.0, 1.0) * (wetSmoothDarkeningShare + (1.0 - wetSmoothDarkeningShare) * rough);
}

float wetRoughnessOf(float base, float wet)
{
    if (base <= wetWetRoughness)
        return base;
    return base - (base - wetWetRoughness) * clamp(wet, 0.0, 1.0);
}

float wetFlatness(float normalZ)
{
    return smoothstep(wetFlatFrom, wetFlatTo, normalZ);
}

float wetPuddleFill(float wet)
{
    return wetPuddleMaxCoverage * clamp((clamp(wet, 0.0, 1.0) - wetPuddleStart) / (1.0 - wetPuddleStart), 0.0, 1.0);
}

uint wetHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float wetLattice(int ix, int iy, uint salt)
{
    uint h = wetHash(uint(ix) * 73856093u ^ uint(iy) * 19349663u ^ salt);
    return float(h >> 8) * (1.0 / 16777216.0);
}

float wetValueNoise(float2 p, uint salt)
{
    float2 fp = floor(p);
    int ix = int(fp.x);
    int iy = int(fp.y);
    float2 t = p - fp;
    float2 s = t * t * (3.0 - 2.0 * t);
    float a = wetLattice(ix, iy, salt);
    float b = wetLattice(ix + 1, iy, salt);
    float c = wetLattice(ix, iy + 1, salt);
    float d = wetLattice(ix + 1, iy + 1, salt);
    float bottom = a + (b - a) * s.x;
    float top = c + (d - c) * s.x;
    return bottom + (top - bottom) * s.y;
}

float wetNoise(float2 p)
{
    float n = 0.65 * wetValueNoise(p, 0x1234u) + 0.35 * wetValueNoise(p * 2.0 + float2(17.0, 5.0), 0x5678u);
    return clamp((n - 0.5) * 1.8 + 0.5, 0.0, 0.999999);
}

float wetPuddleMask(float wet, float normalZ, float noiseValue)
{
    float fill = wetPuddleFill(wet);
    if (!(fill > 0.0))
        return 0.0;
    float threshold = 1.0 - fill;
    return wetFlatness(normalZ) * smoothstep(threshold - wetPuddleEdge, threshold + wetPuddleEdge, noiseValue);
}

float wetExposure(float visibilityUp, float dataWeight)
{
    float open = smoothstep(wetCoverBelow, wetCoverAbove, visibilityUp);
    return 1.0 + (open - 1.0) * clamp(dataWeight, 0.0, 1.0);
}

float wetFresnel(float cosTheta)
{
    float t = pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
    return wetWaterF0 + (1.0 - wetWaterF0) * t;
}

float2 wetRipple(float2 world, float time, float rain)
{
    float strength = clamp(rain, 0.0, 1.0);
    if (!(strength > 0.0))
        return float2(0.0);
    float2 cell = floor(world / wetRippleCell);
    int ix = int(cell.x);
    int iy = int(cell.y);
    float jitterX = (wetLattice(ix, iy, 0x9e37u) - 0.5) * 0.3;
    float jitterY = (wetLattice(ix, iy, 0x79b9u) - 0.5) * 0.3;
    float2 centre = (cell + 0.5 + float2(jitterX, jitterY)) * wetRippleCell;
    float phase = wetLattice(ix, iy, 0x7f4au);
    float cycle = time / wetRipplePeriod + phase;
    float age01 = cycle - floor(cycle);
    float2 delta = world - centre;
    float dist = length(delta);
    float u = dist - age01 * wetRippleMaxRadius;
    float envelope = exp(-(u * u) / (wetRippleWidth * wetRippleWidth));
    float fade = (1.0 - age01) * (1.0 - age01);
    float magnitude = wetRippleMaxSlope * strength * fade * envelope * cos(u * wetTwoPi / wetRippleWavelength);
    if (!(dist > 1e-4))
        return float2(0.0);
    return delta * (magnitude / dist);
}

// What a surface gets from the rain at one point (see the GLSL). `worldNormal` is the surface's world normal (Z up).
struct WetSurface
{
    float wet;
    float exposure;
    float puddle;
};

WetSurface wetSurfaceAt(float3 worldPos, float3 worldNormal)
{
    WetSurface s;
    s.wet = 0.0;
    s.exposure = 1.0;
    s.puddle = 0.0;
    float raw = max(scene.ambient.w - 1.0, 0.0);
    raw -= 2.0 * floor(raw * 0.5);
    if (raw <= 0.0)
        return s;
#ifdef GI_SKY
    float2 sky = skyVisUp(worldPos);
    s.exposure = wetExposure(sky.x, sky.y);
#endif
    s.wet = raw * s.exposure;
#ifdef WETNESS_PUDDLES
    if (s.wet > wetPuddleStart)
        s.puddle = wetPuddleMask(s.wet, worldNormal.z, wetNoise(worldPos.xy / wetPuddleNoiseCell));
#endif
    return s;
}

// The world normal of a puddle's surface: up, tilted by the rain's ripples.
float3 wetPuddleNormal(float3 worldPos)
{
    float2 slope = wetRipple(worldPos.xy, scene.time, scene.rainIntensity);
    return normalize(float3(-slope.x, -slope.y, 1.0));
}

// The sky seen in `dir` (world): the exterior probe's sky image when it is there, else a gradient.
float3 wetSkyReflection(float3 worldPos, float3 dir, float surfaceRoughness)
{
#ifdef GI_SKY
    if (giReflectionEnabled())
        return giReflection(worldPos, dir, surfaceRoughness, scene.ambient.xyz);
#endif
    float up = clamp(dir.z, 0.0, 1.0);
    return lerp(scene.fogColor.xyz, scene.skyColor.xyz, sqrt(up));
}

#endif
