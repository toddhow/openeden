// The terrain's fragment stage, in Slang (task 0072, terrain family): the twin of terrain/main.frag. It must declare
// what the GLSL declares with the same defines (checked by SlangPermutations); the define sets with a file are listed
// in terrain/main.frag.sets (a part of those the settings can make, which the GLSL draws). The statements follow the
// GLSL one for one and its comments give the reasons; change both together. GLSL's mat3(a, b, c) takes columns and
// float3x3(a, b, c) rows, so the tangent frame is transposed; mat3(m) of a mat4 is (float3x3)m; texelFetch is Load,
// textureSize GetDimensions, dFdx ddx, mix lerp.
import glsl;

static const int colorMode = 2;

#include "lib/sets.slang"
#include "lib/view/scene.slang"
#include "lib/view/linear.slang"
#include "lib/view/fog.slang"
#include "lib/view/shadow.slang"
#include "lib/light/directional.slang"
#include "lib/material/terms.slang"
#ifdef GI_SKY
#include "lib/view/skyvis.slang"
#endif
#if defined(WETNESS) || defined(TERRAIN_PBR)
#include "lib/material/pbr.slang"
#endif
#ifdef WETNESS
#include "lib/material/wetness.slang"
#endif
#ifdef COVER
#include "lib/material/cover.slang"
#endif
#include "terrain/bindings.slang"
#include "terrain/data.slang"
#include "terrain/inout.slang"
#include "lib/view/dither.slang"

// Task 0087: the batch's uniform, as terrain.vert.slang declares it; the fragment stage reads only the fade.
[[vk::binding(BATCH_DATA_BINDING, TEXTURE_SET)]] ConstantBuffer<Data> data;

[[vk::binding(BLENDMAP_BINDING, TEXTURE_SET)]] Sampler2D blendMap;

[[vk::binding(LAYER_BINDING, TEXTURE_SET)]] Sampler2DArray diffuseMap;

[[vk::binding(COLORMAP_BINDING, TEXTURE_SET)]] Sampler2D colorMap;

#ifdef TERRAIN_MAPS
// Terrain normal and specular maps (task 0012, V4i): each layer's flags hold its normal map's index in normalLayers
// (0xffff for none), then parallax (bit 16) and specular (bit 17).
[[vk::binding(NORMAL_LAYER_BINDING, TEXTURE_SET)]] Sampler2DArray normalLayers;
[[vk::binding(LAYER_FLAGS_BINDING, TEXTURE_SET)]] StructuredBuffer<uint> layerFlags;
static const uint NO_NORMAL_MAP = 0xffffu;
static const uint PARALLAX_FLAG = 1u << 16;
static const uint SPECULAR_FLAG = 1u << 17;
#include "lib/view/screencoord.slang"
#ifdef POINT_SHADOW
#include "lib/light/pointshadow.slang"
#endif
#include "lib/light/pixellighting.slang"
#ifdef PARALLAX_POM
// Task 0045: the layers' `_p` height files and per layer its array index (x) and depth (y); pomLayer is the array
// layer the height function reads.
[[vk::binding(HEIGHT_LAYER_BINDING, TEXTURE_SET)]] Sampler2DArray heightLayers;
[[vk::binding(LAYER_HEIGHTS_BINDING, TEXTURE_SET)]] StructuredBuffer<float4> layerHeights;
static float pomLayer = 0.0;
float pomHeight(float2 uv, float lod)
{
    return heightLayers.SampleLevel(float3(uv, pomLayer), lod).r;
}
float2 pomTextureSize()
{
    uint width, height, elements, levels;
    heightLayers.GetDimensions(0, width, height, elements, levels);
    return float2(int2(int(width), int(height)));
}
#endif
#include "lib/material/parallax.slang"
#ifdef TERRAIN_PBR
// Task 0044: bit 18 (the `_rmaos` layout) and bits 20 to 31 (the material map's index, 0xfff for none) of the flags;
// the maps; per layer a roughness scale and a reflectance.
static const uint RMAOS_FLAG = 1u << 18;
static const uint MATERIAL_SHIFT = 20u;
static const uint NO_MATERIAL_MAP = 0xfffu;
[[vk::binding(MATERIAL_LAYER_BINDING, TEXTURE_SET)]] Sampler2DArray materialLayers;
[[vk::binding(LAYER_SCALES_BINDING, TEXTURE_SET)]] StructuredBuffer<float4> layerScales;
#include "lib/light/pbrlighting.slang"
#endif
#endif

struct FragmentInput
{
    TerrainInout io;
    float4 fragCoord : SV_Position;
};

struct FragmentOutput
{
    float4 color : SV_Target0;
#ifdef GBUFFER
    float4 normal : SV_Target1;
    float linearDepth : SV_Target2;
    float2 motion : SV_Target3;
#ifdef GBUFFER_MATERIAL
    float4 materialValues : SV_Target4;
#endif
#ifdef GBUFFER_AMBIENT
    float4 ambientPart : SV_Target5;
#endif
#endif
};

[shader("fragment")]
FragmentOutput fragMain(FragmentInput input)
{
    FragmentOutput output;
    uint blendWidth, blendHeight;
    blendMap.GetDimensions(blendWidth, blendHeight);
    float blendTexSize = float(int(blendWidth));
    float2 blendCoord = input.io.texCoord * blendTexSize;
    int2 blendTexel = int2(blendCoord);
    float2 blendPos = blendCoord - float2(blendTexel);
    blendPos = clamp((blendPos - 0.5) * 2 + 0.5, 0.0, 1.0);
    float4 blendCoeffs = float4(blendPos, 1 - blendPos);
    blendCoeffs = blendCoeffs.zxzx * blendCoeffs.wwyy;
    int4 textures = int4(blendMap.Load(int3(blendTexel, 0)) * 255);
    float2 diffuseTexCoord = input.io.texCoord * blendTexSize;

    float4 outColor = float4(0, 0, 0, 1);
#ifdef COVER
    CoverSurface coverS = coverSurfaceAt(mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz,
        mul((float3x3)scene.invView, normalize(input.io.viewNormal)));
#endif
#ifdef TERRAIN_MAPS
    float3x3 normalToView = transpose(float3x3(input.io.viewTangent, input.io.viewBinormal, input.io.viewNormal));
    float3 eyeDir = mul(transpose(normalToView), normalize(-input.io.viewPos));
    float3 blendedNormal = float3(0);
    float specularStrength = 0.0;
#ifdef PARALLAX_POM
    float2 pomDx = ddx(diffuseTexCoord);
    float2 pomDy = ddy(diffuseTexCoord);
    float2 pomShift = float2(0.0);
    bool pomActive = false;
    {
        int first = -1;
        int second = -1;
        for (int i = 0; i < 4; ++i)
        {
            if (layerHeights[textures[i]].x < 0.0)
                continue;
            if (first < 0 || blendCoeffs[i] > blendCoeffs[first])
            {
                second = first;
                first = i;
            }
            else if (second < 0 || blendCoeffs[i] > blendCoeffs[second])
                second = i;
        }
        float up = mul((float3x3)scene.invView, normalize(input.io.viewNormal)).z;
        float pomFade = pomDistanceFade(length(input.io.viewPos)) * pomSlopeFade(up);
        float worldPerUv = pomWorldPerUvPlanar(mul((float3x3)scene.invView, input.io.viewPos), pomDx, pomDy);
        if (first >= 0 && pomFade > 0.0)
        {
            float weightSecond = second >= 0 ? blendCoeffs[second] : 0.0;
            float weightFirst = blendCoeffs[first];
            float mixSecond = weightSecond / max(weightFirst + weightSecond, 1e-4);
            float4 heightInfo = layerHeights[textures[first]];
            pomLayer = heightInfo.x;
            pomShift = pomOffset(diffuseTexCoord, eyeDir, pomUvDepth(heightInfo.y, worldPerUv) * pomFade,
                1.0 - heightLayers.SampleLevel(float3(0.5, 0.5, pomLayer), 16.0).r, pomDx, pomDy);
            if (mixSecond > 0.02)
            {
                heightInfo = layerHeights[textures[second]];
                pomLayer = heightInfo.x;
                float2 shiftSecond = pomOffset(diffuseTexCoord, eyeDir,
                    pomUvDepth(heightInfo.y, worldPerUv) * pomFade,
                    1.0 - heightLayers.SampleLevel(float3(0.5, 0.5, pomLayer), 16.0).r, pomDx, pomDy);
                pomShift = lerp(pomShift, shiftSecond, mixSecond);
            }
            pomActive = true;
        }
    }
#endif
#ifdef TERRAIN_PBR
    PbrSurface terrainS;
    terrainS.roughness = 0.0;
    terrainS.metalness = 0.0;
    terrainS.reflectance = 0.0;
    terrainS.occlusion = 0.0;
    terrainS.subsurface = 0.0;
#endif
    for (int i = 0; i < 4; ++i)
    {
        // Every texture is sampled outside branches; the flags only select the results (see the GLSL).
        uint flags = layerFlags[textures[i]];
        uint normalIndex = flags & NO_NORMAL_MAP;
        bool hasNormalMap = normalIndex != NO_NORMAL_MAP;
        float height = normalLayers.Sample(float3(diffuseTexCoord, float(normalIndex))).a;
        float2 parallaxOffset = getParallaxOffset(eyeDir, height);
        float2 layerCoord
            = diffuseTexCoord + ((hasNormalMap && (flags & PARALLAX_FLAG) != 0u) ? parallaxOffset : float2(0.0));
#ifdef PARALLAX_POM
        if (pomActive && layerHeights[textures[i]].x >= 0.0)
            layerCoord = diffuseTexCoord + pomShift;
#endif
        float4 diffuseTex = diffuseMap.Sample(float3(layerCoord, float(textures[i])));
        outColor.xyz += diffuseTex.xyz * blendCoeffs[i];
        specularStrength += ((flags & SPECULAR_FLAG) != 0u ? diffuseTex.a : 0.0) * blendCoeffs[i];
        float3 mapNormal = normalLayers.Sample(float3(layerCoord, float(normalIndex))).xyz * 2.0 - 1.0;
        blendedNormal += (hasNormalMap ? mapNormal : float3(0.0, 0.0, 1.0)) * blendCoeffs[i];
#ifdef TERRAIN_PBR
        uint materialIndex = (flags >> MATERIAL_SHIFT) & NO_MATERIAL_MAP;
        float4 layerMap = materialLayers.Sample(float3(layerCoord, float(materialIndex)));
        float4 layerScale = layerScales[textures[i]];
        PbrSurface layerS;
        layerS.roughness = 0.95;
        layerS.metalness = 0.0;
        layerS.reflectance = layerScale.y >= 0.0 ? layerScale.y : pbrDefaultReflectance;
        layerS.occlusion = 1.0;
        layerS.subsurface = 0.0;
        if (materialIndex != NO_MATERIAL_MAP)
            layerS = pbrSurfaceFromMap(layerS, (flags & RMAOS_FLAG) != 0u, layerMap, layerScale.x);
        terrainS.roughness += layerS.roughness * blendCoeffs[i];
        terrainS.metalness += layerS.metalness * blendCoeffs[i];
        terrainS.reflectance += layerS.reflectance * blendCoeffs[i];
        terrainS.occlusion += layerS.occlusion * blendCoeffs[i];
#endif
    }
#if defined(COVER) && defined(TERRAIN_PBR)
    terrainS.roughness = lerp(terrainS.roughness, coverRoughnessOf(coverS.kind), coverS.mask);
    terrainS.metalness *= 1.0 - coverS.mask;
#endif
    float3 viewNormal = normalize(mul(normalToView, blendedNormal));
    // The shadow bias keeps the geometry normal, as OpenMW's shadows did.
    float sunShadow = shadow(input.io.viewPos, input.io.viewNormal);
#ifdef POINT_SHADOW
    pointShadowNormal = normalize(input.io.viewNormal);
#endif
    // terrain.frag's specular: shininess 128 and the layer's diffuse alpha as a grey specular colour.
    const float shininess = 128.0;
    float3 pointDiffuse, pointSpecular;
#ifdef TERRAIN_PBR
    float3 pbrBase = toLinearIfView(outColor.xyz);
    float3 pbrF0 = pbrSpecularF0(pbrBase, terrainS.metalness, terrainS.reflectance);
    pbrPointLights(viewPosToScreenCoord(input.io.viewPos, scene.projection), viewNormal, input.io.viewPos,
        terrainS.roughness, pbrF0, pointDiffuse, pointSpecular);
    float3 diffuse = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    float3 specularLight = pointSpecular
        + scene.lightSpecular.xyz
            * pbrSpecularLobe(
                viewNormal, normalize(input.io.viewPos), scene.lightViewPos.xyz, terrainS.roughness, pbrF0)
            * sunShadow;
#else
    pointLightsPerPixel(viewPosToScreenCoord(input.io.viewPos, scene.projection), viewNormal, input.io.viewPos,
        shininess, pointDiffuse, pointSpecular);
    float3 diffuse = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    float3 specularLight = pointSpecular
        + scene.lightSpecular.xyz
            * specularIntensity(viewNormal, normalize(input.io.viewPos), shininess, scene.lightViewPos.xyz)
            * sunShadow;
#endif
#else
    for (int i = 0; i < 4; ++i)
        outColor.xyz += diffuseMap.Sample(float3(diffuseTexCoord, float(textures[i]))).xyz * blendCoeffs[i];

    float3 diffuse = input.io.pointLightDiffuse
        + directionalDiffuse(normalize(input.io.viewNormal)) * shadow(input.io.viewPos, input.io.viewNormal);
#endif

    // The layers are blended on encoded values and converted once (task 0025).
    outColor.xyz = toLinearIfView(outColor.xyz);
#ifdef COVER
    outColor.xyz = lerp(outColor.xyz, coverColourOf(coverS.kind), coverS.mask);
#endif
#ifdef WETNESS
#ifdef TERRAIN_PBR
    float wetTerrainRoughness = terrainS.roughness;
#else
    const float wetTerrainRoughness = 0.95;
#endif
    float3 wetWorldPos = mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz;
    WetSurface wetS = wetSurfaceAt(wetWorldPos, mul((float3x3)scene.invView, normalize(input.io.viewNormal)));
    if (wetS.wet > 0.0)
        outColor.xyz *= wetDarkening(wetS.wet, wetTerrainRoughness) * lerp(1.0, wetPuddleDarkening, wetS.puddle);
#endif

    uint colorWidth, colorHeight;
    colorMap.GetDimensions(colorWidth, colorHeight);
    float2 colorTexSize = float2(int2(int(colorWidth), int(colorHeight)));
    float2 colorCoord = (input.io.texCoord - 0.5) * colorTexSize / (colorTexSize + 1) + 0.5;
    float4 vertexColor = colorMap.Sample(colorCoord);
#ifdef COVER
    vertexColor.xyz = lerp(vertexColor.xyz, float3(dot(vertexColor.xyz, float3(1.0 / 3.0))), coverS.mask);
#endif
#ifdef AO_TERRAIN
    // Task 0048: the ambient part of this pixel's colour (see the GLSL); set in each lighting branch.
    float3 terrainAmbientPart = float3(0.0);
#endif
#ifdef TERRAIN_PBR
    {
        float3 terrainAmbient = scene.ambient.xyz;
#ifdef GI_SKY
        terrainAmbient = skyVisAmbient(mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz,
            mul((float3x3)scene.invView, normalize(input.io.viewNormal)), terrainAmbient);
#endif
        float terrainDiffuseScale = 1.0 - terrainS.metalness;
#ifdef AO_TERRAIN
        terrainAmbientPart
            = outColor.xyz * (terrainAmbient * getAmbient(vertexColor) * (terrainDiffuseScale * terrainS.occlusion));
#endif
        outColor *= materialTermsScaled(
            terrainAmbient, diffuse, vertexColor, terrainDiffuseScale * terrainS.occlusion, terrainDiffuseScale);
        outColor.xyz += specularLight;
    }
#else
#ifdef GI_SKY
    float3 terrainSkyAmbient = skyVisAmbient(mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz,
        mul((float3x3)scene.invView, normalize(input.io.viewNormal)), scene.ambient.xyz);
#ifdef AO_TERRAIN
    terrainAmbientPart = outColor.xyz * (terrainSkyAmbient * getAmbient(vertexColor));
#endif
    outColor *= materialTermsAmbient(terrainSkyAmbient, diffuse, vertexColor);
#else
#ifdef AO_TERRAIN
    terrainAmbientPart = outColor.xyz * (scene.ambient.xyz * getAmbient(vertexColor));
#endif
    outColor *= materialTerms(diffuse, vertexColor);
#endif
#ifdef TERRAIN_MAPS
    outColor.xyz += specularStrength * specularLight;
#endif
#endif
#ifdef WETNESS
    if (wetS.wet > 0.0)
    {
        float3 wetN = normalize(input.io.viewNormal);
        if (wetS.puddle > 0.0)
            wetN = normalize(
                lerp(wetN, mul(transpose((float3x3)scene.invView), wetPuddleNormal(wetWorldPos)), wetS.puddle));
        float wetRough = lerp(wetRoughnessOf(wetTerrainRoughness, wetS.wet), wetPuddleRoughness, wetS.puddle);
        float wetShadow = shadow(input.io.viewPos, input.io.viewNormal);
        outColor.xyz += scene.lightSpecular.xyz
            * pbrSpecularLobe(wetN, normalize(input.io.viewPos), scene.lightViewPos.xyz, wetRough, float3(0.04))
            * wetShadow * wetS.wet;
        if (wetS.puddle > 0.0)
        {
            float3 wetEye = mul((float3x3)scene.invView, normalize(input.io.viewPos));
            float3 wetNW = mul((float3x3)scene.invView, wetN);
            float3 wetSky = wetSkyReflection(wetWorldPos, reflect(wetEye, wetNW), wetPuddleRoughness);
            outColor.xyz += wetSky * (wetFresnel(dot(wetNW, -wetEye)) * wetPuddleReflection * wetS.puddle);
        }
    }
#ifdef WETNESS_DEBUG
    outColor.xyz = float3(wetS.wet, wetS.exposure, wetS.puddle);
#endif
#endif
#ifdef COVER_DEBUG
    outColor.xyz = float3(coverS.amount, coverS.upwardness, coverS.mask);
#endif
    outColor.xyz = applyFog(outColor.xyz, input.io.viewPos, input.fragCoord.xy);
#ifdef AO_TERRAIN
    // The fog's share of the colour, as default/main.frag does it for objects.
    terrainAmbientPart *= applyFog(float3(1.0), input.io.viewPos, input.fragCoord.xy)
        - applyFog(float3(0.0), input.io.viewPos, input.fragCoord.xy);
#endif

#ifdef DEBUG_SHADOW
    outColor = debugShadow(outColor, input.fragCoord.xy);
#endif

#ifdef GBUFFER
    float4 outNormal;
#ifdef TERRAIN_MAPS
    outNormal = float4(viewNormal, 1.0);
#else
    outNormal = float4(normalize(input.io.viewNormal), 1.0);
#endif
    output.linearDepth = -input.io.viewPos.z;
    output.motion = float2(0.0);
#ifdef GBUFFER_MATERIAL
    // Render::Pbr::terrainRoughness in the normal's w; reflectance 0.5, occlusion 1.
#ifdef TERRAIN_PBR
    outNormal.w = terrainS.roughness;
    output.materialValues = float4(terrainS.metalness, terrainS.reflectance, terrainS.occlusion, 0.0);
#else
    outNormal.w = 0.95;
    output.materialValues = float4(0.0, 0.5, 1.0, 0.0);
#endif
#endif
#ifdef GBUFFER_AMBIENT
#ifdef AO_TERRAIN
    output.ambientPart = float4(terrainAmbientPart, 1.0);
#else
    output.ambientPart = float4(0.0);
#endif
#endif
    output.normal = outNormal;
#endif

#ifdef DEBUG_CHUNKS
    float b = 0.005;
    if (input.io.texCoord.x < b || input.io.texCoord.y < b || input.io.texCoord.x > 1 - b
        || input.io.texCoord.y > 1 - b)
        outColor.xyz = float3(1, 0, 0);
    if (input.io.localTexCoord.x < b || input.io.localTexCoord.y < b || input.io.localTexCoord.x > 1 - b
        || input.io.localTexCoord.y > 1 - b)
        outColor.xyz = float3(0, 0, 1);
#endif
    // Task 0087: the dither of the GLSL's last statement (see terrain/main.frag).
    if (data.batch.fade < 1.0 && dither8(int(input.fragCoord.x) & 7, int(input.fragCoord.y) & 7, data.batch.fade) == 0.0)
        discard;
    output.color = outColor;
    return output;
}
