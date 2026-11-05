// The volumetric clouds' compute passes, in Slang (task 0042; decision 0009: new shaders are written in Slang).
//
// The clouds are a layer of space between two heights filled with a 3D density built from noise. For each pixel of a
// small image the march walks a ray from the camera through the layer, adds up the sunlight each step scatters towards
// the eye and dims what is behind. The result is blended with the previous frame and drawn by the sky cloud dome.
//   noiseShapeMain, noiseDetailMain, noiseCurlMain   build the three noise textures once, from a fixed seed.
//   marchMain      the ray march, at a fraction of the screen size (rgb: light towards the eye, a: transmittance).
//   resolveMain    the full size picture: this frame march (bilinear, jitter aware) blended with the previous
//                  frame picture, found through the camera rotation, clamped to the march neighbourhood.
//   copyMain       keeps the resolved picture as the next frame history.
//
// The twins of the maths are components/render/cloudvolumemath.hpp (heightGradient, cloudDensity, the phase and
// multiple scattering functions, layerSpan, the hash and noise functions) and components/render/cloudvolumegpu.hpp (the
// parameter block). Change them together; a test reads the constants back from this file.
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

// lib/sets.glsl
static const int VIEW_SET = 0;
static const int COMPUTE_SET = 3;

// lib/view/data.glsl (std140). The whole struct is declared so that the block has the size the C++ side uploads.
struct Scene
{
    float4x4 invProjection;
    float4x4 projection;
    float4x4 invView;
    float4 cascadeSplits;
    float4x4 viewToCascadeProj[4];
    float4 cascadeFrustumWidths;
    float4 lightDiffuse;
    float4 lightSpecular;
    float4 lightViewPos;
    float4 ambient;
    float4 fogColor;
    float4 skyColor; // .a = glareView
    float2 resolution;
    float fogStart;
    float fogScale;
    float time;
    float zNear;
    float zFar;
    float rainIntensity;
    float4 groundcoverPlayer;
    float4 groundcoverParams;
};
// lib/view/bindings.glsl: VIEW_SCENE_BINDING = 0
[[vk::binding(0, 0)]] ConstantBuffer<Scene> scene;

// components/render/cloudvolumegpu.hpp (std140)
struct CloudParams
{
    float4 sizes; // x, y: the march image size in texels; z, w: the resolved image size
    float4 layer; // x: base (game units), y: top, z: the farthest a ray is followed, w: coverage
    float4 density; // x: density scale, y, z: wind offset in metres (wrapped by the tile), w: steps
    float4 march; // x: sun light steps, y, z: this frame jitter (0 to 1), w: history weight (0: none)
    float4 look; // x: erosion, y: sun gain, z: ambient gain, w: haze length in metres
    float4 debug; // x: debug view (0 none), y: frame counter, z: share of the sky the volume draws, w: shadow strength
    float4x4 toPreviousClip; // this frame view direction to the previous frame clip space (rotation only)
    float4 curve; // x: planet radius in metres (0: the flat layer), y: 1 with the large scale variety
};
// The compute set (lib/sets.glsl: COMPUTE_SET = 3).
[[vk::binding(0, 3)]] [format("rgba8")] RWTexture3D<float4> shapeStore;
[[vk::binding(1, 3)]] [format("rgba8")] RWTexture3D<float4> detailStore;
[[vk::binding(2, 3)]] [format("rgba8")] RWTexture2D<float4> curlStore;
[[vk::binding(3, 3)]] Sampler3D shapeTex;
[[vk::binding(4, 3)]] Sampler3D detailTex;
[[vk::binding(5, 3)]] Sampler2D curlTex;
[[vk::binding(6, 3)]] [format("rgba16f")] RWTexture2D<float4> marchStore;
[[vk::binding(7, 3)]] [format("rgba16f")] RWTexture2D<float4> resolveStore;
[[vk::binding(8, 3)]] Sampler2D historyTex;
[[vk::binding(9, 3)]] [format("rgba16f")] RWTexture2D<float4> historyStore;
[[vk::binding(10, 3)]] ConstantBuffer<CloudParams> cloud;
// The ground shadow map: shadowMapSize by shadowMapSize texels of transmittance (red), and two more texels in one column
// to the right that hold the map's placement (lib/view/volumecloudshadow.glsl reads them).
[[vk::binding(11, 3)]] [format("rgba32f")] RWTexture2D<float4> shadowStore;
// The blue noise (components/render/bluenoise.hpp): 64 by 64, one byte a texel, repeating, read at the texel's centre.
[[vk::binding(12, 3)]] Sampler2D blueTex;

static const float pi = 3.14159265358979;

// ---- cloudvolumemath.hpp constants ----
static const float unitsPerMeter = 69.99125109;
static const float shapeTileMeters = 12000.0;
static const float detailTileMeters = 800.0;
static const float detailFineTileMeters = 300.0;
static const float detailFineWeight = 0.4;
static const float detailContrast = 1.6;
static const uint blueNoiseSize = 64;
static const float goldenRatioFraction = 0.6180339887;
static const uint blueNoiseFrameMask = 1023;
static const float sunPhaseScale = 6.28318530718;
static const uint shapeSize = 128;
static const uint detailSize = 32;
static const uint curlSize = 128;
static const uint noiseSeed = 0x0E0E1042u;
static const int shapePerlinPeriod = 4;
static const int curlPeriod = 8;
static const float forwardAnisotropy = 0.8;
static const float backAnisotropy = -0.3;
static const float forwardWeight = 0.5;
static const float multiScatterExtinction = 0.5;
static const float multiScatterEnergy = 0.5;
static const float multiScatterPhase = 0.5;
static const float extinctionPerMeter = 0.045;
static const float baseAmbientShare = 0.35;
static const float lightRayMeters = 600.0;
static const float minTransmittance = 0.01;
// How far the curl noise bends the detail lookup (a chosen value, set by looking at frames).
static const float curlStrength = 0.04;
// The length the powder term looks at, and how dark it can make a very thin edge (chosen by looking at frames).
static const float powderLengthMeters = 100.0;
static const float powderFloor = 0.35;
static const uint shadowMapSize = 256;
static const float shadowHalfExtentMeters = 20000.0;
static const float shadowSunUp = 0.05;
static const int shadowSteps = 12;
// cloudcurvemath.hpp: the far fade of the curved layer, and the large scale variety.
static const float farFadeStart = 0.6;
static const float largeTileMeters = 84000.0;
static const float varietyWarpMeters = 6000.0;
static const float varietyCoverageAmount = 0.2;

// ---------------------------------------------------------------------------------------------------------------------
// The noise (cloudvolumemath.hpp: hashUint, hashCell, hashToUnit, worley, perlin, perlinFbm, perlinWorley)
// ---------------------------------------------------------------------------------------------------------------------

uint hashUint(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint wrapCell(int v, int period)
{
    int m = v % period;
    return uint(m < 0 ? m + period : m);
}

uint hashCell(int x, int y, int z, int period, uint seed)
{
    uint h = hashUint(wrapCell(x, period) + hashUint(seed));
    h = hashUint(h ^ (wrapCell(y, period) * 0x9E3779B9u));
    h = hashUint(h ^ (wrapCell(z, period) * 0x85EBCA6Bu));
    return h;
}

float hashToUnit(uint h)
{
    return float(h >> 8u) * (1.0 / 16777216.0);
}

float worley(float3 p, int period, uint seed)
{
    int3 c = int3(floor(p));
    float best = 1e9;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                int3 i = c + int3(dx, dy, dz);
                uint h = hashCell(i.x, i.y, i.z, period, seed);
                float3 feature = float3(i) + float3(hashToUnit(h), hashToUnit(hashUint(h + 1u)), hashToUnit(hashUint(h + 2u)));
                float3 o = feature - p;
                best = min(best, dot(o, o));
            }
    return saturate(1.0 - sqrt(best));
}

static const float3 gradients[12] = { float3(1, 1, 0), float3(-1, 1, 0), float3(1, -1, 0), float3(-1, -1, 0),
    float3(1, 0, 1), float3(-1, 0, 1), float3(1, 0, -1), float3(-1, 0, -1), float3(0, 1, 1), float3(0, -1, 1),
    float3(0, 1, -1), float3(0, -1, -1) };

float perlin(float3 p, int period, uint seed)
{
    int3 p0 = int3(floor(p));
    float3 f = p - float3(p0);
    float3 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float corner[2][2][2];
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
            {
                float3 g = gradients[hashCell(p0.x + i, p0.y + j, p0.z + k, period, seed) % 12u];
                corner[k][j][i] = dot(g, f - float3(i, j, k));
            }
    float x00 = lerp(corner[0][0][0], corner[0][0][1], w.x);
    float x10 = lerp(corner[0][1][0], corner[0][1][1], w.x);
    float x01 = lerp(corner[1][0][0], corner[1][0][1], w.x);
    float x11 = lerp(corner[1][1][0], corner[1][1][1], w.x);
    float value = lerp(lerp(x00, x10, w.y), lerp(x01, x11, w.y), w.z);
    return saturate(value * 0.5 + 0.5);
}

float perlinFbm(float3 p, int period, uint seed, int octaves)
{
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    float scale = 1.0;
    int scaleInt = 1;
    for (int i = 0; i < octaves; ++i)
    {
        sum += weight * perlin(p * scale, period * scaleInt, seed + uint(i));
        total += weight;
        weight *= 0.5;
        scale *= 2.0;
        scaleInt *= 2;
    }
    return total > 0.0 ? sum / total : 0.0;
}

float remap(float v, float a0, float a1, float b0, float b1)
{
    if (a1 == a0)
        return b0;
    return b0 + (v - a0) / (a1 - a0) * (b1 - b0);
}

float perlinWorley(float perlinValue, float worleyValue)
{
    return saturate(remap(perlinValue, worleyValue, 1.0, 0.0, 1.0));
}

float worleyFbm(float low, float mid, float high)
{
    return low * 0.625 + mid * 0.25 + high * 0.125;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void noiseShapeMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= uint3(shapeSize, shapeSize, shapeSize)))
        return;
    float3 uvw = (float3(id) + 0.5) / float(shapeSize);
    float w0 = worley(uvw * 4.0, 4, noiseSeed + 10u);
    float w1 = worley(uvw * 8.0, 8, noiseSeed + 11u);
    float w2 = worley(uvw * 16.0, 16, noiseSeed + 12u);
    float p = perlinFbm(uvw * float(shapePerlinPeriod), shapePerlinPeriod, noiseSeed, 5);
    shapeStore[id] = float4(perlinWorley(p, w0), w0, w1, w2);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void noiseDetailMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= uint3(detailSize, detailSize, detailSize)))
        return;
    float3 uvw = (float3(id) + 0.5) / float(detailSize);
    float a = worley(uvw * 2.0, 2, noiseSeed + 20u);
    float b = worley(uvw * 4.0, 4, noiseSeed + 21u);
    float c = worley(uvw * 8.0, 8, noiseSeed + 22u);
    detailStore[id] = float4(a, b, c, worleyFbm(a, b, c));
}

// A 2D curl noise: the curl of a Perlin fBm potential, to bend the detail lookup into wisps.
float curlPotential(float2 uv)
{
    return perlinFbm(float3(uv * float(curlPeriod), 0.5), curlPeriod, noiseSeed + 30u, 3);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void noiseCurlMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(curlSize, curlSize)))
        return;
    float2 uv = (float2(id.xy) + 0.5) / float(curlSize);
    float e = 1.0 / float(curlSize);
    float dx = curlPotential(uv + float2(e, 0.0)) - curlPotential(uv - float2(e, 0.0));
    float dy = curlPotential(uv + float2(0.0, e)) - curlPotential(uv - float2(0.0, e));
    float2 curl = float2(dy, -dx) * (0.5 / e) / float(curlPeriod);
    curlStore[id.xy] = float4(clamp(curl, -1.0, 1.0) * 0.5 + 0.5, 0.0, 1.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Density and light (cloudvolumemath.hpp: heightGradient, shapeBase, cloudDensity, phase functions)
// ---------------------------------------------------------------------------------------------------------------------

float smoothStep(float edge0, float edge1, float x)
{
    float t = saturate((x - edge0) / max(edge1 - edge0, 1e-6));
    return t * t * (3.0 - 2.0 * t);
}

float heightGradient(float fraction)
{
    return smoothStep(0.0, 0.1, fraction) * (1.0 - smoothStep(0.6, 1.0, fraction));
}

float cloudDensity(float shape, float detail, float coverage, float gradient, float erosion)
{
    if (coverage <= 0.0 || gradient <= 0.0)
        return 0.0;
    float base = saturate(shape) * saturate(gradient);
    float d = saturate(remap(base, 1.0 - saturate(coverage), 1.0, 0.0, 1.0));
    return saturate(remap(d, saturate(detail) * saturate(erosion), 1.0, 0.0, 1.0));
}

float henyeyGreenstein(float cosTheta, float g)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = clamp(g, -0.99, 0.99);
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return (1.0 - k * k) / (4.0 * pi * denominator * sqrt(denominator));
}

float dualLobePhase(float cosTheta, float gForward, float gBack)
{
    return forwardWeight * henyeyGreenstein(cosTheta, gForward) + (1.0 - forwardWeight) * henyeyGreenstein(cosTheta, gBack);
}

float multiScatter(float opticalDepthToSun, float cosTheta, int octaves)
{
    float sum = 0.0;
    float a = 1.0;
    float b = 1.0;
    float c = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += b * dualLobePhase(cosTheta, forwardAnisotropy * c, backAnisotropy * c) * exp(-opticalDepthToSun * a);
        a *= multiScatterExtinction;
        b *= multiScatterEnergy;
        c *= multiScatterPhase;
    }
    return sum;
}

// The shift of a ray's samples inside one step (bluenoise.hpp: ditherOffset): the blue noise texel of the march texel
// plus the golden ratio sequence over the frames, so the history blend converges on an even spread.
float ditherOffset(float2 pixel)
{
    float2 uv = (float2(uint2(pixel) & uint2(blueNoiseSize - 1, blueNoiseSize - 1)) + 0.5) / float(blueNoiseSize);
    uint texel = uint(blueTex.SampleLevel(uv, 0.0).r * 255.0 + 0.5);
    float shifted = (float(texel) + 0.5) / 256.0 + float(uint(cloud.debug.y) & blueNoiseFrameMask) * goldenRatioFraction;
    float f = frac(shifted);
    return f >= 1.0 ? 0.0 : f;
}

// How densityAt treats the detail noise: `detailBound` takes it as 0, which gives the most the density can be there (so a
// zero means the space is truly empty), `detailMean` as its mean 0.5 (the cheap form the sun light march uses) and
// `detailFull` reads it and the curl.
static const int detailBound = 0;
static const int detailMean = 1;
static const int detailFull = 2;

// The large scale variety at a place (cloudcurvemath.hpp: varietyWarp, varietyCoverage): x, y the sideways move of the
// shape lookup in metres, z the coverage there. One lookup of the shape noise over a tile of `largeTileMeters` for the
// warp and one, shifted, for the coverage; without the variety (cloud.curve.y is 0) the move is 0 and the coverage the
// weather's own, and nothing is read.
float4 varietyAt(float2 xyMeters)
{
    float coverage = cloud.layer.w;
    if (cloud.curve.y < 0.5)
        return float4(0.0, 0.0, coverage, 0.0);
    float2 uv = frac((xyMeters - cloud.density.yz) / largeTileMeters);
    float4 a = shapeTex.SampleLevel(float3(uv, 0.5), 0.0);
    float4 b = shapeTex.SampleLevel(float3(frac(uv + float2(0.41, 0.17)), 0.5), 0.0);
    float2 warp = (clamp(a.gb, 0.0, 1.0) - 0.5) * 2.0 * varietyWarpMeters;
    coverage = saturate(coverage + (clamp(b.g, 0.0, 1.0) - 0.5) * 2.0 * varietyCoverageAmount);
    return float4(warp, coverage, 0.0);
}

// The density at `positionMeters` (world, metres), whose height fraction in the layer is `f`. The wind carries the
// clouds along windDirection, so the noise is read where the point came from. `variety` is varietyAt of the place.
float densityAt(float3 positionMeters, float f, int detailMode, float4 variety)
{
    float3 wind = float3(cloud.density.y, cloud.density.z, 0.0);
    float3 shapePoint = positionMeters - wind;
    if (cloud.curve.y > 0.5)
        shapePoint.xy += variety.xy;
    float3 uvw = frac(shapePoint / shapeTileMeters);
    float4 s = shapeTex.SampleLevel(uvw, 0.0);
    float shape = saturate(remap(s.r, worleyFbm(s.g, s.b, s.a) - 1.0, 1.0, 0.0, 1.0));
    float detail = detailMode == detailBound ? 0.0 : 0.5;
    if (detailMode == detailFull)
    {
        float2 curl = curlTex.SampleLevel(uvw.xy, 0.0).xy * 2.0 - 1.0;
        float3 detailUvw = frac((positionMeters - wind) / detailTileMeters + float3(curl * curlStrength * (1.0 - f), 0.0));
        float coarse = detailTex.SampleLevel(detailUvw, 0.0).a;
        // The finer lookup is shifted so that it does not read the same cell (cloudvolumemath.hpp: combineDetail).
        float3 fineUvw = frac((positionMeters - wind) / detailFineTileMeters + float3(0.37, 0.11, 0.73)
            + float3(curl * curlStrength * (1.0 - f), 0.0));
        float fine = detailTex.SampleLevel(fineUvw, 0.0).a;
        float mixed = coarse * (1.0 - detailFineWeight) + fine * detailFineWeight;
        detail = saturate(0.5 + (mixed - 0.5) * detailContrast);
    }
    return cloudDensity(shape, detail, variety.z, heightGradient(f), cloud.look.x);
}

// The optical depth of `density` over `stepUnits` game units (cloudvolumemath.hpp: opticalDepth).
float opticalDepthOf(float density, float stepUnits)
{
    return max(density, 0.0) * cloud.density.x * extinctionPerMeter * (stepUnits / unitsPerMeter);
}

// ---------------------------------------------------------------------------------------------------------------------
// The curved layer (cloudcurvemath.hpp: Detail::rootToHeight, curvedLayerSpan, radialHeight, farFade)
// ---------------------------------------------------------------------------------------------------------------------

// The positive distance along an upward ray from height z0 to the sphere h above the surface (z0 < h), in metres.
float rootToHeight(float z0, float dirZ, float radius, float h)
{
    float b = (radius + z0) * dirZ;
    float c = (z0 - h) * (2.0 * radius + z0 + h);
    float q = -(b + sqrt(b * b - c));
    return c / q;
}

// The height above the surface of the point at the offset (relXY, relZ) metres from a camera z0 up.
float radialHeight(float2 relXY, float z0, float relZ, float radius)
{
    float z = z0 + relZ;
    float d2 = dot(relXY, relXY);
    float r = sqrt(d2 + (radius + z) * (radius + z));
    return (d2 + z * (2.0 * radius + z)) / (r + radius);
}

float farFade(float distanceMeters, float capMeters)
{
    float start = capMeters * farFadeStart;
    if (distanceMeters <= start)
        return 1.0;
    return 1.0 - smoothStep(start, capMeters, distanceMeters);
}

// The point of a ray at `position` (game units) as the noise and the height gradient want it: metres, with z the flat
// height or the height above the curved surface, and the unclamped height fraction in the layer. The planet's centre is
// straight below the camera at `origin`.
float3 cloudPoint(float3 position, float3 origin, out float fraction)
{
    float base = cloud.layer.x;
    float top = cloud.layer.y;
    if (cloud.curve.x <= 0.0)
    {
        fraction = (position.z - base) / (top - base);
        return position / unitsPerMeter;
    }
    float3 rel = (position - origin) / unitsPerMeter;
    float h = radialHeight(rel.xy, origin.z / unitsPerMeter, rel.z, cloud.curve.x);
    fraction = (h * unitsPerMeter - base) / (top - base);
    return float3(position.xy / unitsPerMeter, h);
}

// ---------------------------------------------------------------------------------------------------------------------
// The march
// ---------------------------------------------------------------------------------------------------------------------

float3 viewDirection(float2 uv)
{
    float2 ndc = uv * 2.0 - 1.0;
    float4 p = mul(scene.invProjection, float4(ndc, 1.0, 1.0));
    return p.xyz / p.w;
}

// Returns (rgb: the light scattered towards the eye, a: the transmittance), for the ray through screen position `uv`.
float4 marchRay(float2 uv, float2 pixel)
{
    float3 dirView = viewDirection(uv);
    float3 dir = normalize(mul(scene.invView, float4(dirView, 0.0)).xyz);
    float3 origin = mul(scene.invView, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float4 clear = float4(0.0, 0.0, 0.0, 1.0);
    bool curved = cloud.curve.x > 0.0;
    if (dir.z <= (curved ? 0.0 : 1e-4))
        return clear;

    float base = cloud.layer.x;
    float top = cloud.layer.y;
    float enter;
    float exitT;
    if (curved)
    {
        // cloudcurvemath.hpp: curvedLayerSpan, in metres (the planet's centre is straight below the camera).
        float z0 = origin.z / unitsPerMeter;
        float baseM = base / unitsPerMeter;
        float topM = top / unitsPerMeter;
        if (z0 >= topM)
            return clear;
        float enterM = z0 >= baseM ? 0.0 : rootToHeight(z0, dir.z, cloud.curve.x, baseM);
        float exitM = min(rootToHeight(z0, dir.z, cloud.curve.x, topM), cloud.layer.z / unitsPerMeter);
        enter = enterM * unitsPerMeter;
        exitT = exitM * unitsPerMeter;
    }
    else
    {
        // cloudvolumemath.hpp: layerSpan, for a camera below the layer or inside it.
        float t0 = (base - origin.z) / dir.z;
        float t1 = (top - origin.z) / dir.z;
        enter = max(min(t0, t1), 0.0);
        exitT = min(max(t0, t1), cloud.layer.z);
    }
    if (exitT <= enter)
        return clear;

    int steps = int(cloud.density.w);
    int lightSteps = int(cloud.march.x);
    float dt = (exitT - enter) / float(steps);
    float offset = ditherOffset(pixel);

    float3 sunDir = normalize(mul(scene.invView, float4(scene.lightViewPos.xyz, 0.0)).xyz);
    float cosTheta = dot(dir, sunDir);
    float3 sunColor = scene.lightDiffuse.xyz * cloud.look.y * sunPhaseScale; // the phase integrates to 1 (see volume.slang), so 4 pi is a full sun; half of it
    float3 ambient = scene.ambient.xyz * cloud.look.z;
    float lightRay = lightRayMeters * unitsPerMeter;

    float3 scattered = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    float distanceSum = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        float t = enter + (float(i) + offset) * dt;
        float3 position = origin + dir * t;
        float fraction;
        float3 positionMeters = cloudPoint(position, origin, fraction);
        float f = saturate(fraction);
        float4 variety = varietyAt(positionMeters.xy);
        // Empty space costs only the shape lookup.
        float coarse = densityAt(positionMeters, f, detailBound, variety);
        if (coarse <= 0.0)
            continue;
        float density = densityAt(positionMeters, f, detailFull, variety);
        if (density <= 0.0)
            continue;
        float od = opticalDepthOf(density, dt);

        // The sun light through the cloud above this point.
        float sunOd = 0.0;
        float travelled = 0.0;
        for (int j = 0; j < lightSteps; ++j)
        {
            float len = lightRay * float(j + 1) / (float(lightSteps) * float(lightSteps + 1) * 0.5);
            float3 lp = position + sunDir * (travelled + 0.5 * len);
            travelled += len;
            float lf;
            float3 lpMeters = cloudPoint(lp, origin, lf);
            if (lf < 0.0 || lf > 1.0)
                continue;
            // The variety is that of the step own place (the light ray is 600 m long).
            sunOd += opticalDepthOf(densityAt(lpMeters, lf, detailMean, variety), len);
        }
        float lit = multiScatter(sunOd, cosTheta, 3);
        // The powder term: thin edges are darker than Beer law alone says (cloudvolumemath.hpp: powder), over a fixed
        // length so that the look does not change with the tier's step.
        float powder = 1.0 - exp(-2.0 * opticalDepthOf(density, powderLengthMeters * unitsPerMeter));
        lit *= lerp(powderFloor, 1.0, powder);
        float3 light = sunColor * lit + ambient * (baseAmbientShare + (1.0 - baseAmbientShare) * f);

        // volumemath.hpp: integrateSlice, with the albedo at 1.
        float stepT = exp(-od);
        float weight = transmittance * (1.0 - stepT);
        scattered += light * weight;
        distanceSum += t * weight;
        weightSum += weight;
        transmittance *= stepT;
        if (transmittance < minTransmittance)
        {
            transmittance = 0.0;
            break;
        }
    }

    // Distant cloud fades into the haze (cloudvolumemath.hpp: hazeBlend).
    if (weightSum > 0.0)
    {
        float meanMeters = (distanceSum / weightSum) / unitsPerMeter;
        float haze = 1.0 - exp(-meanMeters / max(cloud.look.w, 1.0));
        if (curved)
        {
            // The far end of the march fades into the haze colour instead of ending (cloudcurvemath.hpp: farFade).
            haze = 1.0 - (1.0 - haze) * farFade(meanMeters, cloud.layer.z / unitsPerMeter);
        }
        scattered = lerp(scattered, scene.fogColor.xyz * (1.0 - transmittance), haze);
    }
    return float4(scattered, transmittance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void marchMain(uint3 id : SV_DispatchThreadID)
{
    uint2 size = uint2(cloud.sizes.xy);
    if (any(id.xy >= size))
        return;
    float2 jitter = cloud.march.yz;
    float2 uv = (float2(id.xy) + jitter) / cloud.sizes.xy;
    marchStore[id.xy] = marchRay(uv, float2(id.xy));
}

// ---------------------------------------------------------------------------------------------------------------------
// The resolve: the full size picture
// ---------------------------------------------------------------------------------------------------------------------

float4 loadMarch(int2 p)
{
    int2 size = int2(cloud.sizes.xy);
    return marchStore[uint2(clamp(p, int2(0, 0), size - int2(1, 1)))];
}

// cloudvolumemath.hpp: catmullRomWeights. The weights of the taps at -1, 0, 1, 2 around the fraction t.
float4 catmullRomWeights(float t)
{
    float x = clamp(t, 0.0, 1.0);
    float x2 = x * x;
    float x3 = x2 * x;
    return float4(-0.5 * x3 + x2 - 0.5 * x, 1.5 * x3 - 2.5 * x2 + 1.0, -1.5 * x3 + 2.0 * x2 + 0.5 * x, 0.5 * x3 - 0.5 * x2);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void resolveMain(uint3 id : SV_DispatchThreadID)
{
    uint2 fullSize = uint2(cloud.sizes.zw);
    if (any(id.xy >= fullSize))
        return;
    float2 uv = (float2(id.xy) + 0.5) / cloud.sizes.zw;
    float2 jitter = cloud.march.yz;

    // The march texel p was cast at p + jitter (in texels), so the value at q lies between the texels round
    // (q - jitter).
    float2 q = uv * cloud.sizes.xy - jitter;
    int2 base = int2(floor(q));
    float2 f = q - float2(base);
    // The 4 by 4 taps round the point (tap [j][i] is the march texel base + (i - 1, j - 1)).
    float4 taps[4][4];
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            taps[j][i] = loadMarch(base + int2(i - 1, j - 1));
    // Bicubic (Catmull-Rom), kept between the four nearest taps so that the negative lobes cannot ring or go below 0
    // (cloudvolumemath.hpp: clampToNearestTaps).
    float4 wx = catmullRomWeights(f.x);
    float4 wy = catmullRomWeights(f.y);
    float4 cubic = float4(0.0, 0.0, 0.0, 0.0);
    for (int j = 0; j < 4; ++j)
    {
        float4 row = taps[j][0] * wx.x + taps[j][1] * wx.y + taps[j][2] * wx.z + taps[j][3] * wx.w;
        cubic += row * wy[j];
    }
    float4 nearest[4] = { taps[1][1], taps[1][2], taps[2][1], taps[2][2] };
    float4 current = clamp(cubic, min(min(nearest[0], nearest[1]), min(nearest[2], nearest[3])),
        max(max(nearest[0], nearest[1]), max(nearest[2], nearest[3])));

    float4 result = current;
    if (cloud.debug.x < 0.5)
    {
        // The neighbourhood the history may take values from: the 3 by 3 texels round each of the four nearest
        // texels (all inside the 4 by 4 taps), mixed by the fraction. A box taken round the single nearest texel would
        // change in steps at the edge of every march texel, and show as a grid of squares in the cloud.
        float4 corners[4];
        float4 cornersHi[4];
        for (int k = 0; k < 4; ++k)
        {
            int cx = k & 1;
            int cy = k >> 1;
            float4 mn = taps[cy + 1][cx + 1];
            float4 mx = mn;
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                {
                    mn = min(mn, taps[cy + y][cx + x]);
                    mx = max(mx, taps[cy + y][cx + x]);
                }
            corners[k] = mn;
            cornersHi[k] = mx;
        }
        float4 lo = min(current, lerp(lerp(corners[0], corners[1], f.x), lerp(corners[2], corners[3], f.x), f.y));
        float4 hi = max(current, lerp(lerp(cornersHi[0], cornersHi[1], f.x), lerp(cornersHi[2], cornersHi[3], f.x), f.y));
        float weight = cloud.march.w;
        if (weight > 0.0)
        {
            float3 dirView = viewDirection(uv);
            float4 clip = mul(cloud.toPreviousClip, float4(dirView, 1.0));
            if (clip.w > 1e-4)
            {
                float2 previous = clip.xy / clip.w * 0.5 + 0.5;
                if (all(previous >= float2(0.0, 0.0)) && all(previous <= float2(1.0, 1.0)))
                {
                    float4 history = historyTex.SampleLevel(previous, 0.0);
                    if (!any(isnan(history)) && !any(isinf(history)))
                    {
                        history = clamp(history, lo, hi);
                        result = lerp(current, history, weight);
                    }
                }
            }
        }
    }
    else if (cloud.debug.x < 1.5)
    {
        // 1: the transmittance as grey (white: no cloud).
        result = float4(current.a, current.a, current.a, 0.0);
    }
    else if (cloud.debug.x < 2.5)
    {
        // 2: a slice of the shape noise (its red channel) across the screen.
        float v = shapeTex.SampleLevel(float3(uv, 0.5), 0.0).r;
        result = float4(v, v, v, 0.0);
    }
    else if (cloud.debug.x < 3.5)
    {
        // 3: a slice of the detail noise (its fBm channel).
        float v = detailTex.SampleLevel(float3(uv, 0.5), 0.0).a;
        result = float4(v, v, v, 0.0);
    }
    else
    {
        // 4: the curl noise.
        float2 v = curlTex.SampleLevel(uv, 0.0).xy;
        result = float4(v, 0.5, 0.0);
    }
    resolveStore[id.xy] = result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void copyMain(uint3 id : SV_DispatchThreadID)
{
    uint2 size = uint2(cloud.sizes.zw);
    if (any(id.xy >= size))
        return;
    historyStore[id.xy] = resolveStore[id.xy];
}

// ---------------------------------------------------------------------------------------------------------------------
// The ground shadow map (task 0042, D6)
// ---------------------------------------------------------------------------------------------------------------------

// For each texel of a square of the layer's middle plane that follows the camera (snapped to whole texels, so that
// the shadows do not crawl), the transmittance of the sun's ray through the layer there. A ground point is sent along
// the sun's ray to the same plane and reads this (lib/view/volumecloudshadow.glsl). It is made from the same density as
// the march, with the same wind and coverage, so the shadow lies under the cloud that casts it.
[shader("compute")]
[numthreads(8, 8, 1)]
void shadowMain(uint3 id : SV_DispatchThreadID)
{
    float halfExtent = shadowHalfExtentMeters * unitsPerMeter;
    float texel = 2.0 * halfExtent / float(shadowMapSize);
    float2 camera = mul(scene.invView, float4(0.0, 0.0, 0.0, 1.0)).xy;
    float2 centre = floor(camera / texel + 0.5) * texel;
    float zMid = 0.5 * (cloud.layer.x + cloud.layer.y);
    float strength = cloud.debug.z * cloud.debug.w;
    if (id.x == shadowMapSize)
    {
        // The placement texels: where the map is, and how much it darkens.
        if (id.y == 0)
            shadowStore[id.xy] = float4(centre, zMid, halfExtent);
        else if (id.y == 1)
            shadowStore[id.xy] = float4(cloud.debug.w, cloud.debug.z, 0.0, 0.0);
        return;
    }
    if (id.x > shadowMapSize || id.y >= shadowMapSize)
        return;
    float3 sunDir = normalize(mul(scene.invView, float4(scene.lightViewPos.xyz, 0.0)).xyz);
    if (strength <= 0.0 || sunDir.z < shadowSunUp)
    {
        shadowStore[id.xy] = float4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    float2 q = centre + ((float2(id.xy) + 0.5) / float(shadowMapSize) - 0.5) * 2.0 * halfExtent;
    float3 start = float3(q, zMid) + sunDir * ((cloud.layer.x - zMid) / sunDir.z);
    float3 span = sunDir * ((cloud.layer.y - cloud.layer.x) / sunDir.z);
    float lengthUnits = length(span);
    float opticalDepth = 0.0;
    for (int i = 0; i < shadowSteps; ++i)
    {
        float3 p = start + span * ((float(i) + 0.5) / float(shadowSteps));
        float f = saturate((p.z - cloud.layer.x) / (cloud.layer.y - cloud.layer.x));
        // The ground shadow keeps the flat layer: at its 20 km reach the curved layer is 31 m lower (d^2 / 2R) than the
        // flat one, of a layer 2.5 km thick.
        float3 pm = p / unitsPerMeter;
        opticalDepth += opticalDepthOf(densityAt(pm, f, detailFull, varietyAt(pm.xy)), lengthUnits / float(shadowSteps));
    }
    shadowStore[id.xy] = float4(exp(-opticalDepth), 0.0, 0.0, 1.0);
}
