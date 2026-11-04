// The fullscreen triangle in Slang (task 0072, stage g): the twin of post/fullscreen.vert, the vertex stage of the
// tonemap, bloom, velocity and ambient occlusion passes (components/render). Texture coordinates run 0 to 1 across
// the target in Vulkan's top-left origin. It must declare what the GLSL declares (checked by SlangPermutations); it has
// no defines.

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
    output.texCoord = position * 0.5 + 0.5;
    return output;
}
