// The moon's vertex stage, in Slang (task 0072, stage b): the twin of sky/moon/main.vert (no defines). It must declare
// what the GLSL declares (checked by SlangPermutations). One file per stage, as the GLSL has: Slang checks every entry
// point of a file, and the fragment stage's textures exist only with its defines.
import lib.pushconstants;

struct VertexOutput
{
    // lib/inout_texcoord.glsl in the Outputs block, location 0
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    output.texCoord = float2((vertexIndex << 1) & 2, vertexIndex & 2) * 0.5;
    float3 vertex = float3(output.texCoord - 0.5, 0.0);
    output.position = mul(pc.projection, mul(pc.modelview, float4(vertex, 1.0)));
    return output;
}
