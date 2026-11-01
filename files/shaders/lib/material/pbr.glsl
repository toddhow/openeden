// OpenEden (task 0027, F4 and R1): the material model's formulas. The CPU twins are in
// components/render/materialmath.hpp (Render::Pbr); change both together. Include after lib/view/scene.glsl.
#ifndef LIB_MATERIAL_PBR
#define LIB_MATERIAL_PBR

const float pbrMinRoughness = 0.045;
const float pbrDefaultRoughness = 0.8;
const float pbrDefaultReflectance = 0.5;
const float pbrPi = 3.14159265358979;

// Blinn-Phong exponent to perceptual roughness: (2 / (n + 2))^(1/4).
float pbrRoughnessFromShininess(float shininess)
{
    if (!(shininess > 0.0))
        return 1.0;
    return clamp(pow(2.0 / (shininess + 2.0), 0.25), pbrMinRoughness, 1.0);
}

float pbrReflectanceFromSpecular(vec3 specularColor)
{
    vec3 c = clamp(specularColor, vec3(0.0), vec3(1.0));
    return 0.5 + 0.5 * dot(c, vec3(0.2126, 0.7152, 0.0722));
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

// The surface's values: the material descriptor's where it has them, else derived from the specular map (a shininess
// of zero or less means there is none) and, failing that, the tier 1 defaults.
PbrSurface pbrSurfaceFrom(vec4 pbr, float subsurface, float specularShininess, vec3 specularColor)
{
    PbrSurface s;
    s.roughness = pbr.x >= 0.0 ? pbr.x : (specularShininess > 0.0 ? pbrRoughnessFromShininess(specularShininess) : pbrDefaultRoughness);
    s.reflectance = pbr.z >= 0.0 ? pbr.z : (specularShininess > 0.0 ? pbrReflectanceFromSpecular(specularColor) : pbrDefaultReflectance);
    s.metalness = clamp(pbr.y, 0.0, 1.0);
    s.occlusion = clamp(pbr.w, 0.0, 1.0);
    s.roughness = clamp(s.roughness, pbrMinRoughness, 1.0);
    s.reflectance = clamp(s.reflectance, 0.0, 1.0);
    s.subsurface = subsurface > 0.5 ? 1.0 : 0.0;
    return s;
}

// OpenEden's lights were never physical: the engine's diffuse is albedo * light * N.L without the 1 / pi, so the
// specular lobe is scaled by pi to sit beside it (the two then carry the energy a physical renderer would give light of
// the same colour).
const float pbrLightScale = pbrPi;

// GGX distribution, Smith height-correlated visibility and Schlick Fresnel (Render::Pbr's twins).
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

vec3 pbrFresnel(float VoH, vec3 f0)
{
    float t = pow(1.0 - clamp(VoH, 0.0, 1.0), 5.0);
    return f0 + (vec3(1.0) - f0) * t;
}

vec3 pbrSpecularF0(vec3 baseColor, float metalness, float reflectance)
{
    return mix(vec3(pbrF0FromReflectance(reflectance)), baseColor, metalness);
}

// One light's specular reflection, N.L included. `viewDir` points from the eye to the surface, `lightDir` from the
// surface to the light (both in view space, like the rest of the lighting).
vec3 pbrSpecularLobe(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness, vec3 f0)
{
    float NoL = clamp(dot(normal, lightDir), 0.0, 1.0);
    if (NoL <= 0.0)
        return vec3(0.0);
    vec3 toEye = -viewDir;
    float NoV = max(dot(normal, toEye), 1e-3);
    vec3 halfVec = normalize(lightDir + toEye);
    float NoH = clamp(dot(normal, halfVec), 0.0, 1.0);
    float VoH = clamp(dot(toEye, halfVec), 0.0, 1.0);
    return pbrDistribution(NoH, roughness) * pbrVisibility(NoV, NoL, roughness) * pbrFresnel(VoH, f0) * NoL
        * pbrLightScale;
}

#endif
