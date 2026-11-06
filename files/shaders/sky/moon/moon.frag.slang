// The moon's fragment stage, in Slang (task 0072, stage b): the twin of sky/moon/main.frag, drawn with DIFFUSE_MAP,
// GLOW_MAP and MATERIAL (MWRender weather.cpp, the moon's pipeline options). It must declare what the GLSL declares
// (checked by SlangPermutations).
#include "lib/material/descriptors_frag.slang"
import lib.view.linear;
import lib.view.scene;

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float4 phase = glowMap.Sample(input.texCoord);
    float4 mask = diffuseMap.Sample(input.texCoord);
    float4 blendedLayer = phase * material.emissive;
    // OpenEden (task 0025): the sum is of encoded colours, as it was chosen, and converted.
    return float4(toLinearIfView(blendedLayer.xyz + toEncodedIfView(scene.skyColor.xyz)),
        scene.skyColor.a * material.diffuse.a * mask.a);
}
