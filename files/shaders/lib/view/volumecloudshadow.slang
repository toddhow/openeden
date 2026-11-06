// The Slang twin of lib/view/volumecloudshadow.glsl (task 0072): the shadow of the ray marched volumetric clouds on
// the ground (task 0042). Included by lib/view/shadow.slang with VOLUME_CLOUD_SHADOW. Include after
// lib/view/scene.slang. The map's layout and the reasons are in the GLSL; change both together.
#ifndef LIB_VIEW_VOLUMECLOUDSHADOW_SLANG
#define LIB_VIEW_VOLUMECLOUDSHADOW_SLANG

#include "lib/sets.slang"
#include "lib/view/bindings.slang"
import lib.view.scene;

[[vk::binding(VIEW_CLOUD_SHADOW_BINDING, VIEW_SET)]] Sampler2D cloudShadowMap;

static const int VOLUME_CLOUD_SHADOW_SIZE = 256;
static const float VOLUME_CLOUD_SUN_UP_FROM = 0.08;
static const float VOLUME_CLOUD_SUN_UP_TO = 0.3;
static const float VOLUME_CLOUD_EDGE_FADE = 0.8;

float cloudShadowTexel(int2 texel)
{
    return cloudShadowMap.Load(int3(texel, 0)).x;
}

// x: the share of the direct sun that reaches the ground at `viewPos` (1: no shadow), y: the share of the sky the
// volume draws (0: the vanilla layers' shadow, if any, applies instead).
float2 volumeCloudShadow(float3 viewPos)
{
    uint width;
    uint height;
    cloudShadowMap.GetDimensions(width, height);
    int2 size = int2(width, height);
    if (size.x != VOLUME_CLOUD_SHADOW_SIZE + 1 || size.y != VOLUME_CLOUD_SHADOW_SIZE)
        return float2(1.0, 0.0);
    float4 place = cloudShadowMap.Load(int3(VOLUME_CLOUD_SHADOW_SIZE, 0, 0));
    float4 amounts = cloudShadowMap.Load(int3(VOLUME_CLOUD_SHADOW_SIZE, 1, 0));
    float share = amounts.y;
    float amount = amounts.x;
    if (amount <= 0.0 || place.w <= 0.0)
        return float2(1.0, share);
    float3 toSun = normalize(mul((float3x3)scene.invView, scene.lightViewPos.xyz));
    amount *= smoothstep(VOLUME_CLOUD_SUN_UP_FROM, VOLUME_CLOUD_SUN_UP_TO, toSun.z);
    if (amount <= 0.0)
        return float2(1.0, share);
    float3 ground = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    // The ground point's ray to the sun meets the layer's middle plane here.
    float2 plane = ground.xy + toSun.xy * ((place.z - ground.z) / max(toSun.z, 0.5 * VOLUME_CLOUD_SUN_UP_FROM));
    float2 uv = (plane - place.xy) / (2.0 * place.w) + 0.5;
    float edge = max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0;
    float fade = 1.0 - smoothstep(VOLUME_CLOUD_EDGE_FADE, 1.0, edge);
    if (fade <= 0.0)
        return float2(1.0, share);
    // Bilinear by hand: the texels hold transmittance and the placement, so the image is read with Load only.
    float2 p = uv * float(VOLUME_CLOUD_SHADOW_SIZE) - 0.5;
    float2 f = floor(p);
    float2 t = p - f;
    int2 lo = clamp(int2(f), int2(0), int2(VOLUME_CLOUD_SHADOW_SIZE - 1));
    int2 hi = clamp(int2(f) + int2(1), int2(0), int2(VOLUME_CLOUD_SHADOW_SIZE - 1));
    float a = lerp(cloudShadowTexel(int2(lo.x, lo.y)), cloudShadowTexel(int2(hi.x, lo.y)), t.x);
    float b = lerp(cloudShadowTexel(int2(lo.x, hi.y)), cloudShadowTexel(int2(hi.x, hi.y)), t.x);
    float transmittance = lerp(a, b, t.y);
    return float2(1.0 - clamp(amount, 0.0, 1.0) * fade * (1.0 - transmittance), share);
}

#endif
