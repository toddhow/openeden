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

    // The shape of the fibre field (all chosen by eye; see the stage log of task 0079).
    constexpr float warpAlongCells = 1.5f; // domain warp displacement along the wind, in cells
    constexpr float warpAcrossCells = 0.75f; // and across it (in the stretched, narrow coordinate)
    constexpr float streakAmount = 0.35f; // the share of the fall-streak modulation
    constexpr float edgeAtZeroCoverage = 0.66f; // the noise level where density starts, at pattern coverage 0 ...
    constexpr float edgeCoverageSlope = 0.34f; // ... lowered by this much at coverage 1
    constexpr float edgeSoftness = 0.35f; // and the width of the ramp up to full density
    // The look of the fibres, by eye (frames of task 0079): the noise's cell is fibreLengthFactor times cellMeters
    // along the wind (its period then holds noisePeriodCells / fibreLengthFactor cells, so the pattern still repeats
    // after noisePeriodCells * cellMeters), and the across-wind coordinate is fibreWidthFactor times what fibreStretch
    // gives, so the fibres are 4 km long and 500 m wide (a 2 km cell with a stretch of 6 gave short dashes).
    constexpr float fibreLengthFactor = 2.f;
    constexpr float fibreWidthFactor = 4.f / 3.f;

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

    // The noise level at which the cover starts, by pattern coverage; density rises from 0 there to 1 over
    // edgeSoftness.
    inline float densityEdge(float coverage)
    {
        return edgeAtZeroCoverage - edgeCoverageSlope * std::clamp(coverage, 0.f, 1.f);
    }

    // The fibre density in [0, 1] at a point of the wind frame (metres, the output of `windFrame`): the Perlin noise of
    // `Render::CloudVolume::perlin` (the cloud noise, a 3D gradient noise) in octaves, a domain warp that shears the
    // field, a slow fall-streak modulation and a threshold set by the pattern coverage. `morph` is the third noise
    // coordinate (the evolution), `footprintMeters` the pixel's size on the layer, which fades each octave and the
    // warp away when the pixel is bigger than they are (0: no fading). 0 for coverage 0.
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
        const int period = static_cast<int>(static_cast<float>(noisePeriodCells) / fibreLengthFactor);

        const float cellLength = cellMeters * fibreLengthFactor;
        float qx = Detail::finiteOr(windFramePoint.x, 0.f) / cellLength;
        float qy = Detail::finiteOr(windFramePoint.y, 0.f) * fibreWidthFactor / cellLength;
        const float qz = Detail::finiteOr(morph, 0.f);

        // The warp: low frequency noise (half the base frequency) moves the lookup point, more across the wind, so the
        // fibres bend and shear. Its wavelength across the wind is two cells over fibreStretch.
        const float warpWeight = octaveWeight(footprintSize, 2.f * cellLength / (fibreStretch * fibreWidthFactor));
        for (int w = 0; w < warpCount; ++w)
        {
            const std::uint32_t warpSeed = seed + 100u + 2u * static_cast<std::uint32_t>(w);
            const float n1 = CloudVolume::perlin(qx * 0.5f, qy * 0.5f, qz * 0.5f, period / 2, warpSeed);
            const float n2 = CloudVolume::perlin(qx * 0.5f, qy * 0.5f, qz * 0.5f, period / 2, warpSeed + 1u);
            qx += warpWeight * warpAlongCells * (n1 - 0.5f) * 2.f;
            qy += warpWeight * warpAcrossCells * (n2 - 0.5f) * 2.f;
        }

        // The base field: perlinFbm's octaves (each double the frequency, half the weight), a faded octave being
        // replaced by the noise's mean 0.5.
        float sum = 0.f;
        float total = 0.f;
        float weight = 0.5f;
        float scale = 1.f;
        int periodScale = 1;
        for (int i = 0; i < octaveCount; ++i)
        {
            const float wavelength = cellLength / (fibreStretch * fibreWidthFactor * scale);
            const float n = CloudVolume::perlin(
                qx * scale, qy * scale, qz * scale, period * periodScale, seed + static_cast<std::uint32_t>(i));
            sum += weight * Detail::lerp(0.5f, n, octaveWeight(footprintSize, wavelength));
            total += weight;
            weight *= 0.5f;
            scale *= 2.f;
            periodScale *= 2;
        }
        float value = total > 0.f ? sum / total : 0.5f;

        // Fall streaks: a very slow modulation (a quarter of the base frequency) that makes some fibres stand out.
        const float streak = CloudVolume::perlin(qx * 0.25f, qy * 0.25f, qz * 0.25f, period / 4, seed + 200u);
        value = std::clamp(value + streakAmount * (streak - 0.5f), 0.f, 1.f);

        const float edge = densityEdge(cover);
        return CloudVolume::smoothstep(edge, edge + edgeSoftness, value);
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
