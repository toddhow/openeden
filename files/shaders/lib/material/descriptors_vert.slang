// The Slang twin of lib/material/descriptors_vert.glsl (task 0072): the texture matrix a vertex stage declares with
// TEXMAT. Change both together.
#ifndef LIB_MATERIAL_DESCRIPTORS_VERT_SLANG
#define LIB_MATERIAL_DESCRIPTORS_VERT_SLANG

#include "lib/material/bindings.slang"
#include "lib/sets.slang"

#if defined(TEXMAT) || defined(LOD_SWAY)
struct TexMat
{
    float4x4 texmat;
};
[[vk::binding(TEXMAT_BINDING, TEXTURE_SET)]] ConstantBuffer<TexMat> texMat;
#endif

#endif
