// The Slang twin of lib/object/object.glsl (task 0072, stage f): the per-object uniform of the object set. Change both
// together.
#ifndef LIB_OBJECT_OBJECT_SLANG
#define LIB_OBJECT_OBJECT_SLANG

import lib.object.data;
#include "lib/sets.slang"

[[vk::binding(0, OBJECT_SET)]] ConstantBuffer<Object> object;

#endif
