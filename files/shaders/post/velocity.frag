#version 450
// The velocity pass (components/render/velocity.cpp, Render::Velocity). The CPU twin is
// Render::GBuffer::reprojectVelocity (gbuffermath.hpp): change both together. The linear depth is the distance in front
// of the camera (view space looks down -Z); projScale holds the projection's x and y scales, which carry the sign of the
// target's y axis. A pixel with no opaque surface (depth 0), or one that was behind the previous camera, has no
// velocity. Task 0072, stage g: it was a string in velocity.cpp; the Slang twin is post/velocity.slang.
layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D objectMotion; // skinned actors' own motion, from the scene pass
layout(push_constant) uniform Push
{
    mat4 toPrevClip;
    vec4 projScale; // xy: the projection's [0][0] and [1][1]
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outVelocity;
void main()
{
    float d = texture(linearDepth, texCoord).r;
    outVelocity = vec4(0.0);
    if (!(d > 0.0))
        return;
    vec2 velocity = texture(objectMotion, texCoord).rg;
    vec3 viewPos = vec3((texCoord * 2.0 - 1.0) * d / push.projScale.xy, -d);
    vec4 prevClip = push.toPrevClip * vec4(viewPos, 1.0);
    if (prevClip.w > 0.0)
        velocity += texCoord - (prevClip.xy / prevClip.w * 0.5 + 0.5);
    outVelocity = vec4(velocity, 0.0, 0.0);
}
