// The Slang twin of lib/light/precipitation.glsl (task 0072, stage f): rain lit as streaks that scatter light (task
// 0040). Include after lib/view/scene.slang. The reasons are in the GLSL; change both together (and
// components/render/precipitationmath.hpp).
#ifndef LIB_LIGHT_PRECIPITATION_SLANG
#define LIB_LIGHT_PRECIPITATION_SLANG

import lib.light.equations;
import lib.light.pointlights;
import lib.light.select;
import lib.view.scene;
import lib.view.screencoord;

static const float precipAmbientShare = 0.9;
static const float precipPointShare = 0.6;
static const float precipSunShare = 0.45;
static const float precipSunAnisotropy = 0.5;
static const float precipFadeStart = 200.0;
static const float precipFadeEnd = 380.0;
static const float precipPi = 3.14159265358979;

float precipSunPhase(float cosTheta)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = precipSunAnisotropy;
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return 4.0 * precipPi * (1.0 - k * k) / (4.0 * precipPi * denominator * sqrt(denominator));
}

float3 precipPointLights(float3 viewPos)
{
    float3 total = float3(0.0);
    uint tileIndex = selectTile(viewPosToScreenCoord(viewPos, scene.projection), viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l = 0; l < count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset + l]];
        float4 lightColor = light.colorIntensity;
        float d = length(light.position.xyz - viewPos);
        if (d <= lightRange(lightColor.w))
            total += lightColor.xyz * attenuate(lightColor.w, d);
    }
    return total;
}

float3 precipitationLight(float3 viewPos, float3 ambient)
{
    float cosTheta = dot(normalize(viewPos), normalize(scene.lightViewPos.xyz));
    float3 total = ambient * precipAmbientShare + scene.lightDiffuse.xyz * (precipSunShare * precipSunPhase(cosTheta))
        + precipPointLights(viewPos) * precipPointShare;
    return max(total, float3(0.0));
}

float precipFade(float distance)
{
    float t = clamp((distance - precipFadeStart) / (precipFadeEnd - precipFadeStart), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

#endif
