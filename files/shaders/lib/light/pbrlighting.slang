// The Slang twin of lib/light/pbrlighting.glsl (task 0072): point lights with the microfacet specular. Include after
// lib/material/pbr.slang and lib/light/pixellighting.slang. Change both together.
#ifndef LIB_LIGHT_PBRLIGHTING_SLANG
#define LIB_LIGHT_PBRLIGHTING_SLANG

import lib.light.equations;
import lib.light.pointlights;
import lib.material.pbr;
import lib.light.select;
#ifdef POINT_SHADOW
import lib.light.pointshadow;
#endif

void pbrPointLights(float2 screenCoord, float3 normal, float3 viewPos, float roughness, float3 f0, out float3 diffuse,
    out float3 specular)
{
    diffuse = float3(0);
    specular = float3(0);
    float3 viewDir = normalize(viewPos);
    uint tileIndex = selectTile(screenCoord, viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l = 0; l < count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset + l]];
        float4 lightColor = light.colorIntensity;
        float3 lightVec = light.position.xyz - viewPos;
        float d = length(lightVec);
        if (d <= lightRange(lightColor.w))
        {
            float3 lightDir = lightVec / d;
            float illumination = attenuate(lightColor.w, d);
#ifdef POINT_SHADOW
            // Task 0077: the shadow of this light, if it has a slot (lib/light/pointshadow.*).
            illumination *= pointLightShadow(lightIndexList[offset + l], viewPos);
#endif
            diffuse += lightColor.xyz * lambert(lightDir, normal) * illumination;
            specular += max(lightColor.xyz, float3(0)) * illumination
                * pbrSpecularLobe(normal, viewDir, lightDir, roughness, f0);
        }
    }
}

#endif
