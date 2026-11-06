// The cirrus layer (task 0079), in Slang (decision 0009: new shaders are Slang). A thin, fibrous, high cloud sheet computed
// from noise on a dome of its own, drawn after the moons and before the low cloud layers. It has no GLSL pair: the C++
// side registers the path `sky/cirrus` on Pipeline::Graphics::slangOnlyPaths, and the pipeline is built with only the
// MATERIAL mode, like the sun glare (the scene block in set 0 binding 0, the material in set 2 binding 1).
//
// The maths is Render::Cirrus (components/render/cirrusmath.hpp) and the noise Render::CloudVolume::perlin: the
// constants and formulas below repeat them, change both together (a test reads the constants back). Nothing reads a
// timer: the drift, the morph and the sun's elevation arrive in the material, summed on the CPU.
//
// The dome has no vertex buffers: the vertex shader builds the triangle list from the vertex index (Render::Cirrus::
// domeVertex). It is a unit hemisphere from 3 degrees under the horizon to the zenith, drawn 900 units from the eye.
//
// What the material carries (Pipeline::Data::Material, std140; this layer has a material of its own, so the fields are
// free to use):
//   diffuse.xyz   the cloud tint of the weather (as the cloud layers' material.diffuse), diffuse.a unused
//   ambient       x the weather's share (0 to 1), y the `cirrus coverage` scale, z the `cirrus opacity` scale,
//                 w the layer's altitude in metres
//   specular      x the drift along the wind in metres (wrapped by the pattern's period), y the morph coordinate,
//                 z the sun's true elevation in radians, w the quality (0 low, 1 medium, 2 high)
//   emissive      xyz the sun's tint (Render::Atmosphere::sunGlowTint), w the debug view (Render::Cirrus::DebugView)
//   pbr           xy the unit wind direction of the layer, z the pattern coverage (Render::Cirrus::patternCoverage)
//
// Compiled with: -profile spirv_1_0 -matrix-layout-column-major, so matrices lie in memory as GLSL's mat4 does.

static const float kTauMax = 0.3;
static const float kFibreStretch = 6.0;
static const float kVeerRadians = 0.52;
static const float kCellMeters = 2000.0;
static const float kMorphSeconds = 600.0;
static const float kAmbientShare = 0.6;
static const float kPhaseAnisotropy = 0.75;
static const float kPhaseCap = 8.0;
static const float kHorizonCutZ = -0.05;
static const float kHorizonFadeEndZ = 0.02;
static const float kSunGain = 0.5;
static const float kEarthRadiusMeters = 6371000.0;
static const float kBaseCoverage = 0.55;
// BEGIN FIELD CONSTANTS (generated from cirrusmath.hpp by cirrus/v2/gen_slang.py; change both together)
static const float kWarpAlongCells = 0.8;
static const float kWarpAcrossCells = 0.09375;
static const float kEdgeAtZeroCoverage = 0.62;
static const float kEdgeCoverageSlope = 0.34;
static const float kEdgeSoftnessTufts = 0.28;
static const float kEdgeSoftnessLong = 0.5;
static const float kFibreLengthFactor = 2.0;
static const float kPatchScale = 0.375;
static const float kPatchContrast = 2.0;
static const float kTypeScale = 0.5;
static const float kTypeLow = 0.4;
static const float kTypeHigh = 0.6;
static const float kSwirlScale = 0.25;
static const float kSwirlCells = 1.6;
static const float kSwirlAcrossShare = 0.5;
static const float kGapEdgeAtZero = 0.52;
static const float kGapEdgeSlope = 0.34;
static const float kGapWidth = 0.22;
static const float kClumpThicken = 0.3;
static const float kLongAlong = 0.5;
static const float kLongAcross = 4.0;
static const float kTuftAlong = 2.0;
static const float kTuftAcross = 8.0;
static const float kHookWarpGain = 0.5;
static const float kTailScale = 0.5;
static const float kTailAcross = 2.0;
static const float kTailPhase = 2.5;
static const float kTailStrength = 0.5;
static const float kTailHead = 0.1;
static const float kTailFloor = 0.85;
static const float kTailMeters = 4000.0;
static const float kVeilAmount = 0.12;
static const float kVeilLow = 0.35;
static const float kVeilHigh = 0.65;
static const int kTuftOctaves = 2;
static const uint kSeedPatch = 300u;
static const uint kSeedType = 310u;
static const uint kSeedSwirl = 320u;
static const uint kSeedTail = 340u;
static const uint kSeedTuft = 10u;
static const float kFibreWidthFactor = 1.3333334;
// END FIELD CONSTANTS
static const float kDomeBelowHorizonRadians = 0.05235988;
static const float kPi = 3.14159265358979;
static const int kNoisePeriodCells = 128;
static const int kDomeColumns = 32;
static const int kDomeRows = 8;
static const float kDomeRadius = 900.0;
// Render::Cloud::noiseSeed + Render::Cirrus::Detail::seedOffset (0x0E0E1042 + 0x00C1AA05)
static const uint kNoiseSeed = 0x0ECFBA47u;

// lib/sets.glsl
static const int VIEW_SET = 0;
static const int TEXTURE_SET = 2;

// lib/pushconstants.glsl
struct PushConstants
{
    float4x4 projection;
    float4x4 modelview;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// lib/material/data.glsl (std140)
struct Material
{
    float4 ambient;
    float4 diffuse;
    float4 emissive;
    float4 specular;
    float shininess;
    float alphaTestCutoff;
    float subsurface;
    float align_dummy2;
    float4 pbr;
};
// lib/material/bindings.glsl: MATERIAL_BINDING = 1 in TEXTURE_SET
[[vk::binding(1, 2)]] ConstantBuffer<Material> material;

// The scene uniform: one declaration for every shader (lib/view/data.slang, lib/view/scene.slang).
import lib.view.scene;

// ---------------------------------------------------------------------------------------------------------------------
// The dome (Render::Cirrus::domeVertex)
// ---------------------------------------------------------------------------------------------------------------------

float3 domeVertex(uint index)
{
    const int cornerColumn[6] = { 0, 1, 0, 1, 1, 0 };
    const int cornerRow[6] = { 0, 0, 1, 0, 1, 1 };
    uint quad = index / 6u;
    uint corner = index % 6u;
    int column = int(quad % uint(kDomeColumns)) + cornerColumn[corner];
    int row = int(quad / uint(kDomeColumns)) + cornerRow[corner];
    float azimuth = 2.0 * kPi * float(column) / float(kDomeColumns);
    float elevation = -kDomeBelowHorizonRadians + (0.5 * kPi + kDomeBelowHorizonRadians) * float(row) / float(kDomeRows);
    float c = cos(elevation);
    return float3(c * cos(azimuth), c * sin(azimuth), sin(elevation));
}

struct VertexOutput
{
    [[vk::location(0)]] float3 domeDir : V_DOMEDIR;
    [[vk::location(1)]] float3 sunLocal : V_SUNLOCAL;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndex : SV_VulkanVertexID)
{
    VertexOutput output;
    float3 dir = domeVertex(vertexIndex);
    float4 position = mul(pc.projection, mul(pc.modelview, float4(dir * kDomeRadius, 1.0)));
    // vsgopenmw-reverse-depth: farDepth + epsilon, as the procedural sun. Depth is neither tested nor written here.
    position.z = 0.001;
    output.position = position;
    output.domeDir = dir;
    // The dome has no rotation or scale of its own, so its inverse is the transpose of the view's: the sun's direction
    // (view space) in the dome's, which is the world's.
    output.sunLocal = mul(transpose((float3x3)pc.modelview), scene.lightViewPos.xyz);
    return output;
}

// ---------------------------------------------------------------------------------------------------------------------
// The noise (cloudvolumemath.hpp: hashUint, hashCell, perlin)
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

// The corner's gradient dotted with the offset d, without a table: the twelve gradients of cloudvolumemath.hpp's perlin
// are (+-1, +-1, 0), (+-1, 0, +-1) and (0, +-1, +-1) in that order, so the hash's value k = h % 12 picks the pair of
// axes by k / 4 and the two signs by the bits of k % 4 (the first sign belongs to the first axis of the pair). The sums
// are the C++'s (a zero term adds nothing), so the result is the same bit for bit.
float cornerDot(uint h, float3 d)
{
    uint k = h % 12u;
    uint group = k >> 2u;
    uint bits = k & 3u;
    float a = (bits & 1u) != 0u ? -1.0 : 1.0;
    float b = (bits & 2u) != 0u ? -1.0 : 1.0;
    float u = group == 2u ? d.y : d.x;
    float v = group == 0u ? d.y : d.z;
    return a * u + b * v;
}

// cloudvolumemath.hpp's perlin, with the lattice hash shared between corners: hashCell chains three hashes, x then y
// then z, so the eight corners need 2 + 4 + 8 hashes, not 8 * 3, with the same values. Cost matters here: this runs
// for every sky pixel, several times.
float perlin(float3 p, int period, uint seed)
{
    int3 p0 = int3(floor(p));
    float3 f = p - float3(p0);
    float3 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    uint hs = hashUint(seed);
    uint wx[2] = { wrapCell(p0.x, period), wrapCell(p0.x + 1, period) };
    uint wy[2] = { wrapCell(p0.y, period), wrapCell(p0.y + 1, period) };
    uint wz[2] = { wrapCell(p0.z, period), wrapCell(p0.z + 1, period) };
    float corner[2][2][2];
    for (int i = 0; i < 2; ++i)
    {
        uint hx = hashUint(wx[i] + hs);
        for (int j = 0; j < 2; ++j)
        {
            uint hy = hashUint(hx ^ (wy[j] * 0x9E3779B9u));
            for (int k = 0; k < 2; ++k)
            {
                uint h = hashUint(hy ^ (wz[k] * 0x85EBCA6Bu));
                corner[k][j][i] = cornerDot(h, f - float3(i, j, k));
            }
        }
    }
    float x00 = lerp(corner[0][0][0], corner[0][0][1], w.x);
    float x10 = lerp(corner[0][1][0], corner[0][1][1], w.x);
    float x01 = lerp(corner[1][0][0], corner[1][0][1], w.x);
    float x11 = lerp(corner[1][1][0], corner[1][1][1], w.x);
    float value = lerp(lerp(x00, x10, w.y), lerp(x01, x11, w.y), w.z);
    return saturate(value * 0.5 + 0.5);
}

// ---------------------------------------------------------------------------------------------------------------------
// The layer (cirrusmath.hpp)
// ---------------------------------------------------------------------------------------------------------------------

float smoothStepEdges(float e0, float e1, float x)
{
    if (e1 <= e0)
        return x < e0 ? 0.0 : 1.0;
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}

// The root of R^2 dz^2 + 2 R h + h^2, which is also R dz + t (the distance from the planet's centre to the sphere's near
// side, along the ray, measured from the closest approach). The shader has no 64 bit floats (a device feature), so the
// distance is written in the form that does not lose digits: for dz > 0, t = (2 R h + h^2) / (R dz + root).
float shellRoot(float dirZ, float altitude)
{
    float dz = clamp(dirZ, -1.0, 1.0);
    float h = max(altitude, 0.0);
    return sqrt(kEarthRadiusMeters * kEarthRadiusMeters * dz * dz + 2.0 * kEarthRadiusMeters * h + h * h);
}

float shellDistance(float dirZ, float altitude)
{
    float dz = clamp(dirZ, -1.0, 1.0);
    float h = max(altitude, 0.0);
    float root = shellRoot(dz, h);
    if (dz > 0.0)
        return (2.0 * kEarthRadiusMeters * h + h * h) / (kEarthRadiusMeters * dz + root);
    return root - kEarthRadiusMeters * dz;
}

// alpha t (R + h) / (R dz + t), and R dz + t is the root.
float footprint(float pixelAngle, float dirZ, float altitude)
{
    if (!(pixelAngle > 0.0))
        return 0.0;
    float h = max(altitude, 0.0);
    float t = shellDistance(dirZ, h);
    float value = pixelAngle * t * (kEarthRadiusMeters + h) / max(shellRoot(dirZ, h), 1.0);
    return clamp(value, 0.0, 1e7);
}

float octaveWeight(float footprintMeters, float wavelengthMeters)
{
    if (!(footprintMeters > 0.0))
        return 1.0;
    if (!(wavelengthMeters > 0.0))
        return 0.0;
    return 1.0 - smoothStepEdges(0.25 * wavelengthMeters, wavelengthMeters, footprintMeters);
}

float depressionLimit(float altitude)
{
    // acos(R / (R + h)); the argument is near 1, so use the half-angle form acos(x) = 2 asin(sqrt((1 - x) / 2)), with
    // 1 - x = h / (R + h) computed without cancelling.
    float h = max(altitude, 0.0);
    return 2.0 * asin(sqrt(0.5 * h / (kEarthRadiusMeters + h)));
}

float litWeight(float sunElevation, float altitude)
{
    if (!(sunElevation < 0.0))
        return 1.0;
    return 1.0 - smoothStepEdges(0.0, depressionLimit(altitude), -sunElevation);
}

float henyeyGreenstein(float cosTheta, float g)
{
    float c = clamp(cosTheta, -1.0, 1.0);
    float k = clamp(g, -0.99, 0.99);
    float denominator = 1.0 + k * k - 2.0 * k * c;
    return (1.0 - k * k) / (4.0 * kPi * denominator * sqrt(denominator));
}

float forwardPhase(float cosTheta)
{
    return min(4.0 * kPi * henyeyGreenstein(cosTheta, kPhaseAnisotropy), kPhaseCap);
}

float horizonFade(float dirZ)
{
    return smoothStepEdges(kHorizonCutZ, kHorizonFadeEndZ, dirZ);
}

// A large field of the layer (cirrusmath.hpp: Detail::field), in [0, 1].
float field(float fx, float fy, float qm, float scale, uint seed, float footprintMeters)
{
    float cellLength = kCellMeters * kFibreLengthFactor;
    int period = int(float(kNoisePeriodCells) * 0.5 * scale + 0.5);
    float weight = octaveWeight(footprintMeters, cellLength / scale);
    if (!(weight > 0.0))
        return 0.5;
    return lerp(0.5, perlin(float3(fx * scale, fy * scale, qm * scale), period, seed), weight);
}

// Detail::fibres: octaves of noise with the lattice cell 1 / alongScale along and 1 / acrossScale across.
float fibres(float ux, float uy, float qm, float alongScale, float acrossScale, int octaves, uint seed, float footprintMeters)
{
    float cellLength = kCellMeters * kFibreLengthFactor;
    float sum = 0.0;
    float total = 0.0;
    float weight = 0.5;
    float scale = 1.0;
    for (int i = 0; i < octaves; ++i)
    {
        float wavelength = cellLength / (acrossScale * scale);
        int period = int(float(kNoisePeriodCells) * 0.5 * alongScale * scale + 0.5);
        float octave = octaveWeight(footprintMeters, wavelength);
        float n = 0.5;
        // An octave the pixel is too big for is its mean: no need to evaluate it (the same value as the C++'s).
        if (octave > 0.0)
            n = lerp(0.5, perlin(float3(ux * alongScale * scale, uy * acrossScale * scale, qm * scale), period, seed + uint(i)), octave);
        sum += weight * n;
        total += weight;
        weight *= 0.5;
        scale *= 2.0;
    }
    return total > 0.0 ? sum / total : 0.5;
}

// density(): the fibre field in [0, 1] at a point of the wind frame (metres); see Render::Cirrus::density.
float density(float2 windFramePoint, float morph, int octaves, int warps, float coverage, float footprintMeters)
{
    if (!(coverage > 0.0))
        return 0.0;
    float cover = min(coverage, 1.0);
    int octaveCount = clamp(octaves, 1, 5);
    int warpCount = clamp(warps, 0, 2);
    uint seed = kNoiseSeed;

    float cellLength = kCellMeters * kFibreLengthFactor;
    float fx = windFramePoint.x / cellLength;
    float fy = windFramePoint.y / kFibreStretch / cellLength;
    float qm = morph;

    float patchRaw = field(fx, fy, qm, kPatchScale, seed + kSeedPatch, footprintMeters);
    float patchValue = clamp(0.5 + (patchRaw - 0.5) * kPatchContrast, 0.0, 1.0);
    float typeNoise = field(fx, fy, qm, kTypeScale, seed + kSeedType, footprintMeters);
    float longShare = smoothStepEdges(kTypeLow, kTypeHigh, typeNoise);
    float ux = fx + kSwirlCells * 2.0 * (field(fx, fy, qm, kSwirlScale, seed + kSeedSwirl, footprintMeters) - 0.5);
    float uy = fy + kSwirlAcrossShare * kSwirlCells * 2.0 * (typeNoise - 0.5);

    // A pixel in a gap needs nothing of the fibres (the C++ does the same: d times 0 is 0).
    float gapEdge = kGapEdgeAtZero - kGapEdgeSlope * cover;
    float gate = smoothStepEdges(gapEdge, gapEdge + kGapWidth, patchValue);
    float d = 0.0;
    if (gate > 0.0)
    {
        float warpWeight = octaveWeight(footprintMeters, 2.0 * cellLength / (kFibreStretch * kFibreWidthFactor));
        float hook = 1.0 + kHookWarpGain * (1.0 - longShare);
        int warpPeriod = kNoisePeriodCells / 4;
        for (int w = 0; w < warpCount && warpWeight > 0.0; ++w)
        {
            uint warpSeed = seed + 100u + 2u * uint(w);
            float n1 = perlin(float3(ux * 0.5, uy * 1.0, qm * 0.5), warpPeriod, warpSeed);
            float n2 = perlin(float3(ux * 0.5, uy * 1.0, qm * 0.5), warpPeriod, warpSeed + 1u);
            ux += warpWeight * kWarpAlongCells * hook * (n1 - 0.5) * 2.0;
            uy += warpWeight * kWarpAcrossCells * hook * (n2 - 0.5) * 2.0;
        }

        float valueLong = longShare > 0.0 ? fibres(ux, uy, qm, kLongAlong, kLongAcross, max(octaveCount - 1, 2), seed, footprintMeters) : 0.5;
        float valueTuft = longShare < 1.0 ? fibres(ux, uy, qm, kTuftAlong, kTuftAcross, kTuftOctaves, seed + kSeedTuft, footprintMeters) : 0.5;
        float value = lerp(valueTuft, valueLong, longShare);
        value = clamp(value + kClumpThicken * (patchValue - 0.5), 0.0, 1.0);

        float edge = kEdgeAtZeroCoverage - kEdgeCoverageSlope * cover;
        float softness = lerp(kEdgeSoftnessTufts, kEdgeSoftnessLong, longShare);
        d = smoothStepEdges(edge, edge + softness, value);

        float tailNoise = perlin(float3(ux * kTailScale, uy * kTailAcross, qm * kTailScale),
            int(float(kNoisePeriodCells) * 0.5 * kTailScale + 0.5), seed + kSeedTail);
        float phase = ux * kTailScale + kTailPhase * 2.0 * (tailNoise - 0.5);
        float saw = phase - floor(phase);
        float tail = smoothStepEdges(0.0, kTailHead, saw) * (1.0 - kTailFloor * smoothStepEdges(kTailHead, 1.0, saw));
        float tailAmount = kTailStrength * (0.4 + 0.6 * longShare) * octaveWeight(footprintMeters, kTailMeters);
        d *= lerp(1.0, tail, tailAmount);
        d *= gate;
    }

    float veil = kVeilAmount * smoothStepEdges(kVeilLow, kVeilHigh, patchRaw);
    return clamp(d + veil * (1.0 - d), 0.0, 1.0);
}

// lib/view/linear.slang: the linear HDR path's conversions (marked by scene.fogColor.w).
bool linearView()
{
    return scene.fogColor.w > 1.5;
}

float3 toLinear(float3 encoded)
{
    return sign(encoded) * pow(abs(encoded), float3(2.2));
}

float3 toEncoded(float3 linear)
{
    return sign(linear) * pow(abs(linear), float3(1.0 / 2.2));
}

struct FragmentInput
{
    [[vk::location(0)]] float3 domeDir : V_DOMEDIR;
    [[vk::location(1)]] float3 sunLocal : V_SUNLOCAL;
    float4 fragCoord : SV_Position;
};

[shader("fragment")]
float4 fragMain(FragmentInput input) : SV_Target0
{
    float3 dir = normalize(input.domeDir);
    float fade = horizonFade(dir.z);
    float share = material.ambient.x;
    if (fade <= 0.0 || share <= 0.0)
        discard;

    float altitude = material.ambient.w;
    float sunElevation = material.specular.z;
    int quality = int(material.specular.w + 0.5);
    int octaves = quality + 3;
    int warps = quality;
    int debugView = int(material.emissive.w + 0.5);

    // Debug view 5, the pattern: the density on a fixed 40 km square of the wind frame (the screen's centre is the
    // frame's origin), with no drift, no morph and no footprint, so that it can be compared with Render::Cirrus::density
    // pixel by pixel whatever the camera does.
    if (debugView == 5)
    {
        float2 uv = input.fragCoord.xy / scene.resolution;
        float2 point = (uv - 0.5) * 40000.0;
        float v = density(point, 0.0, octaves, warps, material.pbr.z, 0.0);
        return float4(v, v, v, 1.0);
    }

    // Where the view direction meets the layer, in the wind's frame (Render::Cirrus::layerPoint and windFrame), then the
    // pattern drifts along the wind: the point is read upwind by the drift, which was wrapped by the pattern's period
    // on the CPU (the noise has that period, so the wrap shows no jump).
    float t = shellDistance(dir.z, altitude);
    float2 p = t * dir.xy;
    float2 windDir = material.pbr.xy;
    float2 frame = float2(p.x * windDir.x + p.y * windDir.y, kFibreStretch * (-p.x * windDir.y + p.y * windDir.x));
    frame.x -= material.specular.x;

    // The size of a pixel on the layer, from the projection (a pixel is 2 / (P11 * height) radians wide, P11 being
    // cot(fov / 2)); the Vulkan flip may make P11 negative.
    float pixelAngle = 2.0 / (abs(scene.projection[1][1]) * max(scene.resolution.y, 1.0));
    float size = footprint(pixelAngle, dir.z, altitude);

    float d = density(frame, material.specular.y, octaves, warps, material.pbr.z, size);
    float tau = kTauMax * saturate(d) * saturate(share) * max(material.ambient.y, 0.0) * max(material.ambient.z, 0.0);
    float alpha = (1.0 - exp(-max(tau, 0.0))) * fade;

    // The light: the same base colour as the cloud layers (the fog colour plus a little, times the weather's tint),
    // sky light always, the sun's forward lobe while the sun still reaches the layer (it does for a few degrees under
    // the horizon, because the layer is so high).
    float3 sunDirection = input.sunLocal;
    float sunLength = length(sunDirection);
    float cosTheta = sunLength > 1e-4 ? dot(dir, sunDirection / sunLength) : 0.0;
    float lit = litWeight(sunElevation, altitude);
    float phase = forwardPhase(cosTheta);
    float3 encodedFog = linearView() ? toEncoded(scene.fogColor.xyz) : scene.fogColor.xyz;
    float3 base = (encodedFog + float3(0.13, 0.13, 0.13)) * material.diffuse.xyz;
    if (linearView())
        base = toLinear(base);
    float3 sun = material.emissive.xyz * (lit * phase * kSunGain);
    float3 colour = base * (kAmbientShare + sun);

    // The debug views replace the colour (and are opaque).
    if (debugView == 1)
        return float4(float3(d, d, d), 1.0);
    if (debugView == 2)
        return float4(float3(share, share, share), 1.0);
    if (debugView == 3)
        return float4(lit, saturate(phase / kPhaseCap), saturate(0.5 + 0.5 * sunElevation / 0.0523), 1.0);
    if (debugView == 4)
        return float4(saturate(size / 1000.0), octaveWeight(size, kCellMeters * kFibreLengthFactor / (kFibreStretch * kFibreWidthFactor)), alpha, 1.0);

    return float4(colour, alpha);
}
