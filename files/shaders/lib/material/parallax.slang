// The Slang twin of lib/material/parallax.glsl (task 0072): the old single-step parallax offset and, with PARALLAX_POM,
// parallax occlusion mapping (task 0045). The C++ twin is components/render/parallaxmath.hpp; change all three
// together (a unit test reads the constants back from both shader files, the quality rows in the order low, high,
// medium). As in the GLSL, the includer defines pomHeight(float2 uv, float lod) and pomTextureSize() first.
#ifndef LIB_MATERIAL_PARALLAX_SLANG
#define LIB_MATERIAL_PARALLAX_SLANG

#define PARALLAX_SCALE 0.04
#define PARALLAX_BIAS -0.02

float2 getParallaxOffset(float3 eyeDir, float height)
{
    return float2(eyeDir.x, eyeDir.y) * (height * PARALLAX_SCALE + PARALLAX_BIAS);
}

#ifdef PARALLAX_POM
#if defined(PARALLAX_POM_LOW)
static const int POM_MIN_STEPS = 8;
static const int POM_MAX_STEPS = 12;
static const int POM_REFINE_TAPS = 0;
#elif defined(PARALLAX_POM_HIGH)
static const int POM_MIN_STEPS = 16;
static const int POM_MAX_STEPS = 48;
static const int POM_REFINE_TAPS = 4;
#else
static const int POM_MIN_STEPS = 10;
static const int POM_MAX_STEPS = 20;
static const int POM_REFINE_TAPS = 4;
#endif
static const float POM_MIN_VIEW_Z = 0.1;
static const float POM_MAX_SLANT = 2.0;
static const float POM_SLOPE_FULL = 0.8660254;
static const float POM_SLOPE_NONE = 0.7071068;
static const float POM_WORLD_DEPTH_PER_DISPLACEMENT = 32.0;
static const float POM_MAX_UV_DEPTH = 0.1;
static const float POM_DEFAULT_DISPLACEMENT = 0.5;
static const float POM_FADE_START = 700.0;
static const float POM_FADE_END = 1800.0;

float pomSlopeFade(float up)
{
    float t = clamp((up - POM_SLOPE_NONE) / (POM_SLOPE_FULL - POM_SLOPE_NONE), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float pomWorldPerUv(float3 viewPos, float2 uvDx, float2 uvDy)
{
    float worldArea = length(cross(ddx(viewPos), ddy(viewPos)));
    float uvArea = abs(uvDx.x * uvDy.y - uvDx.y * uvDy.x);
    return sqrt(worldArea / max(uvArea, 1e-12));
}

float pomWorldPerUvPlanar(float3 worldPos, float2 uvDx, float2 uvDy)
{
    float2 wx = ddx(worldPos).xy;
    float2 wy = ddy(worldPos).xy;
    float worldArea = abs(wx.x * wy.y - wx.y * wy.x);
    float uvArea = abs(uvDx.x * uvDy.y - uvDx.y * uvDy.x);
    return sqrt(worldArea / max(uvArea, 1e-12));
}

float pomUvDepth(float scale, float worldPerUv)
{
    return clamp(scale * POM_WORLD_DEPTH_PER_DISPLACEMENT / max(worldPerUv, 1e-6), 0.0, POM_MAX_UV_DEPTH);
}

float pomDepthFromField(float field, float worldPerUv)
{
    if (field >= 1.0)
        return pomUvDepth(field - 1.0, worldPerUv);
    return field > 0.0 ? field : pomUvDepth(POM_DEFAULT_DISPLACEMENT, worldPerUv);
}

float pomDistanceFade(float distance)
{
    float t = clamp((distance - POM_FADE_START) / (POM_FADE_END - POM_FADE_START), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

static const float POM_BLUR_BIAS = 1.0;
static const float POM_MIN_LOD = 1.5;

float pomBlurLod(float2 dx, float2 dy)
{
    float2 sx = dx * pomTextureSize();
    float2 sy = dy * pomTextureSize();
    float footprint = max(dot(sx, sx), dot(sy, sy));
    return max(0.5 * log2(max(footprint, 1e-8)) + POM_BLUR_BIAS, POM_MIN_LOD);
}

float pomAbove(float2 origin, float2 rayStep, float d, float lod)
{
    return (1.0 - pomHeight(origin - rayStep * d, lod)) - d;
}

struct PomResult
{
    float2 offset;
    float2 slope;
    float cavity;
};

static const float POM_STEEP_FROM = 1.0;
static const float POM_STEEP_TO = 4.0;
static const float POM_STEEP_KEEP = 0.25;
static const float POM_MAX_NORMAL_SLOPE = 1.5;
static const float POM_CAVITY_SHADE = 0.3;

PomResult pomMarch(float2 uv, float3 eyeDir, float uvDepth, float pivot, float2 dx, float2 dy)
{
    PomResult result;
    result.offset = float2(0.0);
    result.slope = float2(0.0);
    result.cavity = 0.0;
    float eyeLength = length(eyeDir);
    if (!(eyeLength > 0.0))
        return result;
    eyeDir /= eyeLength;
    float viewFade = clamp(eyeDir.z / POM_MIN_VIEW_Z, 0.0, 1.0);
    if (!(viewFade > 0.0))
        return result;
    float lod = pomBlurLod(dx, dy);
    float2 slant = eyeDir.xy / max(eyeDir.z, 1e-4);
    float slantLength = length(slant);
    if (slantLength > POM_MAX_SLANT)
        slant *= POM_MAX_SLANT / slantLength;
    float2 rayStep = slant * uvDepth * viewFade;
    float viewSteps = ceil(lerp(float(POM_MIN_STEPS), float(POM_MAX_STEPS), 1.0 - eyeDir.z));
    float travel = length(rayStep * pomTextureSize()) / exp2(lod);
    int steps = int(min(max(viewSteps, ceil(travel)), float(POM_MAX_STEPS)));
    float2 origin = uv + rayStep * pivot;
    float loDepth = 0.0;
    float loAbove = pomAbove(origin, rayStep, 0.0, lod);
    float hitDepth = 0.0;
    if (loAbove > 0.0)
    {
        float hiDepth = 1.0;
        float hiAbove = 0.0;
        for (int i = 1; i <= steps; ++i)
        {
            float depth = float(i) / float(steps);
            float a = pomAbove(origin, rayStep, depth, lod);
            if (a <= 0.0)
            {
                hiDepth = depth;
                hiAbove = a;
                break;
            }
            loDepth = depth;
            loAbove = a;
        }
        for (int k = 0; k < POM_REFINE_TAPS; ++k)
        {
            float mid = 0.5 * (loDepth + hiDepth);
            float m = pomAbove(origin, rayStep, mid, lod);
            if (m <= 0.0)
            {
                hiDepth = mid;
                hiAbove = m;
            }
            else
            {
                loDepth = mid;
                loAbove = m;
            }
        }
        float span = loAbove - hiAbove;
        float t = span > 0.0 ? clamp(loAbove / span, 0.0, 1.0) : 1.0;
        hitDepth = loDepth + t * (hiDepth - loDepth);
    }
    float2 hitUv = origin - rayStep * hitDepth;
    float2 e = exp2(lod) / pomTextureSize();
    float gx = (pomHeight(hitUv + float2(e.x, 0.0), lod) - pomHeight(hitUv - float2(e.x, 0.0), lod)) / (2.0 * e.x);
    float gy = (pomHeight(hitUv + float2(0.0, e.y), lod) - pomHeight(hitUv - float2(0.0, e.y), lod)) / (2.0 * e.y);
    float2 slope = -float2(gx, gy) * uvDepth;
    float steep = length(slope);
    if (steep > POM_MAX_NORMAL_SLOPE)
        slope *= POM_MAX_NORMAL_SLOPE / steep;
    float keep = lerp(1.0, POM_STEEP_KEEP, smoothstep(POM_STEEP_FROM, POM_STEEP_TO, steep));
    result.offset = (hitUv - uv) * keep;
    result.slope = slope;
    result.cavity = pivot < 1.0 ? clamp((hitDepth - pivot) / (1.0 - pivot), 0.0, 1.0) : 0.0;
    return result;
}

// The offset only (the terrain's use).
float2 pomOffset(float2 uv, float3 eyeDir, float uvDepth, float pivot, float2 dx, float2 dy)
{
    return pomMarch(uv, eyeDir, uvDepth, pivot, dx, dy).offset;
}
#endif

#endif
