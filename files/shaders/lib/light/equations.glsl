// OpenEden: guarded, since directional and per-pixel lighting both include it (task 0012, V4h).
#ifndef LIB_LIGHT_EQUATIONS
#define LIB_LIGHT_EQUATIONS

#include "light.settings"
#include "lib/util/quickstep.glsl"

float lambert(vec3 light, vec3 normal)
{
    return max(dot(normal, light),0.0);
}

float lightRange(float intensity)
{
    // OpenEden: without classic falloff a light ends at twice its radius (see attenuate), as OpenMW's doLighting.
    if (!cClassicFalloff)
        return intensity * 2.0;
#if 0
return intensity*4;
#else
    float r = intensity * LightAttenuation_LinearRadiusMult;
    float attenuation = LightAttenuation_LinearValue/r;
    const float epsilon = 0.03;
    return 1/(epsilon*attenuation);
#endif
}

float attenuate(float intensity, float distance)
{
    float value = 0;
    if (LightAttenuation_UseConstant)
        value += LightAttenuation_ConstantValue;
    if (LightAttenuation_UseLinear)
    {
        float r = intensity * LightAttenuation_LinearRadiusMult;
        float attenuation = LightAttenuation_LinearValue;
        if (LightAttenuation_LinearMethod == 1)
            attenuation /= r;
        if (LightAttenuation_LinearMethod == 2)
            attenuation /= r * r;
        value += attenuation * distance;
    }
    if (LightAttenuation_UseQuadratic)
    {
        float r = intensity * LightAttenuation_QuadraticRadiusMult;
        float attenuation = LightAttenuation_QuadraticValue;
        if (LightAttenuation_QuadraticMethod == 1)
            attenuation /= r;
        if (LightAttenuation_QuadraticMethod == 2)
            attenuation /= r * r;
        value += attenuation * distance * distance;
    }
    float illumination = clamp(1.0 / value, 0.0, 1.0);
    // OpenEden: fades between the radius and the radius doubled, as OpenMW's lcalcIllumination at 442eec6e2f.
    if (!cClassicFalloff)
        illumination *= 1.0 - quickstep(distance / intensity - 1.0);
    return illumination;
}

#endif
