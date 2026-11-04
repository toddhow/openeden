// The Slang twin of sky/clouds/lit.glsl (task 0072): the lit look of the sky's clouds (task 0038). The C++ twin is
// components/render/cloudmath.hpp; change all three together.
#ifndef SKY_CLOUDS_LIT_SLANG
#define SKY_CLOUDS_LIT_SLANG

static const float cloudBrightnessPivot = 0.55;
static const float cloudBrightnessGain = 2.0;
static const float cloudExtinction = 4.0;
static const float cloudMaxShade = 0.65;
static const float cloudForwardG = 0.7;
static const float cloudSilverGain = 1.6;
static const float cloudLightFrom = 0.08;
static const float cloudLightTo = 0.5;

float cloudThickness(float3 rgb, float alpha)
{
    float lum = clamp(dot(rgb, float3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
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

float cloudStrength(float lightLuminance, float weatherShare)
{
    return smoothstep(cloudLightFrom, cloudLightTo, lightLuminance) * clamp(weatherShare, 0.0, 1.0);
}

float3 cloudLitFactor(float thick, float cosTheta, float sunUp, float amount, float3 sunTint)
{
    float T = cloudTransmittance(thick);
    float shade = cloudMaxShade * (1.0 - T) * (0.4 + 0.6 * clamp(sunUp, 0.0, 1.0));
    float silver = cloudSilverGain * cloudForwardPhase(cosTheta) * T;
    float3 lit = float3(1.0 - shade) + silver * sunTint;
    return float3(1.0) + clamp(amount, 0.0, 1.0) * (lit - float3(1.0));
}

#endif
