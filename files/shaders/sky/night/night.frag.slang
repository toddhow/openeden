// The night sky (stars), fragment stage, in Slang (task 0072, stage b): the twin of sky/night/main.frag. It must
// declare what the GLSL declares (checked by SlangPermutations).
#include "lib/material/descriptors_frag.slang"
import lib.view.linear;
import lib.view.scene;

struct FragmentInput
{
    [[vk::location(0)]] float4 color : V_COLOR;
#ifdef TEXCOORD
    [[vk::location(1)]] float2 texCoord : V_TEXCOORD0;
#endif
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
#ifdef DIFFUSE_MAP
    float4 outColor = diffuseMap.Sample(input.texCoord);
#else
    float4 outColor = float4(1.0, 1.0, 1.0, 1.0);
#endif
    outColor.xyz = toLinearIfView(outColor.xyz);
    outColor.a *= material.diffuse.a * scene.skyColor.a;
#ifdef COLOR
    outColor.a *= input.color.a;
#endif
    return outColor;
}
