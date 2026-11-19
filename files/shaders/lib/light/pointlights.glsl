struct PointLight
{
    vec4 colorIntensity;
    vec4 position;
};

layout(std430, set=VIEW_SET, binding=VIEW_LIGHTS_BINDING) readonly buffer Lights {
    PointLight pointLights[];
};

// Spot lights (task 0355 stage g; layout in components/view/lightdata.hpp, twin pointlights.slang). The buffer holds,
// after the lights, one vec4 a light: the spot axis in view space and the cos of its half angle (0: not a spot). The
// second light's position.w says where that region starts (maxLights), 0 or absent for any other buffer.
const float spotConeSoftness = 0.3; // the cone's edge fades between cosOuter and cosOuter + this * (1 - cosOuter)

vec4 pointLightSpot(uint lightIndex)
{
    uint n = uint(pointLights.length());
    if (n < 2u)
        return vec4(0.0);
    float marker = pointLights[1].position.w;
    if (!(marker >= 2.0) || marker >= 4096.0)
        return vec4(0.0);
    uint maxLights = uint(marker);
    uint s = maxLights + (lightIndex >> 1);
    if (lightIndex >= maxLights || s >= n)
        return vec4(0.0);
    return ((lightIndex & 1u) != 0u) ? pointLights[s].position : pointLights[s].colorIntensity;
}

// 1 for a light that is not a spot; for a spot the smooth cone factor of the unit vector from the light to the point.
float pointLightCone(uint lightIndex, vec3 lightToPoint)
{
    vec4 spot = pointLightSpot(lightIndex);
    if (!(spot.w > 0.0))
        return 1.0;
    float c = dot(spot.xyz, lightToPoint);
    float t = clamp((c - spot.w) / (spotConeSoftness * (1.0 - spot.w)), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
