// The water's fragment stage, in Slang (task 0072, water family): the twin of water/main.frag. It must declare what the
// GLSL declares with the same defines (checked by SlangPermutations); the define sets are BUMP_MAP with the
// shadow.settings defines (water/main.frag.sets). The statements follow the GLSL one for one; change both together,
// and components/render/watermath.hpp for the far fade of the ripples (task 0068), whose constants a test reads back
// from both files. GLSL's dFdx is ddx, mix is lerp, a row vector times a matrix is mul(v, m).
import glsl;

import lib.inout;
import lib.light.pointspecular;
#include "lib/material/bindings.slang"
#include "lib/material/descriptors_frag.slang"
import lib.view.fog;
import lib.view.linear;
import lib.view.reflect;
import lib.view.scene;
import lib.view.screencoord;
import lib.view.shadow;
import water.fresnel;
import water.rainripples;

struct FragmentInput
{
    CommonInout io;
    float4 fragCoord : SV_Position;
};

static const float shininess = 255;

static const float3 waterColorDeep = float3(0.05, 0.3, 0.5);

static const float waveChoppyness = 0.2;
static const float waveScale = 175.0;

static const float2 bigWaves = float2(0.2, 0.3);
static const float2 midWaves = float2(0.9, 0.3);
static const float2 smallWaves = float2(0.1, 0.1);

static const float bump = 1.7;

static const float2 windDir = float2(0.5f, -0.8f);
static const float windSpeed = 0.2f;

float2 normalCoords(
    float2 uv, float scale, float speed, float time, float timer1, float timer2, float3 previousNormal, float2 dir)
{
    return uv * waveScale * scale + dir * time * windSpeed * speed
        - (previousNormal.xy / previousNormal.zz) * waveChoppyness + float2(time * timer1, time * timer2);
}

// OpenEden (task 0068): the far fade of the ripples; the reasons are in the GLSL. The C++ twin is
// components/render/watermath.hpp (octaveFade).
static const float bumpMapSize = 128.0;
static const float waterFadeFromFar = 20000.0; // the far plane from which the fade is on

float waterOctaveFade(float texelsPerPixel)
{
    return 1.0 / max(1.0, texelsPerPixel);
}

// The share of the octave `scale` to keep at this pixel; `uv` is the smooth, un-offset ripple coordinate.
float octaveFade(float2 uv, float scale)
{
    if (scene.zFar < waterFadeFromFar)
        return 1.0;
    float texelsPerUnit = waveScale * scale * bumpMapSize;
    return waterOctaveFade(texelsPerUnit * max(length(ddx(uv)), length(ddy(uv))));
}

float3 fadeOctave(float3 normal, float fade)
{
    return float3(normal.xy * fade, normal.z);
}

float3 bumpNormal(float2 coords)
{
    return 2.0 * bumpMap.Sample(coords).rgb - 1.0;
}

float3 getNormal(float3 worldPos)
{
    float2 uv = worldPos.xy / float2(13653.0, -13653.0);
    float2 dir = float2(1.0, -1.0) * normalize(worldPos.xy + float2(-20000.0, -69000.0));
    // The GLSL passes a zero normal here, so normalCoords divides 0 by 0. Its frames show that term as 0 on this
    // machine's driver (task 0072 stage d log); slangc folds 0 / 0 to a NaN constant, which made the whole ripple chain
    // noise. A normal of (0, 0, 1) gives the 0 the GLSL draws.
    float3 normal0
        = bumpNormal(normalCoords(uv, 0.05, 0.04, scene.time, -0.015, -0.005, float3(0.0, 0.0, 1.0), windDir));
    normal0 = fadeOctave(normal0, octaveFade(uv, 0.05));
    float3 normal1a = bumpNormal(normalCoords(uv, 0.02, 0.05, 0.1 * scene.time, 0.0, 0.0, normal0, windDir * 1.0));
    normal1a = fadeOctave(normal1a, octaveFade(uv, 0.02));
    float3 normal1
        = bumpNormal(normalCoords(uv, 0.001, 0.01, 0.0001 * scene.time, 0.0, 0.0, normal1a * 1.0, 0.1 * dir));
    normal1 = fadeOctave(normal1, octaveFade(uv, 0.001));
    float3 normal2 = bumpNormal(normalCoords(uv, 0.25, 0.01, scene.time, -0.04, -0.03, normal1, windDir));
    normal2 = fadeOctave(normal2, octaveFade(uv, 0.25));
    float3 normal3 = bumpNormal(normalCoords(uv, 0.5, 0.09, scene.time, 0.03, 0.04, normal2, windDir));
    normal3 = fadeOctave(normal3, octaveFade(uv, 0.5));

    float3 rainRipple = float3(0);
    if (scene.rainIntensity > 0.01)
        rainRipple = rainCombined(worldPos.xy / 1000.0, scene.time) * scene.rainIntensity * 10;

    float3 normal = normal1 * bigWaves.y + normal2 * midWaves.x + normal3 * midWaves.y + rainRipple;
    normal = normalize(float3(-normal.x, -normal.y, normal.z * bump));
    return normal;
}

float3 ambientReflection(float3 worldDir)
{
    float fraction
        = worldDir.z > 0 ? clamp(worldDir.z * 2 - 0.15, 0.0, 1.0) : 1 - clamp(-worldDir.z * 2 - 0.15, 0.35, 1.0);
    // OpenEden (task 0025): mixed on encoded colours like the sky's gradient it reflects.
    return toLinearIfView(lerp(toEncodedIfView(scene.fogColor.xyz), toEncodedIfView(scene.skyColor.xyz), fraction));
}

float3 colorRefraction(float waterDepth, float surfaceDepth, float3 refraction)
{
    float3 incidentLight = scene.lightDiffuse.xyz + scene.ambient.xyz;
    float inverseScatterAmount = exp(-0.00045 * waterDepth);
    float3 deepColor = incidentLight * toLinearIfView(waterColorDeep) * 0.25;
    deepColor = applyFog(deepColor, surfaceDepth);
    return lerp(deepColor, refraction, clamp(inverseScatterAmount, 0.0, 1.0));
}

int2 reflectMapSize()
{
    uint width;
    uint height;
    reflectMap.GetDimensions(width, height);
    return int2(width, height);
}

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float3 viewPos = input.io.viewPos;
    float3 worldPos = mul(scene.invView, float4(viewPos, 1)).xyz;

    float3 normal = getNormal(worldPos);
    float3 viewNormal = mul(float4(normal, 0), scene.invView).xyz;
    float viewLength = length(viewPos);
    float3 viewVec = viewPos / viewLength;

    float3 reflectedDir = reflect(viewVec, viewNormal);
    float3 reflectedWorldDir = mul(scene.invView, float4(reflectedDir, 0)).xyz;
    bool eyeUnderwater = reflectedWorldDir.z < 0;
    float3 specular = pow(max(dot(reflectedDir, scene.lightViewPos.xyz), 0.0), shininess) * float3(1, 1, 1);

    float ior = eyeUnderwater ? 0.75 : 1.333; // water to air ; air to water
    float fresnel = fresnel_dielectric(viewVec, viewNormal, ior);

    float3 reflection;
    float3 fallbackReflection = ambientReflection(reflectedWorldDir);
    float4 reflected = screenSpaceReflection(
        scene.projection, scene.invProjection, viewPos, viewNormal, scene.zFar, input.fragCoord.xy);
    reflection = lerp(fallbackReflection, reflectMap.Sample(reflected.xy).xyz, reflected.w);

    float4 outColor;
    float2 screenCoord = input.fragCoord.xy / scene.resolution;
    if (fresnel < 1)
    {
        float4 refracted = screenSpaceRefraction(
            scene.projection, scene.invProjection, viewPos, viewNormal, 1 / ior, 2000, input.fragCoord.xy);
        refracted.xy = lerp(screenCoord, refracted.xy, refracted.w);

        float refractDepth = reflectDepthMap.Sample(refracted.xy).r;
        float4 refractedPos = screenCoordToViewPos(refracted.xy, refractDepth, scene.invProjection);
        // OpenEden (task 0068): the long view reads the colour at the same texel as the depth (see the GLSL).
        float3 refraction;
        if (scene.zFar < waterFadeFromFar)
            refraction = reflectMap.Sample(refracted.xy).xyz;
        else
        {
            int2 size = reflectMapSize();
            int2 texel = clamp(int2(refracted.xy * float2(size)), int2(0), size - int2(1));
            refraction = reflectMap.Load(int3(texel, 0)).xyz;
        }

        float waterDepth = length(viewPos - refractedPos.xyz);
        float surfaceDepth = viewLength;
        if (eyeUnderwater)
        {
            surfaceDepth = waterDepth;
            waterDepth = viewLength;
        }
        refraction.xyz = colorRefraction(waterDepth, viewLength, refraction.xyz);
        outColor = float4(lerp(refraction, reflection, fresnel), 1.0);
    }
    else
        outColor = float4(reflection, 1.0);

    outColor.xyz += specular * shadow(viewPos, viewNormal);
    float3 pointSpec = pointLightSpecular(screenCoord, reflectedDir, viewPos, shininess);
    pointSpec *= clamp(1 - pointSpec.b * 2.5, 0.0, 1.0); // don't reflect ambient lights
    outColor.xyz += pointSpec;
    return outColor;
}
