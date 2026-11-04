// The Slang twin of lib/view/gi.glsl (task 0072, stage f): baked light probes (task 0028). The layout of the buffer and
// the reasons are in the GLSL; change both together (and components/render/probesamplermath.hpp).
#ifndef LIB_VIEW_GI_SLANG
#define LIB_VIEW_GI_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

[[vk::binding(VIEW_PROBE_BINDING, VIEW_SET)]] StructuredBuffer<float4> giData;

float3 giProbeAmbient(int index, float3 n)
{
    int base = 4 + 9 * index;
    float basis[9] = { 0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
        0.546274 * (n.x * n.x - n.y * n.y) };
    float band[9] = { 1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25 };
    float3 sum = float3(0.0);
    for (int k = 0; k < 9; ++k)
        sum += giData[base + k].xyz * (basis[k] * band[k]);
    return max(sum, float3(0.0));
}

float3 giSampleAmbient(float3 worldPos, float3 worldNormal)
{
    float3 n = dot(worldNormal, worldNormal) > 1e-20 ? normalize(worldNormal) : float3(0.0, 0.0, 1.0);
    float spacing = giData[0].w;
    float3 p = worldPos + n * (giData[2].x * spacing);
    float3 size = giData[1].xyz;
    float3 g = clamp((p - giData[0].xyz) / spacing, float3(0.0), size - 1.0);
    float3 f = floor(g);
    float3 t = g - f;
    int3 i0 = int3(f);
    int3 i1 = min(i0 + 1, int3(size) - 1);
    int sx = int(size.x);
    int sy = int(size.y);
    float3 x00 = lerp(
        giProbeAmbient(i0.x + sx * (i0.y + sy * i0.z), n), giProbeAmbient(i1.x + sx * (i0.y + sy * i0.z), n), t.x);
    float3 x10 = lerp(
        giProbeAmbient(i0.x + sx * (i1.y + sy * i0.z), n), giProbeAmbient(i1.x + sx * (i1.y + sy * i0.z), n), t.x);
    float3 x01 = lerp(
        giProbeAmbient(i0.x + sx * (i0.y + sy * i1.z), n), giProbeAmbient(i1.x + sx * (i0.y + sy * i1.z), n), t.x);
    float3 x11 = lerp(
        giProbeAmbient(i0.x + sx * (i1.y + sy * i1.z), n), giProbeAmbient(i1.x + sx * (i1.y + sy * i1.z), n), t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}

float giVolumeWeight(float3 worldPos)
{
    float3 lo = giData[0].xyz;
    float3 hi = lo + giData[0].w * (giData[1].xyz - 1.0);
    float3 outside = max(lo - worldPos, worldPos - hi);
    float d = max(max(outside.x, outside.y), max(outside.z, 0.0));
    return clamp(1.0 - d / giData[0].w, 0.0, 1.0);
}

float3 giAmbient(float3 worldPos, float3 worldNormal, float3 flatAmbient)
{
    if (giData[1].w < 0.5)
        return flatAmbient;
    float w = giVolumeWeight(worldPos);
    float3 offset = flatAmbient - giData[3].xyz;
    float3 gi = max(giSampleAmbient(worldPos, worldNormal) * giData[2].y + offset, float3(0.0));
    return lerp(flatAmbient, gi, w);
}

#endif
