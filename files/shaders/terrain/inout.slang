// The Slang twin of terrain/inout.glsl (task 0072): the terrain's varyings, in the GLSL block's order (each member takes
// the next location, from 0) under the same #ifdefs. Semantics are V_* (files/shaders/CLAUDE.md). Change both together.
#ifndef TERRAIN_INOUT_SLANG
#define TERRAIN_INOUT_SLANG

struct TerrainInout
{
    float2 texCoord : V_TEXCOORD;
    float3 viewPos : V_VIEWPOS;
    float3 viewNormal : V_VIEWNORMAL;
    float3 pointLightDiffuse : V_POINTLIGHTDIFFUSE;
#ifdef DEBUG_CHUNKS
    float2 localTexCoord : V_LOCALTEXCOORD;
#endif
#ifdef TERRAIN_MAPS
    float3 viewTangent : V_VIEWTANGENT;
    float3 viewBinormal : V_VIEWBINORMAL;
#endif
    // Task 0088: see inout.glsl.
    nointerpolation float chunkFade : V_CHUNKFADE;
};

#endif
