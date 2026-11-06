// The Slang twin of lib/light/pointspecular.glsl (task 0072): the specular highlights of the clustered point lights.
// Include after lib/view/scene.slang. Change both together.
#ifndef LIB_LIGHT_POINTSPECULAR_SLANG
#define LIB_LIGHT_POINTSPECULAR_SLANG

import lib.light.equations;
import lib.light.pointlights;
#include "lib/light/select.slang"

float3 pointLightSpecular(float2 screenCoord, float3 reflectedViewNormal, float3 viewPos, float shininess)
{
    float3 specular = float3(0);
    uint tileIndex = selectTile(screenCoord, viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l = 0; l < count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset + l]];
        float4 lightColor = light.colorIntensity;
        float3 lightViewPos = light.position.xyz;
        float d = length(light.position.xyz - viewPos.xyz);
        float range = lightRange(light.colorIntensity.w);
        if (d <= range)
            specular += pow(max(dot(reflectedViewNormal, normalize(lightViewPos)), 0.0), shininess) * lightColor.xyz
                * attenuate(lightColor.w, d);
    }
    return specular;
}

#endif
