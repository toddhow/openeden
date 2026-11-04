// The bloom chain in Slang (task 0072, stage g): the twins of post/bloomdown.frag (downMain) and post/bloomup.frag
// (upMain), the fragment stages of Render::Bloom (components/render/bloom.cpp). Both have no defines and read the same
// binding and push constant, so they share a file. They must declare what the GLSL declares (checked by
// SlangPermutations).

[[vk::binding(0, 0)]] Sampler2D source;

struct Push
{
    float4 texel; // xy: the source's texel size
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

// The 13 taps of Jimenez's downsample, in the source's texels around the pixel: the centre and the corners of two
// overlapping 2x2 boxes at each of four offsets, weighted so that the whole is a 4x4 box filter with a soft edge. Each
// filtered sample is capped at kMaxInput and a NaN sample counts as black (see post/bloomdown.frag).
static const float kMaxInput = 64.0; // about 1 / 1000 of the float target's largest value

float3 tap(float2 texCoord, float2 offset)
{
    float3 v = source.Sample(texCoord + offset * push.texel.xy).rgb;
    if (any(isnan(v)))
        return float3(0.0);
    return min(v, float3(kMaxInput));
}

[shader("fragment")]
float4 downMain(FragmentInput input) : SV_Target0
{
    float2 uv = input.texCoord;
    float3 a = tap(uv, float2(-2, 2)), b = tap(uv, float2(0, 2)), c = tap(uv, float2(2, 2));
    float3 d = tap(uv, float2(-2, 0)), e = tap(uv, float2(0, 0)), f = tap(uv, float2(2, 0));
    float3 g = tap(uv, float2(-2, -2)), h = tap(uv, float2(0, -2)), i = tap(uv, float2(2, -2));
    float3 j = tap(uv, float2(-1, 1)), k = tap(uv, float2(1, 1)), l = tap(uv, float2(-1, -1)), m = tap(uv, float2(1, -1));
    float3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    return float4(result, 1.0);
}

// The 3x3 tent filter of the same article; the result is added to the image below by the blend state.
[shader("fragment")]
float4 upMain(FragmentInput input) : SV_Target0
{
    float2 texCoord = input.texCoord;
    float2 t = push.texel.xy;
    float3 a = source.Sample(texCoord + float2(-t.x, t.y)).rgb;
    float3 b = source.Sample(texCoord + float2(0, t.y)).rgb;
    float3 c = source.Sample(texCoord + float2(t.x, t.y)).rgb;
    float3 d = source.Sample(texCoord + float2(-t.x, 0)).rgb;
    float3 e = source.Sample(texCoord).rgb;
    float3 f = source.Sample(texCoord + float2(t.x, 0)).rgb;
    float3 g = source.Sample(texCoord + float2(-t.x, -t.y)).rgb;
    float3 h = source.Sample(texCoord + float2(0, -t.y)).rgb;
    float3 i = source.Sample(texCoord + float2(t.x, -t.y)).rgb;
    float3 result = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    return float4(result, 1.0);
}
