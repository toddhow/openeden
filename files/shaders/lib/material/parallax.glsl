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
//     float pomHeight(vec2 uv, float lod)
//     vec2 pomTextureSize()
// which read the height file (1 = the top of the surface) at a mip level, and its size in texels. That is the only place that names
// a texture, so a Slang port is this function as an interface method plus a mechanical translation of the rest.
//
// The ray goes down into the surface. It passes through the pixel's own coordinate at depth `pivot` (the map's mean
// depth), so at depth d (0 at the top of the map, 1 the deepest it can have) its texture coordinate is
// uv + rayStep * (pivot - d), with rayStep the view's limited slant times uvDepth. It is below the surface once
// d >= 1 - pomHeight(there), and the colours are read at that same coordinate.
#if defined(PARALLAX_POM_LOW)
const int POM_MIN_STEPS = 8;
const int POM_MAX_STEPS = 12;
const int POM_REFINE_TAPS = 0;
#elif defined(PARALLAX_POM_HIGH)
const int POM_MIN_STEPS = 16;
const int POM_MAX_STEPS = 48;
const int POM_REFINE_TAPS = 4;
#else
const int POM_MIN_STEPS = 10;
const int POM_MAX_STEPS = 20;
const int POM_REFINE_TAPS = 4;
#endif
// Render::Parallax::minViewZ: the ray fades to nothing below this, at the silhouette.
const float POM_MIN_VIEW_Z = 0.1;
// Render::Parallax::maxSlant, slopeFadeFull and slopeFadeNone.
const float POM_MAX_SLANT = 2.0;
const float POM_SLOPE_FULL = 0.8660254;
const float POM_SLOPE_NONE = 0.7071068;
// Render::Parallax::worldDepthPerDisplacement, maxUvDepth and defaultDisplacementScale.
const float POM_WORLD_DEPTH_PER_DISPLACEMENT = 32.0;
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
// coordinates (call it outside any branch). The depth of the surface is a length, so it is divided by this. It changes
// from triangle to triangle with the mapping's density, so it is only the fall-back for a material with no number for
// its whole mesh (Render::Parallax::meshWorldPerUv).
float pomWorldPerUv(vec3 viewPos, vec2 uvDx, vec2 uvDy)
{
    float worldArea = length(cross(dFdx(viewPos), dFdy(viewPos)));
    float uvArea = abs(uvDx.x * uvDy.y - uvDx.y * uvDy.x);
    return sqrt(worldArea / max(uvArea, 1e-12));
}

// On terrain the texture coordinates are a projection from above: the area from the world's x and y alone, which does
// not change with the slope, so neighbouring triangles agree. worldPos: the position in world orientation.
float pomWorldPerUvPlanar(vec3 worldPos, vec2 uvDx, vec2 uvDy)
{
    vec2 wx = dFdx(worldPos).xy;
    vec2 wy = dFdy(worldPos).xy;
    float worldArea = abs(wx.x * wy.y - wx.y * wy.x);
    float uvArea = abs(uvDx.x * uvDy.y - uvDx.y * uvDy.x);
    return sqrt(worldArea / max(uvArea, 1e-12));
}

// Render::Parallax::uvDepthFromWorld.
float pomUvDepth(float scale, float worldPerUv)
{
    return clamp(scale * POM_WORLD_DEPTH_PER_DISPLACEMENT / max(worldPerUv, 1e-6), 0.0, POM_MAX_UV_DEPTH);
}

// The material's depth field: Render::Parallax::meshDepthField and depthFromField. A field of 1 or more is 1 + a depth
// scale for a mesh with no number of its own, a smaller one above 0 is the mesh's depth in texture coordinates, and 0
// means no material value. worldPerUv: the derivatives' (the fall-back).
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

// The mip level the height file is read at: the pixel's own footprint plus a bias, and never sharper than a floor, so
// that a mortar joint is a slope and not a cliff (a cliff makes the ray, and the texture with it, jump across the joint
// and stretch into strips). Render::Parallax::blurLod. The includer defines pomTextureSize().
const float POM_BLUR_BIAS = 1.0;
const float POM_MIN_LOD = 1.5;

float pomBlurLod(vec2 dx, vec2 dy)
{
    vec2 sx = dx * pomTextureSize();
    vec2 sy = dy * pomTextureSize();
    float footprint = max(dot(sx, sx), dot(sy, sy));
    return max(0.5 * log2(max(footprint, 1e-8)) + POM_BLUR_BIAS, POM_MIN_LOD);
}

// How far the ray is above the surface at depth d: positive above, zero or less once it is under it. origin: the
// ray's coordinate at depth 0.
float pomAbove(vec2 origin, vec2 rayStep, float d, float lod)
{
    return (1.0 - pomHeight(origin - rayStep * d, lod)) - d;
}

// What a march finds. offset: the texture coordinate offset to add to uv. slope: the tangent space x and y of the
// normal of the height field where the ray ended (zero where flat), for shading. cavity: 0 at the mean surface to 1 at
// the deepest, for a little occlusion in recesses.
struct PomResult
{
    vec2 offset;
    vec2 slope;
    float cavity;
};

// Where the local slope of the height field (world units of depth per world unit across) is steep, the offset is
// pulled back toward the unshifted coordinates: a texture read on the wall of a cliff is a stretched strip.
// Render::Parallax::steepSoftening.
const float POM_STEEP_FROM = 1.0;
const float POM_STEEP_TO = 4.0;
const float POM_STEEP_KEEP = 0.25;
const float POM_MAX_NORMAL_SLOPE = 1.5;
// How much a recess at the deepest point is darkened (Render::Parallax::cavityShade).
const float POM_CAVITY_SHADE = 0.3;

// eyeDir: the unit vector from the surface to the eye in tangent space; uvDepth: the depth of the surface in texture
// coordinates (already faded); pivot: the ray depth that stays in place (1 - the map's mean height,
// Render::Parallax::pivotDepth), so that fading the depth does not slide the whole texture; dx and dy: the derivatives
// of uv.
PomResult pomMarch(vec2 uv, vec3 eyeDir, float uvDepth, float pivot, vec2 dx, vec2 dy)
{
    PomResult result;
    result.offset = vec2(0.0);
    result.slope = vec2(0.0);
    result.cavity = 0.0;
    // A degenerate tangent frame gives no direction: no offset.
    float eyeLength = length(eyeDir);
    if (!(eyeLength > 0.0))
        return result;
    eyeDir /= eyeLength;
    // Render::Parallax::viewFade: nothing for a view from behind, and zero at the silhouette.
    float viewFade = clamp(eyeDir.z / POM_MIN_VIEW_Z, 0.0, 1.0);
    if (!(viewFade > 0.0))
        return result;
    float lod = pomBlurLod(dx, dy);
    vec2 slant = eyeDir.xy / max(eyeDir.z, 1e-4);
    float slantLength = length(slant);
    if (slantLength > POM_MAX_SLANT)
        slant *= POM_MAX_SLANT / slantLength;
    vec2 rayStep = slant * uvDepth * viewFade;
    // Render::Parallax::travelSteps: by the view's angle, and at least one step per blurred texel crossed.
    float viewSteps = ceil(mix(float(POM_MIN_STEPS), float(POM_MAX_STEPS), 1.0 - eyeDir.z));
    float travel = length(rayStep * pomTextureSize()) / exp2(lod);
    int steps = int(min(max(viewSteps, ceil(travel)), float(POM_MAX_STEPS)));
    // Render::Parallax::marchOffset: the ray passes through uv at depth pivot.
    vec2 origin = uv + rayStep * pivot;
    float loDepth = 0.0;
    float loAbove = pomAbove(origin, rayStep, 0.0, lod);
    float hitDepth = 0.0;
    if (loAbove > 0.0)
    {
        // At depth 1 the ray is at or under any height, so the last step always ends the search.
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
    vec2 hitUv = origin - rayStep * hitDepth;
    // The slope of the height field at the hit by central differences, one blurred texel apart. In world terms
    // (height scaled by the depth, distance by the tile) the gradient per uv unit is multiplied by uvDepth.
    vec2 e = exp2(lod) / pomTextureSize();
    float gx = (pomHeight(hitUv + vec2(e.x, 0.0), lod) - pomHeight(hitUv - vec2(e.x, 0.0), lod)) / (2.0 * e.x);
    float gy = (pomHeight(hitUv + vec2(0.0, e.y), lod) - pomHeight(hitUv - vec2(0.0, e.y), lod)) / (2.0 * e.y);
    vec2 slope = -vec2(gx, gy) * uvDepth;
    float steep = length(slope);
    if (steep > POM_MAX_NORMAL_SLOPE)
        slope *= POM_MAX_NORMAL_SLOPE / steep;
    float keep = mix(1.0, POM_STEEP_KEEP, smoothstep(POM_STEEP_FROM, POM_STEEP_TO, steep));
    result.offset = (hitUv - uv) * keep;
    result.slope = slope;
    // Render::Parallax::cavityFromDepth.
    result.cavity = pivot < 1.0 ? clamp((hitDepth - pivot) / (1.0 - pivot), 0.0, 1.0) : 0.0;
    return result;
}

// The offset only (the terrain's use).
vec2 pomOffset(vec2 uv, vec3 eyeDir, float uvDepth, float pivot, vec2 dx, vec2 dy)
{
    return pomMarch(uv, eyeDir, uvDepth, pivot, dx, dy).offset;
}
#endif

#endif
