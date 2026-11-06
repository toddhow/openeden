// The Slang twin of lib/view/cloudshadow.glsl (task 0072): the shadow of the sky's clouds on the ground (task 0038).
// Included by lib/view/shadow.slang with CLOUD_SHADOW; it declares the sky-visibility header buffer. Include after
// lib/view/scene.slang. The C++ twins and the reasons are named in the GLSL; change them together. GLSL's
// mat3(a, b, c) takes columns, so the rotation is transposed here; a row vector times it is mul(v, m).
#ifndef LIB_VIEW_CLOUDSHADOW_SLANG
#define LIB_VIEW_CLOUDSHADOW_SLANG

import glsl;

#include "lib/sets.slang"
#include "lib/view/bindings.slang"
import lib.view.scene;

[[vk::binding(VIEW_SKYVIS_SKY_BINDING, VIEW_SET)]] StructuredBuffer<float4> skyHeader;

static const int CLOUD_BASE = 61;
static const int CLOUD_GRID = 64;
static const int CLOUD_GRID_OFFSET = 8;
static const int CLOUD_GRID_VEC4S = 1024;
static const float CLOUD_UV_CENTER_U = 2.5;
static const float CLOUD_UV_CENTER_V = -1.5;
static const float CLOUD_UV_PER_UNIT = 0.0024975;
static const float CLOUD_LAYER_HEIGHT = 15000.0;
static const float CLOUD_SUN_UP_FROM = 0.08;
static const float CLOUD_SUN_UP_TO = 0.3;
static const float CLOUD_SHADOW_EXTINCTION = 2.0;
static const float CLOUD_DOME_RADIUS[5] = { 0.0, 275.0, 462.0, 688.0, 1000.0 };
static const float CLOUD_DOME_HEIGHT[5] = { 306.733, 288.429, 254.262, 200.076, 100.0 };

// Where a ray from the camera (unit, Z up, in the dome's frame) meets the dome, as its X and Y (domePoint).
float2 cloudDomePoint(float3 direction)
{
    float horizontal = sqrt(direction.x * direction.x + direction.y * direction.y);
    if (horizontal < 1e-6)
        return float2(0.0);
    float slope = max(direction.z, 0.0) / horizontal;
    float radius = CLOUD_DOME_RADIUS[4];
    for (int i = 1; i < 5; ++i)
    {
        float farSlope = CLOUD_DOME_HEIGHT[i] / CLOUD_DOME_RADIUS[i];
        if (farSlope > slope)
            continue;
        float s = (CLOUD_DOME_HEIGHT[i] - CLOUD_DOME_HEIGHT[i - 1]) / (CLOUD_DOME_RADIUS[i] - CLOUD_DOME_RADIUS[i - 1]);
        radius = (CLOUD_DOME_HEIGHT[i - 1] - s * CLOUD_DOME_RADIUS[i - 1]) / (slope - s);
        radius = clamp(radius, CLOUD_DOME_RADIUS[i - 1], CLOUD_DOME_RADIUS[i]);
        break;
    }
    return direction.xy / horizontal * radius;
}

float cloudGridCell(int layer, int x, int y)
{
    int index = y * CLOUD_GRID + x;
    return skyHeader[CLOUD_BASE + CLOUD_GRID_OFFSET + layer * CLOUD_GRID_VEC4S + (index >> 2)][index & 3];
}

// Bilinear, wrapping: sampleGrid. GLSL's mod is floor-based (import glsl), unlike fmod.
float cloudSampleGrid(int layer, float2 uv)
{
    float2 p = uv * float(CLOUD_GRID) - 0.5;
    float2 f = floor(p);
    float2 t = p - f;
    int x0 = int(mod(f.x, float(CLOUD_GRID)));
    int y0 = int(mod(f.y, float(CLOUD_GRID)));
    int x1 = (x0 + 1) % CLOUD_GRID;
    int y1 = (y0 + 1) % CLOUD_GRID;
    float a = lerp(cloudGridCell(layer, x0, y0), cloudGridCell(layer, x1, y0), t.x);
    float b = lerp(cloudGridCell(layer, x0, y1), cloudGridCell(layer, x1, y1), t.x);
    return lerp(a, b, t.y);
}

// The thickness of one layer's cloud in the direction `worldDirection` from the camera (domeUv, then the grid).
float cloudLayerThickness(int layer, float3 worldDirection, float scroll)
{
    int at = CLOUD_BASE + 1 + layer * 3;
    float3x3 rotation = transpose(float3x3(skyHeader[at].xyz, skyHeader[at + 1].xyz, skyHeader[at + 2].xyz));
    // The rotation takes the dome's frame to the world, so a row vector times it takes a direction back.
    float3 local = mul(worldDirection, rotation);
    float2 point = cloudDomePoint(local);
    float2 uv = float2(
        CLOUD_UV_CENTER_U + CLOUD_UV_PER_UNIT * point.x, CLOUD_UV_CENTER_V - CLOUD_UV_PER_UNIT * point.y - scroll);
    return cloudSampleGrid(layer, uv);
}

// The share of the direct sun that reaches the ground at `viewPos` through the clouds (1 when there is no shadow).
float cloudShadow(float3 viewPos)
{
    float4 params = skyHeader[CLOUD_BASE];
    float amount = params.x;
    if (amount <= 0.0)
        return 1.0;
    float3 toSun = normalize(mul((float3x3)scene.invView, scene.lightViewPos.xyz));
    amount *= smoothstep(CLOUD_SUN_UP_FROM, CLOUD_SUN_UP_TO, toSun.z);
    if (amount <= 0.0)
        return 1.0;
    float3 camera = float3(scene.invView[0][3], scene.invView[1][3], scene.invView[2][3]);
    float3 ground = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    // shadowDirection: from the camera towards the cloud the sun's ray from the ground meets.
    float up = max(toSun.z, CLOUD_SUN_UP_FROM * 0.5);
    float travel = max(CLOUD_LAYER_HEIGHT - (ground.z - camera.z), 0.0) / up;
    float3 target = (ground - camera) + toSun * travel;
    float len = sqrt(dot(target, target));
    float3 direction = len < 1e-6 ? float3(0.0, 0.0, 1.0) : target / len;
    float blend = params.y;
    float4 flags = skyHeader[CLOUD_BASE + 7];
    float thick = 0.0;
    if (flags.x > 0.5)
        thick += (1.0 - blend) * cloudLayerThickness(0, direction, params.z);
    if (flags.y > 0.5 && blend > 0.0)
        thick += blend * cloudLayerThickness(1, direction, params.z);
    return 1.0 - clamp(amount, 0.0, 1.0) * (1.0 - exp(-CLOUD_SHADOW_EXTINCTION * clamp(thick, 0.0, 1.0)));
}

#endif
