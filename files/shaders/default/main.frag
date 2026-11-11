#pragma import_defines (NORMAL, PARTICLE, COLOR, TEXCOORD, DIFFUSE_MAP, DARK_MAP, DETAIL_MAP, DECAL_MAP, GLOW_MAP, BUMP_MAP, ENV_MAP, MATERIAL, NORMAL_MAP, PARALLAX, RECONSTRUCT_NORMAL_Z, SPECULAR_MAP, ALPHA_TO_COVERAGE, ADJUST_COVERAGE, SOFT_PARTICLE, PARTICLE_OCCLUSION, GBUFFER, GBUFFER_MOTION, GBUFFER_MATERIAL, PBR, PBR_PHYSICAL, MATERIAL_MAP, MATERIAL_MAP_RMAOS, GI, GI_SKY, GBUFFER_AMBIENT, WETNESS, WETNESS_PUDDLES, WETNESS_DEBUG, COVER, COVER_DEBUG, HEIGHT_MAP, PARALLAX_POM, PARALLAX_POM_LOW, PARALLAX_POM_HIGH, PARALLAX_DISCARD, PRECIPITATION_LIT, LOD_FADE, POINT_SHADOW)

#include "lib/material/descriptors_frag.glsl"

layout(constant_id=0) const int numUvSets=1;
layout(constant_id=1) const int alphaTestMode=0;
layout(constant_id=2) const int colorMode=0;
layout(constant_id=3) const bool specular=false;
layout(constant_id=START_UVSET_CONSTANTS+DIFFUSE_UNIT) const int diffuseUV=0;
layout(constant_id=START_UVSET_CONSTANTS+DARK_UNIT) const int darkUV=0;
layout(constant_id=START_UVSET_CONSTANTS+DETAIL_UNIT) const int detailUV=0;
layout(constant_id=START_UVSET_CONSTANTS+DECAL_UNIT) const int decalUV=0;
layout(constant_id=START_UVSET_CONSTANTS+GLOW_UNIT) const int glowUV=0;
layout(constant_id=START_UVSET_CONSTANTS+BUMP_UNIT) const int bumpUV=0;
// OpenEden: normal and specular maps use the diffuse texture's UV set (task 0012, V4h); see mwrender/CLAUDE.md on why
// they have no specialization constants of their own.

// OpenEden: normal and specular maps light each pixel, as OpenMW's PER_PIXEL_LIGHTING (task 0012, V4h).
#if defined(NORMAL) && (defined(NORMAL_MAP) || defined(SPECULAR_MAP) || defined(PBR))
#define PER_PIXEL_LIGHTING 1
#endif
// OpenEden (task 0027, R1): lit surfaces with the material model's lighting.
#if defined(NORMAL) && defined(PBR)
#define PBR_LIT 1
#endif
// OpenEden (task 0037): rain wetness, in the lit surfaces that take the material model.
#if defined(NORMAL) && defined(PBR) && defined(WETNESS)
#define WET_LIT 1
#endif
// OpenEden (task 0039): snow and ash cover, in the same surfaces.
#if defined(NORMAL) && defined(PBR) && defined(COVER)
#define COVER_LIT 1
#endif

#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "lib/view/env.glsl"
#include "lib/view/fog.glsl"
#include "lib/view/shadow.glsl"
#include "lib/view/dither.glsl"
#include "lib/object/object.glsl"
#include "lib/material/alphatest.glsl"
#include "lib/material/terms.glsl"
#if defined(GBUFFER_MATERIAL) || defined(PBR)
#include "lib/material/pbr.glsl"
#endif
#ifdef GI
#include "lib/view/gi.glsl"
#include "lib/view/reflection.glsl"
 #ifdef GI_SKY
#include "lib/view/skyvis.glsl"
 #endif
#endif
#ifdef WET_LIT
#include "lib/material/wetness.glsl"
#ifdef WETNESS_PUDDLES
// OpenEden (task 0095, stage W-c): puddles from the baked puddle map, on the surfaces the bake knows (roads, decks).
#include "lib/material/puddlemap.glsl"
#endif
#endif
#ifdef COVER_LIT
#include "lib/material/cover.glsl"
#endif
#include "lib/light/directional.glsl"
#ifdef PER_PIXEL_LIGHTING
#include "lib/view/screencoord.glsl"
 #ifdef POINT_SHADOW
#include "lib/light/pointshadow.glsl"
 #endif
#include "lib/light/pixellighting.glsl"
 #ifdef PBR_LIT
#include "lib/light/pbrlighting.glsl"
 #endif
#endif
#ifdef PRECIPITATION_LIT
// OpenEden (task 0040): rain lit as scattering streaks (lib/light/precipitation.glsl).
#include "lib/light/precipitation.glsl"
#endif
#ifdef PARALLAX_POM
// OpenEden (task 0045): the height file, read with the caller's derivatives (lib/material/parallax.glsl).
float pomHeight(vec2 uv, float lod)
{
    return textureLod(heightMap, uv, lod).r;
}
vec2 pomTextureSize()
{
    return vec2(textureSize(heightMap, 0));
}
#endif
#if defined(PARALLAX) || defined(PARALLAX_POM)
#include "lib/material/parallax.glsl"
#endif
// OpenEden (task 0045): with a height file the descriptor's spare float holds two half floats (the roughness scale and
// the displacement scale, Render::Parallax::packScales); without one it is the roughness scale itself.
#ifdef HEIGHT_MAP
#define MATERIAL_ROUGHNESS_SCALE unpackHalf2x16(floatBitsToUint(material.align_dummy2)).x
#else
#define MATERIAL_ROUGHNESS_SCALE material.align_dummy2
#endif
#ifdef PARTICLE_OCCLUSION
#include "lib/particle/weatherocclusion.glsl"
#endif
#ifdef SOFT_PARTICLE
// OpenEden: soft particles fade against the opaque scene's depth (task 0012, V4k).
 #ifndef PER_PIXEL_LIGHTING
#include "lib/view/screencoord.glsl"
 #endif
layout(set=VIEW_SET, binding=VIEW_REFLECT_DEPTH_MAP_BINDING) uniform sampler2D reflectDepthMap;
#endif

layout(location=0) in Inputs
{
#include "lib/inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;
#ifdef GBUFFER
// OpenEden (task 0026, F3): the opaque scene's view-space shading normal (w = 1 where written) and the distance along
// the view axis; both stay zero where nothing opaque is drawn (the pass clears them to zero).
layout(location=1) out vec4 outNormal;
layout(location=2) out float outLinearDepth;
// The motion of skinned actors' bones on screen; zero for everything else.
layout(location=3) out vec2 outMotion;
#ifdef GBUFFER_MATERIAL
// OpenEden (task 0027): metalness, reflectance, occlusion and the subsurface mark; roughness goes in outNormal.w.
layout(location=4) out vec4 outMaterial;
#endif
#ifdef GBUFFER_AMBIENT
// OpenEden (task 0029): the ambient part of the colour (linear), which ambient occlusion darkens in the tonemap.
layout(location=5) out vec4 outAmbient;
#endif
#endif

void main()
{
#ifdef GBUFFER
    outNormal = vec4(0.0);
    outLinearDepth = 0.0;
    outMotion = vec2(0.0);
 #ifdef GBUFFER_MATERIAL
    outMaterial = vec4(0.0);
 #endif
 #ifdef GBUFFER_AMBIENT
    outAmbient = vec4(0.0);
 #endif
#endif
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    // OpenEden: once the shader writes the sample mask, every path must, or the mask is undefined (task 0012, V4j).
    // All samples by default, which is the same as not writing it; narrowed below where the coverage applies.
    gl_SampleMask[0] = -1;
#endif
    // OpenEden: parallax moves the diffuse and normal map coordinates (objects.frag).
    vec2 offset = vec2(0.0);
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    // Task 0045: the slope of the height field where the ray ended (added to the normal map's) and a little occlusion
    // in recesses.
    vec2 pomNormalXY = vec2(0.0);
    float pomShade = 1.0;
#endif
#if defined(NORMAL_MAP) && defined(TEXCOORD)
    mat3 normalToView = mat3(frag_in.viewTangent, frag_in.viewBinormal, frag_in.viewNormal);
 #if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    // Task 0045: the height file wins over a normal map's alpha. The derivatives are taken before any branch; the
    // march is skipped in the shadow pass (the depth is not displaced) and beyond the fade distance.
    {
        vec2 pomDx = dFdx(frag_in.texCoord[diffuseUV].st);
        vec2 pomDy = dFdy(frag_in.texCoord[diffuseUV].st);
        float pomFade = pomDistanceFade(length(frag_in.viewPos));
  #ifdef MATERIAL
        float pomMeshDepth = unpackHalf2x16(floatBitsToUint(material.align_dummy2)).y;
  #else
        float pomMeshDepth = 0.0;
  #endif
        // With a material the high half is normally the mesh's own depth in texture coordinates, one number for the
        // whole mesh (nif.cpp); the derivatives are the fall-back.
        float pomDepth = pomDepthFromField(pomMeshDepth, pomWorldPerUv(frag_in.viewPos, pomDx, pomDy)) * pomFade;
        if (pomFade > 0.0 && dot(scene.lightViewPos.xyz, scene.lightViewPos.xyz) != 0)
        {
            PomResult pom = pomMarch(frag_in.texCoord[diffuseUV].st, transpose(normalToView) * normalize(-frag_in.viewPos),
                pomDepth, 1.0 - textureLod(heightMap, vec2(0.5), 16.0).r, pomDx, pomDy);
            offset = pom.offset;
            pomNormalXY = pom.slope * pomFade;
            pomShade = 1.0 - POM_CAVITY_SHADE * pom.cavity * pomFade;
        }
  #ifdef PARALLAX_DISCARD
        vec2 pomUv = frag_in.texCoord[diffuseUV].st + offset;
        if (pomUv.x < 0.0 || pomUv.y < 0.0 || pomUv.x > 1.0 || pomUv.y > 1.0)
            discard;
  #endif
    }
 #elif defined(PARALLAX)
    float height = texture(normalMap, frag_in.texCoord[diffuseUV]).a;
    offset = getParallaxOffset(transpose(normalToView) * normalize(-frag_in.viewPos), height);
 #endif
#endif
#ifdef DIFFUSE_MAP
    outColor = texture(diffuseMap, frag_in.texCoord[diffuseUV].st + offset);
 #ifdef ADJUST_COVERAGE
    outColor.a *= coveragePreservingAlphaScale((frag_in.texCoord[diffuseUV].st + offset) * vec2(textureSize(diffuseMap, 0)));
 #endif
#else
    outColor = vec4(1);
#endif
#if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    outColor.xyz *= pomShade;
#endif
#ifdef DARK_MAP
    outColor *= texture(darkMap, frag_in.texCoord[darkUV]);
 #ifdef ADJUST_COVERAGE
    outColor.a *= coveragePreservingAlphaScale(frag_in.texCoord[darkUV] * vec2(textureSize(darkMap, 0)));
 #endif
#endif
#ifdef DETAIL_MAP
    outColor.xyz *= texture(detailMap, frag_in.texCoord[detailUV]).xyz * 2;
#endif

#ifdef MATERIAL
 #ifdef ALPHA_TO_COVERAGE
    // OpenEden: alpha to coverage in multisampled views (task 0012, V4j); the coverage is written to the sample mask
    // below, since VSG overrides a pipeline's multisample state.
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
    // OpenEden (task 0059): a far object cluster fading in or out; the fade is the material's specular alpha.
    if (dither8(int(gl_FragCoord.x) & 7, int(gl_FragCoord.y) & 7, material.specular.a) == 0.0)
        discard;
#endif
    if (/*depthPass*/dot(scene.lightViewPos.xyz, scene.lightViewPos.xyz) == 0)
    {
        if (outColor.a < 0.5)
            discard;
        return;
    }
#ifdef PARTICLE_OCCLUSION
    if (isOccludedParticle((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz))
        discard;
#endif

#ifdef DECAL_MAP
    vec4 decalTex = texture(decalMap, frag_in.texCoord[decalUV]);
    outColor.xyz = mix(outColor.xyz, decalTex.xyz, decalTex.a /* * diffuseColor.a*/);
#endif
    // OpenEden (task 0025): the maps are combined on encoded values, as the detail map's times two is not neutral in linear
    // light, and converted once. Unchanged outside the linear view.
    outColor.xyz = toLinearIfView(outColor.xyz);

#ifdef COLOR
    vec4 vertexColor = frag_in.color;
#else
    vec4 vertexColor = vec4(1);
#endif

#if defined(GBUFFER_MATERIAL) || defined(PBR)
    // The specular map's shininess and colour, when the surface has one (task 0027, tier 1).
    float pbrShininess = 0.0;
    vec3 pbrSpecular = vec3(0.0);
#endif
#ifdef GBUFFER_AMBIENT
    vec3 ambientPart = vec3(0.0);
#endif
    // The ambient light: the cell's flat colour, or the baked probes' value (task 0028).
    vec3 ambientLight = scene.ambient.xyz;
#ifdef NORMAL
 #if defined(NORMAL_MAP) && defined(TEXCOORD)
    vec3 tangentNormal = texture(normalMap, frag_in.texCoord[diffuseUV] + offset).xyz * 2.0 - 1.0;
  #if defined(HEIGHT_MAP) && defined(PARALLAX_POM)
    tangentNormal.xy += pomNormalXY;
  #endif
  #ifdef RECONSTRUCT_NORMAL_Z
    tangentNormal.z = sqrt(1.0 - dot(tangentNormal.xy, tangentNormal.xy));
  #endif
    vec3 viewNormal = normalize(normalToView * tangentNormal);
    // With a normal map, the environment coordinates use the pixel's normal (objects.frag).
    vec3 envReflect = reflect(normalize(frag_in.viewPos), viewNormal);
    float envM = 2.0 * sqrt(envReflect.x * envReflect.x + envReflect.y * envReflect.y + (envReflect.z + 1.0) * (envReflect.z + 1.0));
    vec2 envUV = envReflect.xy / envM + 0.5;
 #else
    vec3 viewNormal = normalize(frag_in.viewNormal);
    vec2 envUV = frag_in.envUV;
 #endif
    // The shadow bias uses the geometry normal, as OpenMW's shadowing never saw the normal map.
    float sunShadow = shadow(frag_in.viewPos, normalize(frag_in.viewNormal));
#ifdef POINT_SHADOW
    pointShadowNormal = normalize(frag_in.viewNormal);
#endif
#ifdef WET_LIT
    // Task 0037: rain wetness at this point (0 under cover, indoors and in dry weather). Skin and glowing surfaces stay
    // dry. A puddle flattens the shading normal and tilts it with the ripples; the rain's darkening and the gloss come
    // below, once the surface's roughness is known.
    WetSurface wetS = wetSurfaceAt((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * viewNormal);
    vec3 wetWorldPos = (scene.invView * vec4(frag_in.viewPos, 1.0)).xyz;
    float wetPuddleDark = wetPuddleDarkening;
    float wetRippleGate = 1.0;
  #ifdef WETNESS_PUDDLES
    // Task 0095, stage W-c: where the baked puddle map covers this pixel (its cell is loaded, the pixel is on the
    // surface the map describes and faces up) it decides the puddle; anywhere else the noise puddles stand.
    PuddleShade wetPm = puddleShadeAt(
        wetWorldPos, wetS.wet, (mat3(scene.invView) * normalize(frag_in.viewNormal)).z, true);
    if (wetPm.covered)
    {
        wetS.puddle = wetS.wet > 0.0 ? wetPm.coverage : 0.0;
        wetPuddleDark = wetPuddleDarkening * puddleDepthTint(wetPm.depth);
        wetRippleGate = wetPm.rippleGate;
    }
  #endif
    {
  #ifdef MATERIAL
        float wetSkin = material.subsurface > 0.5 ? 1.0 : 0.0;
  #else
        float wetSkin = 0.0;
  #endif
        float wetGlow = smoothstep(0.0, 0.3, dot(getEmission(vertexColor), vec3(1.0)));
        float wetKeep = (1.0 - wetSkin) * (1.0 - wetGlow);
        wetS.wet *= wetKeep;
        wetS.puddle *= wetKeep;
        if (wetS.puddle > 0.0)
        {
            vec3 wetPN = wetPuddleNormal(wetWorldPos);
            // The rain's ripples only show where the water is deep enough (the baked map).
            if (wetRippleGate < 1.0)
                wetPN = normalize(mix(vec3(0.0, 0.0, 1.0), wetPN, wetRippleGate));
            viewNormal = normalize(mix(viewNormal, transpose(mat3(scene.invView)) * wetPN, wetS.puddle));
        }
    }
#endif
#ifdef COVER_LIT
    // Task 0039: snow or ash on this point (nothing indoors, in other weather, on walls and under cover). Skin and
    // glowing surfaces stay bare. The colour and roughness change below, once the surface's own are known.
    CoverSurface coverS = coverSurfaceAt((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * viewNormal);
    if (coverS.mask > 0.0)
    {
  #ifdef MATERIAL
        float coverSkin = material.subsurface > 0.5 ? 1.0 : 0.0;
  #else
        float coverSkin = 0.0;
  #endif
        float coverGlow = smoothstep(0.0, 0.3, dot(getEmission(vertexColor), vec3(1.0)));
        coverS.mask *= (1.0 - coverSkin) * (1.0 - coverGlow);
    }
#endif
#ifdef GI
    ambientLight = giAmbient((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * viewNormal, ambientLight);
 #ifdef GI_SKY
    // Task 0036: the exterior sky-visibility field (it leaves the flat ambient alone where no cell has a file).
    float skyReflVisibility;
    float skyDataWeight;
    ambientLight = skyVisAmbientKeep((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * viewNormal, ambientLight,
        mat3(scene.invView) * reflect(normalize(frag_in.viewPos), viewNormal), skyReflVisibility, skyDataWeight);
 #endif
#endif
 #ifdef PER_PIXEL_LIGHTING
    // OpenMW's doLighting with PER_PIXEL_LIGHTING: point lights summed here, specular from the specular map.
  #if defined(SPECULAR_MAP) && defined(TEXCOORD)
    vec4 specularTex = texture(specularMap, frag_in.texCoord[diffuseUV]);
    float shininess = max(specularTex.a * 255.0, 1e-4);
    vec3 specularColor = toLinearIfView(specularTex.xyz);
   #if defined(GBUFFER_MATERIAL) || defined(PBR)
    pbrShininess = shininess;
    pbrSpecular = specularTex.xyz;
   #endif
  #else
    float shininess = 1e-4;
    vec3 specularColor = vec3(0);
  #endif
    vec3 pointDiffuse, pointSpecular;
  #ifdef PBR_LIT
    // The material model: the vanilla diffuse light, a microfacet specular (the specular map's colour and shininess
    // are read as its roughness and reflectance, lib/material/pbr.glsl).
   #ifdef MATERIAL
    PbrSurface pbrLit = pbrSurfaceFrom(material.pbr, material.subsurface, pbrShininess, pbrSpecular);
   #else
    PbrSurface pbrLit = pbrSurfaceFrom(vec4(-1.0, 0.0, -1.0, 1.0), 0.0, pbrShininess, pbrSpecular);
   #endif
   #if defined(MATERIAL_MAP) && defined(TEXCOORD) && defined(MATERIAL)
   #ifdef MATERIAL_MAP_RMAOS
    // Task 0044: the True PBR layout; the scale is in the descriptor's spare float (0: none).
    pbrLit = pbrApplyMapRmaos(pbrLit, material.pbr, texture(materialMap, frag_in.texCoord[diffuseUV] + offset),
        MATERIAL_ROUGHNESS_SCALE);
   #else
    pbrLit = pbrApplyMap(pbrLit, material.pbr, texture(materialMap, frag_in.texCoord[diffuseUV] + offset).rgb);
   #endif
   #endif
    vec3 pbrBase = outColor.xyz;
#ifdef COVER_LIT
    // Task 0039: a layer of snow or ash: its colour replaces the surface's, it is not a metal, and it is rough.
    if (coverS.mask > 0.0)
    {
        vec3 coverColour = coverColourOf(coverS.kind);
        pbrBase = mix(pbrBase, coverColour, coverS.mask);
        outColor.xyz = mix(outColor.xyz, coverColour, coverS.mask);
        pbrLit.metalness *= 1.0 - coverS.mask;
        pbrLit.roughness = mix(pbrLit.roughness, coverRoughnessOf(coverS.kind), coverS.mask);
    }
#endif
#ifdef WET_LIT
    // Task 0037: wet surfaces are darker and smoother (a puddle most of all). Metals keep their colour.
    if (wetS.wet > 0.0)
    {
        float wetSurface = wetS.wet * (1.0 - pbrLit.metalness);
        outColor.xyz *= wetDarkening(wetSurface, pbrLit.roughness)
            * mix(1.0, wetPuddleDark, wetS.puddle);
        pbrLit.roughness = mix(wetRoughnessOf(pbrLit.roughness, wetSurface), wetPuddleRoughness, wetS.puddle);
    }
#endif
    vec3 pbrF0 = pbrSpecularF0(pbrBase, pbrLit.metalness, pbrLit.reflectance);
    pbrPointLights(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        pbrLit.roughness, pbrF0, pointDiffuse, pointSpecular);
    vec3 diffuseLight = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * pbrSpecularLobe(viewNormal, normalize(frag_in.viewPos), scene.lightViewPos.xyz, pbrLit.roughness, pbrF0)
        * sunShadow;
  #else
    pointLightsPerPixel(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        shininess, pointDiffuse, pointSpecular);
    vec3 diffuseLight = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * specularIntensity(viewNormal, normalize(frag_in.viewPos), shininess, scene.lightViewPos.xyz) * sunShadow;
  #endif
 #else
    vec3 diffuseLight = frag_in.pointLightDiffuse + directionalDiffuse(viewNormal) * sunShadow;
 #endif
#else
    vec3 diffuseLight = vec3(0);
#endif
#ifdef PBR_LIT
    {
        // Metals have no diffuse (and tint their specular with the base colour). `physical` also takes the share of
        // light the Fresnel reflection already carried away, scales the ambient by the baked occlusion, and lets every
        // surface reflect; `vanilla` keeps the vanilla diffuse and reflects only where there is specular data.
        float pbrNoV = max(dot(viewNormal, -normalize(frag_in.viewPos)), 1e-3);
  #ifdef PBR_PHYSICAL
        vec3 pbrKd = vec3(1.0) - pbrFresnel(pbrNoV, pbrF0);
        float pbrDiffuseScale = (1.0 - pbrLit.metalness) * dot(pbrKd, vec3(1.0 / 3.0));
        float pbrAmbientScale = (1.0 - pbrLit.metalness) * pbrLit.occlusion;
        float pbrSpecularScale = 1.0;
  #else
        float pbrDiffuseScale = 1.0 - pbrLit.metalness;
        float pbrAmbientScale = pbrDiffuseScale;
        float pbrSpecularScale = max(clamp((pbrLit.reflectance - 0.5) * 2.0, 0.0, 1.0), pbrLit.metalness);
  #endif
#ifdef GBUFFER_AMBIENT
        // The part of the colour that ambient light makes, as materialTermsScaled adds it: the texture colour times the
        // ambient light times the material's ambient colour. Ambient occlusion takes the occluded share of it out.
        ambientPart = outColor.xyz * (ambientLight * getAmbient(vertexColor) * pbrAmbientScale);
#endif
        outColor *= materialTermsScaled(ambientLight, diffuseLight, vertexColor, pbrAmbientScale, pbrDiffuseScale);
        outColor.xyz += specularLight * pbrSpecularScale;
  #ifdef GI
        // Baked reflection probes (task 0028, stage d): what the room around looks like in the mirror direction,
        // blurred by roughness, times the split-sum environment BRDF. Only where the surface reflects at all.
        if (giReflectionEnabled())
        {
            vec3 reflWorldPos = (scene.invView * vec4(frag_in.viewPos, 1.0)).xyz;
            vec3 reflWorldDir = mat3(scene.invView) * reflect(normalize(frag_in.viewPos), viewNormal);
            vec2 reflAB = reflEnvBrdf(pbrLit.roughness, pbrNoV);
            vec3 reflLight = giReflection(reflWorldPos, reflWorldDir, pbrLit.roughness, scene.ambient.xyz);
   #ifdef PBR_PHYSICAL
            float reflScale = pbrSpecularScale * pbrLit.occlusion;
   #else
            float reflScale = pbrSpecularScale;
   #endif
   #ifdef GI_SKY
            // The sky image is one picture of the whole sky: it lights a surface only as far as that surface sees the
            // sky in the mirror direction, and a rough dielectric gets little of it (skyReflectionFactor). Interior
            // probes are baked at a room and are unchanged.
            if (giReflectionIsSkyEnvironment())
                reflScale *= skyReflectionFactor(skyReflVisibility, skyDataWeight, pbrLit.roughness, pbrLit.metalness);
   #endif
            outColor.xyz += reflLight * (pbrF0 * reflAB.x + vec3(reflAB.y)) * reflScale;
        }
  #endif
  #ifdef WET_LIT
        if (wetS.puddle > 0.0)
        {
            // The sky in the puddle (task 0037): the world's mirror direction off the rippled water, times water's Fresnel.
            vec3 wetEye = mat3(scene.invView) * normalize(frag_in.viewPos);
            vec3 wetN = mat3(scene.invView) * viewNormal;
            vec3 wetDir = reflect(wetEye, wetN);
            vec3 wetSky = wetSkyReflection(wetWorldPos, wetDir, wetPuddleRoughness);
            outColor.xyz += wetSky * (wetFresnel(dot(wetN, -wetEye)) * wetPuddleReflection * wetS.puddle);
        }
   #ifdef WETNESS_DEBUG
        outColor.xyz = vec3(wetS.wet, wetS.exposure, wetS.puddle);
   #endif
   #ifdef WETNESS_PUDDLES
        // Task 0095: a debug view of the puddle map (the setting `wetness puddle debug`).
        if (puddleDebugMode() != 0u)
            outColor.xyz = puddleDebugColour(wetPm);
   #endif
  #endif
  #ifdef COVER_DEBUG
        outColor.xyz = vec3(coverS.amount, coverS.upwardness, coverS.mask);
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
        outColor.xyz += toLinearIfView(texture(sceneEnv, vec3(frag_in.envUV, envIndex(scene.time))).xyz * object.envColor.xyz);
#endif
#ifdef ENV_MAP
    float envLuma = 1.0;
 #ifdef NORMAL
    vec2 envTexCoordGen = envUV;
 #else
    vec2 envTexCoordGen = frag_in.envUV;
 #endif
 #if defined(BUMP_MAP) && defined(TEXCOORD)
/*
    vec4 bumpTex = texture(bumpMap, frag_in.texCoord[bumpUV]);
    envTexCoordGen += bumpTex.rg * bumpMapMatrix;
    envLuma = clamp(bumpTex.b * envMapLumaBias.x + envMapLumaBias.y, 0.0, 1.0);
*/
 #endif

    outColor.xyz += toLinearIfView(texture(envMap, envTexCoordGen).xyz * envLuma);
#endif

#ifdef GLOW_MAP
    outColor.xyz += toLinearIfView(texture(glowMap, frag_in.texCoord[glowUV]).xyz);
#endif
#ifdef PRECIPITATION_LIT
    // OpenEden (task 0040): a streak scatters the scene's light (ambient, the sun it faces, the lamps near it) instead of
    // being shaded as a surface with a made-up normal; far streaks fade out into the fog. The texture colour stays.
    outColor.xyz = toLinearIfView(texture(diffuseMap, frag_in.texCoord[diffuseUV].st).xyz) * precipitationLight(frag_in.viewPos, ambientLight);
    outColor.a *= precipFade(length(frag_in.viewPos));
 #ifdef GBUFFER_AMBIENT
    ambientPart = vec3(0.0); // the streak's light is not the surface ambient that ambient occlusion would darken
 #endif
#endif
    // OpenEden: debug geometry (unlit colour mode 3) is not fogged, as the OSG build's debug shader was not.
    if (colorMode != 3)
    {
#ifdef GBUFFER_AMBIENT
        // Fog keeps this share of the colour (mix(fog, c, k) = k * c + (1 - k) * fog): the ambient part shrinks with it.
        ambientPart *= applyFog(vec3(1.0), frag_in.viewPos) - applyFog(vec3(0.0), frag_in.viewPos);
#endif
        outColor.xyz = applyFog(outColor.xyz, frag_in.viewPos);
    }

#ifdef DEBUG_SHADOW
    outColor = debugShadow(outColor);
#endif

#ifdef NORMAL
    if (object.alpha != 1)
    {
        int x = int(mod(gl_FragCoord.x, 8));
        int y = int(mod(gl_FragCoord.y, 8));
        float b = texture(sceneEnv, vec3(frag_in.envUV, envIndex(scene.time))).x;
        b = mix(object.alpha, 1.0, b);
        float dither = dither8(x, y, b);
        if (dither == 0)
            discard;
    }
#endif
#ifdef SOFT_PARTICLE
    // OpenMW's calcSoftParticleFade without the view falloff (lib/particle/soft.glsl at 442eec6e2f): the alpha fades
    // over a third of the particle size in front of the opaque scene. The scene's depth is the copy the water uses,
    // taken before the depth-sorted bins and scaled to its own size, so it is read in screen coordinates. Only the main
    // view has the copy (marked by its sample count in groundcoverParams.w); others bind a placeholder and skip it.
    if (scene.groundcoverParams.w > 0.0)
    {
        vec2 softCoord = gl_FragCoord.xy / scene.resolution;
        float softSceneDepth = texture(reflectDepthMap, softCoord).r;
        vec3 softSceneViewPos = screenCoordToViewPos(softCoord, softSceneDepth, scene.invProjection).xyz;
        float softFalloff = max(frag_in.softParticleSize * 0.33, 0.0001);
        float softDelta = frag_in.viewPos.z - softSceneViewPos.z;
        outColor.a *= 0.845 * pow(clamp(softDelta / softFalloff, 0.0, 1.0), 1.3);
    }
#endif
#ifdef GBUFFER
 #ifdef NORMAL
    outNormal = vec4(viewNormal, 1.0);
 #else
    outNormal = vec4(0.0, 0.0, 1.0, 1.0);
 #endif
    outLinearDepth = -frag_in.viewPos.z;
 #ifdef GBUFFER_AMBIENT
    outAmbient = vec4(ambientPart, 1.0);
 #endif
 #ifdef GBUFFER_MOTION
    outMotion = frag_in.motion;
 #endif
 #ifdef GBUFFER_MATERIAL
  #ifdef MATERIAL
    PbrSurface pbrOut = pbrSurfaceFrom(material.pbr, material.subsurface, pbrShininess, pbrSpecular);
  #else
    PbrSurface pbrOut = pbrSurfaceFrom(vec4(-1.0, 0.0, -1.0, 1.0), 0.0, pbrShininess, pbrSpecular);
  #endif
  #if defined(MATERIAL_MAP) && defined(TEXCOORD) && defined(MATERIAL)
   #ifdef MATERIAL_MAP_RMAOS
    pbrOut = pbrApplyMapRmaos(pbrOut, material.pbr, texture(materialMap, frag_in.texCoord[diffuseUV] + offset),
        MATERIAL_ROUGHNESS_SCALE);
   #else
    pbrOut = pbrApplyMap(pbrOut, material.pbr, texture(materialMap, frag_in.texCoord[diffuseUV] + offset).rgb);
   #endif
  #endif
    outNormal.w = pbrOut.roughness;
    outMaterial = vec4(pbrOut.metalness, pbrOut.reflectance, pbrOut.occlusion, pbrOut.subsurface);
 #endif
#endif
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    // The coverage covers that share of the pixel's samples, as the hardware's alpha to coverage would (undithered).
    if (toCoverage)
    {
        int samples = int(scene.groundcoverParams.w);
        int covered = int(clamp(coverage, 0.0, 1.0) * float(samples) + 0.5);
        // A shift by 32 is undefined; 32 or more covered samples is the whole first mask word.
        gl_SampleMask[0] = covered >= 32 ? -1 : (1 << covered) - 1;
    }
#endif
}
