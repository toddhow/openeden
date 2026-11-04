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

#endif
