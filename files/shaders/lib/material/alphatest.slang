// The Slang twin of lib/material/alphatest.glsl (task 0072, stage f): the alpha test and alpha to coverage. The includer
// declares `alphaTestMode` (in the object shader the specialization constant 1). It is switched on, never compared:
// a comparison with a literal would be an OpSpecConstantOp (files/shaders/CLAUDE.md). The reasons are in the GLSL;
// change both together.
#ifndef LIB_MATERIAL_ALPHATEST_SLANG
#define LIB_MATERIAL_ALPHATEST_SLANG

bool alphaTest(float alpha, float cutoff)
{
    switch (alphaTestMode)
    {
        case 1:
            if (alpha >= cutoff)
                return false;
            break;
        case 2:
            if (alpha != cutoff)
                return false;
            break;
        case 3:
            if (alpha > cutoff)
                return false;
            break;
        case 4:
            if (alpha <= cutoff)
                return false;
            break;
        case 5:
            if (alpha == cutoff)
                return false;
            break;
        case 6:
            if (alpha < cutoff)
                return false;
            break;
        case 7:
            return false;
        default:
            break;
    }
    return true;
}

bool alphaTestToCoverage()
{
    switch (alphaTestMode)
    {
        case 1:
        case 3:
        case 4:
        case 6:
            return true;
        default:
            return false;
    }
}

float coverageAlpha(float alpha, float cutoff)
{
    float coverage = (alpha - clamp(cutoff, 0.0001, 0.9999)) / max(fwidth(alpha), 0.0001) + 0.5;
    switch (alphaTestMode)
    {
        case 1:
        case 3:
            return 1.0 - coverage;
        default:
            return coverage;
    }
}

float coveragePreservingAlphaScale(float2 scaledUv)
{
    float2 dUVdx = ddx(scaledUv);
    float2 dUVdy = ddy(scaledUv);
    float maxDUVSquared = max(dot(dUVdx, dUVdx), dot(dUVdy, dUVdy));
    return 1.0 + max(0.0, 0.5 * log2(maxDUVSquared)) * 0.25;
}

#endif
