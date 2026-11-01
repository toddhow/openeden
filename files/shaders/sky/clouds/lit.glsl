// OpenEden (task 0038): the lit look of the sky's clouds. The C++ twin is components/render/cloudmath.hpp (the
// transmittance, forward phase, silver lining, underside shade, strength and thickness functions); change them
// together.
const float cloudBrightnessPivot = 0.55;
const float cloudBrightnessGain = 2.0;
const float cloudExtinction = 4.0;
const float cloudMaxShade = 0.65;
const float cloudForwardG = 0.7;
const float cloudSilverGain = 1.6;
const float cloudLightFrom = 0.08;
const float cloudLightTo = 0.5;

// A brighter pixel of the picture is thinner cloud; an opaque one thicker.
float cloudThickness(vec3 rgb, float alpha)
{
    float lum = clamp(dot(rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    return clamp(clamp(alpha, 0.0, 1.0) * (1.0 + cloudBrightnessGain * (cloudBrightnessPivot - lum)), 0.0, 1.0);
}

float cloudTransmittance(float thick)
{
    return exp(-cloudExtinction * clamp(thick, 0.0, 1.0));
}

float cloudForwardPhase(float cosTheta)
{
    float g = cloudForwardG;
    float c = clamp(cosTheta, -1.0, 1.0);
    float hg = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * c, 1.5);
    float peak = (1.0 - g * g) / pow((1.0 - g) * (1.0 - g), 1.5);
    return hg / peak;
}

// How much of the lit look applies: it needs direct light (the night's mirrored light is dim) and a weather with a sun.
float cloudStrength(float lightLuminance, float weatherShare)
{
    return smoothstep(cloudLightFrom, cloudLightTo, lightLuminance) * clamp(weatherShare, 0.0, 1.0);
}

// The colour factor: the dark underside, and the silver lining in the sun's colour.
vec3 cloudLitFactor(float thick, float cosTheta, float sunUp, float amount, vec3 sunTint)
{
    float T = cloudTransmittance(thick);
    float shade = cloudMaxShade * (1.0 - T) * (0.4 + 0.6 * clamp(sunUp, 0.0, 1.0));
    float silver = cloudSilverGain * cloudForwardPhase(cosTheta) * T;
    vec3 lit = vec3(1.0 - shade) + silver * sunTint;
    return vec3(1.0) + clamp(amount, 0.0, 1.0) * (lit - vec3(1.0));
}
