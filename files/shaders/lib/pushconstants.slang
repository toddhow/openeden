// The Slang twin of lib/pushconstants.glsl (task 0072). With -matrix-layout-column-major the matrices lie in memory as
// GLSL's mat4 does, and mul(M, v) is GLSL's M * v.
#ifndef LIB_PUSHCONSTANTS_SLANG
#define LIB_PUSHCONSTANTS_SLANG

struct PushConstants
{
    float4x4 projection;
    float4x4 modelview;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

#endif
