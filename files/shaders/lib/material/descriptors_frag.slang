// The Slang twin of lib/material/descriptors_frag.glsl (task 0072): the textures and the material a fragment stage
// declares for the defines it is compiled with. Change both together.
#ifndef LIB_MATERIAL_DESCRIPTORS_FRAG_SLANG
#define LIB_MATERIAL_DESCRIPTORS_FRAG_SLANG

#include "lib/material/bindings.slang"
import lib.material.data;
#include "lib/sets.slang"

#ifdef DIFFUSE_MAP
[[vk::binding(DIFFUSE_UNIT, TEXTURE_SET)]] Sampler2D diffuseMap;
#endif
#ifdef DARK_MAP
[[vk::binding(DARK_UNIT, TEXTURE_SET)]] Sampler2D darkMap;
#endif
#ifdef DETAIL_MAP
[[vk::binding(DETAIL_UNIT, TEXTURE_SET)]] Sampler2D detailMap;
#endif
#ifdef DECAL_MAP
[[vk::binding(DECAL_UNIT, TEXTURE_SET)]] Sampler2D decalMap;
#endif
#ifdef GLOW_MAP
[[vk::binding(GLOW_UNIT, TEXTURE_SET)]] Sampler2D glowMap;
#endif
#ifdef BUMP_MAP
[[vk::binding(BUMP_UNIT, TEXTURE_SET)]] Sampler2D bumpMap;
#endif
#ifdef ENV_MAP
[[vk::binding(ENV_UNIT, TEXTURE_SET)]] Sampler2D envMap;
#endif
#ifdef NORMAL_MAP
[[vk::binding(NORMAL_UNIT, TEXTURE_SET)]] Sampler2D normalMap;
#endif
#ifdef SPECULAR_MAP
[[vk::binding(SPECULAR_UNIT, TEXTURE_SET)]] Sampler2D specularMap;
#endif
#ifdef MATERIAL_MAP
[[vk::binding(MATERIAL_MAP_UNIT, TEXTURE_SET)]] Sampler2D materialMap;
#endif
#ifdef HEIGHT_MAP
[[vk::binding(HEIGHT_UNIT, TEXTURE_SET)]] Sampler2D heightMap;
#endif

#ifdef MATERIAL
[[vk::binding(MATERIAL_BINDING, TEXTURE_SET)]] ConstantBuffer<Material> material;
#endif

#endif
