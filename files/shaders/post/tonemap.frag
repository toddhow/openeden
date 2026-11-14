#version 450
// The tonemap pass (components/render/tonemap.cpp, Render::Tonemap): the float scene to the display, with the
// optional bloom, ambient occlusion, subsurface scattering and G-buffer debug view. Task 0072, stage g: it was a string
// in tonemap.cpp, whose constants are now in post/tonemapconstants.glsl; the Slang twin is post/tonemap.slang.
#pragma import_defines (AO, BLOOM, DEBUG_VIEW, GRADE, SSS)
#include "post/tonemapconstants.glsl"

layout(set=0, binding=0) uniform sampler2D sceneColor;
#ifdef BLOOM
layout(set=0, binding=1) uniform sampler2D bloomColor;
#endif
#ifdef AO
layout(set=0, binding=6) uniform sampler2D gAmbient;
layout(set=0, binding=7) uniform sampler2D gAo;       // half resolution, nearest
layout(set=0, binding=8) uniform sampler2D gAoNormal;
layout(set=0, binding=9) uniform sampler2D gAoDepth;
#endif
#ifdef SSS
layout(set=0, binding=10) uniform sampler2D gSssMaterial; // alpha: 1 on skin
layout(set=0, binding=11) uniform sampler2D gSssDepth;
layout(set=0, binding=12) uniform sampler2D gSssNormal;
#endif
#ifdef DEBUG_VIEW
layout(set=0, binding=2) uniform sampler2D gNormal;
layout(set=0, binding=3) uniform sampler2D gLinearDepth;
layout(set=0, binding=4) uniform sampler2D gVelocity;
layout(set=0, binding=5) uniform sampler2D gMaterial;
#endif
layout(push_constant) uniform Push
{
    vec4 params; // x exposure scale, y curve, z encode, w bloom strength
#ifdef AO
    vec4 ao;     // xy the size of one texel of the occlusion image, z strength
#endif
#ifdef SSS
    vec4 sss;    // x strength, y width in game units, z focal length in pixels
#endif
#ifdef DEBUG_VIEW
    vec4 debug;  // x mode (1 normals, 2 depth, 3 velocity, 4 roughness, 5 material, 6 subsurface, 7 ao, 8 ambient, 9 sss), y depth range, z velocity scale
#endif
#ifdef GRADE
    vec4 grade;     // x saturation, y contrast, z tint amount, w 1 when the grade applies
    vec4 gradeTint; // xyz the tint colour
#endif
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;

vec3 reinhard(vec3 x)
{
    return x * (vec3(1.0) + x / (REINHARD_WHITE * REINHARD_WHITE)) / (vec3(1.0) + x);
}
vec3 aces(vec3 x)
{
    return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

#ifdef AO
// Task 0029: the CPU twins are Render::Ao::bilateralWeight and aoCurve (aomath.hpp). The half resolution occlusion is
// filtered over 5 by 5 of its texels, each weighted by whether its surface (the stored depth and normal at that
// texel centre) is this pixel's.
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
// pow(0, 0) is undefined in GLSL, so strength 0 (no occlusion) is handled first.
float aoApply(float ao, float strength)
{
    return strength > 0.0 ? pow(max(ao, 0.0), strength) : 1.0;
}
float aoFiltered(vec2 uv)
{
    float d0 = texture(gAoDepth, uv).r;
    if (!(d0 > 0.0))
        return 1.0;
    vec3 n0 = texture(gAoNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            vec2 tap = uv + vec2(float(x), float(y)) * push.ao.xy;
            vec2 centre = (floor(tap / push.ao.xy) + 0.5) * push.ao.xy;
            float d = texture(gAoDepth, centre).r;
            if (!(d > 0.0))
                continue;
            vec3 n = texture(gAoNormal, centre).xyz;
            n = dot(n, n) > 0.0 ? normalize(n) : n0;
            float w = aoBilateral(d - d0, d0, dot(n, n0)) * (1.0 - 0.1 * float(x * x + y * y));
            sum += texture(gAo, centre).r * w;
            total += w;
        }
    // No neighbour on this surface: no occlusion, rather than another surface's.
    return total > 1e-4 ? sum / total : 1.0;
}
#endif

#ifdef SSS
// Task 0033: the CPU twins are Render::Sss::gaussian, tapOffset, surfaceWeight and scatter (sssmath.hpp). For a pixel on
// skin, each colour channel becomes: itself times a gain, the gain being a wide Gaussian average of the neighbouring
// skin on the same surface over a very narrow one, blended in by the strength. The constants repeat sssmath.hpp (the
// numbers are written in).
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
vec3 sssApply(vec2 uv, vec3 centre)
{
    // Skin is marked by the material output's alpha; multisampling averages it, so an edge pixel counts when half is skin.
    if (!(push.sss.x > 0.0) || texture(gSssMaterial, uv).a < 0.5)
        return centre;
    float depth = texture(gSssDepth, uv).r;
    if (!(depth > 0.0))
        return centre;
    float width = push.sss.y;
    float pixelsPerUnit = push.sss.z / depth;
    vec3 n0 = texture(gSssNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));
    // The spiral is not turned from pixel to pixel: with 16 taps a per-pixel turn made the wide and the narrow averages
    // differ by noise, and the face came out dotted (found by the frame check).
    float rotation = 0.0;
    vec3 sigma = SSS_CHANNEL_SIGMA * width;
    float narrow = SSS_NARROW_SIGMA * width;
    float reach = SSS_REACH * width;
    vec3 wideSum = centre;
    vec3 wideWeight = vec3(1.0);
    vec3 narrowSum = centre;
    float narrowWeight = 1.0;
    for (int i = 0; i < SSS_TAPS; ++i)
    {
        float r = reach * sqrt((float(i) + 0.5) / float(SSS_TAPS));
        float a = float(i) * 2.39996323 + rotation;
        vec2 tap = uv + vec2(cos(a), sin(a)) * (r * pixelsPerUnit) * texel;
        if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
            continue;
        if (texture(gSssMaterial, tap).a < 0.5)
            continue;
        float d = texture(gSssDepth, tap).r;
        if (!(d > 0.0))
            continue;
        vec3 n = texture(gSssNormal, tap).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n0;
        float v = sssSurface(d - depth, width, dot(n, n0));
        if (v <= 0.0)
            continue;
        vec3 c = texture(sceneColor, tap).rgb;
        vec3 w1 = vec3(sssGaussian(r, sigma.x), sssGaussian(r, sigma.y), sssGaussian(r, sigma.z)) * v;
        float w2 = sssGaussian(r, narrow) * v;
        wideSum += c * w1;
        wideWeight += w1;
        narrowSum += c * w2;
        narrowWeight += w2;
    }
    vec3 gain = clamp((wideSum / wideWeight + SSS_EPSILON) / (narrowSum / narrowWeight + SSS_EPSILON),
        vec3(SSS_GAIN_MIN), vec3(SSS_GAIN_MAX));
    return max(centre * (vec3(1.0) + push.sss.x * (gain - vec3(1.0))), vec3(0.0));
}
#endif

#ifdef GRADE
// Task 0127 stage W-S4: the image space grade. The CPU twin is Render::ImageSpace::apply (imagespacemath.hpp): saturation
// about the luma, a tint (the picture's grey colourised at the tint's own brightness, by an amount), then contrast about
// one half; the display value in, the display value out.
vec3 gradeApply(vec3 c)
{
    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    float saturation = max(push.grade.x, 0.0);
    float contrast = max(push.grade.y, 0.0);
    float amount = clamp(push.grade.z, 0.0, 1.0);
    float l = dot(c, lumaWeights);
    c = vec3(l) + saturation * (c - vec3(l));
    float tintLuma = dot(push.gradeTint.xyz, lumaWeights);
    if (amount > 0.0 && tintLuma > 1e-4)
        c = c + (dot(c, lumaWeights) * push.gradeTint.xyz / tintLuma - c) * amount;
    return clamp((c - vec3(0.5)) * contrast + vec3(0.5), vec3(0.0), vec3(1.0));
}
#endif

void main()
{
#ifdef DEBUG_VIEW
    // Task 0026: a stored image in place of the scene (the CPU twins are Render::GBuffer::debug*).
    {
        int mode = int(push.debug.x + 0.5);
        vec3 shown = vec3(0.0);
        if (mode == 1)
        {
            vec3 n = texture(gNormal, texCoord).xyz;
            shown = dot(n, n) > 0.0 ? normalize(n) * 0.5 + 0.5 : vec3(0.0);
        }
        else if (mode == 2)
        {
            float d = texture(gLinearDepth, texCoord).r;
            shown = vec3((d > 0.0 && push.debug.y > 0.0) ? 1.0 - exp(-d / push.debug.y) : 0.0);
        }
        else if (mode == 3)
        {
            vec2 v = texture(gVelocity, texCoord).rg;
            shown = vec3(clamp(0.5 + v * push.debug.z, 0.0, 1.0), 0.5);
        }
        else if (mode == 4)
        {
            // Task 0027: roughness is the normal image's w; black where there is no surface.
            vec4 n = texture(gNormal, texCoord);
            shown = vec3(dot(n.xyz, n.xyz) > 0.0 ? n.w : 0.0);
        }
        else if (mode == 5)
            shown = texture(gMaterial, texCoord).rgb; // metalness, reflectance, occlusion
        else if (mode == 6)
            shown = vec3(texture(gMaterial, texCoord).a);
#ifdef AO
        else if (mode == 7)
            shown = vec3(aoApply(aoFiltered(texCoord), push.ao.z));
        else if (mode == 8)
            shown = texture(gAmbient, texCoord).rgb;
#endif
#ifdef SSS
        else if (mode == 9)
        {
            vec3 base = texture(sceneColor, texCoord).rgb;
            shown = clamp(abs(sssApply(texCoord, base) - base) * 8.0, vec3(0.0), vec3(1.0));
        }
#endif
        if (mode != 0)
        {
            outColor = vec4(shown, 1.0);
            return;
        }
    }
#endif
    vec3 color = texture(sceneColor, texCoord).rgb;
#ifdef SSS
    color = sssApply(texCoord, color);
#endif
#ifdef AO
    // Only the ambient part of the colour is darkened (Render::Ao::removeOcclusion).
    color = max(color - texture(gAmbient, texCoord).rgb * (1.0 - aoApply(aoFiltered(texCoord), push.ao.z)), vec3(0.0));
#endif
#ifdef BLOOM
    color += texture(bloomColor, texCoord).rgb * push.params.w;
#endif
    // A NaN would spread through the 8-bit chain after this; draw black instead.
    if (any(isnan(color)))
        color = vec3(0.0);
    color = clamp(color * push.params.x, vec3(0.0), vec3(MAX_TARGET_VALUE));
    if (push.params.y > 1.5)
        color = aces(color);
    else if (push.params.y > 0.5)
        color = reinhard(color);
    color = clamp(color, 0.0, 1.0);
    if (push.params.z > 0.5)
        color = pow(color, vec3(1.0 / DISPLAY_GAMMA));
#ifdef GRADE
    if (push.grade.w > 0.5)
        color = gradeApply(color);
#endif
    outColor = vec4(color, 1.0);
}
