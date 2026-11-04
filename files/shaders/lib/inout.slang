// The Slang twin of lib/inout.glsl (task 0072): the varyings between the vertex and fragment stages of the object,
// water and terrain shaders, in the GLSL block's order (each member takes the next location, from 0). Put it first
// in the stage's input or output struct; the semantics are V_* so that a define such as COLOR cannot expand inside
// them. Change both together.
//
// The texture coordinates (TEXCOORD or PARTICLE): the GLSL declares them last, as an array whose length is the
// specialization constant numUvSets; a twin is built for one count, the set's UVSETS_<n> name (lib/uvsets.slang), and
// the pipeline chooses the twin of its count (task 0072, stage f). Without the name such a stage stops here rather than
// declare a different interface.
#ifndef LIB_INOUT_SLANG
#define LIB_INOUT_SLANG

#include "lib/uvsets.slang"

#if (defined(TEXCOORD) || defined(PARTICLE)) && !defined(NUM_UV_SETS)
#error lib/inout.slang: the texture coordinate arrays need the set's UVSETS_<n> name (task 0072, stage f)
#endif

struct CommonInout
{
#if defined(COLOR) || defined(PARTICLE)
    float4 color : V_COLOR;
#endif
    float3 viewPos : V_VIEWPOS;
#ifdef NORMAL
    float3 viewNormal : V_VIEWNORMAL;
    float2 envUV : V_ENVUV;
#endif
#ifdef SOFT_PARTICLE
    float softParticleSize : V_SOFTPARTICLESIZE;
#endif
#ifdef NORMAL_MAP
    float3 viewTangent : V_VIEWTANGENT;
    float3 viewBinormal : V_VIEWBINORMAL;
#endif
    float3 pointLightDiffuse : V_POINTLIGHTDIFFUSE;
#ifdef GBUFFER_MOTION
    float2 motion : V_MOTION;
#endif
#if defined(TEXCOORD) || defined(PARTICLE)
    // Last, as in the GLSL: NUM_UV_SETS locations.
    float2 texCoord[NUM_UV_SETS] : V_TEXCOORD;
#endif
};

#endif
