#pragma import_defines (TERRAIN_MAPS, TERRAIN_PBR, GBUFFER, GBUFFER_MATERIAL, GBUFFER_AMBIENT, AO_TERRAIN, GI_SKY, WETNESS, WETNESS_PUDDLES, WETNESS_DEBUG, COVER, COVER_DEBUG, PARALLAX_POM, PARALLAX_POM_LOW, PARALLAX_POM_HIGH, POINT_SHADOW)
const int colorMode = 2;

#include "lib/view/scene.glsl"
#include "lib/view/linear.glsl"
#include "lib/view/fog.glsl"
#include "lib/view/shadow.glsl"
#include "lib/light/directional.glsl"
#include "lib/material/terms.glsl"
#ifdef GI_SKY
#include "lib/view/skyvis.glsl"
#endif
#if defined(WETNESS) || defined(TERRAIN_PBR)
// OpenEden (task 0037): rain wetness; its specular lobe is the material model's. Task 0044: so is the terrain's with
// its packed material maps.
#include "lib/material/pbr.glsl"
#endif
#ifdef WETNESS
#include "lib/material/wetness.glsl"
#ifdef WETNESS_PUDDLES
// OpenEden (task 0095): puddles from the baked puddle map (the terrain only; objects keep the noise puddles).
#include "lib/material/puddlemap.glsl"
#endif
#endif
#ifdef COVER
// OpenEden (task 0039): snow and ash cover.
#include "lib/material/cover.glsl"
#endif
#include "bindings.glsl"
#include "data.glsl"
#include "terrain.settings"
#include "lib/view/dither.glsl"

layout(set=TEXTURE_SET, binding=BLENDMAP_BINDING) uniform sampler2D blendMap;

// OpenEden (task 0087): the batch's uniform, as terrain/main.vert declares it; the fragment stage reads only the fade.
layout(std140, set=TEXTURE_SET, binding=BATCH_DATA_BINDING) uniform Data {
    Batch batch;
};

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
#ifdef POINT_SHADOW
#include "lib/light/pointshadow.glsl"
#endif
#include "lib/light/pixellighting.glsl"
#ifdef PARALLAX_POM
// OpenEden (task 0045): the layers' `_p` height files (an array) and per layer its array index (below zero: none) in x and
// its depth in texture coordinates, `parallax scale` included, in y. One march a pixel, on the layer with the largest
// blend weight; pomLayer is the array layer the height function reads.
layout(set=TEXTURE_SET, binding=HEIGHT_LAYER_BINDING) uniform sampler2DArray heightLayers;
layout(std430, set=TEXTURE_SET, binding=LAYER_HEIGHTS_BINDING) readonly buffer LayerHeights {
    vec4 layerHeights[];
};
float pomLayer = 0.0;
float pomHeight(vec2 uv, float lod)
{
    return textureLod(heightLayers, vec3(uv, pomLayer), lod).r;
}
vec2 pomTextureSize()
{
    return vec2(textureSize(heightLayers, 0).xy);
}
#endif
#include "lib/material/parallax.glsl"
#ifdef TERRAIN_PBR
// Task 0044: bits 18 (the map has the `_rmaos` layout) and 20 to 31 (its index, 0xfff for none) of the same flags
// (Terrain::packLayerFlags); the maps themselves; and per layer a roughness scale (0: none) and a reflectance (< 0:
// the default 0.5), which the tier 2 files give.
const uint RMAOS_FLAG = 1u << 18;
const uint MATERIAL_SHIFT = 20u;
const uint NO_MATERIAL_MAP = 0xfffu;
layout(set=TEXTURE_SET, binding=MATERIAL_LAYER_BINDING) uniform sampler2DArray materialLayers;
layout(std430, set=TEXTURE_SET, binding=LAYER_SCALES_BINDING) readonly buffer LayerScales {
    vec4 layerScales[];
};
#include "lib/light/pbrlighting.glsl"
#endif
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
#ifdef GBUFFER_AMBIENT
// OpenEden (task 0029): the ambient part of the colour; the terrain writes zero unless AO_TERRAIN (task 0048).
layout(location=5) out vec4 outAmbient;
#endif
#endif

void main()
{
    ivec2 blendSize = textureSize(blendMap, 0);
    float blendTexSize = float(blendSize.x);
    vec4 blendCoeffs;
    ivec4 textures;
    if (blendSize.y > blendSize.x)
    {
        // OpenEden (task 0149): a splat blend map (components/neutralterrain/splat.hpp; Splat::lookupFor and tilesAcross
        // are this maths in C++). It is not square: k x k quadrant slots of s x s texels hold the four layers' weights
        // (one texel a vertex, blended bilinearly inside one slot), and k rows of k texels below hold the four layer indices of each
        // quadrant. The ground texture repeats 16 times a cell, as with the corner scheme.
        int splatK = blendSize.y - blendSize.x;
        int splatS = blendSize.x / splatK;
        vec2 slotPos = clamp(frag_in.texCoord, vec2(0.0), vec2(1.0)) * float(splatK);
        ivec2 slot = min(ivec2(slotPos), ivec2(splatK - 1));
        vec2 vertexPos = (slotPos - vec2(slot)) * float(splatS - 1);
        // Bilinear by hand: no dependence on the sampler's state, and never across two slots.
        ivec2 vertex0 = min(ivec2(vertexPos), ivec2(splatS - 1));
        ivec2 vertex1 = min(vertex0 + 1, ivec2(splatS - 1));
        vec2 vertexFrac = vertexPos - vec2(vertex0);
        ivec2 slotOrigin = slot * splatS;
        blendCoeffs = mix(
            mix(texelFetch(blendMap, slotOrigin + vertex0, 0), texelFetch(blendMap, slotOrigin + ivec2(vertex1.x, vertex0.y), 0), vertexFrac.x),
            mix(texelFetch(blendMap, slotOrigin + ivec2(vertex0.x, vertex1.y), 0), texelFetch(blendMap, slotOrigin + vertex1, 0), vertexFrac.x),
            vertexFrac.y);
        textures = ivec4(texelFetch(blendMap, ivec2(slot.x, blendSize.x + slot.y), 0) * 255.0 + 0.5);
        blendTexSize = float(splatS - 1) * float(splatK) * 0.5;
    }
    else
    {
        vec2 blendCoord = frag_in.texCoord * blendTexSize;
        ivec2 blendTexel = ivec2(blendCoord);
        //blendTexel = min(blendTexel, int(blendTexSize-1));
        vec2 blendPos = blendCoord - blendTexel;
        blendPos = clamp((blendPos-0.5)*2+0.5, 0.0, 1.0);
        blendCoeffs = vec4(blendPos, 1-blendPos);
        blendCoeffs = blendCoeffs.zxzx * blendCoeffs.wwyy;
        textures = ivec4(texelFetch(blendMap, blendTexel, 0) * 255);
    }
    vec2 diffuseTexCoord = frag_in.texCoord * blendTexSize;

    outColor = vec4(0,0,0,1);
#ifdef COVER
    // Task 0039: snow or ash on this point of the ground (the colour changes after the layers are blended).
    CoverSurface coverS = coverSurfaceAt(
        (scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * normalize(frag_in.viewNormal));
#endif
#ifdef TERRAIN_MAPS
    // The four layers' colours, tangent-space normals and specular strengths are blended, then lit once per pixel
    // (OpenMW drew one pass per layer and blended the lit results; compatibility/terrain.frag).
    mat3 normalToView = mat3(frag_in.viewTangent, frag_in.viewBinormal, frag_in.viewNormal);
    vec3 eyeDir = transpose(normalToView) * normalize(-frag_in.viewPos);
    vec3 blendedNormal = vec3(0);
    float specularStrength = 0.0;
#ifdef PARALLAX_POM
    // Task 0045: the march on the strongest layers that have a height file; every such layer then reads at the shifted
    // coordinates. The derivatives are taken before any branch.
    vec2 pomDx = dFdx(diffuseTexCoord);
    vec2 pomDy = dFdy(diffuseTexCoord);
    vec2 pomShift = vec2(0.0);
    bool pomActive = false;
    {
        // Only a layer with a `_p` file has relief, and a layer without one is never shifted by its neighbours' height
        // maps. The march follows the two strongest of those, mixed by their blend weights, so that the shift does not
        // jump where the strongest layer changes; it fades out on steep faces (the texture is projected from above and
        // already stretched there) and with distance.
        int first = -1;
        int second = -1;
        for (int i=0; i<4; ++i)
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
        float up = (mat3(scene.invView) * normalize(frag_in.viewNormal)).z;
        float pomFade = pomDistanceFade(length(frag_in.viewPos)) * pomSlopeFade(up);
        float worldPerUv = pomWorldPerUvPlanar(mat3(scene.invView) * frag_in.viewPos, pomDx, pomDy);
        if (first >= 0 && pomFade > 0.0)
        {
            float weightSecond = second >= 0 ? blendCoeffs[second] : 0.0;
            float weightFirst = blendCoeffs[first];
            float mixSecond = weightSecond / max(weightFirst + weightSecond, 1e-4);
            vec4 heightInfo = layerHeights[textures[first]];
            pomLayer = heightInfo.x;
            pomShift = pomOffset(diffuseTexCoord, eyeDir, pomUvDepth(heightInfo.y, worldPerUv) * pomFade,
                1.0 - textureLod(heightLayers, vec3(0.5, 0.5, pomLayer), 16.0).r, pomDx, pomDy);
            if (mixSecond > 0.02)
            {
                heightInfo = layerHeights[textures[second]];
                pomLayer = heightInfo.x;
                vec2 shiftSecond = pomOffset(diffuseTexCoord, eyeDir, pomUvDepth(heightInfo.y, worldPerUv) * pomFade,
                    1.0 - textureLod(heightLayers, vec3(0.5, 0.5, pomLayer), 16.0).r, pomDx, pomDy);
                pomShift = mix(pomShift, shiftSecond, mixSecond);
            }
            pomActive = true;
        }
    }
#endif
#ifdef TERRAIN_PBR
    // Task 0044: the layers' material values, blended like their colours. A layer with no map has the tier 1 terrain
    // values (Render::Pbr::terrainRoughness, no metal, F0 0.04, no occlusion).
    PbrSurface terrainS;
    terrainS.roughness = 0.0;
    terrainS.metalness = 0.0;
    terrainS.reflectance = 0.0;
    terrainS.occlusion = 0.0;
    terrainS.subsurface = 0.0;
#endif
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
#ifdef PARALLAX_POM
        if (pomActive && layerHeights[textures[i]].x >= 0.0)
            layerCoord = diffuseTexCoord + pomShift;
#endif
        vec4 diffuseTex = texture(diffuseMap, vec3(layerCoord, textures[i]));
        outColor.xyz += diffuseTex.xyz * blendCoeffs[i];
        specularStrength += ((flags & SPECULAR_FLAG) != 0u ? diffuseTex.a : 0.0) * blendCoeffs[i];
        vec3 mapNormal = texture(normalLayers, vec3(layerCoord, normalIndex)).xyz * 2.0 - 1.0;
        blendedNormal += (hasNormalMap ? mapNormal : vec3(0.0, 0.0, 1.0)) * blendCoeffs[i];
#ifdef TERRAIN_PBR
        uint materialIndex = (flags >> MATERIAL_SHIFT) & NO_MATERIAL_MAP;
        vec4 layerMap = texture(materialLayers, vec3(layerCoord, materialIndex));
        vec4 layerScale = layerScales[textures[i]];
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
    // Task 0039: a layer of snow or ash is rough and not a metal.
    terrainS.roughness = mix(terrainS.roughness, coverRoughnessOf(coverS.kind), coverS.mask);
    terrainS.metalness *= 1.0 - coverS.mask;
#endif
    vec3 viewNormal = normalize(normalToView * blendedNormal);
    // The shadow bias keeps the geometry normal, as OpenMW's shadows did.
    float sunShadow = shadow(frag_in.viewPos, frag_in.viewNormal);
#ifdef POINT_SHADOW
    pointShadowNormal = normalize(frag_in.viewNormal);
#endif
    // terrain.frag's specular: shininess 128 and the layer's diffuse alpha as a grey specular colour.
    const float shininess = 128.0;
    vec3 pointDiffuse, pointSpecular;
#ifdef TERRAIN_PBR
    // Task 0044: the material model, over the same lights (the encoded colours are converted below, so the metal tint
    // uses the encoded base colour converted here).
    vec3 pbrBase = toLinearIfView(outColor.xyz);
    vec3 pbrF0 = pbrSpecularF0(pbrBase, terrainS.metalness, terrainS.reflectance);
    pbrPointLights(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        terrainS.roughness, pbrF0, pointDiffuse, pointSpecular);
    vec3 diffuse = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * pbrSpecularLobe(viewNormal, normalize(frag_in.viewPos), scene.lightViewPos.xyz, terrainS.roughness, pbrF0)
        * sunShadow;
#else
    pointLightsPerPixel(viewPosToScreenCoord(frag_in.viewPos, scene.projection), viewNormal, frag_in.viewPos,
        shininess, pointDiffuse, pointSpecular);
    vec3 diffuse = pointDiffuse + directionalDiffuse(viewNormal) * sunShadow;
    vec3 specularLight = pointSpecular + scene.lightSpecular.xyz
        * specularIntensity(viewNormal, normalize(frag_in.viewPos), shininess, scene.lightViewPos.xyz) * sunShadow;
#endif
#else
    for (int i=0; i<4; ++i)
        outColor.xyz += texture(diffuseMap, vec3(diffuseTexCoord, textures[i])).xyz * blendCoeffs[i];

    vec3 diffuse = frag_in.pointLightDiffuse + directionalDiffuse(normalize(frag_in.viewNormal)) * shadow(frag_in.viewPos, frag_in.viewNormal);
#endif

    // OpenEden (task 0025): the layers are blended on encoded values and converted once (lib/view/linear.glsl).
    outColor.xyz = toLinearIfView(outColor.xyz);
#ifdef COVER
    outColor.xyz = mix(outColor.xyz, coverColourOf(coverS.kind), coverS.mask);
#endif
#ifdef WETNESS
    // Task 0037: rain wetness (the terrain takes the darker and glossier terms and puddles; roughness is the material
    // model's terrain value, Render::Pbr::terrainRoughness).
#ifdef TERRAIN_PBR
    float wetTerrainRoughness = terrainS.roughness;
#else
    const float wetTerrainRoughness = 0.95;
#endif
    vec3 wetWorldPos = (scene.invView * vec4(frag_in.viewPos, 1.0)).xyz;
    WetSurface wetS = wetSurfaceAt(wetWorldPos, mat3(scene.invView) * normalize(frag_in.viewNormal));
    float wetPuddleDark = wetPuddleDarkening;
    float wetRippleGate = 1.0;
#ifdef WETNESS_PUDDLES
    // Task 0095: where the baked puddle map covers the pixel it decides the puddle (its depth, edge and darkening);
    // anywhere else the noise puddles of wetSurfaceAt stand.
    PuddleShade wetPm = puddleShadeAt(
        wetWorldPos, wetS.wet, (mat3(scene.invView) * normalize(frag_in.viewNormal)).z, false);
    if (wetPm.covered)
    {
        wetS.puddle = wetS.wet > 0.0 ? wetPm.coverage : 0.0;
        wetPuddleDark = wetPuddleDarkening * puddleDepthTint(wetPm.depth);
        wetRippleGate = wetPm.rippleGate;
    }
#endif
    if (wetS.wet > 0.0)
        outColor.xyz *= wetDarkening(wetS.wet, wetTerrainRoughness) * mix(1.0, wetPuddleDark, wetS.puddle);
#endif

    vec2 colorTexSize = textureSize(colorMap, 0);
    vec2 colorCoord = (frag_in.texCoord - 0.5) * colorTexSize / (colorTexSize+1) + 0.5;
    vec4 vertexColor = texture(colorMap, colorCoord);
#ifdef COVER
    // Task 0039: the land's colour map tints the ground green and brown; snow and ash keep its shading and lose that hue.
    vertexColor.xyz = mix(vertexColor.xyz, vec3(dot(vertexColor.xyz, vec3(1.0 / 3.0))), coverS.mask);
#endif
#ifdef AO_TERRAIN
    // Task 0048: the ambient part of this pixel's colour, for the ambient occlusion pass (default/main.frag's
    // ambientPart): the texture colour just before the lighting multiply (wetness and cover included) times the ambient
    // light, colour term and scale the lighting below uses. It is set in each lighting branch.
    vec3 terrainAmbientPart = vec3(0.0);
#endif
#ifdef TERRAIN_PBR
    {
        // Task 0044: metals have no diffuse, and the maps' occlusion darkens the ambient light (the AO pass cannot do
        // it: the terrain writes no ambient part unless AO_TERRAIN, task 0048, and the pass then removes a
        // share of what is left). With the sky-visibility field it scales that field's result.
        vec3 terrainAmbient = scene.ambient.xyz;
 #ifdef GI_SKY
        terrainAmbient = skyVisAmbient((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz,
            mat3(scene.invView) * normalize(frag_in.viewNormal), terrainAmbient);
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
    // Task 0036: the exterior sky-visibility field, with the terrain's geometry normal.
    vec3 terrainSkyAmbient = skyVisAmbient((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz,
        mat3(scene.invView) * normalize(frag_in.viewNormal), scene.ambient.xyz);
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
        vec3 wetN = normalize(frag_in.viewNormal);
        if (wetS.puddle > 0.0)
        {
            vec3 wetPN = wetPuddleNormal(wetWorldPos);
            // Task 0095: the rain's ripples only show where the water is deep enough.
            if (wetRippleGate < 1.0)
                wetPN = normalize(mix(vec3(0.0, 0.0, 1.0), wetPN, wetRippleGate));
            wetN = normalize(mix(wetN, transpose(mat3(scene.invView)) * wetPN, wetS.puddle));
        }
        float wetRough = mix(wetRoughnessOf(wetTerrainRoughness, wetS.wet), wetPuddleRoughness, wetS.puddle);
        float wetShadow = shadow(frag_in.viewPos, frag_in.viewNormal);
        outColor.xyz += scene.lightSpecular.xyz
            * pbrSpecularLobe(wetN, normalize(frag_in.viewPos), scene.lightViewPos.xyz, wetRough, vec3(0.04)) * wetShadow
            * wetS.wet;
        if (wetS.puddle > 0.0)
        {
            vec3 wetEye = mat3(scene.invView) * normalize(frag_in.viewPos);
            vec3 wetNW = mat3(scene.invView) * wetN;
            vec3 wetSky = wetSkyReflection(wetWorldPos, reflect(wetEye, wetNW), wetPuddleRoughness);
            outColor.xyz += wetSky * (wetFresnel(dot(wetNW, -wetEye)) * wetPuddleReflection * wetS.puddle);
        }
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
    outColor.xyz = applyFog(outColor.xyz, frag_in.viewPos);
#ifdef AO_TERRAIN
    // The fog's share of the colour, as default/main.frag does it for objects.
    terrainAmbientPart *= applyFog(vec3(1.0), frag_in.viewPos) - applyFog(vec3(0.0), frag_in.viewPos);
#endif

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
#ifdef TERRAIN_PBR
    // Task 0044: the blended layer values.
    outNormal.w = terrainS.roughness;
    outMaterial = vec4(terrainS.metalness, terrainS.reflectance, terrainS.occlusion, 0.0);
#else
    outNormal.w = 0.95;
    outMaterial = vec4(0.0, 0.5, 1.0, 0.0);
#endif
#endif
#ifdef GBUFFER_AMBIENT
 #ifdef AO_TERRAIN
    outAmbient = vec4(terrainAmbientPart, 1.0);
 #else
    outAmbient = vec4(0.0);
 #endif
#endif
#endif

#ifdef DEBUG_CHUNKS
    float b = 0.005;
    if (frag_in.texCoord.x < b || frag_in.texCoord.y < b || frag_in.texCoord.x > 1-b ||  frag_in.texCoord.y > 1-b)
        outColor.xyz = vec3(1,0,0);
    if (frag_in.localTexCoord.x < b || frag_in.localTexCoord.y < b || frag_in.localTexCoord.x > 1-b ||  frag_in.localTexCoord.y > 1-b)
        outColor.xyz = vec3(0,0,1);
#endif
    // OpenEden (task 0087): a world LOD tile fading in or out drops pixels by the ordered dither of default/main.frag's
    // LOD_FADE, here so that the colour, the G-buffer and the shadow cascades drop the same ones. Last, so that no
    // derivative above (the layers, the parallax march) is taken in a quad with a dropped pixel. A fade of 1 drops none.
    // Task 0088: a live chunk fading in or out multiplies its own share (from the draw) into the batch's.
    // The fade code (below 0: the complement of the incoming draw's dither, so that the two never draw one pixel
    // twice and never leave one out). A chunk's own code wins; a tile's is the batch's.
    float code = frag_in.chunkFade < 1.0 ? frag_in.chunkFade : batch.fade;
    if (code < 0.0)
    {
        if (dither8(int(gl_FragCoord.x) & 7, int(gl_FragCoord.y) & 7, -1.0 - code) != 0.0)
            discard;
    }
    else if (code < 1.0 && dither8(int(gl_FragCoord.x) & 7, int(gl_FragCoord.y) & 7, code) == 0.0)
        discard;
}
