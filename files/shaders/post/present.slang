// Post-processing's present pass in Slang (task 0072, stage g): the twins of post/present.vert (vertMain) and
// post/present.frag (fragMain), which draw a post-processing result, in OpenGL's origin, to the screen
// (apps/openmw/mwrender/postprocess.cpp, PostProcess::createPresent, with multisampling). No defines. They must declare
// what the GLSL declares (checked by SlangPermutations).

struct VertexOutput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndexU : SV_VulkanVertexID)
{
    int vertexIndex = int(vertexIndexU);
    float2 position = float2(float((vertexIndex << 1) & 2), float(vertexIndex & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.texCoord = float2(position.x, -position.y) * 0.5 + 0.5;
    return output;
}

[[vk::binding(0, 0)]] Sampler2D result;

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    return float4(result.Sample(input.texCoord).rgb, 1.0);
}
