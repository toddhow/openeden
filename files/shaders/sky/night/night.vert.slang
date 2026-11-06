// The night sky (stars), vertex stage, in Slang (task 0072, stage b): the twin of sky/night/main.vert. It must declare
// what the GLSL declares (checked by SlangPermutations), apart from the texture coordinate array of
// lib/attributes.slang (see sky/night/main.vert.interface-exceptions).
import lib.attributes;
import lib.pushconstants;

// sky/night/inout.glsl: color is always declared (a block must not be empty), texCoord with TEXCOORD.
struct VertexOutput
{
    [[vk::location(0)]] float4 color : V_COLOR;
#ifdef TEXCOORD
    [[vk::location(1)]] float2 texCoord : V_TEXCOORD0;
#endif
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexAttributes input)
{
    VertexOutput output;
    output.position = mul(pc.projection, mul(pc.modelview, float4(input.inVertex, 1.0)));
#ifdef COLOR
    output.color = float4(1, 1, 1, float(int(input.inColor.x)));
#else
    // The GLSL leaves it unwritten; nothing reads it without COLOR.
    output.color = float4(0.0);
#endif
#ifdef TEXCOORD
    output.texCoord = input.inTexCoord0;
#endif
    return output;
}
