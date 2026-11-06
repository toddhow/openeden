// The sun, in Slang: the twin of sky/sun/main.vert and main.frag. First ported by hand in task 0023 (F1b) as the proof
// of the pipeline, with its own copies of the GLSL declarations; since task 0072 (stage b) it uses the shared includes
// and is built as a permutation family like the rest of the sky. It must declare what the GLSL declares (checked by
// SlangPermutations, and by the task 0023 test SlangSunTest against the single files still built from this source).
// The sun is drawn with DIFFUSE_MAP and MATERIAL (MWRender weather.cpp, CelestialBody::pipelineOptions); the single
// files are compiled without -D, so the two are defined here when missing. Unused declarations are dropped from the
// vertex stage.
#ifndef DIFFUSE_MAP
#define DIFFUSE_MAP
#endif
#ifndef MATERIAL
#define MATERIAL
#endif
#include "lib/material/bindings.slang"
#include "lib/material/descriptors_frag.slang"
#include "lib/pushconstants.slang"
import lib.view.linear;
import lib.view.scene;

struct VertexOutput
{
    // lib/inout_texcoord.glsl, location 0
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    output.texCoord = float2((vertexIndex << 1) & 2, vertexIndex & 2) * 0.5;
    float3 vertex = float3(output.texCoord - 0.5, 0.0);
    float4 position = mul(pc.projection, mul(pc.modelview, float4(vertex, 1.0)));
    // vsgopenmw-reverse-depth: farDepth + epsilon, as in the GLSL
    position.z = 0.001;
    output.position = position;
    return output;
}

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float4 outColor = diffuseMap.Sample(input.texCoord);
    outColor.xyz = toLinearIfView(outColor.xyz * material.emissive.xyz);
    outColor.a *= material.diffuse.a * scene.skyColor.a;
    if (outColor.a == 0)
        discard;
    return outColor;
}
