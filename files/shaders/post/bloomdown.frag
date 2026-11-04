#version 450
// The bloom chain's downsample (components/render/bloom.cpp, Render::Bloom): the 13 taps of Jimenez's downsample, in
// the source's texels around the pixel: the centre and the corners of two overlapping 2x2 boxes at each of four offsets,
// weighted so that the whole is a 4x4 box filter with a soft edge (for even sizes; an odd size drifts by less than a
// texel). Each filtered sample is capped at kMaxInput and a NaN sample counts as black, so that a very bright texel (a
// sun disc, a light's centre) cannot flash from frame to frame and one NaN cannot spread through the chain. kMaxInput
// is 1 / 1000 of the largest value the float target holds (Render::Hdr::maxTargetValue), enough for any light and far
// above the display's white. Task 0072, stage g: it was a string in bloom.cpp; the Slang twin is post/bloom.slang.
layout(set=0, binding=0) uniform sampler2D source;
layout(push_constant) uniform Push { vec4 texel; } push; // xy: the source's texel size
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;
const float kMaxInput = 64.0; // about 1 / 1000 of the float target's largest value
vec3 tap(vec2 offset)
{
    vec3 v = texture(source, texCoord + offset * push.texel.xy).rgb;
    if (any(isnan(v)))
        return vec3(0.0);
    return min(v, vec3(kMaxInput));
}
void main()
{
    vec3 a = tap(vec2(-2, 2)), b = tap(vec2(0, 2)), c = tap(vec2(2, 2));
    vec3 d = tap(vec2(-2, 0)), e = tap(vec2(0, 0)), f = tap(vec2(2, 0));
    vec3 g = tap(vec2(-2, -2)), h = tap(vec2(0, -2)), i = tap(vec2(2, -2));
    vec3 j = tap(vec2(-1, 1)), k = tap(vec2(1, 1)), l = tap(vec2(-1, -1)), m = tap(vec2(1, -1));
    vec3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    outColor = vec4(result, 1.0);
}
