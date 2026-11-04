// The Slang twin of lib/material/pbr.glsl (task 0072): the material model's surface values and microfacet specular. The
// C++ twin and the reasons for each constant are named in the GLSL; change all three together.
#ifndef LIB_MATERIAL_PBR_SLANG
#define LIB_MATERIAL_PBR_SLANG

static const float pbrMinRoughness = 0.045;
static const float pbrDefaultRoughness = 0.8;
static const float pbrDefaultReflectance = 0.5;
static const float pbrPi = 3.14159265358979;

float pbrRoughnessFromShininess(float shininess)
{
    if (!(shininess > 0.0))
        return 1.0;
    return clamp(pow(2.0 / (shininess + 2.0), 0.25), pbrMinRoughness, 1.0);
}

float pbrReflectanceFromSpecular(float3 specularColor)
{
    float3 c = clamp(specularColor, float3(0.0), float3(1.0));
    return 0.5 + 0.5 * dot(c, float3(0.2126, 0.7152, 0.0722));
}

float pbrF0FromReflectance(float reflectance)
{
    return 0.16 * reflectance * reflectance;
}

struct PbrSurface
{
    float roughness;
    float metalness;
    float reflectance;
    float occlusion;
    float subsurface;
};

PbrSurface pbrSurfaceFrom(float4 pbr, float subsurface, float specularShininess, float3 specularColor)
{
    PbrSurface s;
    s.roughness = pbr.x >= 0.0
        ? pbr.x
        : (specularShininess > 0.0 ? pbrRoughnessFromShininess(specularShininess) : pbrDefaultRoughness);
    s.reflectance = pbr.z >= 0.0
        ? pbr.z
        : (specularShininess > 0.0 ? pbrReflectanceFromSpecular(specularColor) : pbrDefaultReflectance);
    s.metalness = clamp(pbr.y, 0.0, 1.0);
    s.occlusion = clamp(pbr.w, 0.0, 1.0);
    s.roughness = clamp(s.roughness, pbrMinRoughness, 1.0);
    s.reflectance = clamp(s.reflectance, 0.0, 1.0);
    s.subsurface = subsurface > 0.5 ? 1.0 : 0.0;
    return s;
}

PbrSurface pbrApplyMap(PbrSurface s, float4 pbr, float3 packedMap)
{
    if (pbr.x < 0.0)
        s.roughness = clamp(packedMap.g, pbrMinRoughness, 1.0);
    if (pbr.y <= 0.0)
        s.metalness = clamp(packedMap.b, 0.0, 1.0);
    if (pbr.w >= 1.0)
        s.occlusion = clamp(packedMap.r, 0.0, 1.0);
    return s;
}

float pbrReflectanceFromF0(float f0)
{
    return clamp(sqrt(max(f0, 0.0) / 0.16), 0.0, 1.0);
}

PbrSurface pbrSurfaceFromMap(PbrSurface base, bool rmaos, float4 texel, float roughnessScale)
{
    PbrSurface s = base;
    float r = rmaos ? texel.r : texel.g;
    float m = rmaos ? texel.g : texel.b;
    float o = rmaos ? texel.b : texel.r;
    float a = rmaos ? texel.a : 1.0;
    s.roughness = clamp(r * (roughnessScale > 0.0 ? roughnessScale : 1.0), pbrMinRoughness, 1.0);
    s.metalness = clamp(m, 0.0, 1.0);
    s.occlusion = clamp(o, 0.0, 1.0);
    s.reflectance = pbrReflectanceFromF0(pbrF0FromReflectance(base.reflectance) * clamp(a, 0.0, 1.0));
    return s;
}

PbrSurface pbrApplyMapRmaos(PbrSurface s, float4 pbr, float4 texel, float roughnessScale)
{
    PbrSurface m = pbrSurfaceFromMap(s, true, texel, roughnessScale);
    if (pbr.x >= 0.0)
        m.roughness = s.roughness;
    if (pbr.y > 0.0)
        m.metalness = s.metalness;
    if (pbr.w < 1.0)
        m.occlusion = s.occlusion;
    return m;
}

static const float pbrLightScale = pbrPi;
static const float pbrMaxLobe = 256.0;

float pbrDistribution(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (pbrPi * d * d);
}

float pbrVisibility(float NoV, float NoL, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    float v = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float l = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(v + l, 1e-5);
}

float3 pbrFresnel(float VoH, float3 f0)
{
    float t = pow(1.0 - clamp(VoH, 0.0, 1.0), 5.0);
    return f0 + (float3(1.0) - f0) * t;
}

float3 pbrSpecularF0(float3 baseColor, float metalness, float reflectance)
{
    return lerp(float3(pbrF0FromReflectance(reflectance)), baseColor, metalness);
}

float3 pbrSpecularLobe(float3 normal, float3 viewDir, float3 lightDir, float roughness, float3 f0)
{
    float NoL = clamp(dot(normal, lightDir), 0.0, 1.0);
    if (NoL <= 0.0)
        return float3(0.0);
    float3 toEye = -viewDir;
    float NoV = max(dot(normal, toEye), 1e-3);
    float3 halfSum = lightDir + toEye;
    float3 halfVec = halfSum / max(length(halfSum), 1e-4);
    float NoH = clamp(dot(normal, halfVec), 0.0, 1.0);
    float VoH = clamp(dot(toEye, halfVec), 0.0, 1.0);
    float3 lobe = pbrDistribution(NoH, roughness) * pbrVisibility(NoV, NoL, roughness) * pbrFresnel(VoH, f0) * NoL
        * pbrLightScale;
    return min(lobe, float3(pbrMaxLobe));
}

#endif
