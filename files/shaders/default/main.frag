#pragma import_defines (NORMAL, PARTICLE, COLOR, TEXCOORD, DIFFUSE_MAP, DARK_MAP, DETAIL_MAP, DECAL_MAP, GLOW_MAP, BUMP_MAP, ENV_MAP, MATERIAL, NORMAL_MAP, PARALLAX, RECONSTRUCT_NORMAL_Z, SPECULAR_MAP)

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
#if defined(NORMAL) && (defined(NORMAL_MAP) || defined(SPECULAR_MAP))
#define PER_PIXEL_LIGHTING 1
#endif

#include "lib/view/scene.glsl"
#include "lib/view/env.glsl"
#include "lib/view/fog.glsl"
#include "lib/view/shadow.glsl"
#include "lib/view/dither.glsl"
#include "lib/object/object.glsl"
#include "lib/material/alphatest.glsl"
#include "lib/material/terms.glsl"
#include "lib/light/directional.glsl"
#ifdef PER_PIXEL_LIGHTING
#include "lib/view/screencoord.glsl"
#include "lib/light/pixellighting.glsl"
#endif
#ifdef PARALLAX
#include "lib/material/parallax.glsl"
#endif

layout(location=0) in Inputs
{
#include "lib/inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;

void main()
{
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
#else
    outColor = vec4(1);
#endif
#ifdef DARK_MAP
    outColor *= texture(darkMap, frag_in.texCoord[darkUV]);
#endif
#ifdef DETAIL_MAP
    outColor.xyz *= texture(detailMap, frag_in.texCoord[detailUV]).xyz * 2;
#endif

#ifdef MATERIAL
    if (!alphaTest(outColor.a, material.alphaTestCutoff))
        discard;
#endif
    if (/*depthPass*/dot(scene.lightViewPos.xyz, scene.lightViewPos.xyz) == 0)
    {
        if (outColor.a < 0.5)
            discard;
        return;
    }

#ifdef DECAL_MAP
    vec4 decalTex = texture(decalMap, frag_in.texCoord[decalUV]);
    outColor.xyz = mix(outColor.xyz, decalTex.xyz, decalTex.a /* * diffuseColor.a*/);
#endif

#ifdef COLOR
    vec4 vertexColor = frag_in.color;
#else
    vec4 vertexColor = vec4(1);
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
    vec3 specularColor = specularTex.xyz;
  #else
    float shininess = 1e-4;
    vec3 specularColor = vec3(0);
  #endif
    vec3 pointDiffuse, pointSpecular;
    pointLightsPerPixel(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        shininess, pointDiffuse, pointSpecular);
    vec3 diffuseLight = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * specularIntensity(viewNormal, normalize(frag_in.viewPos), shininess, scene.lightViewPos.xyz) * sunShadow;
 #else
    vec3 diffuseLight = frag_in.pointLightDiffuse + directionalDiffuse(viewNormal) * sunShadow;
 #endif
#else
    vec3 diffuseLight = vec3(0);
#endif
    outColor *= materialTerms(diffuseLight, vertexColor);
#ifdef PER_PIXEL_LIGHTING
    outColor.xyz += specularColor * specularLight;
#endif

#ifdef NORMAL
    if (object.envColor.w != 0)
        outColor.xyz += texture(sceneEnv, vec3(frag_in.envUV, envIndex(scene.time))).xyz * object.envColor.xyz;
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

    outColor.xyz += texture(envMap, envTexCoordGen).xyz * envLuma;
#endif

#ifdef GLOW_MAP
    outColor.xyz += texture(glowMap, frag_in.texCoord[glowUV]).xyz;
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
}
