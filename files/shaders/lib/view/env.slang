// The Slang twin of lib/view/env.glsl (task 0072, stage f): the scene's environment map array. Change both together.
#ifndef LIB_VIEW_ENV_SLANG
#define LIB_VIEW_ENV_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"

[[vk::binding(VIEW_ENV_BINDING, VIEW_SET)]] Sampler2DArray sceneEnv;

int envIndex(float time)
{
    return int(time * 16) % VIEW_ENV_COUNT;
}

#endif
