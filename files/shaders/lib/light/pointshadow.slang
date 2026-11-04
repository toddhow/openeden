// Shadows of the point lights (task 0077). Include after lib/view/scene.slang. The GLSL twin is pointshadow.glsl, the C++
// twin components/render/pointshadowmath.hpp, and the buffer's layout is components/render/pointshadowgpu.hpp: change
// them together.
//
// A light with a slot has six depth faces (a 2D array, layer 6 * slot + face) drawn from its position in world axes.
// The caller sets `pointShadowNormal`, the receiver's geometric normal in view space (unit), then asks
// `pointLightShadow` for each light of the cluster's list; the answer is the lit fraction, 1 for a light with no slot.
#ifndef LIB_LIGHT_POINTSHADOW_SLANG
#define LIB_LIGHT_POINTSHADOW_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

static const int POINT_SHADOW_HEADER_VEC4S = 2;
static const int POINT_SHADOW_MAX_SLOTS = 8;

[[vk::binding(VIEW_POINT_SHADOW_MAP_BINDING, VIEW_SET)]] Texture2DArray<float> pointShadowMap;
[[vk::binding(VIEW_POINT_SHADOW_SAMPLER_BINDING, VIEW_SET)]] SamplerComparisonState pointShadowSampler;
[[vk::binding(VIEW_POINT_SHADOW_DATA_BINDING, VIEW_SET)]] StructuredBuffer<float4> pointShadowData;

// The receiver's geometric normal in view space, set by the caller before the light loop.
static float3 pointShadowNormal = float3(0.0, 0.0, 1.0);

static const float2 pointShadowPoisson[16] = { float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760), float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188), float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790) };

// Faces 0..5 = +X, -X, +Y, -Y, +Z, -Z; right = cross(forward, up).
static const float3 pointShadowFaceForward[6]
    = { float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1) };
static const float3 pointShadowFaceUp[6]
    = { float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1), float3(0, -1, 0), float3(0, 1, 0) };
static const float3 pointShadowFaceRight[6]
    = { float3(0, -1, 0), float3(0, 1, 0), float3(1, 0, 0), float3(-1, 0, 0), float3(1, 0, 0), float3(1, 0, 0) };

// The face whose forward axis has the largest component of |d| (x before y before z on ties).
int pointShadowFaceOf(float3 d)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x < 0.0 ? 1 : 0;
    if (a.y >= a.z)
        return d.y < 0.0 ? 3 : 2;
    return d.z < 0.0 ? 5 : 4;
}

// Frisvad's construction, as tangentBasis in pointshadowmath.hpp: for a normal that points down (n.z < -0.95) it is
// done for the normal turned half a turn about the X axis and the answer turned back.
void pointShadowFrisvad(float3 n, out float3 t1, out float3 t2)
{
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    t1 = float3(1.0 - n.x * n.x * a, b, -n.x);
    t2 = float3(b, 1.0 - n.y * n.y * a, -n.y);
}

void pointShadowTangents(float3 n, out float3 t1, out float3 t2)
{
    if (n.z < -0.95)
    {
        pointShadowFrisvad(float3(n.x, -n.y, -n.z), t1, t2);
        t1 = float3(t1.x, -t1.y, -t1.z);
        t2 = float3(t2.x, -t2.y, -t2.z);
        return;
    }
    pointShadowFrisvad(n, t1, t2);
}

// One tap: `q` is the point's direction from the light in world axes.
float pointShadowTap(float3 q, uint slot, float far, float tanHalf, float resolution, float near, float depthBias)
{
    int face = pointShadowFaceOf(q);
    float dist = dot(q, pointShadowFaceForward[face]);
    float scale = 0.5 / (dist * tanHalf);
    float u = 0.5 + scale * dot(q, pointShadowFaceRight[face]);
    float v = 0.5 + scale * dot(q, pointShadowFaceUp[face]);
    float texel = 2.0 * dist * tanHalf / resolution;
    float d = max(dist - depthBias * texel, near);
    // The reversed depth the face stores: n (f - d) / ((f - n) d).
    float compare = near * (far - d) / ((far - near) * d);
    return pointShadowMap.SampleCmp(pointShadowSampler, float3(u, v, float(6 * slot + face)), compare);
}

// The lit fraction of light `lightIndex` (its index in the light buffer) at the view-space position `viewPos`.
float pointLightShadow(uint lightIndex, float3 viewPos)
{
    float4 entry = pointShadowData[POINT_SHADOW_HEADER_VEC4S + lightIndex];
    if (entry.x < 0.0)
        return 1.0;
    uint slot = uint(entry.x);
    uint count, stride;
    pointShadowData.GetDimensions(count, stride);
    uint maxLights = count - uint(POINT_SHADOW_HEADER_VEC4S) - uint(POINT_SHADOW_MAX_SLOTS);
    float4 slotData = pointShadowData[POINT_SHADOW_HEADER_VEC4S + maxLights + slot];
    float4 h0 = pointShadowData[0];
    float4 h1 = pointShadowData[1];
    float resolution = h0.y;
    float tanHalf = h0.z;
    float near = h0.w;
    float far = slotData.w;

    float3x3 toWorld = (float3x3)scene.invView;
    float3 toReceiver = mul(toWorld, viewPos - slotData.xyz);
    float3 normal = mul(toWorld, pointShadowNormal);
    int face0 = pointShadowFaceOf(toReceiver);
    float d0 = dot(toReceiver, pointShadowFaceForward[face0]);
    if (!(d0 > 0.0))
        return 1.0;
    float texel0 = 2.0 * d0 * tanHalf / resolution;
    float3 p = toReceiver + normal * (h1.x * texel0);
    float radius = h1.z * texel0;
    float3 t1, t2;
    pointShadowTangents(normal, t1, t2);
    int taps = clamp(int(h1.w), 1, 16);
    float sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        float2 o = pointShadowPoisson[i];
        float3 q = p + t1 * (o.x * radius) + t2 * (o.y * radius);
        sum += pointShadowTap(q, slot, far, tanHalf, resolution, near, h1.y);
    }
    return lerp(1.0, sum / float(taps), entry.y);
}

#endif
