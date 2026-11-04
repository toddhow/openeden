#include "fog.settings"
#include "lib/view/linear.glsl"
#include "lib/view/volume.glsl"

// OpenEden (task 0040): the glow of haze round the sun. The C++ twin is components/render/atmospheremath.hpp
// (henyeyGreenstein, sunGlowPhase, hazeAmount, sunGlowTint, sunGlow, and the air model's airMass, rayleighDepth,
// hazeDepth and sunTransmittance); change both together. Forward scattering in fog and haze makes the air round the
// sun bright and warm; the glow is added to the fog's colour, so it shows where the fog does. Only the main view with
// the linear HDR path takes it, and only with `sun glow` on (fog.settings: cSunGlow, cSunGlowStrength, cSunGlowDebug).
// How hazy the air is comes from the weather's clearness (scene.skyColor.w, the Glare_View value): clear weather has
// none, so it is exactly as before. Its brightness follows the fog's and its colour is the sun's, warm when low.
const float sunGlowAnisotropy = 0.7;
const float sunGlowGain = 0.06;
const float sunGlowTintShare = 0.7;
const float sunGlowPresenceFrom = 0.02;
const float sunGlowPresenceTo = 0.2;
const float sunGlowHazeTurbidity = 6.0;
const float sunGlowPi = 3.14159265358979;
const float sunGlowWavelengthR = 0.65;
const float sunGlowWavelengthG = 0.55;
const float sunGlowWavelengthB = 0.45;

float sunGlowHenyeyGreenstein(float cosTheta, float g)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = clamp(g, -0.99, 0.99);
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return (1.0 - k * k) / (4.0 * sunGlowPi * denominator * sqrt(denominator));
}

// The air model's optical depth of the air for one wavelength (Rayleigh plus haze) and the length of the air path
// (Kasten and Young), as atmospheremath.hpp.
float sunGlowOpticalDepth(float wavelength)
{
    float l2 = wavelength * wavelength;
    float rayleigh = 0.008569 / (l2 * l2) * (1.0 + 0.0113 / l2 + 0.00013 / (l2 * l2));
    float beta = max(0.04608 * sunGlowHazeTurbidity - 0.04586, 0.0);
    return rayleigh + beta * pow(wavelength, -1.3);
}

vec3 sunGlowTint(float elevation)
{
    float e = max(elevation, 0.0);
    float degrees = e * 180.0 / sunGlowPi;
    float airMass = 1.0 / (sin(e) + 0.50572 * pow(degrees + 6.07995, -1.6364));
    vec3 t = exp(-vec3(sunGlowOpticalDepth(sunGlowWavelengthR), sunGlowOpticalDepth(sunGlowWavelengthG),
        sunGlowOpticalDepth(sunGlowWavelengthB)) * airMass);
    vec3 hue = t / max(max(t.x, max(t.y, t.z)), 1e-6);
    return vec3(1.0) + (hue - vec3(1.0)) * sunGlowTintShare;
}

// The glow's colour (linear) in the direction `viewDir` (a unit vector from the eye, in view space) over the fog colour
// `fogColour` (linear); zero when it is off.
vec3 sunGlowColour(vec3 viewDir, vec3 fogColour)
{
    if (!cSunGlow || !linearView())
        return vec3(0.0);
    float haze = 1.0 - clamp(scene.skyColor.w, 0.0, 1.0);
    vec3 sun = scene.lightDiffuse.xyz;
    float presence = smoothstep(sunGlowPresenceFrom, sunGlowPresenceTo, max(sun.x, max(sun.y, sun.z)));
    if (haze <= 0.0 || presence <= 0.0)
        return vec3(0.0);
    vec3 toSun = normalize(scene.lightViewPos.xyz);
    float phase = 4.0 * sunGlowPi * sunGlowHenyeyGreenstein(dot(viewDir, toSun), sunGlowAnisotropy);
    float elevation = asin(clamp((mat3(scene.invView) * toSun).z, -1.0, 1.0));
    float luma = dot(fogColour, vec3(0.2126, 0.7152, 0.0722));
    return sunGlowTint(elevation) * (sunGlowGain * phase * haze * max(cSunGlowStrength, 0.0) * presence * luma);
}

// OpenEden (task 0066, stage c): the aerial perspective of the whole-map view, the haze that far land fades into. The
// C++ twin is components/render/aerialmath.hpp (clearFogDepth, maxWeatherExcess, maxCameraHeightScales, baseBeta,
// weatherBeta, fogDepthFromRange, opticalDepth, fogAmount); change both together (a test reads these constants back).
// The air's density falls off with height as beta * exp(-h / H); a straight ray crosses an optical depth with a closed
// form, and the fog's share of the colour is 1 - exp(-depth). It replaces the linear fog end on the land and the sea,
// in objects, tiles, clusters and water, since they all go through applyFog; the sky keeps its own colours. It is on
// when the scene's fog scale is negative (View::Scene::setFogRange, only in the main view's exterior above water);
// the weather's fog depth is read back from the linear range (start = range * (1 - depth), end = range).
// cAerialHaze, cAerialVisibility, cAerialScaleHeight and cAerialWeatherGain are in fog.settings.
const float aerialClearFogDepth = 0.7;
const float aerialMaxWeatherExcess = 0.42;
const float aerialMaxCameraHeightScales = 30.0;

bool aerialActive()
{
    return cAerialHaze && scene.fogScale < 0.0;
}

// The density at the ground for the current weather.
float aerialBeta()
{
    float fogEnd = scene.fogStart - 1.0 / scene.fogScale; // fogScale is negative here
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
    float extra = 0.0; // the part of the ray at or below the ground level, at full density
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
    // The integral of exp(-(h0 + t dz) / H) over [0, s]: exp(-h0 / H) * (1 - exp(-x)) / k, with a series for small x.
    float along = abs(x) < 1e-3 ? s * (1.0 - 0.5 * x + x * x / 6.0) : (1.0 - exp(-x)) / k;
    return beta * (exp(-h0 / H) * along + extra);
}

// The fog's share (1 - T) of the colour for the ray to `viewPos` (view space, the eye at the origin).
float aerialFogAmount(vec3 viewPos)
{
    float distance = length(viewPos);
    if (!(distance > 0.0))
        return 0.0;
    float rayDirZ = (mat3(scene.invView) * viewPos).z / distance; // the z (up) component of the direction in the world
    return 1.0 - exp(-aerialOpticalDepth(aerialBeta(), scene.invView[3].z, rayDirZ, distance));
}

// OpenEden (task 0025): in the linear view the mix is made on encoded colours and converted, so that fog keeps the
// density it was tuned for; a mix in linear light hazes much more.
vec3 mixFog(vec3 src, vec3 fogColour, float fogValue)
{
    if (linearView())
        return toLinear(mix(toEncoded(src), toEncoded(fogColour), fogValue));
    return mix(src, fogColour, fogValue);
}

// openeden-vsg: OpenMW's [Fog] radial fog and exponential fog (compatibility/fog.glsl at cf3dbd3aa7; task 0012).
// OpenEden (task 0040): `fogColour` is the colour the fog tends to (the scene's, plus the sun glow where there is one).
vec3 applyFog(vec3 src, float distance, vec3 fogColour)
{
    float fogValue;
    if (aerialActive())
    {
        // No direction here (water's depth fog): a ray along the ground in the weather's air.
        fogValue = 1.0 - exp(-aerialBeta() * max(distance, 0.0));
    }
    else if (cExponentialFog)
    {
        float fogEnd = scene.fogStart + (scene.fogScale > 0.0 ? 1.0 / scene.fogScale : 0.0);
        fogValue = 1.0 - exp(-2.0 * max(0.0, distance - scene.fogStart / 2.0) / max(fogEnd - scene.fogStart / 2.0, 1e-6));
    }
    else
        fogValue = clamp((distance - scene.fogStart) * scene.fogScale, 0.0, 1.0);
    return mixFog(src, fogColour, fogValue);
}

vec3 applyFog(/*Scene scene, */vec3 src, float distance)
{
    return applyFog(src, distance, scene.fogColor.xyz);
}

// Distance from the eye (radial fog) or along the view direction (OpenMW's default).
vec3 applyFog(vec3 src, vec3 viewPos)
{
    // OpenEden (task 0066): the aerial perspective is the fog where it is on; the volumetric fog is not used with it.
    if (aerialActive())
    {
        float fogValue = aerialFogAmount(viewPos);
        if (cSunGlow)
        {
            vec3 glow = sunGlowColour(normalize(viewPos), scene.fogColor.xyz);
            if (cSunGlowDebug)
                return mixFog(vec3(0.0), glow, fogValue);
            return mixFog(src, scene.fogColor.xyz + glow, fogValue);
        }
        return mixFog(src, scene.fogColor.xyz, fogValue);
    }
    // OpenEden (task 0041): with the volumetric fog on, the volume is the fog (and holds the sun's glow).
    if (volumeActive())
        return applyVolumeFog(src, viewPos);
    float distance = cRadialFog ? length(viewPos) : abs(viewPos.z);
    if (cSunGlow)
    {
        vec3 glow = sunGlowColour(normalize(viewPos), scene.fogColor.xyz);
        if (cSunGlowDebug)
            return applyFog(vec3(0.0), distance, glow);
        return applyFog(src, distance, scene.fogColor.xyz + glow);
    }
    return applyFog(src, distance);
}
