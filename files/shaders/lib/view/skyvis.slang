// The Slang twin of lib/view/skyvis.glsl (task 0072): the exterior sky-visibility field (task 0036). Include after
// lib/view/scene.slang and, with CLOUD_SHADOW, after lib/view/shadow.slang: lib/view/cloudshadow.slang then declares
// the header buffer, as the GLSL's cloudshadow.glsl does. The C++ twins and the reasons are named in the GLSL; change
// them together.
#ifndef LIB_VIEW_SKYVIS_SLANG
#define LIB_VIEW_SKYVIS_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

#ifndef CLOUD_SHADOW
[[vk::binding(VIEW_SKYVIS_SKY_BINDING, VIEW_SET)]] StructuredBuffer<float4> skyHeader;
#endif
[[vk::binding(VIEW_SKYVIS_BINDING, VIEW_SET)]] StructuredBuffer<float> skyData;

static const int SKY_HEADER_FIXED = 12;
static const int SKY_WINDOW = 7;
static const float SKY_NORMAL_BIAS = 0.3;
static const float SKY_SPHERE = 12.5663706;
static const float SKY_OPEN_GROUND = 1.7724539; // the constant term of the visibility of open flat ground

int skyFloorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

int skyColumnOffset(int cx, int cy, int divisions, int side, int stride, int2 window)
{
    int cellX = skyFloorDiv(cx, divisions);
    int cellY = skyFloorDiv(cy, divisions);
    int ix = cx - cellX * divisions;
    int iy = cy - cellY * divisions;
    for (int ax = 0; ax < 2; ++ax)
        for (int ay = 0; ay < 2; ++ay)
        {
            if ((ax == 1 && ix != 0) || (ay == 1 && iy != 0))
                continue;
            int sx = cellX - ax - window.x;
            int sy = cellY - ay - window.y;
            if (sx < 0 || sy < 0 || sx >= SKY_WINDOW || sy >= SKY_WINDOW)
                continue;
            float4 slot = skyHeader[SKY_HEADER_FIXED + sx + SKY_WINDOW * sy];
            if (slot.x < 0.5)
                continue;
            int cix = ax == 1 ? divisions : ix;
            int ciy = ay == 1 ? divisions : iy;
            return int(slot.y) + (cix + side * ciy) * stride;
        }
    return -1;
}

float skyVisCosine(float v[9], float3 n)
{
    float basis[9] = { 0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
        0.546274 * (n.x * n.x - n.y * n.y) };
    float band[9] = { 1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25 };
    float sum = 0.0;
    for (int k = 0; k < 9; ++k)
        sum += v[k] * basis[k] * band[k];
    return sum;
}

float3 skyOcclusion(float v[9], float3 normal)
{
    float3 ground = float3(0.0);
    float3 here = float3(0.0);
    for (int k = 0; k < 9; ++k)
        here += skyHeader[2 + k].xyz * v[k];
    here /= SKY_SPHERE;
    ground = skyHeader[11].xyz;
    float omni = v[0] / SKY_OPEN_GROUND;
    float3 skyRatio = float3(ground.x > 1e-6 ? here.x / ground.x : 1.0, ground.y > 1e-6 ? here.y / ground.y : 1.0,
        ground.z > 1e-6 ? here.z / ground.z : 1.0);
    float geometric = clamp(omni, 0.0, 1.0);
    float length2 = dot(normal, normal);
    if (length2 > 1e-20)
    {
        float3 n = normal * rsqrt(length2);
        float groundShare = 0.5 + 0.5 * n.z;
        float s = clamp(groundShare / 0.2, 0.0, 1.0);
        float weight = s * s * (3.0 - 2.0 * s);
        float facing = clamp(skyVisCosine(v, n) / max(groundShare, 1e-3), 0.0, 1.0);
        geometric = lerp(geometric, facing, weight);
    }
    float3 shift = omni > 1e-3 ? skyRatio / omni : float3(1.0);
    return clamp(geometric * shift, float3(0.0), float3(1.0));
}

float skyVisSample(float3 worldPos, float3 worldNormal, out float sum[9])
{
    float spacing = skyHeader[1].x;
    float levelSpacing = skyHeader[1].y;
    int levels = int(skyHeader[1].z);
    int side = int(skyHeader[1].w);
    int divisions = side - 1;
    int stride = 1 + levels * 9;
    int2 window = int2(int(skyHeader[0].z), int(skyHeader[0].w));

    float3 p = worldPos;
    float nl = length(worldNormal);
    if (nl > 1e-10)
        p += worldNormal * (SKY_NORMAL_BIAS * spacing / nl);
    float2 g = p.xy / spacing;
    int2 g0 = int2(floor(g));
    float2 f = g - float2(g0);

    float weightSum = 0.0;
    for (int k = 0; k < 9; ++k)
        sum[k] = 0.0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
        {
            float w = (dx == 1 ? f.x : 1.0 - f.x) * (dy == 1 ? f.y : 1.0 - f.y);
            if (w <= 0.0)
                continue;
            int offset = skyColumnOffset(g0.x + dx, g0.y + dy, divisions, side, stride, window);
            if (offset < 0)
                continue;
            float level = clamp((p.z - skyData[offset]) / levelSpacing - 0.5, 0.0, float(levels - 1));
            int l0 = int(floor(level));
            int l1 = min(l0 + 1, levels - 1);
            float t = level - float(l0);
            for (int k = 0; k < 9; ++k)
                sum[k] += lerp(skyData[offset + 1 + l0 * 9 + k], skyData[offset + 1 + l1 * 9 + k], t) * w;
            weightSum += w;
        }
    if (weightSum > 1e-4)
        for (int k = 0; k < 9; ++k)
            sum[k] /= weightSum;
    return weightSum;
}

float3 skyVisAmbient(float3 worldPos, float3 worldNormal, float3 flatAmbient)
{
    if (skyHeader[0].x < 0.5)
        return flatAmbient;
    float sum[9];
    float weightSum = skyVisSample(worldPos, worldNormal, sum);
    if (weightSum <= 1e-4)
        return flatAmbient;
    float3 occlusion = skyOcclusion(sum, worldNormal);
    float strength = skyHeader[0].y;
    float3 kept = max(float3(1.0) - strength * (float3(1.0) - occlusion), float3(0.0));
    return flatAmbient * lerp(float3(1.0), kept, weightSum);
}

float2 skyVisUp(float3 worldPos)
{
    if (skyHeader[0].x < 0.5)
        return float2(1.0, 0.0);
    float sum[9];
    float weightSum = skyVisSample(worldPos, float3(0.0, 0.0, 1.0), sum);
    if (weightSum <= 1e-4)
        return float2(1.0, 0.0);
    return float2(skyVisCosine(sum, float3(0.0, 0.0, 1.0)), weightSum);
}

// OpenEden (the glaze fix): the factor the exterior's sky environment image is multiplied by on an object. Twins:
// skyReflectionRoughnessWeight, skyDirectionVisibility and skyReflectionFactor in components/render/skyvismath.hpp.
static const float SKY_REFLECTION_UNBAKED = 0.25; // skyReflectionUnbakedVisibility: a point with no baked data

float skyReflectionRoughnessWeight(float roughness, float metalness)
{
    float smoothness = 1.0 - clamp(roughness, 0.0, 1.0);
    float m = clamp(metalness, 0.0, 1.0);
    return smoothness * smoothness * (1.0 - m) + m;
}

// The geometric part of skyOcclusion for the mirror direction `dir`, without the colour shift.
float skyDirectionVisibility(float v[9], float3 dir)
{
    float omni = clamp(v[0] / SKY_OPEN_GROUND, 0.0, 1.0);
    float length2 = dot(dir, dir);
    if (!(length2 > 1e-20))
        return omni;
    float3 n = dir * rsqrt(length2);
    float groundShare = 0.5 + 0.5 * n.z;
    float s = clamp(groundShare / 0.2, 0.0, 1.0);
    float weight = s * s * (3.0 - 2.0 * s);
    float facing = clamp(skyVisCosine(v, n) / max(groundShare, 1e-3), 0.0, 1.0);
    return lerp(omni, facing, weight);
}

// skyVisAmbient, also working out how much sky the point sees in the mirror direction `reflDir` (skyDirectionVisibility)
// from the same sample, so the object shader does not sample the field twice. `reflVisibility` is 1 and `dataWeight` 0
// where there is no data.
float3 skyVisAmbientKeep(float3 worldPos, float3 worldNormal, float3 flatAmbient, float3 reflDir, out float reflVisibility, out float dataWeight)
{
    reflVisibility = 1.0;
    dataWeight = 0.0;
    if (skyHeader[0].x < 0.5)
        return flatAmbient;
    float sum[9];
    float weightSum = skyVisSample(worldPos, worldNormal, sum);
    if (weightSum <= 1e-4)
        return flatAmbient;
    dataWeight = weightSum;
    reflVisibility = skyDirectionVisibility(sum, reflDir);
    float3 occlusion = skyOcclusion(sum, worldNormal);
    float strength = skyHeader[0].y;
    float3 kept = max(float3(1.0) - strength * (float3(1.0) - occlusion), float3(0.0));
    return flatAmbient * lerp(float3(1.0), kept, weightSum);
}

// The factor the sky image's reflection is multiplied by (skyReflectionFactor in skyvismath.hpp), from what
// skyVisAmbientKeep handed out: the sky seen in the mirror direction, a fixed fraction where there is no data.
float skyReflectionFactor(float reflVisibility, float dataWeight, float roughness, float metalness)
{
    float w = clamp(dataWeight, 0.0, 1.0);
    float vis = SKY_REFLECTION_UNBAKED * (1.0 - w) + clamp(reflVisibility, 0.0, 1.0) * w;
    return vis * skyReflectionRoughnessWeight(roughness, metalness);
}

#endif
