// OpenEden (task 0042): the shadow of the ray marched volumetric clouds on the ground. The compute pass `shadowMain` in
// comp/cloud/cloud.slang fills a map of the sun's transmittance through the cloud layer on the layer's middle plane, and
// two placement texels beside it; the C++ twins of the constants are components/render/cloudvolumegpu.hpp
// (shadowMapSize). Included by lib/view/shadow.glsl when `shadow.settings` defines VOLUME_CLOUD_SHADOW. Include after
// lib/view/bindings.glsl, lib/sets.glsl and lib/view/scene.glsl.
//
// The map is shadowMapSize texels a side in x and shadowMapSize + 1 wide: the extra column holds, at row 0, the
// placement (centre x, centre y, the plane's height, half the width, all in game units) and, at row 1, the strength of
// the darkening (x) and the share of the sky the volume draws (y).
layout(set=VIEW_SET, binding=VIEW_CLOUD_SHADOW_BINDING) uniform sampler2D cloudShadowMap;

const int VOLUME_CLOUD_SHADOW_SIZE = 256;
// The sun is too low to shadow the ground below these (the shadow would be a long smear): the same limits as the
// vanilla clouds' shadow (cloudshadow.glsl).
const float VOLUME_CLOUD_SUN_UP_FROM = 0.08;
const float VOLUME_CLOUD_SUN_UP_TO = 0.3;
// The map fades out over the outer tenth of its width, so its edge shows no seam.
const float VOLUME_CLOUD_EDGE_FADE = 0.8;

// x: the share of the direct sun that reaches the ground at `viewPos` (1: no shadow), y: the share of the sky the
// volume draws (0: the vanilla layers' shadow, if any, applies instead).
vec2 volumeCloudShadow(vec3 viewPos)
{
    ivec2 size = textureSize(cloudShadowMap, 0);
    if (size.x != VOLUME_CLOUD_SHADOW_SIZE + 1 || size.y != VOLUME_CLOUD_SHADOW_SIZE)
        return vec2(1.0, 0.0);
    vec4 place = texelFetch(cloudShadowMap, ivec2(VOLUME_CLOUD_SHADOW_SIZE, 0), 0);
    vec4 amounts = texelFetch(cloudShadowMap, ivec2(VOLUME_CLOUD_SHADOW_SIZE, 1), 0);
    float share = amounts.y;
    float amount = amounts.x;
    if (amount <= 0.0 || place.w <= 0.0)
        return vec2(1.0, share);
    vec3 toSun = normalize(mat3(scene.invView) * scene.lightViewPos.xyz);
    amount *= smoothstep(VOLUME_CLOUD_SUN_UP_FROM, VOLUME_CLOUD_SUN_UP_TO, toSun.z);
    if (amount <= 0.0)
        return vec2(1.0, share);
    vec3 ground = (scene.invView * vec4(viewPos, 1.0)).xyz;
    // The ground point's ray to the sun meets the layer's middle plane here.
    vec2 plane = ground.xy + toSun.xy * ((place.z - ground.z) / max(toSun.z, 0.5 * VOLUME_CLOUD_SUN_UP_FROM));
    vec2 uv = (plane - place.xy) / (2.0 * place.w) + 0.5;
    float edge = max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0;
    float fade = 1.0 - smoothstep(VOLUME_CLOUD_EDGE_FADE, 1.0, edge);
    if (fade <= 0.0)
        return vec2(1.0, share);
    // Bilinear by hand: the texels hold transmittance and the placement, so the image is read with texelFetch only.
    vec2 p = uv * float(VOLUME_CLOUD_SHADOW_SIZE) - 0.5;
    vec2 f = floor(p);
    vec2 t = p - f;
    ivec2 lo = clamp(ivec2(f), ivec2(0), ivec2(VOLUME_CLOUD_SHADOW_SIZE - 1));
    ivec2 hi = clamp(ivec2(f) + ivec2(1), ivec2(0), ivec2(VOLUME_CLOUD_SHADOW_SIZE - 1));
    float a = mix(texelFetch(cloudShadowMap, ivec2(lo.x, lo.y), 0).x, texelFetch(cloudShadowMap, ivec2(hi.x, lo.y), 0).x, t.x);
    float b = mix(texelFetch(cloudShadowMap, ivec2(lo.x, hi.y), 0).x, texelFetch(cloudShadowMap, ivec2(hi.x, hi.y), 0).x, t.x);
    float transmittance = mix(a, b, t.y);
    return vec2(1.0 - clamp(amount, 0.0, 1.0) * fade * (1.0 - transmittance), share);
}
