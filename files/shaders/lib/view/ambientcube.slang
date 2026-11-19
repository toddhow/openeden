// The Slang twin of lib/view/ambientcube.glsl (task 0355 stage e): the ambient cube of an interior. The layout, the C++
// twin and the reasons are in the GLSL and components/render/ambientcube.hpp; change them together. Include after
// lib/view/shadow.slang (and lib/view/skyvis.slang): the header is declared by cloudshadow.slang with CLOUD_SHADOW, by
// skyvis.slang with GI and GI_SKY, else here.
#ifndef LIB_VIEW_AMBIENTCUBE_SLANG
#define LIB_VIEW_AMBIENTCUBE_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

#if !defined(CLOUD_SHADOW) && !(defined(GI) && defined(GI_SKY))
[[vk::binding(VIEW_SKYVIS_SKY_BINDING, VIEW_SET)]] StructuredBuffer<float4> skyHeader;
#endif

static const int AMBIENT_CUBE_BASE = 2117; // Render::AmbientCube::base = Render::Cloud::headerTotalVec4s

float3 ambientCubeAmbient(float3 worldNormal, float3 flatAmbient)
{
    if (skyHeader[AMBIENT_CUBE_BASE].x < 0.5)
        return flatAmbient;
    float length2 = dot(worldNormal, worldNormal);
    float3 n = length2 > 1e-20 ? worldNormal * rsqrt(length2) : float3(0.0, 0.0, 1.0);
    float3 sq = n * n;
    float3 cube = skyHeader[AMBIENT_CUBE_BASE + (n.x >= 0.0 ? 2 : 3)].xyz * sq.x
        + skyHeader[AMBIENT_CUBE_BASE + (n.y >= 0.0 ? 4 : 5)].xyz * sq.y
        + skyHeader[AMBIENT_CUBE_BASE + (n.z >= 0.0 ? 6 : 7)].xyz * sq.z;
    return max(cube + (flatAmbient - skyHeader[AMBIENT_CUBE_BASE + 1].xyz), float3(0.0));
}

#endif
