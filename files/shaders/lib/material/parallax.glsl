#ifndef LIB_MATERIAL_PARALLAX
#define LIB_MATERIAL_PARALLAX

#define PARALLAX_SCALE 0.04
#define PARALLAX_BIAS -0.02

vec2 getParallaxOffset(vec3 eyeDir, float height)
{
    return vec2(eyeDir.x, eyeDir.y) * ( height * PARALLAX_SCALE + PARALLAX_BIAS );
}

#ifdef PARALLAX_POM
// OpenEden (task 0045): parallax occlusion mapping, a stepped march with a refined hit. The twin of
// components/render/parallaxmath.hpp (Render::Parallax::march, limits, stepCount, distanceFade): change both together.
//
// The including shader defines, before including this file,
//     float pomHeight(vec2 uv, vec2 dx, vec2 dy)
// which reads the height file (1 = the top of the surface) with explicit derivatives. That is the only place that names
// a texture, so a Slang port is this function as an interface method plus a mechanical translation of the rest.
//
// The ray goes down into the surface; at depth d (0 at the surface, 1 the deepest the map can have) its texture
// coordinate is uv - eyeDir.xy / max(eyeDir.z, POM_MIN_VIEW_Z) * d * uvDepth, and it is below the surface once
// d >= 1 - pomHeight(there).
#if defined(PARALLAX_POM_LOW)
const int POM_MIN_STEPS = 8;
const int POM_MAX_STEPS = 12;
const int POM_REFINE_TAPS = 0;
#elif defined(PARALLAX_POM_HIGH)
const int POM_MIN_STEPS = 16;
const int POM_MAX_STEPS = 32;
const int POM_REFINE_TAPS = 4;
#else
const int POM_MIN_STEPS = 10;
const int POM_MAX_STEPS = 20;
const int POM_REFINE_TAPS = 4;
#endif
const float POM_MIN_VIEW_Z = 0.1;
// Render::Parallax::maxSlant, slopeFadeFull and slopeFadeNone.
const float POM_MAX_SLANT = 2.0;
const float POM_SLOPE_FULL = 0.8660254;
const float POM_SLOPE_NONE = 0.7071068;
// Render::Parallax::worldDepthPerDisplacement, maxUvDepth and defaultDisplacementScale.
const float POM_WORLD_DEPTH_PER_DISPLACEMENT = 16.0;
const float POM_MAX_UV_DEPTH = 0.1;
const float POM_DEFAULT_DISPLACEMENT = 0.5;
// The march fades out between these distances (game units), and is skipped beyond the second.
const float POM_FADE_START = 700.0;
const float POM_FADE_END = 1800.0;

float pomSlopeFade(float up)
{
    float t = clamp((up - POM_SLOPE_NONE) / (POM_SLOPE_FULL - POM_SLOPE_NONE), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Game units per unit of texture coordinates at this pixel, from the derivatives of the view space position and of the
// coordinates (call it outside any branch). The depth of the surface is a length, so it is divided by this.
float pomWorldPerUv(vec3 viewPos, vec2 uvDx, vec2 uvDy)
{
    float worldArea = length(cross(dFdx(viewPos), dFdy(viewPos)));
    float uvArea = abs(uvDx.x * uvDy.y - uvDx.y * uvDy.x);
    return sqrt(worldArea / max(uvArea, 1e-12));
}

// Render::Parallax::uvDepthFromWorld.
float pomUvDepth(float scale, float worldPerUv)
{
    return clamp(scale * POM_WORLD_DEPTH_PER_DISPLACEMENT / max(worldPerUv, 1e-6), 0.0, POM_MAX_UV_DEPTH);
}

float pomDistanceFade(float distance)
{
    float t = clamp((distance - POM_FADE_START) / (POM_FADE_END - POM_FADE_START), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

// How far the ray is above the surface at depth d: positive above, zero or less once it is under it.
float pomAbove(vec2 uv, vec2 rayStep, float d, vec2 dx, vec2 dy)
{
    return (1.0 - pomHeight(uv - rayStep * d, dx, dy)) - d;
}

// The texture coordinate offset to add to uv. eyeDir: the unit vector from the surface to the eye in tangent space;
// uvDepth: the depth of the surface in texture coordinates (already faded); pivot: the ray depth that stays in place
// (1 - the map's mean height, Render::Parallax::pivotDepth), so that fading the depth does not slide the whole
// texture; dx and dy: the derivatives of uv.
vec2 pomOffset(vec2 uv, vec3 eyeDir, float uvDepth, float pivot, vec2 dx, vec2 dy)
{
    float viewCos = clamp(abs(eyeDir.z), 0.0, 1.0);
    int steps = int(ceil(mix(float(POM_MIN_STEPS), float(POM_MAX_STEPS), 1.0 - viewCos)));
    vec2 slant = eyeDir.xy / max(eyeDir.z, POM_MIN_VIEW_Z);
    float slantLength = length(slant);
    if (slantLength > POM_MAX_SLANT)
        slant *= POM_MAX_SLANT / slantLength;
    vec2 rayStep = slant * uvDepth;
    float loDepth = 0.0;
    float loAbove = pomAbove(uv, rayStep, 0.0, dx, dy);
    if (loAbove <= 0.0)
        return rayStep * pivot;
    float hiDepth = 1.0;
    float hiAbove = 0.0;
    bool hit = false;
    for (int i = 1; i <= POM_MAX_STEPS; ++i)
    {
        if (i > steps)
            break;
        float depth = float(i) / float(steps);
        float a = pomAbove(uv, rayStep, depth, dx, dy);
        if (a <= 0.0)
        {
            hiDepth = depth;
            hiAbove = a;
            hit = true;
            break;
        }
        loDepth = depth;
        loAbove = a;
    }
    if (!hit)
        return -rayStep * (1.0 - pivot);
    for (int k = 0; k < POM_REFINE_TAPS; ++k)
    {
        float mid = 0.5 * (loDepth + hiDepth);
        float m = pomAbove(uv, rayStep, mid, dx, dy);
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
    return -rayStep * (loDepth + t * (hiDepth - loDepth) - pivot);
}
#endif

#endif
