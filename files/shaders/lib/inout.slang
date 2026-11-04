// The Slang twin of lib/inout.glsl (task 0072): the varyings between the vertex and fragment stages of the object,
// water and terrain shaders, in the GLSL block's order (each member takes the next location, from 0). Put it first
// in the stage's input or output struct; the semantics are V_* so that a define such as COLOR cannot expand inside
// them. Change both together.
//
// The texture coordinates are not here yet: the GLSL declares them as an array sized by the specialization constant
// numUvSets, which Slang cannot place; the first family that needs them (the object shader, task 0072 stage f) decides
// how. A stage compiled with TEXCOORD or PARTICLE therefore stops here rather than declare a different interface.
#ifndef LIB_INOUT_SLANG
#define LIB_INOUT_SLANG

#if defined(TEXCOORD) || defined(PARTICLE)
#error lib/inout.slang: the texture coordinate array of lib/inout.glsl has no Slang twin yet (task 0072 stage f)
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
};

#endif
