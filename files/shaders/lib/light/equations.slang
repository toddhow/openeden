// The Slang twin of lib/light/equations.glsl (task 0072): light range and attenuation. Change both together, and
// components/render/probebake.hpp, which repeats them.
#ifndef LIB_LIGHT_EQUATIONS_SLANG
#define LIB_LIGHT_EQUATIONS_SLANG

import lib.light.lightsettings;
import lib.util.quickstep;

float lambert(float3 light, float3 normal)
{
    return max(dot(normal, light), 0.0);
}

float lightRange(float intensity)
{
    // OpenEden: without classic falloff a light ends at twice its radius (see attenuate), as OpenMW's doLighting.
    // Task 0072 (review): a branch on the specialization constant itself, never `!cClassicFalloff` (an
    // OpSpecConstantOp, the pattern apps/openmw/mwrender/CLAUDE.md records as miscompiled).
    if (cClassicFalloff)
    {
        float r = intensity * LightAttenuation_LinearRadiusMult;
        float attenuation = LightAttenuation_LinearValue / r;
        const float epsilon = 0.03;
        return 1 / (epsilon * attenuation);
    }
    return intensity * 2.0;
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
        // Task 0072 (water): a switch on the constant, not a comparison with it (an OpSpecConstantOp).
        switch (LightAttenuation_LinearMethod)
        {
            case 1:
                attenuation /= r;
                break;
            case 2:
                attenuation /= r * r;
                break;
            default:
                break;
        }
        value += attenuation * distance;
    }
    if (LightAttenuation_UseQuadratic)
    {
        float r = intensity * LightAttenuation_QuadraticRadiusMult;
        float attenuation = LightAttenuation_QuadraticValue;
        switch (LightAttenuation_QuadraticMethod)
        {
            case 1:
                attenuation /= r;
                break;
            case 2:
                attenuation /= r * r;
                break;
            default:
                break;
        }
        value += attenuation * distance * distance;
    }
    float illumination = clamp(1.0 / value, 0.0, 1.0);
    // OpenEden: fades between the radius and the radius doubled, as OpenMW's lcalcIllumination at 442eec6e2f.
    if (cClassicFalloff)
        return illumination;
    return illumination * (1.0 - quickstep(distance / intensity - 1.0));
}

#endif
