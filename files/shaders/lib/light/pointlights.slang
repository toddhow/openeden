// The Slang twin of lib/light/pointlights.glsl (task 0072): the point lights of the view set (std430; element 0's
// position.w holds the count). Change both together.
#ifndef LIB_LIGHT_POINTLIGHTS_SLANG
#define LIB_LIGHT_POINTLIGHTS_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

struct PointLight
{
    float4 colorIntensity;
    float4 position;
};

[[vk::binding(VIEW_LIGHTS_BINDING, VIEW_SET)]] StructuredBuffer<PointLight> pointLights;

// Spot lights (task 0355 stage g), the twin of the block in pointlights.glsl (which says what the buffer holds).
static const float spotConeSoftness = 0.3;

float4 pointLightSpot(uint lightIndex)
{
    uint n;
    uint stride;
    pointLights.GetDimensions(n, stride);
    if (n < 2u)
        return float4(0.0);
    float marker = pointLights[1].position.w;
    if (!(marker >= 2.0) || marker >= 4096.0)
        return float4(0.0);
    uint maxLights = uint(marker);
    uint s = maxLights + (lightIndex >> 1);
    if (lightIndex >= maxLights || s >= n)
        return float4(0.0);
    return ((lightIndex & 1u) != 0u) ? pointLights[s].position : pointLights[s].colorIntensity;
}

float pointLightCone(uint lightIndex, float3 lightToPoint)
{
    float4 spot = pointLightSpot(lightIndex);
    if (!(spot.w > 0.0))
        return 1.0;
    float c = dot(spot.xyz, lightToPoint);
    float t = clamp((c - spot.w) / (spotConeSoftness * (1.0 - spot.w)), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

#endif
