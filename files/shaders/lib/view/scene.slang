// The Slang twin of lib/view/scene.glsl (task 0072): the scene uniform of the view set.
#ifndef LIB_VIEW_SCENE_SLANG
#define LIB_VIEW_SCENE_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"
#include "lib/view/data.slang"

[[vk::binding(VIEW_SCENE_BINDING, VIEW_SET)]] ConstantBuffer<Scene> scene;

#endif
