// The Slang twin of lib/light/pixellighting.glsl (task 0072): per-pixel lighting for normal and specular maps. Include
// after lib/view/scene.slang. Change both together.
#ifndef LIB_LIGHT_PIXELLIGHTING_SLANG
#define LIB_LIGHT_PIXELLIGHTING_SLANG

import lib.light.equations;
import lib.light.pointlights;
import lib.light.select;
#ifdef POINT_SHADOW
import lib.light.pointshadow;
#endif

// OpenMW's calcSpecIntensity.
float specularIntensity(float3 viewNormal, float3 viewDir, float shininess, float3 lightDir)
{
    if (dot(viewNormal, lightDir) > 0.0)
    {
        float3 halfVec = normalize(lightDir - viewDir);
        float NdotH = max(dot(viewNormal, halfVec), 0.0);
        return pow(NdotH, shininess);
    }
    return 0.0;
}

void pointLightsPerPixel(float2 screenCoord, float3 viewNormal, float3 viewPos, float shininess, out float3 diffuse,
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
            diffuse += lightColor.xyz * lambert(lightDir, viewNormal) * illumination;
            specular += max(lightColor.xyz, float3(0)) * specularIntensity(viewNormal, viewDir, shininess, lightDir)
                * illumination;
        }
    }
}

#endif
