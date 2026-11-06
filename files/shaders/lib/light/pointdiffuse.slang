// The Slang twin of lib/light/pointdiffuse.glsl (task 0072): the diffuse light of the clustered point lights. Include
// after lib/view/scene.slang. Change both together.
#ifndef LIB_LIGHT_POINTDIFFUSE_SLANG
#define LIB_LIGHT_POINTDIFFUSE_SLANG

import lib.light.equations;
import lib.light.pointlights;
import lib.light.select;

float3 pointLightDiffuse(float2 screenCoord, float3 viewNormal, float3 viewPos)
{
    float3 diffuse = float3(0);
    uint tileIndex = selectTile(screenCoord, viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l = 0; l < count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset + l]];
        float4 lightColor = light.colorIntensity;
        float3 lightVec = light.position.xyz - viewPos;
        float d = length(lightVec);
        if (d <= lightRange(light.colorIntensity.w))
            diffuse += lightColor.xyz * lambert(lightVec / d, viewNormal) * attenuate(lightColor.w, d);
    }
    return diffuse;
}

#endif
