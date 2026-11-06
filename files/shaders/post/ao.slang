// The ambient occlusion pass in Slang (task 0072, stage g): the twin of post/ao.frag, the fragment stage of
// Render::AmbientOcclusion (components/render/ambientocclusion.cpp). The CPU twins are Render::Ao::gtaoSliceVisibility
// and horizonCosine (aomath.hpp): change them together. It must declare what the GLSL declares (checked by
// SlangPermutations); the settings are specialization constants (post/aosettings.slang). View space looks down -Z and
// the linear depth is the distance in front of the camera; proj.xy are the projection's x and y scales (their signs
// carry the target's axis directions). A pixel with no opaque surface is fully open.
import post.aosettings;

[[vk::binding(0, 0)]] Sampler2D linearDepth;
[[vk::binding(1, 0)]] Sampler2D viewNormal;

struct Push
{
    float4 proj;
};
[[vk::push_constant]] ConstantBuffer<Push> push;

static const float PI = 3.14159265358979;

float gradientNoise(float2 p)
{
    // Interleaved gradient noise (Jimenez 2014): a fixed pattern by pixel position, no time.
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float3 viewPosAt(float2 uv, float d)
{
    return float3((uv * 2.0 - 1.0) * d / push.proj.xy, -d);
}

float horizonCosine(float3 delta, float3 toCamera)
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

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
    float4 fragCoord : SV_Position;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float2 texCoord = input.texCoord;
    float d = linearDepth.Sample(texCoord).r;
    if (!(d > 0.0))
        return float4(1.0);
    float3 n = viewNormal.Sample(texCoord).xyz;
    if (!(dot(n, n) > 0.0))
        return float4(1.0);
    n = normalize(n);
    float3 position = viewPosAt(texCoord, d);
    float3 toCamera = normalize(-position);
    float2 noise = float2(gradientNoise(input.fragCoord.xy), gradientNoise(input.fragCoord.xy + float2(5.588238, 5.588238)));
    // The radius on the screen, per axis (a view space circle), kept to a quarter of the screen.
    float2 uvRadius = min(AO_RADIUS * abs(push.proj.xy) * 0.5 / d, float2(0.25));
    float visibility = 0.0;
    for (int s = 0; s < AO_SLICES; ++s)
    {
        float phi = (float(s) + noise.x) / float(AO_SLICES) * PI;
        float2 uvDir = float2(cos(phi), sin(phi));
        float3 dirView = normalize(float3(uvDir * float2(sign(push.proj.xy)), 0.0));
        float3 orthoDir = dirView - dot(dirView, toCamera) * toCamera;
        float3 axis = normalize(cross(dirView, toCamera));
        float3 projected = n - axis * dot(n, axis);
        float projectedLength = length(projected);
        if (projectedLength < 1e-4)
        {
            visibility += 1.0;
            continue;
        }
        float cosN = clamp(dot(projected, toCamera) / projectedLength, 0.0, 1.0);
        float nAngle = (dot(orthoDir, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
        float horizon[2] = { -1.0, -1.0 };
        for (int side = 0; side < 2; ++side)
        {
            float direction = side == 0 ? 1.0 : -1.0;
            for (int k = 1; k <= AO_STEPS; ++k)
            {
                float t = (float(k) - noise.y) / float(AO_STEPS);
                float2 sampleUv = texCoord + direction * uvDir * uvRadius * t;
                float sampleDepth = linearDepth.Sample(sampleUv).r;
                if (!(sampleDepth > 0.0))
                    continue;
                horizon[side] = max(horizon[side], horizonCosine(viewPosAt(sampleUv, sampleDepth) - position, toCamera));
            }
        }
        float h1 = -acos(clamp(horizon[1], -1.0, 1.0));
        float h2 = acos(clamp(horizon[0], -1.0, 1.0));
        visibility += projectedLength * sliceVisibility(h1, h2, nAngle);
    }
    return float4(clamp(visibility / float(AO_SLICES), 0.0, 1.0));
}
