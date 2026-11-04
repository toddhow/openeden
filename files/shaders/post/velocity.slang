// The velocity pass in Slang (task 0072, stage g): the twin of post/velocity.frag, the fragment stage of
// Render::Velocity (components/render/velocity.cpp). The CPU twin is Render::GBuffer::reprojectVelocity
// (gbuffermath.hpp): change them together. It must declare what the GLSL declares (checked by SlangPermutations); the
// matrix lies in memory as GLSL's mat4 (-matrix-layout-column-major) and mul(M, v) is GLSL's M * v.

[[vk::binding(0, 0)]] Sampler2D linearDepth;
[[vk::binding(1, 0)]] Sampler2D objectMotion; // skinned actors' own motion, from the scene pass

struct Push
{
    float4x4 toPrevClip;
    float4 projScale; // xy: the projection's [0][0] and [1][1]
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float2 texCoord = input.texCoord;
    float d = linearDepth.Sample(texCoord).r;
    if (!(d > 0.0))
        return float4(0.0);
    float2 velocity = objectMotion.Sample(texCoord).rg;
    float3 viewPos = float3((texCoord * 2.0 - 1.0) * d / push.projScale.xy, -d);
    float4 prevClip = mul(push.toPrevClip, float4(viewPos, 1.0));
    if (prevClip.w > 0.0)
        velocity += texCoord - (prevClip.xy / prevClip.w * 0.5 + 0.5);
    return float4(velocity, 0.0, 0.0);
}
