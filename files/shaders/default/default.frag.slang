// The object shader's fragment stage, in Slang (task 0072, stage f): the twin of default/main.frag. It must declare
// what the GLSL declares with the same defines (checked by SlangPermutations); the define sets with a file are listed in
// default/main.frag.sets, and a set with TEXCOORD or PARTICLE names its count of texture coordinate sets (UVSETS_<n>,
// lib/uvsets.slang). The statements follow the GLSL one for one and its comments give the reasons; change both
// together. The specialization constants are switched on, never compared with a literal (an OpSpecConstantOp,
// files/shaders/CLAUDE.md). GLSL's mat3(a, b, c) takes columns and float3x3(a, b, c) rows, so the tangent frame is
// transposed; M * v is mul(M, v); texture is Sample, textureLod SampleLevel, textureSize GetDimensions, dFdx ddx, mix
// lerp, .st .xy; gl_SampleMask[0] is the SV_Coverage output.
import glsl;

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

#include "lib/material/descriptors_frag.slang"

[[vk::constant_id(1)]] const int alphaTestMode = 0;
[[vk::constant_id(2)]] const int colorMode = 0;
[[vk::constant_id(START_UVSET_CONSTANTS + DIFFUSE_UNIT)]] const int diffuseUV = 0;
[[vk::constant_id(START_UVSET_CONSTANTS + DARK_UNIT)]] const int darkUV = 0;
[[vk::constant_id(START_UVSET_CONSTANTS + DETAIL_UNIT)]] const int detailUV = 0;
[[vk::constant_id(START_UVSET_CONSTANTS + DECAL_UNIT)]] const int decalUV = 0;
[[vk::constant_id(START_UVSET_CONSTANTS + GLOW_UNIT)]] const int glowUV = 0;
// The GLSL's numUvSets (0) is the length of the texture coordinate array, fixed here by UVSETS_<n>, and its specular
// (3) and bumpUV are never read.

// Normal and specular maps light each pixel, as OpenMW's PER_PIXEL_LIGHTING (task 0012, V4h).
#if defined(NORMAL) && (defined(NORMAL_MAP) || defined(SPECULAR_MAP) || defined(PBR))
#define PER_PIXEL_LIGHTING 1
#endif
// Task 0027, R1: lit surfaces with the material model's lighting.
#if defined(NORMAL) && defined(PBR)
#define PBR_LIT 1
#endif
// Task 0037: rain wetness, in the lit surfaces that take the material model.
#if defined(NORMAL) && defined(PBR) && defined(WETNESS)
#define WET_LIT 1
#endif
// Task 0039: snow and ash cover, in the same surfaces.
#if defined(NORMAL) && defined(PBR) && defined(COVER)
#define COVER_LIT 1
#endif

import lib.view.scene;
import lib.view.linear;
import lib.view.env;
import lib.view.fog;
import lib.view.shadow;
import lib.view.dither;
#include "lib/object/object.slang"
#include "lib/material/alphatest.slang"
#include "lib/material/terms.slang"
#if defined(GBUFFER_MATERIAL) || defined(PBR)
#include "lib/material/pbr.slang"
#endif
#ifdef GI
import lib.view.gi;
import lib.view.reflection;
#ifdef GI_SKY
#include "lib/view/skyvis.slang"
#endif
#endif
#ifdef WET_LIT
#include "lib/material/wetness.slang"
#endif
#ifdef COVER_LIT
#include "lib/material/cover.slang"
#endif
#include "lib/light/directional.slang"
#ifdef PER_PIXEL_LIGHTING
import lib.view.screencoord;
#ifdef POINT_SHADOW
import lib.light.pointshadow;
#endif
#include "lib/light/pixellighting.slang"
#ifdef PBR_LIT
#include "lib/light/pbrlighting.slang"
#endif
#endif
#ifdef PRECIPITATION_LIT
// Task 0040: rain lit as scattering streaks.
#include "lib/light/precipitation.slang"
#endif
#ifdef PARALLAX_POM
// Task 0045: the height file, read with the caller's derivatives.
float pomHeight(float2 uv, float lod)
{
    return heightMap.SampleLevel(uv, lod).r;
}
float2 pomTextureSize()
{
    uint width, height;
    heightMap.GetDimensions(width, height);
    return float2(int2(int(width), int(height)));
}
#endif
#if defined(PARALLAX) || defined(PARALLAX_POM)
#include "lib/material/parallax.slang"
#endif
// Task 0045: with a height file the descriptor's spare float holds two half floats (Render::Parallax::packScales).
#ifdef HEIGHT_MAP
#define MATERIAL_ROUGHNESS_SCALE unpackHalf2x16(floatBitsToUint(material.align_dummy2)).x
#else
#define MATERIAL_ROUGHNESS_SCALE material.align_dummy2
#endif
#ifdef PARTICLE_OCCLUSION
#include "lib/particle/weatherocclusion.slang"
#endif
#ifdef SOFT_PARTICLE
// Soft particles fade against the opaque scene's depth (task 0012, V4k).
import lib.view.screencoord;
[[vk::binding(VIEW_REFLECT_DEPTH_MAP_BINDING, VIEW_SET)]] Sampler2D reflectDepthMap;
#endif

#include "lib/inout.slang"
#include "lib/sets.slang"
#include "lib/view/bindings.slang"

struct FragmentInput
{
    CommonInout io;
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
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    uint sampleMask : SV_Coverage;
#endif
};

[shader("fragment")]
FragmentOutput fragMain(FragmentInput input)
{
    FragmentOutput output;
    float4 outColor;
#ifdef GBUFFER
    output.normal = float4(0.0);
    output.linearDepth = 0.0;
    output.motion = float2(0.0);
#ifdef GBUFFER_MATERIAL
    output.materialValues = float4(0.0);
#endif
#ifdef GBUFFER_AMBIENT
    output.ambientPart = float4(0.0);
#endif
#endif
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    // Once the shader writes the sample mask, every path must (see the GLSL): all samples by default.
    output.sampleMask = 0xffffffffu;
#endif
    // Parallax moves the diffuse and normal map coordinates (objects.frag).
    float2 offset = float2(0.0);
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    // Task 0045: the slope of the height field where the ray ended and a little occlusion in recesses.
    float2 pomNormalXY = float2(0.0);
    float pomShade = 1.0;
#endif
#if defined(NORMAL_MAP) && defined(TEXCOORD)
    float3x3 normalToView = transpose(float3x3(input.io.viewTangent, input.io.viewBinormal, input.io.viewNormal));
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    // Task 0045: the height file wins over a normal map's alpha (see the GLSL).
    {
        float2 pomDx = ddx(input.io.texCoord[diffuseUV].xy);
        float2 pomDy = ddy(input.io.texCoord[diffuseUV].xy);
        float pomFade = pomDistanceFade(length(input.io.viewPos));
#ifdef MATERIAL
        float pomMeshDepth = unpackHalf2x16(floatBitsToUint(material.align_dummy2)).y;
#else
        float pomMeshDepth = 0.0;
#endif
        float pomDepth = pomDepthFromField(pomMeshDepth, pomWorldPerUv(input.io.viewPos, pomDx, pomDy)) * pomFade;
        if (pomFade > 0.0 && dot(scene.lightViewPos.xyz, scene.lightViewPos.xyz) != 0)
        {
            PomResult pom = pomMarch(input.io.texCoord[diffuseUV].xy,
                mul(transpose(normalToView), normalize(-input.io.viewPos)), pomDepth,
                1.0 - heightMap.SampleLevel(float2(0.5), 16.0).r, pomDx, pomDy);
            offset = pom.offset;
            pomNormalXY = pom.slope * pomFade;
            pomShade = 1.0 - POM_CAVITY_SHADE * pom.cavity * pomFade;
        }
#ifdef PARALLAX_DISCARD
        float2 pomUv = input.io.texCoord[diffuseUV].xy + offset;
        if (pomUv.x < 0.0 || pomUv.y < 0.0 || pomUv.x > 1.0 || pomUv.y > 1.0)
            discard;
#endif
    }
#elif defined(PARALLAX)
    float height = normalMap.Sample(input.io.texCoord[diffuseUV]).a;
    offset = getParallaxOffset(mul(transpose(normalToView), normalize(-input.io.viewPos)), height);
#endif
#endif
#ifdef DIFFUSE_MAP
    outColor = diffuseMap.Sample(input.io.texCoord[diffuseUV].xy + offset);
#ifdef ADJUST_COVERAGE
    uint diffuseWidth, diffuseHeight;
    diffuseMap.GetDimensions(diffuseWidth, diffuseHeight);
    outColor.a *= coveragePreservingAlphaScale((input.io.texCoord[diffuseUV].xy + offset)
        * float2(int2(int(diffuseWidth), int(diffuseHeight))));
#endif
#else
    outColor = float4(1);
#endif
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    outColor.xyz *= pomShade;
#endif
#ifdef DARK_MAP
    outColor *= darkMap.Sample(input.io.texCoord[darkUV]);
#ifdef ADJUST_COVERAGE
    uint darkWidth, darkHeight;
    darkMap.GetDimensions(darkWidth, darkHeight);
    outColor.a
        *= coveragePreservingAlphaScale(input.io.texCoord[darkUV] * float2(int2(int(darkWidth), int(darkHeight))));
#endif
#endif
#ifdef DETAIL_MAP
    outColor.xyz *= detailMap.Sample(input.io.texCoord[detailUV]).xyz * 2;
#endif

#ifdef MATERIAL
#ifdef ALPHA_TO_COVERAGE
    // Alpha to coverage in multisampled views (task 0012, V4j); the coverage is written to the sample mask below.
    bool toCoverage = scene.groundcoverParams.w > 1.0 && alphaTestToCoverage();
    float coverage = 1.0;
    if (toCoverage)
    {
        coverage = coverageAlpha(outColor.a, material.alphaTestCutoff);
        outColor.a = coverage;
    }
    else if (!alphaTest(outColor.a, material.alphaTestCutoff))
        discard;
#else
    if (!alphaTest(outColor.a, material.alphaTestCutoff))
        discard;
#endif
#endif
#ifdef LOD_FADE
    // Task 0059: a far object cluster fading in or out; the fade is the material's specular alpha.
    if (dither8(int(input.fragCoord.x) & 7, int(input.fragCoord.y) & 7, material.specular.a) == 0.0)
        discard;
#endif
    if (/*depthPass*/ dot(scene.lightViewPos.xyz, scene.lightViewPos.xyz) == 0)
    {
        if (outColor.a < 0.5)
            discard;
        output.color = outColor;
        return output;
    }
#ifdef PARTICLE_OCCLUSION
    if (isOccludedParticle(mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz))
        discard;
#endif

#ifdef DECAL_MAP
    float4 decalTex = decalMap.Sample(input.io.texCoord[decalUV]);
    outColor.xyz = lerp(outColor.xyz, decalTex.xyz, decalTex.a);
#endif
    // Task 0025: the maps are combined on encoded values and converted once.
    outColor.xyz = toLinearIfView(outColor.xyz);

#if defined(COLOR) || defined(PARTICLE)
    // PARTICLE: the particle's own colour and alpha, which the GLSL never reads (lib/material/terms.slang, task 0072,
    // stage h; files/shaders/CLAUDE.md).
    float4 vertexColor = input.io.color;
#else
    float4 vertexColor = float4(1);
#endif

#if defined(GBUFFER_MATERIAL) || defined(PBR)
    // The specular map's shininess and colour, when the surface has one (task 0027, tier 1).
    float pbrShininess = 0.0;
    float3 pbrSpecular = float3(0.0);
#endif
#ifdef GBUFFER_AMBIENT
    float3 ambientPart = float3(0.0);
#endif
    // The ambient light: the cell's flat colour, or the baked probes' value (task 0028).
    float3 ambientLight = scene.ambient.xyz;
#ifdef NORMAL
#if defined(NORMAL_MAP) && defined(TEXCOORD)
    float3 tangentNormal = normalMap.Sample(input.io.texCoord[diffuseUV] + offset).xyz * 2.0 - 1.0;
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    tangentNormal.xy += pomNormalXY;
#endif
#ifdef RECONSTRUCT_NORMAL_Z
    tangentNormal.z = sqrt(1.0 - dot(tangentNormal.xy, tangentNormal.xy));
#endif
    float3 viewNormal = normalize(mul(normalToView, tangentNormal));
    // With a normal map, the environment coordinates use the pixel's normal (objects.frag).
    float3 envReflect = reflect(normalize(input.io.viewPos), viewNormal);
    float envM = 2.0
        * sqrt(envReflect.x * envReflect.x + envReflect.y * envReflect.y + (envReflect.z + 1.0) * (envReflect.z + 1.0));
    float2 envUV = envReflect.xy / envM + 0.5;
#else
    float3 viewNormal = normalize(input.io.viewNormal);
    float2 envUV = input.io.envUV;
#endif
    // The shadow bias uses the geometry normal, as OpenMW's shadowing never saw the normal map.
    float sunShadow = shadow(input.io.viewPos, normalize(input.io.viewNormal));
#ifdef POINT_SHADOW
    pointShadowNormal = normalize(input.io.viewNormal);
#endif
#ifdef WET_LIT
    // Task 0037: rain wetness at this point (see the GLSL).
    WetSurface wetS = wetSurfaceAt(
        mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz, mul((float3x3)scene.invView, viewNormal));
    float3 wetWorldPos = mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz;
    {
#ifdef MATERIAL
        float wetSkin = material.subsurface > 0.5 ? 1.0 : 0.0;
#else
        float wetSkin = 0.0;
#endif
        float wetGlow = smoothstep(0.0, 0.3, dot(getEmission(vertexColor), float3(1.0)));
        float wetKeep = (1.0 - wetSkin) * (1.0 - wetGlow);
        wetS.wet *= wetKeep;
        wetS.puddle *= wetKeep;
        if (wetS.puddle > 0.0)
            viewNormal = normalize(
                lerp(viewNormal, mul(transpose((float3x3)scene.invView), wetPuddleNormal(wetWorldPos)), wetS.puddle));
    }
#endif
#ifdef COVER_LIT
    // Task 0039: snow or ash on this point (see the GLSL).
    CoverSurface coverS = coverSurfaceAt(
        mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz, mul((float3x3)scene.invView, viewNormal));
    if (coverS.mask > 0.0)
    {
#ifdef MATERIAL
        float coverSkin = material.subsurface > 0.5 ? 1.0 : 0.0;
#else
        float coverSkin = 0.0;
#endif
        float coverGlow = smoothstep(0.0, 0.3, dot(getEmission(vertexColor), float3(1.0)));
        coverS.mask *= (1.0 - coverSkin) * (1.0 - coverGlow);
    }
#endif
#ifdef GI
    ambientLight = giAmbient(
        mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz, mul((float3x3)scene.invView, viewNormal), ambientLight);
#ifdef GI_SKY
    // Task 0036: the exterior sky-visibility field.
    ambientLight = skyVisAmbient(
        mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz, mul((float3x3)scene.invView, viewNormal), ambientLight);
#endif
#endif
#ifdef PER_PIXEL_LIGHTING
    // OpenMW's doLighting with PER_PIXEL_LIGHTING: point lights summed here, specular from the specular map.
#if defined(SPECULAR_MAP) && defined(TEXCOORD)
    float4 specularTex = specularMap.Sample(input.io.texCoord[diffuseUV]);
    float shininess = max(specularTex.a * 255.0, 1e-4);
    float3 specularColor = toLinearIfView(specularTex.xyz);
#if defined(GBUFFER_MATERIAL) || defined(PBR)
    pbrShininess = shininess;
    pbrSpecular = specularTex.xyz;
#endif
#else
    float shininess = 1e-4;
    float3 specularColor = float3(0);
#endif
    float3 pointDiffuse, pointSpecular;
#ifdef PBR_LIT
    // The material model (lib/material/pbr.glsl).
#ifdef MATERIAL
    PbrSurface pbrLit = pbrSurfaceFrom(material.pbr, material.subsurface, pbrShininess, pbrSpecular);
#else
    PbrSurface pbrLit = pbrSurfaceFrom(float4(-1.0, 0.0, -1.0, 1.0), 0.0, pbrShininess, pbrSpecular);
#endif
#if defined(MATERIAL_MAP) && defined(TEXCOORD) && defined(MATERIAL)
#ifdef MATERIAL_MAP_RMAOS
    // Task 0044: the True PBR layout; the scale is in the descriptor's spare float (0: none).
    pbrLit = pbrApplyMapRmaos(
        pbrLit, material.pbr, materialMap.Sample(input.io.texCoord[diffuseUV] + offset), MATERIAL_ROUGHNESS_SCALE);
#else
    pbrLit = pbrApplyMap(pbrLit, material.pbr, materialMap.Sample(input.io.texCoord[diffuseUV] + offset).rgb);
#endif
#endif
    float3 pbrBase = outColor.xyz;
#ifdef COVER_LIT
    // Task 0039: a layer of snow or ash replaces the surface's colour; it is not a metal, and it is rough.
    if (coverS.mask > 0.0)
    {
        float3 coverColour = coverColourOf(coverS.kind);
        pbrBase = lerp(pbrBase, coverColour, coverS.mask);
        outColor.xyz = lerp(outColor.xyz, coverColour, coverS.mask);
        pbrLit.metalness *= 1.0 - coverS.mask;
        pbrLit.roughness = lerp(pbrLit.roughness, coverRoughnessOf(coverS.kind), coverS.mask);
    }
#endif
#ifdef WET_LIT
    // Task 0037: wet surfaces are darker and smoother (a puddle most of all). Metals keep their colour.
    if (wetS.wet > 0.0)
    {
        float wetSurface = wetS.wet * (1.0 - pbrLit.metalness);
        outColor.xyz *= wetDarkening(wetSurface, pbrLit.roughness) * lerp(1.0, wetPuddleDarkening, wetS.puddle);
        pbrLit.roughness = lerp(wetRoughnessOf(pbrLit.roughness, wetSurface), wetPuddleRoughness, wetS.puddle);
    }
#endif
    float3 pbrF0 = pbrSpecularF0(pbrBase, pbrLit.metalness, pbrLit.reflectance);
    pbrPointLights(viewPosToScreenCoord(input.io.viewPos, scene.projection), viewNormal, input.io.viewPos,
        pbrLit.roughness, pbrF0, pointDiffuse, pointSpecular);
    float3 diffuseLight = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    float3 specularLight = pointSpecular
        + scene.lightSpecular.xyz
            * pbrSpecularLobe(
                viewNormal, normalize(input.io.viewPos), scene.lightViewPos.xyz, pbrLit.roughness, pbrF0)
            * sunShadow;
#else
    pointLightsPerPixel(viewPosToScreenCoord(input.io.viewPos, scene.projection), viewNormal, input.io.viewPos,
        shininess, pointDiffuse, pointSpecular);
    float3 diffuseLight = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    float3 specularLight = pointSpecular
        + scene.lightSpecular.xyz
            * specularIntensity(viewNormal, normalize(input.io.viewPos), shininess, scene.lightViewPos.xyz)
            * sunShadow;
#endif
#else
    float3 diffuseLight = input.io.pointLightDiffuse + directionalDiffuse(viewNormal) * sunShadow;
#endif
#else
    float3 diffuseLight = float3(0);
#endif
#ifdef PBR_LIT
    {
        // Metals have no diffuse (and tint their specular with the base colour); see the GLSL for `physical`.
        float pbrNoV = max(dot(viewNormal, -normalize(input.io.viewPos)), 1e-3);
#ifdef PBR_PHYSICAL
        float3 pbrKd = float3(1.0) - pbrFresnel(pbrNoV, pbrF0);
        float pbrDiffuseScale = (1.0 - pbrLit.metalness) * dot(pbrKd, float3(1.0 / 3.0));
        float pbrAmbientScale = (1.0 - pbrLit.metalness) * pbrLit.occlusion;
        float pbrSpecularScale = 1.0;
#else
        float pbrDiffuseScale = 1.0 - pbrLit.metalness;
        float pbrAmbientScale = pbrDiffuseScale;
        float pbrSpecularScale = max(clamp((pbrLit.reflectance - 0.5) * 2.0, 0.0, 1.0), pbrLit.metalness);
#endif
#ifdef GBUFFER_AMBIENT
        // The part of the colour that ambient light makes, as materialTermsScaled adds it.
        ambientPart = outColor.xyz * (ambientLight * getAmbient(vertexColor) * pbrAmbientScale);
#endif
        outColor *= materialTermsScaled(ambientLight, diffuseLight, vertexColor, pbrAmbientScale, pbrDiffuseScale);
        outColor.xyz += specularLight * pbrSpecularScale;
#ifdef GI
        // Baked reflection probes (task 0028, stage d), only where the surface reflects at all.
        if (giReflectionEnabled())
        {
            float3 reflWorldPos = mul(scene.invView, float4(input.io.viewPos, 1.0)).xyz;
            float3 reflWorldDir
                = mul((float3x3)scene.invView, reflect(normalize(input.io.viewPos), viewNormal));
            float2 reflAB = reflEnvBrdf(pbrLit.roughness, pbrNoV);
            float3 reflLight = giReflection(reflWorldPos, reflWorldDir, pbrLit.roughness, scene.ambient.xyz);
#ifdef PBR_PHYSICAL
            float reflScale = pbrSpecularScale * pbrLit.occlusion;
#else
            float reflScale = pbrSpecularScale;
#endif
            outColor.xyz += reflLight * (pbrF0 * reflAB.x + float3(reflAB.y)) * reflScale;
        }
#endif
#ifdef WET_LIT
        if (wetS.puddle > 0.0)
        {
            // The sky in the puddle (task 0037).
            float3 wetEye = mul((float3x3)scene.invView, normalize(input.io.viewPos));
            float3 wetN = mul((float3x3)scene.invView, viewNormal);
            float3 wetDir = reflect(wetEye, wetN);
            float3 wetSky = wetSkyReflection(wetWorldPos, wetDir, wetPuddleRoughness);
            outColor.xyz += wetSky * (wetFresnel(dot(wetN, -wetEye)) * wetPuddleReflection * wetS.puddle);
        }
#ifdef WETNESS_DEBUG
        outColor.xyz = float3(wetS.wet, wetS.exposure, wetS.puddle);
#endif
#endif
#ifdef COVER_DEBUG
        outColor.xyz = float3(coverS.amount, coverS.upwardness, coverS.mask);
#endif
    }
#else
#ifdef GI
    outColor *= materialTermsAmbient(ambientLight, diffuseLight, vertexColor);
#else
    outColor *= materialTerms(diffuseLight, vertexColor);
#endif
#ifdef PER_PIXEL_LIGHTING
    outColor.xyz += specularColor * specularLight;
#endif
#endif

#ifdef NORMAL
    if (object.envColor.w != 0)
        outColor.xyz += toLinearIfView(
            sceneEnv.Sample(float3(input.io.envUV, float(envIndex(scene.time)))).xyz * object.envColor.xyz);
#endif
#ifdef ENV_MAP
    float envLuma = 1.0;
#ifdef NORMAL
    float2 envTexCoordGen = envUV;
#else
    float2 envTexCoordGen = input.io.envUV;
#endif
    // The GLSL's bump map lines under BUMP_MAP and TEXCOORD are commented out.
    outColor.xyz += toLinearIfView(envMap.Sample(envTexCoordGen).xyz * envLuma);
#endif

#ifdef GLOW_MAP
    outColor.xyz += toLinearIfView(glowMap.Sample(input.io.texCoord[glowUV]).xyz);
#endif
#ifdef PRECIPITATION_LIT
    // Task 0040: a streak scatters the scene's light (see the GLSL). The texture colour stays.
    outColor.xyz = toLinearIfView(diffuseMap.Sample(input.io.texCoord[diffuseUV].xy).xyz)
        * precipitationLight(input.io.viewPos, ambientLight);
    outColor.a *= precipFade(length(input.io.viewPos));
#ifdef GBUFFER_AMBIENT
    ambientPart = float3(0.0); // the streak's light is not the surface ambient that ambient occlusion would darken
#endif
#endif
    // Debug geometry (unlit colour mode 3) is not fogged, as the OSG build's debug shader was not.
    switch (colorMode)
    {
        case 3:
            break;
        default:
        {
#ifdef GBUFFER_AMBIENT
            // Fog keeps this share of the colour: the ambient part shrinks with it.
            ambientPart *= applyFog(float3(1.0), input.io.viewPos, input.fragCoord.xy)
                - applyFog(float3(0.0), input.io.viewPos, input.fragCoord.xy);
#endif
            outColor.xyz = applyFog(outColor.xyz, input.io.viewPos, input.fragCoord.xy);
            break;
        }
    }

#ifdef DEBUG_SHADOW
    outColor = debugShadow(outColor, input.fragCoord.xy);
#endif

#ifdef NORMAL
    if (object.alpha != 1)
    {
        int x = int(mod(input.fragCoord.x, 8.0));
        int y = int(mod(input.fragCoord.y, 8.0));
        float b = sceneEnv.Sample(float3(input.io.envUV, float(envIndex(scene.time)))).x;
        b = lerp(object.alpha, 1.0, b);
        float dither = dither8(x, y, b);
        if (dither == 0)
            discard;
    }
#endif
#ifdef SOFT_PARTICLE
    // OpenMW's calcSoftParticleFade without the view falloff (see the GLSL); only the main view has the depth copy.
    if (scene.groundcoverParams.w > 0.0)
    {
        float2 softCoord = input.fragCoord.xy / scene.resolution;
        float softSceneDepth = reflectDepthMap.Sample(softCoord).r;
        float3 softSceneViewPos = screenCoordToViewPos(softCoord, softSceneDepth, scene.invProjection).xyz;
        float softFalloff = max(input.io.softParticleSize * 0.33, 0.0001);
        float softDelta = input.io.viewPos.z - softSceneViewPos.z;
        outColor.a *= 0.845 * pow(clamp(softDelta / softFalloff, 0.0, 1.0), 1.3);
    }
#endif
#ifdef GBUFFER
#ifdef NORMAL
    output.normal = float4(viewNormal, 1.0);
#else
    output.normal = float4(0.0, 0.0, 1.0, 1.0);
#endif
    output.linearDepth = -input.io.viewPos.z;
#ifdef GBUFFER_AMBIENT
    output.ambientPart = float4(ambientPart, 1.0);
#endif
#ifdef GBUFFER_MOTION
    output.motion = input.io.motion;
#endif
#ifdef GBUFFER_MATERIAL
#ifdef MATERIAL
    PbrSurface pbrOut = pbrSurfaceFrom(material.pbr, material.subsurface, pbrShininess, pbrSpecular);
#else
    PbrSurface pbrOut = pbrSurfaceFrom(float4(-1.0, 0.0, -1.0, 1.0), 0.0, pbrShininess, pbrSpecular);
#endif
#if defined(MATERIAL_MAP) && defined(TEXCOORD) && defined(MATERIAL)
#ifdef MATERIAL_MAP_RMAOS
    pbrOut = pbrApplyMapRmaos(
        pbrOut, material.pbr, materialMap.Sample(input.io.texCoord[diffuseUV] + offset), MATERIAL_ROUGHNESS_SCALE);
#else
    pbrOut = pbrApplyMap(pbrOut, material.pbr, materialMap.Sample(input.io.texCoord[diffuseUV] + offset).rgb);
#endif
#endif
    output.normal.w = pbrOut.roughness;
    output.materialValues = float4(pbrOut.metalness, pbrOut.reflectance, pbrOut.occlusion, pbrOut.subsurface);
#endif
#endif
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    // The coverage covers that share of the pixel's samples, as the hardware's alpha to coverage would (undithered).
    if (toCoverage)
    {
        int samples = int(scene.groundcoverParams.w);
        int covered = int(clamp(coverage, 0.0, 1.0) * float(samples) + 0.5);
        // A shift by 32 is undefined; 32 or more covered samples is the whole first mask word.
        output.sampleMask = covered >= 32 ? 0xffffffffu : (1u << uint(covered)) - 1u;
    }
#endif
    output.color = outColor;
    return output;
}
