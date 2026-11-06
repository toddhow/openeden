// The Slang twin of lib/view/fog.glsl (task 0072): the sun's glow in haze (task 0040), the aerial perspective of the
// whole-map view (task 0066) and OpenMW's fog modes. Include after lib/view/scene.slang. The reasons and the C++ twins
// (components/render/atmospheremath.hpp, aerialmath.hpp) are named in the GLSL; change all of them together.
//
// Differences from the GLSL, both forced by Slang:
// - GLSL indexes a matrix by column (m[3].z is column 3, row 2); Slang by row, so the same element is m[2][3].
// - applyFog(src, viewPos) reads gl_FragCoord through the volumetric fog; here the caller passes the fragment's window
//   position as a third argument.
#ifndef LIB_VIEW_FOG_SLANG
#define LIB_VIEW_FOG_SLANG

__exported import lib.view.fogsettings;
import lib.view.scene;
import lib.view.linear;
import lib.view.volume;

static const float sunGlowAnisotropy = 0.7;
static const float sunGlowGain = 0.06;
static const float sunGlowTintShare = 0.7;
static const float sunGlowPresenceFrom = 0.02;
static const float sunGlowPresenceTo = 0.2;
static const float sunGlowHazeTurbidity = 6.0;
static const float sunGlowPi = 3.14159265358979;
static const float sunGlowWavelengthR = 0.65;
static const float sunGlowWavelengthG = 0.55;
static const float sunGlowWavelengthB = 0.45;

float sunGlowHenyeyGreenstein(float cosTheta, float g)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = clamp(g, -0.99, 0.99);
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return (1.0 - k * k) / (4.0 * sunGlowPi * denominator * sqrt(denominator));
}

float sunGlowOpticalDepth(float wavelength)
{
    float l2 = wavelength * wavelength;
    float rayleigh = 0.008569 / (l2 * l2) * (1.0 + 0.0113 / l2 + 0.00013 / (l2 * l2));
    float beta = max(0.04608 * sunGlowHazeTurbidity - 0.04586, 0.0);
    return rayleigh + beta * pow(wavelength, -1.3);
}

float3 sunGlowTint(float elevation)
{
    float e = max(elevation, 0.0);
    float degrees = e * 180.0 / sunGlowPi;
    float airMass = 1.0 / (sin(e) + 0.50572 * pow(degrees + 6.07995, -1.6364));
    float3 t = exp(-float3(sunGlowOpticalDepth(sunGlowWavelengthR), sunGlowOpticalDepth(sunGlowWavelengthG),
                       sunGlowOpticalDepth(sunGlowWavelengthB))
        * airMass);
    float3 hue = t / max(max(t.x, max(t.y, t.z)), 1e-6);
    return float3(1.0) + (hue - float3(1.0)) * sunGlowTintShare;
}

float3 sunGlowColourOn(float3 viewDir, float3 fogColour)
{
    float haze = 1.0 - clamp(scene.skyColor.w, 0.0, 1.0);
    float3 sun = scene.lightDiffuse.xyz;
    float presence = smoothstep(sunGlowPresenceFrom, sunGlowPresenceTo, max(sun.x, max(sun.y, sun.z)));
    if (haze <= 0.0 || presence <= 0.0)
        return float3(0.0);
    float3 toSun = normalize(scene.lightViewPos.xyz);
    float phase = 4.0 * sunGlowPi * sunGlowHenyeyGreenstein(dot(viewDir, toSun), sunGlowAnisotropy);
    float elevation = asin(clamp(mul((float3x3)scene.invView, toSun).z, -1.0, 1.0));
    float luma = dot(fogColour, float3(0.2126, 0.7152, 0.0722));
    return sunGlowTint(elevation) * (sunGlowGain * phase * haze * max(cSunGlowStrength, 0.0) * presence * luma);
}

// Task 0072 (review of stages b and c): branches on the specialization constant itself, never on an expression over it
// (`!cSunGlow` would be an OpSpecConstantOp, the pattern apps/openmw/mwrender/CLAUDE.md records as miscompiled).
float3 sunGlowColour(float3 viewDir, float3 fogColour)
{
    if (cSunGlow)
    {
        if (linearView())
            return sunGlowColourOn(viewDir, fogColour);
    }
    return float3(0.0);
}

static const float aerialClearFogDepth = 0.7;
static const float aerialMaxWeatherExcess = 0.42;
static const float aerialMaxCameraHeightScales = 30.0;

bool aerialActive()
{
    return cAerialHaze && scene.fogScale < 0.0;
}

float aerialBeta()
{
    float fogEnd = scene.fogStart - 1.0 / scene.fogScale;
    float depth = fogEnd > 0.0 ? 1.0 - scene.fogStart / fogEnd : 0.0;
    float excess = clamp(depth - aerialClearFogDepth, 0.0, aerialMaxWeatherExcess);
    return log(10.0) / max(cAerialVisibility, 1.0) * exp(cAerialWeatherGain * excess);
}

float aerialOpticalDepth(float beta, float cameraHeight, float rayDirZ, float distance)
{
    if (!(distance > 0.0) || !(beta > 0.0))
        return 0.0;
    float H = max(cAerialScaleHeight, 1.0);
    float h0 = clamp(cameraHeight, 0.0, aerialMaxCameraHeightScales * H);
    float dz = clamp(rayDirZ, -1.0, 1.0);
    float s = distance;
    float extra = 0.0;
    if (dz < 0.0)
    {
        float toGround = h0 / -dz;
        if (s > toGround)
        {
            extra = s - toGround;
            s = toGround;
        }
    }
    float k = dz / H;
    float x = k * s;
    float along = abs(x) < 1e-3 ? s * (1.0 - 0.5 * x + x * x / 6.0) : (1.0 - exp(-x)) / k;
    return beta * (exp(-h0 / H) * along + extra);
}

float aerialFogAmount(float3 viewPos)
{
    float distance = length(viewPos);
    if (!(distance > 0.0))
        return 0.0;
    float rayDirZ = mul((float3x3)scene.invView, viewPos).z / distance;
    // GLSL scene.invView[3].z: column 3, row 2.
    return 1.0 - exp(-aerialOpticalDepth(aerialBeta(), scene.invView[2][3], rayDirZ, distance));
}

float3 mixFog(float3 src, float3 fogColour, float fogValue)
{
    if (linearView())
        return toLinear(lerp(toEncoded(src), toEncoded(fogColour), fogValue));
    return lerp(src, fogColour, fogValue);
}

float3 applyFog(float3 src, float distance, float3 fogColour)
{
    float fogValue;
    if (aerialActive())
    {
        fogValue = 1.0 - exp(-aerialBeta() * max(distance, 0.0));
    }
    else if (cExponentialFog)
    {
        float fogEnd = scene.fogStart + (scene.fogScale > 0.0 ? 1.0 / scene.fogScale : 0.0);
        fogValue = 1.0
            - exp(-2.0 * max(0.0, distance - scene.fogStart / 2.0) / max(fogEnd - scene.fogStart / 2.0, 1e-6));
    }
    else
        fogValue = clamp((distance - scene.fogStart) * scene.fogScale, 0.0, 1.0);
    return mixFog(src, fogColour, fogValue);
}

float3 applyFog(float3 src, float distance)
{
    return applyFog(src, distance, scene.fogColor.xyz);
}

float3 applyFog(float3 src, float3 viewPos, float2 fragCoord)
{
    if (aerialActive())
    {
        float fogValue = aerialFogAmount(viewPos);
        if (cSunGlow)
        {
            float3 glow = sunGlowColour(normalize(viewPos), scene.fogColor.xyz);
            if (cSunGlowDebug)
                return mixFog(float3(0.0), glow, fogValue);
            return mixFog(src, scene.fogColor.xyz + glow, fogValue);
        }
        return mixFog(src, scene.fogColor.xyz, fogValue);
    }
    if (volumeActive())
        return applyVolumeFog(src, viewPos, fragCoord);
    float distance = cRadialFog ? length(viewPos) : abs(viewPos.z);
    if (cSunGlow)
    {
        float3 glow = sunGlowColour(normalize(viewPos), scene.fogColor.xyz);
        if (cSunGlowDebug)
            return applyFog(float3(0.0), distance, glow);
        return applyFog(src, distance, scene.fogColor.xyz + glow);
    }
    return applyFog(src, distance);
}

#endif
