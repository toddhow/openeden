#version 450
// The ambient occlusion pass (components/render/ambientocclusion.cpp, Render::AmbientOcclusion). The CPU twins are
// Render::Ao::gtaoSliceVisibility and horizonCosine (aomath.hpp): change both together. View space looks down -Z and
// the linear depth is the distance in front of the camera, as in the velocity pass; proj.xy are the projection's x and
// y scales (their signs carry the target's axis directions). A pixel with no opaque surface is fully open.
// Task 0072, stage g: it was a string in ambientocclusion.cpp, which wrote AO_RADIUS, AO_SLICES and AO_STEPS in from
// [Rendering] ao radius, ao slices and ao steps; ao.settings (Resource::ShaderSettings) defines them now. The Slang twin
// is post/ao.slang.
#include "ao.settings"

layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D viewNormal;
layout(push_constant) uniform Push
{
    vec4 proj;
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outAo;

const float PI = 3.14159265358979;

float gradientNoise(vec2 p)
{
    // Interleaved gradient noise (Jimenez 2014): a fixed pattern by pixel position, no time.
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 viewPosAt(vec2 uv, float d)
{
    return vec3((uv * 2.0 - 1.0) * d / push.proj.xy, -d);
}

float horizonCosine(vec3 delta, vec3 toCamera)
{
    float len = length(delta);
    if (!(len > 0.0) || len >= AO_RADIUS)
        return -1.0;
    float weight = clamp(1.0 - len / AO_RADIUS, 0.0, 1.0);
    float cosine = dot(delta, toCamera) / len;
    // The falloff of Render::Ao::horizonCosine at 1.
    return -1.0 + (cosine + 1.0) * weight;
}

float sliceVisibility(float h1, float h2, float n)
{
    h1 = n + max(h1 - n, -0.5 * PI);
    h2 = n + min(h2 - n, 0.5 * PI);
    float sinN = sin(n);
    float cosN = cos(n);
    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN) + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

void main()
{
    outAo = vec4(1.0);
    float d = texture(linearDepth, texCoord).r;
    if (!(d > 0.0))
        return;
    vec3 n = texture(viewNormal, texCoord).xyz;
    if (!(dot(n, n) > 0.0))
        return;
    n = normalize(n);
    vec3 position = viewPosAt(texCoord, d);
    vec3 toCamera = normalize(-position);
    vec2 noise = vec2(gradientNoise(gl_FragCoord.xy), gradientNoise(gl_FragCoord.xy + vec2(5.588238, 5.588238)));
    // The radius on the screen, per axis (a view space circle), kept to a quarter of the screen.
    vec2 uvRadius = min(AO_RADIUS * abs(push.proj.xy) * 0.5 / d, vec2(0.25));
    float visibility = 0.0;
    for (int s = 0; s < AO_SLICES; ++s)
    {
        float phi = (float(s) + noise.x) / float(AO_SLICES) * PI;
        vec2 uvDir = vec2(cos(phi), sin(phi));
        vec3 dirView = normalize(vec3(uvDir * sign(push.proj.xy), 0.0));
        vec3 orthoDir = dirView - dot(dirView, toCamera) * toCamera;
        vec3 axis = normalize(cross(dirView, toCamera));
        vec3 projected = n - axis * dot(n, axis);
        float projectedLength = length(projected);
        if (projectedLength < 1e-4)
        {
            visibility += 1.0;
            continue;
        }
        float cosN = clamp(dot(projected, toCamera) / projectedLength, 0.0, 1.0);
        float nAngle = (dot(orthoDir, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
        float horizon[2] = float[2](-1.0, -1.0);
        for (int side = 0; side < 2; ++side)
        {
            float direction = side == 0 ? 1.0 : -1.0;
            for (int k = 1; k <= AO_STEPS; ++k)
            {
                float t = (float(k) - noise.y) / float(AO_STEPS);
                vec2 sampleUv = texCoord + direction * uvDir * uvRadius * t;
                float sampleDepth = texture(linearDepth, sampleUv).r;
                if (!(sampleDepth > 0.0))
                    continue;
                horizon[side] = max(horizon[side], horizonCosine(viewPosAt(sampleUv, sampleDepth) - position, toCamera));
            }
        }
        float h1 = -acos(clamp(horizon[1], -1.0, 1.0));
        float h2 = acos(clamp(horizon[0], -1.0, 1.0));
        visibility += projectedLength * sliceVisibility(h1, h2, nAngle);
    }
    outAo = vec4(clamp(visibility / float(AO_SLICES), 0.0, 1.0));
}
