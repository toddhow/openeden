#pragma import_defines (TERRAIN_MAPS, GBUFFER, GBUFFER_MATERIAL)
const int colorMode = 2;

#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "lib/view/fog.glsl"
#include "lib/view/shadow.glsl"
#include "lib/light/directional.glsl"
#include "lib/material/terms.glsl"
#include "bindings.glsl"
#include "terrain.settings"

layout(set=TEXTURE_SET, binding=BLENDMAP_BINDING) uniform sampler2D blendMap;

layout(set=TEXTURE_SET, binding=LAYER_BINDING) uniform sampler2DArray diffuseMap;

layout(set=TEXTURE_SET, binding=COLORMAP_BINDING) uniform sampler2D colorMap;

#ifdef TERRAIN_MAPS
// OpenEden: terrain normal and specular maps (task 0012, V4i). Each layer's flags hold its normal map's index in
// normalLayers (0xffff for none), then parallax (bit 16) and specular (bit 17); Terrain::Builder fills them.
layout(set=TEXTURE_SET, binding=NORMAL_LAYER_BINDING) uniform sampler2DArray normalLayers;
layout(std430, set=TEXTURE_SET, binding=LAYER_FLAGS_BINDING) readonly buffer LayerFlags {
    uint layerFlags[];
};
const uint NO_NORMAL_MAP = 0xffffu;
const uint PARALLAX_FLAG = 1u << 16;
const uint SPECULAR_FLAG = 1u << 17;
#include "lib/view/screencoord.glsl"
#include "lib/light/pixellighting.glsl"
#include "lib/material/parallax.glsl"
#endif

layout(location=0) in Inputs
{
#include "inout.glsl"
} frag_in;

layout(location=0) out vec4 outColor;
#ifdef GBUFFER
// OpenEden (task 0026, F3): see default/main.frag.
layout(location=1) out vec4 outNormal;
layout(location=2) out float outLinearDepth;
layout(location=3) out vec2 outMotion;
#ifdef GBUFFER_MATERIAL
// OpenEden (task 0027): metalness, reflectance, occlusion, subsurface. The terrain has no per-layer data.
layout(location=4) out vec4 outMaterial;
#endif
#endif

void main()
{
    float blendTexSize = textureSize(blendMap, 0).x;
    vec2 blendCoord = frag_in.texCoord * blendTexSize;
    ivec2 blendTexel = ivec2(blendCoord);
    //blendTexel = min(blendTexel, int(blendTexSize-1));
    vec2 blendPos = blendCoord - blendTexel;
    blendPos = clamp((blendPos-0.5)*2+0.5, 0.0, 1.0);
    vec4 blendCoeffs = vec4(blendPos, 1-blendPos);
    blendCoeffs = blendCoeffs.zxzx * blendCoeffs.wwyy;
    ivec4 textures = ivec4(texelFetch(blendMap, blendTexel, 0) * 255);
    vec2 diffuseTexCoord = frag_in.texCoord * blendTexSize;

    outColor = vec4(0,0,0,1);
#ifdef TERRAIN_MAPS
    // The four layers' colours, tangent-space normals and specular strengths are blended, then lit once per pixel
    // (OpenMW drew one pass per layer and blended the lit results; compatibility/terrain.frag).
    mat3 normalToView = mat3(frag_in.viewTangent, frag_in.viewBinormal, frag_in.viewNormal);
    vec3 eyeDir = transpose(normalToView) * normalize(-frag_in.viewPos);
    vec3 blendedNormal = vec3(0);
    float specularStrength = 0.0;
    for (int i=0; i<4; ++i)
    {
        // Layers change per blend map texel, so neighbouring pixels may take different layers: every texture is
        // sampled outside branches (implicit derivatives are undefined in non-uniform control flow) and the flags only
        // select the results. A missing normal map's index is clamped to a valid array layer by the sampler.
        uint flags = layerFlags[textures[i]];
        uint normalIndex = flags & NO_NORMAL_MAP;
        bool hasNormalMap = normalIndex != NO_NORMAL_MAP;
        float height = texture(normalLayers, vec3(diffuseTexCoord, normalIndex)).a;
        vec2 parallaxOffset = getParallaxOffset(eyeDir, height);
        vec2 layerCoord = diffuseTexCoord + ((hasNormalMap && (flags & PARALLAX_FLAG) != 0u) ? parallaxOffset : vec2(0.0));
        vec4 diffuseTex = texture(diffuseMap, vec3(layerCoord, textures[i]));
        outColor.xyz += diffuseTex.xyz * blendCoeffs[i];
        specularStrength += ((flags & SPECULAR_FLAG) != 0u ? diffuseTex.a : 0.0) * blendCoeffs[i];
        vec3 mapNormal = texture(normalLayers, vec3(layerCoord, normalIndex)).xyz * 2.0 - 1.0;
        blendedNormal += (hasNormalMap ? mapNormal : vec3(0.0, 0.0, 1.0)) * blendCoeffs[i];
    }
    vec3 viewNormal = normalize(normalToView * blendedNormal);
    // The shadow bias keeps the geometry normal, as OpenMW's shadows did.
    float sunShadow = shadow(frag_in.viewPos, frag_in.viewNormal);
    // terrain.frag's specular: shininess 128 and the layer's diffuse alpha as a grey specular colour.
    const float shininess = 128.0;
    vec3 pointDiffuse, pointSpecular;
    pointLightsPerPixel(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        shininess, pointDiffuse, pointSpecular);
    vec3 diffuse = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * specularIntensity(viewNormal, normalize(frag_in.viewPos), shininess, scene.lightViewPos.xyz) * sunShadow;
#else
    for (int i=0; i<4; ++i)
        outColor.xyz += texture(diffuseMap, vec3(diffuseTexCoord, textures[i])).xyz * blendCoeffs[i];

    vec3 diffuse = frag_in.pointLightDiffuse + directionalDiffuse(normalize(frag_in.viewNormal)) * shadow(frag_in.viewPos, frag_in.viewNormal);
#endif

    // OpenEden (task 0025): the layers are blended on encoded values and converted once (lib/view/linear.glsl).
    outColor.xyz = toLinearIfView(outColor.xyz);

    vec2 colorTexSize = textureSize(colorMap, 0);
    vec2 colorCoord = (frag_in.texCoord - 0.5) * colorTexSize / (colorTexSize+1) + 0.5;
    vec4 vertexColor = texture(colorMap, colorCoord);
    outColor *= materialTerms(diffuse, vertexColor);
#ifdef TERRAIN_MAPS
    outColor.xyz += specularStrength * specularLight;
#endif
    outColor.xyz = applyFog(outColor.xyz, frag_in.viewPos);

#ifdef DEBUG_SHADOW
    outColor = debugShadow(outColor);
#endif

#ifdef GBUFFER
 #ifdef TERRAIN_MAPS
    outNormal = vec4(viewNormal, 1.0);
 #else
    outNormal = vec4(normalize(frag_in.viewNormal), 1.0);
 #endif
    outLinearDepth = -frag_in.viewPos.z;
    outMotion = vec2(0.0);
#ifdef GBUFFER_MATERIAL
    // Render::Pbr::terrainRoughness (materialmath.hpp) in the normal's w; reflectance 0.5, occlusion 1.
    outNormal.w = 0.95;
    outMaterial = vec4(0.0, 0.5, 1.0, 0.0);
#endif
#endif

#ifdef DEBUG_CHUNKS
    float b = 0.005;
    if (frag_in.texCoord.x < b || frag_in.texCoord.y < b || frag_in.texCoord.x > 1-b ||  frag_in.texCoord.y > 1-b)
        outColor.xyz = vec3(1,0,0);
    if (frag_in.localTexCoord.x < b || frag_in.localTexCoord.y < b || frag_in.localTexCoord.x > 1-b ||  frag_in.localTexCoord.y > 1-b)
        outColor.xyz = vec3(0,0,1);
#endif
}
