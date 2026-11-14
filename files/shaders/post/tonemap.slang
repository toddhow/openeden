// The tonemap pass in Slang (task 0072, stage g): the twin of post/tonemap.frag, compiled for every combination of
// AO, BLOOM, DEBUG_VIEW and SSS (Render::Tonemap::setSource) and, at run time, GRADE (task 0127 stage W-S4: not a built
// permutation). It must declare what the GLSL declares (checked by
// SlangPermutations): the C++ side builds the descriptor set layout and the push constant offsets by hand. The curves
// and the constants repeat Render::Hdr (hdrmath.hpp), the occlusion filter Render::Ao (aomath.hpp) and the subsurface
// scattering Render::Sss (sssmath.hpp); change them together.
import post.tonemapconstants;

[[vk::binding(0, 0)]] Sampler2D sceneColor;
#ifdef BLOOM
[[vk::binding(1, 0)]] Sampler2D bloomColor;
#endif
#ifdef AO
[[vk::binding(6, 0)]] Sampler2D gAmbient;
[[vk::binding(7, 0)]] Sampler2D gAo; // half resolution, nearest
[[vk::binding(8, 0)]] Sampler2D gAoNormal;
[[vk::binding(9, 0)]] Sampler2D gAoDepth;
#endif
#ifdef SSS
[[vk::binding(10, 0)]] Sampler2D gSssMaterial; // alpha: 1 on skin
[[vk::binding(11, 0)]] Sampler2D gSssDepth;
[[vk::binding(12, 0)]] Sampler2D gSssNormal;
#endif
#ifdef DEBUG_VIEW
[[vk::binding(2, 0)]] Sampler2D gNormal;
[[vk::binding(3, 0)]] Sampler2D gLinearDepth;
[[vk::binding(4, 0)]] Sampler2D gVelocity;
[[vk::binding(5, 0)]] Sampler2D gMaterial;
#endif

struct Push
{
    float4 params; // x exposure scale, y curve, z encode, w bloom strength
#ifdef AO
    float4 ao; // xy the size of one texel of the occlusion image, z strength
#endif
#ifdef SSS
    float4 sss; // x strength, y width in game units, z focal length in pixels
#endif
#ifdef DEBUG_VIEW
    float4 debug; // x mode (1 normals, 2 depth, 3 velocity, 4 roughness, 5 material, 6 subsurface, 7 ao, 8 ambient,
                  // 9 sss), y depth range, z velocity scale
#endif
#ifdef GRADE
    float4 grade; // x saturation, y contrast, z tint amount, w 1 when the grade applies
    float4 gradeTint; // xyz the tint colour
#endif
};
[[vk::push_constant]] ConstantBuffer<Push> push;

float3 reinhard(float3 x)
{
    return x * (float3(1.0) + x / (REINHARD_WHITE * REINHARD_WHITE)) / (float3(1.0) + x);
}
float3 aces(float3 x)
{
    return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

#ifdef AO
// Task 0029: the CPU twins are Render::Ao::bilateralWeight and aoCurve (aomath.hpp). The half resolution occlusion is
// filtered over 5 by 5 of its texels, each weighted by whether its surface (the stored depth and normal at that texel
// centre) is this pixel's.
float aoSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float aoBilateral(float depthDifference, float depth, float normalDot)
{
    float relative = depth > 0.0 ? abs(depthDifference) / depth : 1.0;
    return (1.0 - aoSmooth(0.02, 0.05, relative)) * aoSmooth(0.707, 0.9, normalDot);
}
// pow(0, 0) is undefined, so strength 0 (no occlusion) is handled first.
float aoApply(float ao, float strength)
{
    return strength > 0.0 ? pow(max(ao, 0.0), strength) : 1.0;
}
float aoFiltered(float2 uv)
{
    float d0 = gAoDepth.Sample(uv).r;
    if (!(d0 > 0.0))
        return 1.0;
    float3 n0 = gAoNormal.Sample(uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : float3(0.0, 0.0, 1.0);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            float2 tap = uv + float2(float(x), float(y)) * push.ao.xy;
            float2 centre = (floor(tap / push.ao.xy) + 0.5) * push.ao.xy;
            float d = gAoDepth.Sample(centre).r;
            if (!(d > 0.0))
                continue;
            float3 n = gAoNormal.Sample(centre).xyz;
            n = dot(n, n) > 0.0 ? normalize(n) : n0;
            float w = aoBilateral(d - d0, d0, dot(n, n0)) * (1.0 - 0.1 * float(x * x + y * y));
            sum += gAo.Sample(centre).r * w;
            total += w;
        }
    // No neighbour on this surface: no occlusion, rather than another surface's.
    return total > 1e-4 ? sum / total : 1.0;
}
#endif

#ifdef SSS
// Task 0033: the CPU twins are Render::Sss::gaussian, tapOffset, surfaceWeight and scatter (sssmath.hpp). For a pixel on
// skin, each colour channel becomes: itself times a gain, the gain being a wide Gaussian average of the neighbouring
// skin on the same surface over a very narrow one, blended in by the strength.
float sssSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float sssGaussian(float distance, float sigma)
{
    if (!(sigma > 0.0))
        return distance == 0.0 ? 1.0 : 0.0;
    return exp(-distance * distance / (2.0 * sigma * sigma));
}
float sssSurface(float depthDelta, float width, float normalDot)
{
    float limit = max(width, 1e-4);
    return (1.0 - sssSmooth(limit, 2.0 * limit, abs(depthDelta))) * sssSmooth(0.2, 0.6, normalDot);
}
float3 sssApply(float2 uv, float3 centre)
{
    // Skin is marked by the material output's alpha; multisampling averages it, so an edge pixel counts when half is skin.
    if (!(push.sss.x > 0.0) || gSssMaterial.Sample(uv).a < 0.5)
        return centre;
    float depth = gSssDepth.Sample(uv).r;
    if (!(depth > 0.0))
        return centre;
    float width = push.sss.y;
    float pixelsPerUnit = push.sss.z / depth;
    float3 n0 = gSssNormal.Sample(uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : float3(0.0, 0.0, 1.0);
    uint sizeX;
    uint sizeY;
    sceneColor.GetDimensions(sizeX, sizeY);
    float2 texel = 1.0 / float2(float(sizeX), float(sizeY));
    // The spiral is not turned from pixel to pixel: with 16 taps a per-pixel turn made the wide and the narrow averages
    // differ by noise, and the face came out dotted (found by the frame check).
    float rotation = 0.0;
    float3 sigma = SSS_CHANNEL_SIGMA * width;
    float narrow = SSS_NARROW_SIGMA * width;
    float reach = SSS_REACH * width;
    float3 wideSum = centre;
    float3 wideWeight = float3(1.0);
    float3 narrowSum = centre;
    float narrowWeight = 1.0;
    for (int i = 0; i < SSS_TAPS; ++i)
    {
        float r = reach * sqrt((float(i) + 0.5) / float(SSS_TAPS));
        float a = float(i) * 2.39996323 + rotation;
        float2 tap = uv + float2(cos(a), sin(a)) * (r * pixelsPerUnit) * texel;
        if (any(tap < float2(0.0)) || any(tap > float2(1.0)))
            continue;
        if (gSssMaterial.Sample(tap).a < 0.5)
            continue;
        float d = gSssDepth.Sample(tap).r;
        if (!(d > 0.0))
            continue;
        float3 n = gSssNormal.Sample(tap).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n0;
        float v = sssSurface(d - depth, width, dot(n, n0));
        if (v <= 0.0)
            continue;
        float3 c = sceneColor.Sample(tap).rgb;
        float3 w1 = float3(sssGaussian(r, sigma.x), sssGaussian(r, sigma.y), sssGaussian(r, sigma.z)) * v;
        float w2 = sssGaussian(r, narrow) * v;
        wideSum += c * w1;
        wideWeight += w1;
        narrowSum += c * w2;
        narrowWeight += w2;
    }
    float3 gain = clamp((wideSum / wideWeight + SSS_EPSILON) / (narrowSum / narrowWeight + SSS_EPSILON),
        float3(SSS_GAIN_MIN), float3(SSS_GAIN_MAX));
    return max(centre * (float3(1.0) + push.sss.x * (gain - float3(1.0))), float3(0.0));
}
#endif

#ifdef GRADE
// Task 0127 stage W-S4: the image space grade (twin of post/tonemap.frag's gradeApply and of Render::ImageSpace::apply in
// imagespacemath.hpp): saturation about the luma, a tint (the picture's grey colourised at the tint's own brightness, by
// an amount), then contrast about one half; the display value in, the display value out.
float3 gradeApply(float3 c)
{
    const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);
    float saturation = max(push.grade.x, 0.0);
    float contrast = max(push.grade.y, 0.0);
    float amount = clamp(push.grade.z, 0.0, 1.0);
    float l = dot(c, lumaWeights);
    c = float3(l) + saturation * (c - float3(l));
    float tintLuma = dot(push.gradeTint.xyz, lumaWeights);
    if (amount > 0.0 && tintLuma > 1e-4)
        c = c + (dot(c, lumaWeights) * push.gradeTint.xyz / tintLuma - c) * amount;
    return clamp((c - float3(0.5)) * contrast + float3(0.5), float3(0.0), float3(1.0));
}
#endif

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : V_TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float2 texCoord = input.texCoord;
#ifdef DEBUG_VIEW
    // Task 0026: a stored image in place of the scene (the CPU twins are Render::GBuffer::debug*).
    {
        int mode = int(push.debug.x + 0.5);
        float3 shown = float3(0.0);
        if (mode == 1)
        {
            float3 n = gNormal.Sample(texCoord).xyz;
            shown = dot(n, n) > 0.0 ? normalize(n) * 0.5 + 0.5 : float3(0.0);
        }
        else if (mode == 2)
        {
            float d = gLinearDepth.Sample(texCoord).r;
            shown = float3((d > 0.0 && push.debug.y > 0.0) ? 1.0 - exp(-d / push.debug.y) : 0.0);
        }
        else if (mode == 3)
        {
            float2 v = gVelocity.Sample(texCoord).rg;
            shown = float3(clamp(0.5 + v * push.debug.z, float2(0.0), float2(1.0)), 0.5);
        }
        else if (mode == 4)
        {
            // Task 0027: roughness is the normal image's w; black where there is no surface.
            float4 n = gNormal.Sample(texCoord);
            shown = float3(dot(n.xyz, n.xyz) > 0.0 ? n.w : 0.0);
        }
        else if (mode == 5)
            shown = gMaterial.Sample(texCoord).rgb; // metalness, reflectance, occlusion
        else if (mode == 6)
            shown = float3(gMaterial.Sample(texCoord).a);
#ifdef AO
        else if (mode == 7)
            shown = float3(aoApply(aoFiltered(texCoord), push.ao.z));
        else if (mode == 8)
            shown = gAmbient.Sample(texCoord).rgb;
#endif
#ifdef SSS
        else if (mode == 9)
        {
            float3 base = sceneColor.Sample(texCoord).rgb;
            shown = clamp(abs(sssApply(texCoord, base) - base) * 8.0, float3(0.0), float3(1.0));
        }
#endif
        if (mode != 0)
            return float4(shown, 1.0);
    }
#endif
    float3 color = sceneColor.Sample(texCoord).rgb;
#ifdef SSS
    color = sssApply(texCoord, color);
#endif
#ifdef AO
    // Only the ambient part of the colour is darkened (Render::Ao::removeOcclusion).
    color = max(color - gAmbient.Sample(texCoord).rgb * (1.0 - aoApply(aoFiltered(texCoord), push.ao.z)), float3(0.0));
#endif
#ifdef BLOOM
    color += bloomColor.Sample(texCoord).rgb * push.params.w;
#endif
    // A NaN would spread through the 8-bit chain after this; draw black instead.
    if (any(isnan(color)))
        color = float3(0.0);
    color = clamp(color * push.params.x, float3(0.0), float3(MAX_TARGET_VALUE));
    if (push.params.y > 1.5)
        color = aces(color);
    else if (push.params.y > 0.5)
        color = reinhard(color);
    color = clamp(color, float3(0.0), float3(1.0));
    if (push.params.z > 0.5)
        color = pow(color, float3(1.0 / DISPLAY_GAMMA));
#ifdef GRADE
    if (push.grade.w > 0.5)
        color = gradeApply(color);
#endif
    return float4(color, 1.0);
}
