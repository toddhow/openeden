// The Slang twin of lib/view/shadow.glsl (task 0072): the sun's shadow maps, PCSS, and the clouds' shadows. Include
// after lib/view/scene.slang. The shadow.settings values are specialization constants (lib/view/shadowsettings.slang);
// its "#define" lines (CLOUD_SHADOW, CLOUD_SHADOW_DEBUG, VOLUME_CLOUD_SHADOW, DEBUG_SHADOW) come in the define set.
// Change both together. Two differences, both of form only:
// - The GLSL samples one `texture2DArray` with one `sampler`, as a shadow sampler for the comparison and as a plain one
//   for the PCSS blocker search. Slang has no combinator for separate texture and sampler objects, so the twin
//   declares the sampler twice at the same binding, as SamplerComparisonState and as SamplerState.
// - Branches only ON the specialization constants and loops that compare a variable with them, never an expression
//   over them alone (files/shaders/CLAUDE.md): the cascade search counts up to cShadowMapCount instead of to
//   cShadowMapCount - 1, with the same result.
#ifndef LIB_VIEW_SHADOW_SLANG
#define LIB_VIEW_SHADOW_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"
import lib.view.scene;
__exported import lib.view.cascades;
__exported import lib.view.shadowsettings;

#ifdef CLOUD_SHADOW
__exported import lib.view.cloudshadow;
#endif
#ifdef VOLUME_CLOUD_SHADOW
__exported import lib.view.volumecloudshadow;
#endif

[[vk::binding(VIEW_SHADOW_MAP_BINDING, VIEW_SET)]] Texture2DArray<float> shadowMap;
[[vk::binding(VIEW_SHADOW_SAMPLER_BINDING, VIEW_SET)]] SamplerComparisonState shadowCompareSampler;
// The second view of binding VIEW_SHADOW_SAMPLER_BINDING (see above); Slang warns about the overlap, which is meant.
// The warning is silenced for this declaration only.
#pragma warning(push)
#pragma warning(disable : 39001)
[[vk::binding(VIEW_SHADOW_SAMPLER_BINDING, VIEW_SET)]] SamplerState shadowSampler;
#pragma warning(pop)

static const float2 poissonDisk[16] = { float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760), float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188), float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790) };

float shadowCompare(float2 coord, uint cascadeIndex, float depth)
{
    return shadowMap.SampleCmp(shadowCompareSampler, float3(coord, float(cascadeIndex)), depth);
}

float shadowPCF(float4 shadowCoord, float uvFilterRadius, uint cascadeIndex)
{
    float shadow = 0.0;
    uint samples = 0;
    for (int i = 0; i < cShadowSamples; ++i)
    {
        shadow += shadowCompare(shadowCoord.xy + uvFilterRadius * poissonDisk[i], cascadeIndex, shadowCoord.z);
        ++samples;
    }
    return shadow / samples;
}

// Based on "Percentage-Closer Soft Shadows", Randima Fernando, NVIDIA Corporation
float shadowPCSS_ortho_directional(float4 shadowCoord, uint cascadeIndex)
{
    // Step 0 - configuration
    float lightFrustumSize = scene.cascadeFrustumWidths[cascadeIndex];
    float lightWorldSize = scene.lightViewPos.a;
    float uvLightSize = lightWorldSize / lightFrustumSize;
    float zReceiver = shadowCoord.z;
    float searchWidth = uvLightSize; // Assuming that cascadeProj.near == 0

    // Step 1 - blocker search
    int blockers = 0;
    float avgBlockerDistance = 0;
    for (int i = 0; i < cBlockerSamples; ++i)
    {
        float2 sampleCoord = poissonDisk[i] * searchWidth + shadowCoord.xy;
        float zBlocker = shadowMap.SampleBias(shadowSampler, float3(sampleCoord, float(cascadeIndex)), 0.0);
        if (zBlocker > zReceiver) // vsgopenmw-reverse-depth
        {
            blockers++;
            avgBlockerDistance += zBlocker;
        }
    }
    if (blockers > 0)
        avgBlockerDistance /= blockers;
    else
        return 1;

    // Step 2 - penumbra estimation
    float penumbraWidth = (zReceiver - avgBlockerDistance) * uvLightSize / avgBlockerDistance;

    // Step 3 - percentage-closer filtering
    return shadowPCF(shadowCoord, penumbraWidth, cascadeIndex);
}

float shadowMapsOn(float3 viewPos, float3 viewNormal)
{
    float fade = 0;
    int last = 0;
    for (int i = 1; i < cShadowMapCount; ++i)
        last = i;
    float far = scene.cascadeSplits[last];
    if (far == 0) // if (!viewReceivesShadows)
        return 1;
    if (cShadowMaxDist != 0)
    {
        float fadeStart = -cShadowFadeStart * cShadowMaxDist;
        fade = max(0.0, fadeStart - viewPos.z);
        if (cShadowFadeStart != 1)
            fade /= cShadowMaxDist + fadeStart;
        if (fade >= 1)
            return 1;
    }

    uint cascadeIndex = 0;
    for (int i = 1; i < cShadowMapCount; ++i)
    {
        if (viewPos.z < scene.cascadeSplits[i - 1])
            cascadeIndex = uint(i);
    }
    float4 shadowCoord = mul(scene.viewToCascadeProj[cascadeIndex], float4(viewPos, 1.0));

    float NdotL = dot(viewNormal, -scene.lightViewPos.xyz);
    float bias = max(0.001 * (1.0 - NdotL), 0.0001);
    shadowCoord.z += bias;

    float shadow;
    if (cPCSS)
        shadow = shadowPCSS_ortho_directional(shadowCoord, cascadeIndex);
    else
        shadow = shadowCompare(shadowCoord.xy, cascadeIndex, shadowCoord.z);
    return lerp(shadow, 1.0, fade);
}

float shadowMaps(float3 viewPos, float3 viewNormal)
{
    if (cEnableShadows)
        return shadowMapsOn(viewPos, viewNormal);
    return 1;
}

// The sun's shadow on the ground, times the clouds' (task 0038, 0042); the reasons are in the GLSL.
#if defined(CLOUD_SHADOW) || defined(VOLUME_CLOUD_SHADOW)
float cloudsShadow(float3 viewPos)
{
#ifdef VOLUME_CLOUD_SHADOW
    float2 volume = volumeCloudShadow(viewPos);
#ifdef CLOUD_SHADOW
    return lerp(cloudShadow(viewPos), volume.x, volume.y);
#else
    return lerp(1.0, volume.x, volume.y);
#endif
#else
    return cloudShadow(viewPos);
#endif
}
#endif

float shadow(float3 viewPos, float3 viewNormal)
{
#if defined(CLOUD_SHADOW) || defined(VOLUME_CLOUD_SHADOW)
#ifdef CLOUD_SHADOW_DEBUG
    return cloudsShadow(viewPos);
#else
    return shadowMaps(viewPos, viewNormal) * cloudsShadow(viewPos);
#endif
#else
    return shadowMaps(viewPos, viewNormal);
#endif
}

#ifdef DEBUG_SHADOW
// The GLSL reads gl_FragCoord; the caller passes the fragment's window position.
float4 debugShadow(float4 color, float2 fragCoord)
{
    uint width = uint(scene.resolution.x) / 4;
    for (int i = 0; i < cShadowMapCount; ++i)
    {
        if (fragCoord.x < width + width * i && fragCoord.x > width * i && fragCoord.y < width)
        {
            float2 coord = (fragCoord - float2(width * i, 0)) / width;
            float depth = shadowMap.SampleBias(shadowSampler, float3(coord, float(i)), 0.0);
            return float4(float3(depth), 1);
        }
    }
    return color;
}
#endif

#endif
