#ifndef OPENEDEN_RENDER_CIRRUSMATH_H
#define OPENEDEN_RENDER_CIRRUSMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

#include "atmospheremath.hpp"
#include "cloudvolumemath.hpp"

/*
 * OpenEden (task 0079): the pure maths of the cirrus layer, a thin, fibrous, high cloud sheet computed from noise in a
 * Slang fragment shader (`files/shaders/sky/cirrus/cirrus.slang`) on a dome of its own. No VSG scene types and no
 * device. The research is `docs/research/0041-cirrus-clouds.md`, the spec `docs/tasks/0079-cirrus-cloud-layer.md`.
 *
 * Real cirrus is a sheet of ice crystals 6 to 12 km up, so thin that the sun shows through it (a visible optical depth
 * of at most about 0.3). From the ground it is a flat pattern on a very distant, curved surface: this file works out
 * where a view direction meets that surface (`shellDistance`), how big a pixel is there (`footprint`), the fibre
 * pattern itself (`density`), whether the sun still lights it after the ground is dark (`litWeight`) and how it follows
 * the weather (`shareTarget`). The shader repeats the formulas that run on the GPU (the constants it repeats are read
 * back by a test): change both together.
 *
 * Units: metres and radians, Z up. The sky is camera-relative, so a direction is all a pixel needs.
 */
namespace Render::Cirrus
{
    constexpr double earthRadiusMeters = 6371000.0;

    // The layer's altitude setting range, in kilometres. Real cirrus sits at 5 to 13 km (temperate), 8 to 12 km for
    // cirrus proper (the research note, section 2).
    constexpr float minAltitudeKm = 6.f, maxAltitudeKm = 12.f, defaultAltitudeKm = 9.f;

    // The visible optical depth of the thickest cirrus drawn: "thin" cirrus is at most 0.3 (Sassen and Cho, 1992,
    // recalled; the research note marks it to verify).
    constexpr float tauMax = 0.3f;
    // Length over width of the fibres. Chosen by eye in the 4 to 8 range the research note gives.
    constexpr float fibreStretch = 6.f;
    // The wind turns with height; the high layer's wind blows this far (counter-clockwise) from the low layer's. 30
    // degrees, in the 20 to 40 degrees the research note gives.
    constexpr float veerRadians = 0.52f;
    // One lattice cell of the base noise octave, along the wind. The pattern repeats after noisePeriodCells cells: 256
    // km along the wind, more than the view reaches at any altitude above 10 degrees.
    constexpr float cellMeters = 2000.f;
    constexpr int noisePeriodCells = 128;
    // Game (simulation) seconds for one unit of the evolution coordinate (the noise's third axis).
    constexpr float morphSeconds = 600.f;
    // Light: the share of the layer's base colour that is always there (sky light), and the forward lobe of ice
    // crystals (Fu 1996, asymmetry parameter 0.75 to 0.85, recalled) capped so that it does not add a second sun to the
    // glow of task 0040. sunGain is the brightness of the sunlit part, chosen by eye.
    constexpr float ambientShare = 0.6f;
    constexpr float phaseAnisotropy = 0.75f;
    constexpr float phaseCap = 8.f;
    constexpr float sunGain = 0.5f;
    // The share moves to its target over this many seconds (as Render::CloudVolume::shareSeconds).
    constexpr float shareSeconds = 3.f;
    // Directions at or below this z (about 2.9 degrees under the horizon) are not drawn; the layer fades in over the
    // z range up to horizonFadeEndZ, so it melts into the horizon haze and has no hard edge at the dome's rim.
    constexpr float horizonCutZ = -0.05f;
    constexpr float horizonFadeEndZ = 0.02f;
    // The share of the sky the fibres cover with `cirrus coverage` = 1 (the setting scales it).
    constexpr float baseCoverage = 0.55f;

    // The dome is drawn without vertex buffers: the vertex shader builds a triangle list from the vertex index.
    constexpr int domeColumns = 32;
    constexpr int domeRows = 8;
    constexpr std::uint32_t domeVertexCount = static_cast<std::uint32_t>(domeColumns * domeRows * 6);
    constexpr float domeBelowHorizonRadians = 0.05235988f; // 3 degrees
    constexpr float pi = 3.14159265358979f;

    // The shape of the fibre field (all chosen by eye; see the stage logs of task 0079).
    constexpr float warpAlongCells = 0.8f; // domain warp displacement along the wind, in cells
    constexpr float warpAcrossCells = 0.09375f; // and across it, in cells (3/32)
    constexpr float edgeAtZeroCoverage = 0.62f; // the noise level where density starts, at pattern coverage 0 ...
    constexpr float edgeCoverageSlope = 0.34f; // ... lowered by this much at coverage 1
    constexpr float edgeSoftnessTufts = 0.28f; // the width of the ramp up to full density: sharp for short tufts ...
    constexpr float edgeSoftnessLong = 0.5f; // ... soft for the long fibres
    // The look of the fibres, by eye (frames of task 0079): the noise's cell is fibreLengthFactor times cellMeters
    // along the wind (its period then holds noisePeriodCells / fibreLengthFactor cells), so one lattice cell of the
    // base octave is 4 km. The pattern repeats after noisePeriodCells * cellMeters along the wind.
    constexpr float fibreLengthFactor = 2.f;
    constexpr float fibreWidthFactor = 4.f / 3.f;

    // The variety of the sky (2026-11-04: the first version was one kind of fibre, all alike, all along the wind, over
    // the whole sky). Every large field below is in true metres of the layer, in units of one 4 km cell (cellMeters
    // times fibreLengthFactor), and has the period of the drift's wrap divided by its scale, so the pattern still
    // repeats after noisePeriodCells * cellMeters whatever the drift does.
    //  - patch: where cloud is and where the sky is clear (two octaves, 16 and 8 km cells);
    //  - type: long, wide streaks against short tufts and hooks (8 km cells);
    //  - swirl: a slow displacement of the whole field (32 km cells, up to swirlCells), which turns the fibres to other
    //    directions from place to place and bends them (wind shear);
    //  - tail: a sawtooth along the fibres (head sharp, tail fading) whose phase moves with a noise;
    //  - veil: a faint, broad sheet (cirrostratus) that is not made of fibres.
    constexpr float patchScale = 0.375f, patchContrast = 2.f;
    constexpr float typeScale = 0.5f, typeLow = 0.4f, typeHigh = 0.6f;
    constexpr float swirlScale = 0.25f, swirlCells = 1.6f, swirlAcrossShare = 0.5f;
    constexpr float gapEdgeAtZero = 0.52f, gapEdgeSlope = 0.34f, gapWidth = 0.22f;
    constexpr float clumpThicken = 0.3f;
    // Long fibres: lattice 8 km along, 1 km across (octaves double both); tufts: 2 km by 500 m.
    constexpr float longAlong = 0.5f, longAcross = 4.f, tuftAlong = 2.f, tuftAcross = 8.f;
    constexpr int tuftOctaves = 2;
    // Tufts bend this much more than long fibres (the warp times 1 + gain * tuft share).
    constexpr float hookWarpGain = 0.5f;
    constexpr float tailScale = 0.5f, tailAcross = 2.f, tailPhase = 2.5f, tailStrength = 0.5f, tailHead = 0.1f;
    constexpr float tailFloor = 0.85f; // how much of the fibre the tail loses at its end
    constexpr float tailMeters = 4000.f; // the size at which the tail pattern is faded out by the pixel's footprint
    constexpr float veilAmount = 0.12f, veilLow = 0.35f, veilHigh = 0.65f;
    // Seeds, added to the layer's first seed.
    constexpr std::uint32_t seedPatch = 300u, seedType = 310u, seedSwirl = 320u, seedTail = 340u, seedTuft = 10u;

    // --- Where the layer is -------------------------------------------------------------------------------------

    // The distance along a ray from the ground (direction cosine `dirZ`, up) to the shell at `altitudeMeters`:
    // t = -R dz + sqrt(R^2 dz^2 + 2 R h + h^2) (the ray meets the sphere of radius R + h). h at the zenith, 338 km at
    // the horizon for h = 9 km. Derived in the research note, section 2. A flat-plane h / dz would go to infinity.
    inline float shellDistance(float dirZ, float altitudeMeters)
    {
        const double dz = std::clamp(static_cast<double>(std::isfinite(dirZ) ? dirZ : 0.f), -1.0, 1.0);
        const double h = std::max(static_cast<double>(std::isfinite(altitudeMeters) ? altitudeMeters : 0.f), 0.0);
        const double r = earthRadiusMeters;
        return static_cast<float>(-r * dz + std::sqrt(r * r * dz * dz + 2.0 * r * h + h * h));
    }

    // Where a view direction meets the layer, in metres east and north of the viewer.
    inline vsg::vec2 layerPoint(const vsg::vec3& unitDir, float altitudeMeters)
    {
        const float t = shellDistance(unitDir.z, altitudeMeters);
        return vsg::vec2(t * unitDir.x, t * unitDir.y);
    }

    // The layer point in the wind's frame: x along the wind, y across it multiplied by `fibreStretch`, so the pattern
    // is `fibreStretch` times narrower across the wind than along it (fibres). `windDir` is a unit vector.
    inline vsg::vec2 windFrame(const vsg::vec2& p, const vsg::vec2& windDir)
    {
        return vsg::vec2(p.x * windDir.x + p.y * windDir.y, fibreStretch * (-p.x * windDir.y + p.y * windDir.x));
    }

    // --- Light --------------------------------------------------------------------------------------------------

    // How far under the horizon the sun can be and still light a layer straight overhead at `altitudeMeters`:
    // acos(R / (R + h)) (first-order geometry; the research note, section 2): 2.5, 3.0 and 3.5 degrees for 6, 9 and
    // 12 km.
    inline float depressionLimit(float altitudeMeters)
    {
        const double h = std::max(static_cast<double>(std::isfinite(altitudeMeters) ? altitudeMeters : 0.f), 0.0);
        return static_cast<float>(std::acos(earthRadiusMeters / (earthRadiusMeters + h)));
    }

    // 1 while the sun is up, then falling to 0 over the depression limit: the cirrus stays lit after the ground is
    // dark. `sunElevation` is the true height of the sun in radians (negative below the horizon).
    inline float litWeight(float sunElevation, float altitudeMeters)
    {
        if (!(sunElevation < 0.f))
        {
            return 1.f;
        }
        return 1.f - CloudVolume::smoothstep(0.f, depressionLimit(altitudeMeters), -sunElevation);
    }

    // The forward lobe of the light in the layer, 4 pi times the Henyey-Greenstein phase (1 means "as bright as an
    // even spread"), capped.
    inline float forwardPhase(float cosTheta)
    {
        const float value
            = 4.f * pi * Atmosphere::henyeyGreenstein(std::isfinite(cosTheta) ? cosTheta : 0.f, phaseAnisotropy);
        return std::min(value, phaseCap);
    }

    // --- Pixel footprint ----------------------------------------------------------------------------------------

    // The weight of a noise octave of `wavelengthMeters` for a pixel that covers `footprintMeters`: all of it while
    // the pixel is at most a quarter of the wavelength, none once the pixel is as big as the wavelength. Fades the fine
    // octaves near the horizon, where one pixel covers kilometres, so that the layer does not shimmer.
    inline float octaveWeight(float footprintMeters, float wavelengthMeters)
    {
        if (!(footprintMeters > 0.f))
        {
            return 1.f;
        }
        if (!(wavelengthMeters > 0.f))
        {
            return 0.f;
        }
        return 1.f - CloudVolume::smoothstep(0.25f * wavelengthMeters, wavelengthMeters, footprintMeters);
    }

    // The length of the layer one pixel covers, for a pixel `pixelAngleRadians` wide: alpha t (R + h) / (R dz + t) on
    // the sphere (derived in the research note, section 2; the flat-plane alpha t^2 / h overstates it about two times
    // at the horizon). alpha h overhead, alpha (R + h) at the horizon.
    inline float footprint(float pixelAngleRadians, float dirZ, float altitudeMeters)
    {
        if (!(pixelAngleRadians > 0.f))
        {
            return 0.f;
        }
        const double dz = std::clamp(static_cast<double>(std::isfinite(dirZ) ? dirZ : 0.f), -1.0, 1.0);
        const double h = std::max(static_cast<double>(std::isfinite(altitudeMeters) ? altitudeMeters : 0.f), 0.0);
        const double t = shellDistance(static_cast<float>(dz), static_cast<float>(h));
        const double denominator = std::max(earthRadiusMeters * dz + t, 1.0);
        const double value = static_cast<double>(pixelAngleRadians) * t * (earthRadiusMeters + h) / denominator;
        return static_cast<float>(std::clamp(value, 0.0, 1e7));
    }

    // 0 at and under the dome's cut, 1 from horizonFadeEndZ up.
    inline float horizonFade(float dirZ)
    {
        return CloudVolume::smoothstep(horizonCutZ, horizonFadeEndZ, dirZ);
    }

    // --- The weather -------------------------------------------------------------------------------------------

    // The share of the layer the weather allows: cirrus is the cloud of fair weather, so it fades out as the low
    // layers thicken (the low cloud is in front of it and cirrus cannot be seen in storms, fog or snow). `lowCoverage`
    // is the vanilla pictures' coverage (Render::CloudVolume::coverageFromThickness, blended); the storm and fog gates
    // are the volumetric clouds' (`volumeShareTarget`).
    inline float shareTarget(float lowCoverage, bool isStorm, float landFogDepth)
    {
        const float low = 1.f - CloudVolume::smoothstep(0.55f, 0.95f, std::isfinite(lowCoverage) ? lowCoverage : 0.f);
        return std::clamp(low * CloudVolume::volumeShareTarget(isStorm, landFogDepth), 0.f, 1.f);
    }

    // Some days have more cirrus than others: a factor from the game date, 0.4 (a faint day) to 1 (a full one), a hash
    // of the day of the month and the month (a fixed seed, no clock and no generator: the same date gives the same
    // answer in every session). It multiplies the weather's share, so fair weather is not always the same sky.
    constexpr float dayFactorMin = 0.4f;
    inline float dayFactor(int day, int month)
    {
        const std::uint32_t key = static_cast<std::uint32_t>(std::clamp(month, 0, 11) * 32 + std::clamp(day, 0, 31));
        const std::uint32_t seed = CloudVolume::noiseSeed + 0x00C1AA06u;
        const float unit = CloudVolume::hashToUnit(CloudVolume::hashUint(key + CloudVolume::hashUint(seed)));
        return dayFactorMin + (1.f - dayFactorMin) * unit;
    }

    // The share moves to its target over `shareSeconds`, without overshoot.
    inline float easedShare(float current, float target, float seconds)
    {
        const float step = std::max(seconds, 0.f) / shareSeconds;
        return current + std::clamp(target - current, -step, step);
    }

    // The visible optical depth for a density in [0, 1], the weather's share and the two user scales.
    inline float opticalDepth(float density, float share, float coverageScale, float opacityScale)
    {
        return tauMax * std::clamp(density, 0.f, 1.f) * std::clamp(share, 0.f, 1.f) * std::max(coverageScale, 0.f)
            * std::max(opacityScale, 0.f);
    }

    // Beer's law: how much of what is behind the layer it hides.
    inline float opacity(float tau)
    {
        return 1.f - std::exp(-std::max(tau, 0.f));
    }

    // The pattern's cover (the share of the sky with fibres) from the `cirrus coverage` setting.
    inline float patternCoverage(float coverageScale)
    {
        return std::clamp(baseCoverage * std::max(coverageScale, 0.f), 0.f, 1.f);
    }

    // --- The fibre field ----------------------------------------------------------------------------------------

    namespace Detail
    {
        // The first noise seed of the layer: the cloud noise's seed plus an offset, so the pattern is its own.
        constexpr std::uint32_t seedOffset = 0x00C1AA05u;

        inline float finiteOr(float v, float fallback)
        {
            return std::isfinite(v) ? v : fallback;
        }

        inline float lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }
    }

    // The noise level at which the cover starts, by pattern coverage; density rises from 0 there over the ramp.
    inline float densityEdge(float coverage)
    {
        return edgeAtZeroCoverage - edgeCoverageSlope * std::clamp(coverage, 0.f, 1.f);
    }

    namespace Detail
    {
        // A large field of the layer, in [0, 1]: Perlin noise with a lattice cell of cellLength / scale along and
        // across (true metres, in units of cellLength), tiling after the drift's period; faded to its mean 0.5 when
        // the pixel is bigger than the cell.
        inline float field(float fx, float fy, float qm, float scale, std::uint32_t seed, float footprintMeters)
        {
            const float cellLength = cellMeters * fibreLengthFactor;
            const int period = static_cast<int>(std::lround(static_cast<float>(noisePeriodCells) * 0.5f * scale));
            const float weight = octaveWeight(footprintMeters, cellLength / scale);
            if (!(weight > 0.f))
            {
                return 0.5f;
            }
            return lerp(0.5f, CloudVolume::perlin(fx * scale, fy * scale, qm * scale, period, seed), weight);
        }

        // `octaves` octaves of Perlin noise (each double the frequency, half the weight) with the lattice cell
        // 1 / alongScale of a cell along the wind and 1 / acrossScale across it, in [0, 1]; a faded octave is
        // replaced by the noise's mean.
        inline float fibres(float ux, float uy, float qm, float alongScale, float acrossScale, int octaves,
            std::uint32_t seed, float footprintMeters)
        {
            const float cellLength = cellMeters * fibreLengthFactor;
            float sum = 0.f;
            float total = 0.f;
            float weight = 0.5f;
            float scale = 1.f;
            for (int i = 0; i < octaves; ++i)
            {
                const float wavelength = cellLength / (acrossScale * scale);
                const int period
                    = static_cast<int>(std::lround(static_cast<float>(noisePeriodCells) * 0.5f * alongScale * scale));
                const float n = CloudVolume::perlin(ux * alongScale * scale, uy * acrossScale * scale, qm * scale,
                    period, seed + static_cast<std::uint32_t>(i));
                sum += weight * lerp(0.5f, n, octaveWeight(footprintMeters, wavelength));
                total += weight;
                weight *= 0.5f;
                scale *= 2.f;
            }
            return total > 0.f ? sum / total : 0.5f;
        }
    }

    // The fibre density in [0, 1] at a point of the wind frame (metres, the output of `windFrame`). The sky is patches
    // of cloud with clear gaps between them (patch); in each place the cloud is long, wide streaks or short tufts
    // (type), made of octaves of `Render::CloudVolume::perlin`; the whole field is slowly displaced (swirl, so the
    // fibres turn and bend from place to place) and domain warped (hooks); the fibres have heads and fading tails, a
    // faint broad veil lies over it all, and a threshold set by the pattern coverage cuts the shapes. `morph` is the
    // third noise coordinate (the evolution), `footprintMeters` the pixel's size on the layer, which fades each octave
    // and field away when the pixel is bigger than it is (0: no fading). 0 for coverage 0; never falls when the
    // coverage grows.
    inline float density(const vsg::vec2& windFramePoint, float morph, int octaves, int warps, float coverage,
        float footprintMeters = 0.f)
    {
        if (!(coverage > 0.f))
        {
            return 0.f;
        }
        const float cover = std::min(coverage, 1.f);
        const int octaveCount = std::clamp(octaves, 1, 5);
        const int warpCount = std::clamp(warps, 0, 2);
        const float footprintSize = Detail::finiteOr(footprintMeters, 0.f);
        const std::uint32_t seed = CloudVolume::noiseSeed + Detail::seedOffset;

        const float cellLength = cellMeters * fibreLengthFactor;
        // The wind frame has the across axis multiplied by fibreStretch; the fields are in true metres.
        const float fx = Detail::finiteOr(windFramePoint.x, 0.f) / cellLength;
        const float fy = Detail::finiteOr(windFramePoint.y, 0.f) / fibreStretch / cellLength;
        const float qm = Detail::finiteOr(morph, 0.f);

        // Where the cloud is, of what kind, and how the field is displaced.
        const float patchRaw = Detail::field(fx, fy, qm, patchScale, seed + seedPatch, footprintSize);
        const float patchValue = std::clamp(0.5f + (patchRaw - 0.5f) * patchContrast, 0.f, 1.f);
        const float typeNoise = Detail::field(fx, fy, qm, typeScale, seed + seedType, footprintSize);
        const float longShare = CloudVolume::smoothstep(typeLow, typeHigh, typeNoise);
        float ux
            = fx + swirlCells * 2.f * (Detail::field(fx, fy, qm, swirlScale, seed + seedSwirl, footprintSize) - 0.5f);
        // Across the fibres the displacement is smaller, and the type field (the next scale up) serves as its noise.
        float uy = fy + swirlAcrossShare * swirlCells * 2.f * (typeNoise - 0.5f);

        // The gaps: where the patch field is low the sky is clear. A pixel in a gap needs nothing of the fibres, so
        // none of them is evaluated there (the same value, d times 0 is 0, for a lot less work).
        const float gapEdge = gapEdgeAtZero - gapEdgeSlope * cover;
        const float gate = CloudVolume::smoothstep(gapEdge, gapEdge + gapWidth, patchValue);
        float d = 0.f;
        if (gate > 0.f)
        {
            // The warp: low frequency noise (half the base frequency) moves the lookup point, more for tufts, so that
            // the fibres bend and hook.
            const float warpWeight = octaveWeight(footprintSize, 2.f * cellLength / (fibreStretch * fibreWidthFactor));
            const float hook = 1.f + hookWarpGain * (1.f - longShare);
            const int warpPeriod = noisePeriodCells / 4;
            for (int w = 0; w < warpCount; ++w)
            {
                const std::uint32_t warpSeed = seed + 100u + 2u * static_cast<std::uint32_t>(w);
                const float n1 = CloudVolume::perlin(ux * 0.5f, uy * 1.f, qm * 0.5f, warpPeriod, warpSeed);
                const float n2 = CloudVolume::perlin(ux * 0.5f, uy * 1.f, qm * 0.5f, warpPeriod, warpSeed + 1u);
                ux += warpWeight * warpAlongCells * hook * (n1 - 0.5f) * 2.f;
                uy += warpWeight * warpAcrossCells * hook * (n2 - 0.5f) * 2.f;
            }

            // The fibres: long streaks and short tufts, mixed by the type. A part with no share is not evaluated.
            const float valueLong = longShare > 0.f
                ? Detail::fibres(ux, uy, qm, longAlong, longAcross, std::max(octaveCount - 1, 2), seed, footprintSize)
                : 0.5f;
            const float valueTuft = longShare < 1.f
                ? Detail::fibres(ux, uy, qm, tuftAlong, tuftAcross, tuftOctaves, seed + seedTuft, footprintSize)
                : 0.5f;
            float value = Detail::lerp(valueTuft, valueLong, longShare);
            // Clumps of the sky thicken the fibres.
            value = std::clamp(value + clumpThicken * (patchValue - 0.5f), 0.f, 1.f);

            const float edge = densityEdge(cover);
            const float softness = Detail::lerp(edgeSoftnessTufts, edgeSoftnessLong, longShare);
            d = CloudVolume::smoothstep(edge, edge + softness, value);

            // Heads and tails: along the (displaced) fibre direction a sawtooth whose phase wanders with a noise, so
            // the fibres start sharply and fade away. Faded out for pixels bigger than its tooth.
            const float tailNoise = CloudVolume::perlin(ux * tailScale, uy * tailAcross, qm * tailScale,
                static_cast<int>(std::lround(static_cast<float>(noisePeriodCells) * 0.5f * tailScale)),
                seed + seedTail);
            const float phase = ux * tailScale + tailPhase * 2.f * (tailNoise - 0.5f);
            const float saw = phase - std::floor(phase);
            const float tail = CloudVolume::smoothstep(0.f, tailHead, saw)
                * (1.f - tailFloor * CloudVolume::smoothstep(tailHead, 1.f, saw));
            const float tailAmount = tailStrength * (0.4f + 0.6f * longShare) * octaveWeight(footprintSize, tailMeters);
            d *= Detail::lerp(1.f, tail, tailAmount);
            d *= gate;
        }

        // The veil: broad and faint, where the patch field is moderately high.
        const float veil = veilAmount * CloudVolume::smoothstep(veilLow, veilHigh, patchRaw);
        return std::clamp(d + veil * (1.f - d), 0.f, 1.f);
    }

    // --- Wind and time ------------------------------------------------------------------------------------------

    // The cirrus wind's direction: the low layer's (Render::CloudVolume::windDirection) turned by veerRadians.
    inline vsg::vec2 veeredWindDirection()
    {
        const float c = std::cos(veerRadians);
        const float s = std::sin(veerRadians);
        const float x = CloudVolume::windDirection[0];
        const float y = CloudVolume::windDirection[1];
        return vsg::vec2(c * x - s * y, s * x + c * y);
    }

    // How far the pattern moves in `seconds` of simulation time: the low layer's wind step at `windScale` times its
    // speed. The caller sums it in double and wraps it with `wrapDrift`.
    inline float driftDistance(float cloudSpeed, float windScale, float seconds)
    {
        if (!(cloudSpeed > 0.f) || !(windScale > 0.f) || !(seconds > 0.f))
        {
            return 0.f;
        }
        return CloudVolume::windStep(cloudSpeed * windScale, seconds);
    }

    inline std::array<float, 2> driftStep(float cloudSpeed, float windScale, float seconds)
    {
        const float distance = driftDistance(cloudSpeed, windScale, seconds);
        const vsg::vec2 direction = veeredWindDirection();
        return { distance * direction.x, distance * direction.y };
    }

    // The pattern repeats along the wind after this many metres.
    constexpr double driftPeriodMeters = static_cast<double>(noisePeriodCells) * static_cast<double>(cellMeters);

    // The summed drift wrapped into one period of the pattern, so the float handed to the shader keeps its precision
    // and the noise (periodic in that distance) shows no jump. 0 for a value that is not finite.
    inline double wrapDrift(double meters)
    {
        if (!std::isfinite(meters))
        {
            return 0.0;
        }
        double wrapped = std::fmod(meters, driftPeriodMeters);
        if (wrapped < 0.0)
        {
            wrapped += driftPeriodMeters;
        }
        // A tiny negative input rounds to the period itself in a double.
        return wrapped >= driftPeriodMeters ? 0.0 : wrapped;
    }

    // The evolution coordinate for the summed simulation seconds, wrapped by the noise period (all three noise axes
    // repeat after noisePeriodCells).
    inline float wrapMorph(double seconds)
    {
        if (!std::isfinite(seconds))
        {
            return 0.f;
        }
        const double units = seconds / static_cast<double>(morphSeconds);
        double wrapped = std::fmod(units, static_cast<double>(noisePeriodCells));
        if (wrapped < 0.0)
        {
            wrapped += static_cast<double>(noisePeriodCells);
        }
        const float result = static_cast<float>(wrapped);
        return result >= static_cast<float>(noisePeriodCells) ? 0.f : result;
    }

    // --- Settings -----------------------------------------------------------------------------------------------

    // The layer needs the linear light path (hdr) and the elevation model (atmosphere).
    inline bool useCirrus(bool setting, bool hdr, bool atmosphere)
    {
        return setting && hdr && atmosphere;
    }

    // The thickness grids of the cloud pictures (their mean gives the weather's coverage) are built when anything
    // reads them: the cloud shadows, the volumetric clouds, or the cirrus's weather share.
    inline bool needsCloudGrids(bool cloudShadows, bool volumetricClouds, bool cirrus)
    {
        return cloudShadows || volumetricClouds || cirrus;
    }

    enum class Quality
    {
        Low,
        Medium,
        High
    };

    inline std::optional<Quality> parseQuality(std::string_view name)
    {
        if (name == "low")
        {
            return Quality::Low;
        }
        if (name == "medium")
        {
            return Quality::Medium;
        }
        if (name == "high")
        {
            return Quality::High;
        }
        return std::nullopt;
    }

    // Noise octaves and domain warps by quality.
    inline int qualityOctaves(Quality quality)
    {
        return quality == Quality::Low ? 3 : (quality == Quality::Medium ? 4 : 5);
    }

    inline int qualityWarps(Quality quality)
    {
        return quality == Quality::Low ? 0 : (quality == Quality::Medium ? 1 : 2);
    }

    // The numbers are the shader's (the debug view is a float in the material).
    enum class DebugView
    {
        None,
        Density,
        Share,
        Light,
        Footprint,
        Pattern // the density on a fixed 40 km square of the wind frame, whatever the camera (compares with the C++)
    };

    inline std::optional<DebugView> parseDebugView(std::string_view name)
    {
        if (name == "none")
        {
            return DebugView::None;
        }
        if (name == "density")
        {
            return DebugView::Density;
        }
        if (name == "share")
        {
            return DebugView::Share;
        }
        if (name == "light")
        {
            return DebugView::Light;
        }
        if (name == "footprint")
        {
            return DebugView::Footprint;
        }
        if (name == "pattern")
        {
            return DebugView::Pattern;
        }
        return std::nullopt;
    }

    // --- The dome -----------------------------------------------------------------------------------------------

    // A unit direction of the dome, a triangle list of domeVertexCount vertices (domeColumns by domeRows quads, six
    // vertices each, corners (0,0), (1,0), (0,1), (1,0), (1,1), (0,1)) from the horizon 3 degrees down to the zenith.
    inline vsg::vec3 domeVertex(std::uint32_t index)
    {
        static constexpr int cornerColumn[6] = { 0, 1, 0, 1, 1, 0 };
        static constexpr int cornerRow[6] = { 0, 0, 1, 0, 1, 1 };
        const std::uint32_t quad = index / 6u;
        const std::uint32_t corner = index % 6u;
        const int column = static_cast<int>(quad % static_cast<std::uint32_t>(domeColumns)) + cornerColumn[corner];
        const int row = static_cast<int>(quad / static_cast<std::uint32_t>(domeColumns)) + cornerRow[corner];
        const float azimuth = 2.f * pi * static_cast<float>(column) / static_cast<float>(domeColumns);
        const float elevation = -domeBelowHorizonRadians
            + (0.5f * pi + domeBelowHorizonRadians) * static_cast<float>(row) / static_cast<float>(domeRows);
        const float c = std::cos(elevation);
        return vsg::vec3(c * std::cos(azimuth), c * std::sin(azimuth), std::sin(elevation));
    }
}

#endif
