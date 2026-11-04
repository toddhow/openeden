#version 450
// The bloom chain's upsample (components/render/bloom.cpp): the 3x3 tent filter of the same article; the result is
// added to the image below by the blend state. Task 0072, stage g: it was a string in bloom.cpp; the Slang twin is
// post/bloom.slang.
layout(set=0, binding=0) uniform sampler2D source;
layout(push_constant) uniform Push { vec4 texel; } push; // xy: the source's texel size
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;
void main()
{
    vec2 t = push.texel.xy;
    vec3 a = texture(source, texCoord + vec2(-t.x, t.y)).rgb;
    vec3 b = texture(source, texCoord + vec2(0, t.y)).rgb;
    vec3 c = texture(source, texCoord + vec2(t.x, t.y)).rgb;
    vec3 d = texture(source, texCoord + vec2(-t.x, 0)).rgb;
    vec3 e = texture(source, texCoord).rgb;
    vec3 f = texture(source, texCoord + vec2(t.x, 0)).rgb;
    vec3 g = texture(source, texCoord + vec2(-t.x, -t.y)).rgb;
    vec3 h = texture(source, texCoord + vec2(0, -t.y)).rgb;
    vec3 i = texture(source, texCoord + vec2(t.x, -t.y)).rgb;
    vec3 result = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    outColor = vec4(result, 1.0);
}
