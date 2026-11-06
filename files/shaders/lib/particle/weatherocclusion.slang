// The Slang twin of lib/particle/weatherocclusion.glsl (task 0072, stage f): weather particles under a roof. Include
// after lib/view/scene.slang. The reasons are in the GLSL; change both together.
#ifndef LIB_PARTICLE_WEATHEROCCLUSION_SLANG
#define LIB_PARTICLE_WEATHEROCCLUSION_SLANG

#include "lib/material/bindings.slang"
#include "lib/sets.slang"
import lib.view.scene;

[[vk::binding(OCCLUSION_UNIT, TEXTURE_SET)]] Sampler2D occlusionMap;

bool isOccludedParticle(float3 worldPos)
{
    // GLSL scene.invView[3].xyz: column 3.
    float3 camera = float3(scene.invView[0][3], scene.invView[1][3], scene.invView[2][3]);
    float2 coord = (worldPos.xy - camera.xy) / scene.lightDiffuse.w + 0.5;
    float particleDepth = 0.5 + (worldPos.z - camera.z) / scene.lightSpecular.w;
    return particleDepth < occlusionMap.Sample(float2(coord.x, 1.0 - coord.y)).r;
}

#endif
