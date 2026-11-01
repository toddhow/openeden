// OpenEden (task 0040): rain lit as streaks that scatter light, not as a surface. The C++ twin is
// components/render/precipitationmath.hpp (lightColour, sunPhase, distanceFade); change both together. Included by
// default/main.frag when the pipeline has PRECIPITATION_LIT (set by MWRender::Rain, only with `precipitation lighting`),
// after the lighting includes. A streak is lit by the ambient light from every side, by the sun when the viewer looks
// towards it (the Henyey-Greenstein phase function with a soft lobe), and by each lamp or torch near it with no regard
// to which side the lamp is on.
//
// GLSL, not Slang: this is an include of the existing GLSL default shader (decision 0009 keeps edits and includes of
// existing GLSL shaders in GLSL until the shader itself is ported).
#ifndef LIB_LIGHT_PRECIPITATION
#define LIB_LIGHT_PRECIPITATION

// Per-pixel lighting includes these already; without a normal or specular map or the material model it does not.
#ifndef PER_PIXEL_LIGHTING
#include "equations.glsl"
#include "select.glsl"
#include "pointlights.glsl"
 #ifndef SOFT_PARTICLE
#include "lib/view/screencoord.glsl"
 #endif
#endif

const float precipAmbientShare = 0.9;
const float precipPointShare = 0.6;
const float precipSunShare = 0.45;
const float precipSunAnisotropy = 0.5;
const float precipFadeStart = 200.0;
const float precipFadeEnd = 380.0;
const float precipPi = 3.14159265358979;

// The phase relative to an even spread (1 = no preference of direction).
float precipSunPhase(float cosTheta)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = precipSunAnisotropy;
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return 4.0 * precipPi * (1.0 - k * k) / (4.0 * precipPi * denominator * sqrt(denominator));
}

// The summed light of the lamps at `viewPos`, ignoring which side they are on. Negative (darkness) lamps subtract; the
// total is clamped at zero by the caller.
vec3 precipPointLights(vec3 viewPos)
{
    vec3 total = vec3(0.0);
    uint tileIndex = selectTile(viewPosToScreenCoord(viewPos, scene.projection), viewPos.z);
    uint count = min(uint(pointLights[0].position.w), lightGrid[tileIndex].count);
    uint offset = lightGrid[tileIndex].offset;
    for (uint l = 0; l < count; ++l)
    {
        PointLight light = pointLights[lightIndexList[offset + l]];
        vec4 lightColor = light.colorIntensity;
        float d = length(light.position.xyz - viewPos);
        if (d <= lightRange(lightColor.w))
            total += lightColor.xyz * attenuate(lightColor.w, d);
    }
    return total;
}

// The light on a streak at `viewPos`; `ambient` is the scene's ambient light.
vec3 precipitationLight(vec3 viewPos, vec3 ambient)
{
    float cosTheta = dot(normalize(viewPos), normalize(scene.lightViewPos.xyz));
    vec3 total = ambient * precipAmbientShare + scene.lightDiffuse.xyz * (precipSunShare * precipSunPhase(cosTheta))
        + precipPointLights(viewPos) * precipPointShare;
    return max(total, vec3(0.0));
}

float precipFade(float distance)
{
    float t = clamp((distance - precipFadeStart) / (precipFadeEnd - precipFadeStart), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

#endif
