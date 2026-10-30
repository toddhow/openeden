//constexpr int alphaTestMode;
bool alphaTest(const float alpha, const float cutoff)
{
    if (alphaTestMode == 1 && alpha >= cutoff)
        return false;
    if (alphaTestMode == 2 && alpha != cutoff)
        return false;
    if (alphaTestMode == 3 && alpha > cutoff)
        return false;
    if (alphaTestMode == 4 && alpha <= cutoff)
        return false;
    if (alphaTestMode == 5 && alpha == cutoff)
        return false;
    if (alphaTestMode == 6 && alpha < cutoff)
        return false;
    if (alphaTestMode == 7)
        return false;
    return true;
}

// OpenEden: alpha to coverage (task 0012, V4j), as OpenMW's alphaTest with @alphaToCoverage (lib/material/alpha.glsl
// at 442eec6e2f). The comparison modes become a coverage value; the others keep alphaTest.
bool alphaTestToCoverage()
{
    if (alphaTestMode == 1)
        return true;
    if (alphaTestMode == 3)
        return true;
    if (alphaTestMode == 4)
        return true;
    if (alphaTestMode == 6)
        return true;
    return false;
}

float coverageAlpha(const float alpha, const float cutoff)
{
    float coverage = (alpha - clamp(cutoff, 0.0001, 0.9999)) / max(fwidth(alpha), 0.0001) + 0.5;
    // Less and less-or-equal keep what is below the cutoff.
    if (alphaTestMode == 1)
        return 1.0 - coverage;
    if (alphaTestMode == 3)
        return 1.0 - coverage;
    return coverage;
}

// OpenMW's coveragePreservingAlphaScale: alpha grows with the mip level, so alpha-tested edges don't shrink with
// distance.
float coveragePreservingAlphaScale(const vec2 scaledUv)
{
    vec2 dUVdx = dFdx(scaledUv);
    vec2 dUVdy = dFdy(scaledUv);
    float maxDUVSquared = max(dot(dUVdx, dUVdx), dot(dUVdy, dUVdy));
    return 1.0 + max(0.0, 0.5 * log2(maxDUVSquared)) * 0.25;
}
