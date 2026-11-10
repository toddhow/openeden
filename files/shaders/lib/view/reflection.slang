// The Slang twin of lib/view/reflection.glsl (task 0072): the baked reflection probes (task 0028). The C++ twins and the
// reasons are named in the GLSL; change them together.
#ifndef LIB_VIEW_REFLECTION_SLANG
#define LIB_VIEW_REFLECTION_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

[[vk::binding(VIEW_REFLECTION_BINDING, VIEW_SET)]] StructuredBuffer<float4> reflData;

static const int reflTexelsBase = 19;

bool giReflectionEnabled()
{
    return reflData[0].w > 0.5;
}

bool giReflectionIsSkyEnvironment()
{
    return reflData[2].x > 0.5;
}

int reflLevelOffset(int res, int level)
{
    int offset = 0;
    for (int l = 0; l < level; ++l)
    {
        int size = res >> l;
        offset += size * size;
    }
    return offset;
}

float2 reflOctEncode(float3 dir)
{
    float sum = abs(dir.x) + abs(dir.y) + abs(dir.z);
    if (!(sum > 0.0))
        return float2(0.5);
    float2 e = dir.xy / sum;
    if (dir.z < 0.0)
        e = float2((1.0 - abs(e.y)) * (e.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(e.x)) * (e.y >= 0.0 ? 1.0 : -1.0));
    return e * 0.5 + 0.5;
}

float3 reflReadLevel(int base, int size, float2 uv)
{
    float maxIndex = float(size - 1);
    float2 f = clamp(uv * float(size) - 0.5, float2(0.0), float2(maxIndex));
    int2 i0 = int2(f);
    int2 i1 = min(i0 + 1, int2(size - 1));
    float2 t = f - float2(i0);
    float3 top = lerp(reflData[base + i0.y * size + i0.x].xyz, reflData[base + i0.y * size + i1.x].xyz, t.x);
    float3 bottom = lerp(reflData[base + i1.y * size + i0.x].xyz, reflData[base + i1.y * size + i1.x].xyz, t.x);
    return lerp(top, bottom, t.y);
}

float3 reflSampleProbe(int probe, int res, int levels, float3 dir, float roughness)
{
    int chain = reflLevelOffset(res, levels);
    int base = reflTexelsBase + probe * chain;
    float lod = clamp(roughness, 0.0, 1.0) * float(levels - 1);
    int l0 = min(int(lod), levels - 1);
    int l1 = min(l0 + 1, levels - 1);
    float t = lod - float(l0);
    float2 uv = reflOctEncode(dir);
    float3 a = reflReadLevel(base + reflLevelOffset(res, l0), res >> l0, uv);
    if (l1 == l0 || t <= 0.0)
        return a;
    float3 b = reflReadLevel(base + reflLevelOffset(res, l1), res >> l1, uv);
    return lerp(a, b, t);
}

float2 reflEnvBrdf(float roughness, float nDotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return max(ab, float2(0.0));
}

float3 giReflection(float3 worldPos, float3 dir, float roughness, float3 flatAmbient)
{
    int count = int(reflData[0].x + 0.5);
    int res = int(reflData[0].y + 0.5);
    int levels = int(reflData[0].z + 0.5);
    float d0 = 1e30, d1 = 1e30, d2 = 1e30;
    int i0 = 0, i1 = 0;
    for (int i = 0; i < count; ++i)
    {
        float dist = length(reflData[3 + i].xyz - worldPos);
        if (dist < d0)
        {
            d2 = d1;
            d1 = d0;
            i1 = i0;
            d0 = dist;
            i0 = i;
        }
        else if (dist < d1)
        {
            d2 = d1;
            d1 = dist;
            i1 = i;
        }
        else if (dist < d2)
            d2 = dist;
    }
    float3 light;
    if (count < 2)
        light = reflSampleProbe(i0, res, levels, dir, roughness);
    else
    {
        float w0, w1;
        if (count == 2)
        {
            w0 = d1;
            w1 = d0;
        }
        else
        {
            w0 = max(0.0, 1.0 - d0 / d2);
            w1 = max(0.0, 1.0 - d1 / d2);
        }
        float total = w0 + w1;
        if (total > 1e-6)
        {
            w0 /= total;
            w1 /= total;
        }
        else
        {
            w0 = 1.0;
            w1 = 0.0;
        }
        light = reflSampleProbe(i0, res, levels, dir, roughness) * w0;
        if (w1 > 0.0)
            light += reflSampleProbe(i1, res, levels, dir, roughness) * w1;
    }
    float3 offset = flatAmbient - reflData[1].yzw;
    return max(light + offset, float3(0.0)) * reflData[1].x;
}

#endif
