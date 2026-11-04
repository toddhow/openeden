// The Slang twin of lib/attributes.glsl (task 0072): the vertex attributes and their locations for the defines a vertex
// stage is compiled with. The locations follow the GLSL's chain (each present attribute takes the next location after
// the previous present one); change both together.
//
// One difference, on purpose: the GLSL declares the texture coordinates as an array sized by the specialization
// constant numUvSets (constant_id 0), which Slang cannot place (it needs a known length to assign locations). The twin
// declares only the first set at TEXCOORD_LOCATION. Stages that read only inTexCoord[0] (the sky's) draw the same:
// the pipeline may supply more sets than a shader reads. The interface checker accepts exactly this difference where a
// family's .interface-exceptions file lists it (task 0072, stage b).
#ifndef LIB_ATTRIBUTES_SLANG
#define LIB_ATTRIBUTES_SLANG

#include "lib/material/bindings.slang"
#include "lib/sets.slang"

#ifdef VERTEX
#define VERTEX_LOCATION 0
#else
#define VERTEX_LOCATION -1
#endif

#ifdef NORMAL
#define NORMAL_LOCATION (VERTEX_LOCATION + 1)
#else
#define NORMAL_LOCATION VERTEX_LOCATION
#endif

#ifdef COLOR
#define COLOR_LOCATION (NORMAL_LOCATION + 1)
#else
#define COLOR_LOCATION NORMAL_LOCATION
#endif

#ifdef SKIN
#define BONE_INDICES_LOCATION (COLOR_LOCATION + 1)
#define BONE_WEIGHTS_LOCATION (BONE_INDICES_LOCATION + 1)
[[vk::binding(BONE_BINDING, TEXTURE_SET)]] StructuredBuffer<float4x4> boneMatrices;
#else
#define BONE_WEIGHTS_LOCATION COLOR_LOCATION
#endif

#ifdef NORMAL_MAP
#define TANGENT_LOCATION (BONE_WEIGHTS_LOCATION + 1)
#else
#define TANGENT_LOCATION BONE_WEIGHTS_LOCATION
#endif

#ifdef INSTANCE
#define INSTANCE_OFFSET_LOCATION (TANGENT_LOCATION + 1)
#define INSTANCE_ROTATION_LOCATION (INSTANCE_OFFSET_LOCATION + 1)
#else
#define INSTANCE_ROTATION_LOCATION TANGENT_LOCATION
#endif

#ifdef TEXCOORD
#define TEXCOORD_LOCATION (INSTANCE_ROTATION_LOCATION + 1)
#endif

struct VertexAttributes
{
#ifdef VERTEX
    [[vk::location(VERTEX_LOCATION)]] float3 inVertex : A_POSITION;
#endif
#ifdef NORMAL
    [[vk::location(NORMAL_LOCATION)]] float3 inNormal : A_NORMAL;
#endif
#ifdef COLOR
    [[vk::location(COLOR_LOCATION)]] float4 inColor : A_COLOR;
#endif
#ifdef SKIN
    [[vk::location(BONE_INDICES_LOCATION)]] float4 inBoneIndices : A_BONEINDICES;
    [[vk::location(BONE_WEIGHTS_LOCATION)]] float4 inBoneWeights : A_BONEWEIGHTS;
#endif
#ifdef NORMAL_MAP
    [[vk::location(TANGENT_LOCATION)]] float4 inTangent : A_TANGENT;
#endif
#ifdef INSTANCE
    [[vk::location(INSTANCE_OFFSET_LOCATION)]] float4 inInstanceOffset : A_INSTANCEOFFSET;
    [[vk::location(INSTANCE_ROTATION_LOCATION)]] float3 inInstanceRotation : A_INSTANCEROTATION;
#endif
#ifdef TEXCOORD
    [[vk::location(TEXCOORD_LOCATION)]] float2 inTexCoord0 : A_TEXCOORD0;
#endif
};

#endif
