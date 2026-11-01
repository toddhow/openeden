#pragma import_defines (NORMAL, PARTICLE, COLOR, TEXCOORD, DIFFUSE_MAP, DARK_MAP, DETAIL_MAP, DECAL_MAP, GLOW_MAP, BUMP_MAP, ENV_MAP, MATERIAL, NORMAL_MAP, PARALLAX, RECONSTRUCT_NORMAL_Z, SPECULAR_MAP, ALPHA_TO_COVERAGE, ADJUST_COVERAGE, SOFT_PARTICLE, PARTICLE_OCCLUSION, GBUFFER, GBUFFER_MOTION, GBUFFER_MATERIAL, PBR, PBR_PHYSICAL)

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
#include "lib/light/directional.glsl"
#ifdef PER_PIXEL_LIGHTING
#include "lib/view/screencoord.glsl"
#include "lib/light/pixellighting.glsl"
 #ifdef PBR_LIT
#include "lib/light/pbrlighting.glsl"
 #endif
#endif
#ifdef PARALLAX
#include "lib/material/parallax.glsl"
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
#endif
#if defined(MATERIAL) && defined(ALPHA_TO_COVERAGE)
    // OpenEden: once the shader writes the sample mask, every path must, or the mask is undefined (task 0012, V4j).
    // All samples by default, which is the same as not writing it; narrowed below where the coverage applies.
    gl_SampleMask[0] = -1;
#endif
    // OpenEden: parallax moves the diffuse and normal map coordinates (objects.frag).
    vec2 offset = vec2(0.0);
#if defined(NORMAL_MAP) && defined(TEXCOORD)
    mat3 normalToView = mat3(frag_in.viewTangent, frag_in.viewBinormal, frag_in.viewNormal);
 #ifdef PARALLAX
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
#ifdef NORMAL
 #if defined(NORMAL_MAP) && defined(TEXCOORD)
    vec3 tangentNormal = texture(normalMap, frag_in.texCoord[diffuseUV] + offset).xyz * 2.0 - 1.0;
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
    vec3 pbrBase = outColor.xyz;
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
        outColor *= materialTermsScaled(diffuseLight, vertexColor, pbrAmbientScale, pbrDiffuseScale);
        outColor.xyz += specularLight * pbrSpecularScale;
    }
#else
    outColor *= materialTerms(diffuseLight, vertexColor);
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
    // OpenEden: debug geometry (unlit colour mode 3) is not fogged, as the OSG build's debug shader was not.
    if (colorMode != 3)
        outColor.xyz = applyFog(outColor.xyz, frag_in.viewPos);

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
 #ifdef GBUFFER_MOTION
    outMotion = frag_in.motion;
 #endif
 #ifdef GBUFFER_MATERIAL
  #ifdef MATERIAL
    PbrSurface pbrOut = pbrSurfaceFrom(material.pbr, material.subsurface, pbrShininess, pbrSpecular);
  #else
    PbrSurface pbrOut = pbrSurfaceFrom(vec4(-1.0, 0.0, -1.0, 1.0), 0.0, pbrShininess, pbrSpecular);
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
