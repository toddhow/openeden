#ifndef OPENEDEN_RENDER_CLOUDVOLUMEMATH_H
#define OPENEDEN_RENDER_CLOUDVOLUMEMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <components/misc/constants.hpp>

#include "volumemath.hpp"

/*
 * OpenEden (task 0042): the pure maths of the ray-marched volumetric clouds. No VSG or game types, no device.
 *
 * The clouds are a layer of space between two heights, filled with a 3D density that is built from noise. To draw a
 * pixel the compute shader (`files/shaders/comp/cloud/cloud.slang`) walks a ray from the camera through the layer, adds
 * up the sunlight that reaches each step, and dims whatever lies behind. This header holds the parts that can be
 * checked by hand: the height gradient, the density function, Beer's law and the powder term, the two-lobe phase
 * function, the multiple-scattering approximation, the ray and layer meeting, the sample jitter, the weather mapping
 * and the noise used to build the textures. The shader repeats the formulas that run on the GPU: change them together
 * (a test reads the shader's constants back).
 *
 * Heights are in metres in this file and are turned into game units at one place, `metersToUnits`.
 */
namespace Render::CloudVolume
{
    // --- Settings and tiers ----------------------------------------------------------------------------------------

    enum class Quality
    {
        Low,
        Medium,
        High
    };

    inline std::optional<Quality> parseQuality(std::string_view name)
    {
        if (name == "low")
            return Quality::Low;
        if (name == "medium")
            return Quality::Medium;
        if (name == "high")
            return Quality::High;
        return std::nullopt;
    }

    // D11: the clouds need the HDR path (they are lit in linear light) and the atmosphere (the sun's colour). They do
    // not need the G-buffer: the history is reprojected by the camera's rotation alone (the clouds are so far away that
    // moving the camera does not shift them), so no velocity image is read.
    inline bool useVolumetricClouds(bool setting, bool hdr, bool atmosphere)
    {
        return setting && hdr && atmosphere;
    }

    // D7: the march runs on a grid `resolutionDivisor` times coarser than the screen, takes `steps` steps through the
    // layer and `lightSteps` samples towards the sun at each. `budgetMs` is the target cost on the benchmark machine
    // (D8).
    struct TierSettings
    {
        std::uint32_t resolutionDivisor;
        std::uint32_t steps;
        std::uint32_t lightSteps;
        float budgetMs;
    };

    inline TierSettings tierSettings(Quality quality)
    {
        switch (quality)
        {
            case Quality::Low:
                return { 8, 24, 4, 1.0f };
            case Quality::High:
                return { 2, 64, 6, 3.0f };
            case Quality::Medium:
                break;
        }
        return { 4, 40, 5, 1.8f };
    }

    // The size of the marched image for a screen of `width` by `height` (never less than one texel).
    inline std::array<std::uint32_t, 2> marchExtent(std::uint32_t width, std::uint32_t height, Quality quality)
    {
        const std::uint32_t divisor = tierSettings(quality).resolutionDivisor;
        return { std::max<std::uint32_t>(1, (width + divisor - 1) / divisor),
            std::max<std::uint32_t>(1, (height + divisor - 1) / divisor) };
    }

    // --- Units and the layer ---------------------------------------------------------------------------------------

    // The one place metres become game units (Constants::UnitsPerMeter).
    constexpr float metersToUnits(float meters)
    {
        return meters * Constants::UnitsPerMeter;
    }

    constexpr float unitsToMeters(float units)
    {
        return units / Constants::UnitsPerMeter;
    }

    // D2: one cumulus layer, 1500 to 4000 m above sea level (the game's z = 0).
    constexpr float layerBaseMeters = 1500.f;
    constexpr float layerTopMeters = 4000.f;

    // 0 at `base`, 1 at `top`, clamped.
    inline float heightFraction(float height, float base, float top)
    {
        if (top <= base)
            return 0.f;
        return std::clamp((height - base) / (top - base), 0.f, 1.f);
    }

    inline float smoothstep(float edge0, float edge1, float x)
    {
        if (edge1 <= edge0)
            return x < edge0 ? 0.f : 1.f;
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // b0 + (v - a0) / (a1 - a0) * (b1 - b0), and b0 when the input range is empty.
    inline float remap(float v, float a0, float a1, float b0, float b1)
    {
        if (a1 == a0)
            return b0;
        return b0 + (v - a0) / (a1 - a0) * (b1 - b0);
    }

    inline float saturate(float v)
    {
        return std::clamp(v, 0.f, 1.f);
    }

    // The height gradient makes bases flat and tops rounded (Schneider 2015): it rises from 0 at the base to 1 at a
    // tenth of the layer, stays at 1, and falls back to 0 between 0.6 and the top. `fraction` is `heightFraction`.
    inline float heightGradient(float fraction)
    {
        return smoothstep(0.f, 0.1f, fraction) * (1.f - smoothstep(0.6f, 1.f, fraction));
    }

    // --- Density ---------------------------------------------------------------------------------------------------

    // The big forms: the Perlin-Worley value eroded by the Worley fBm (Schneider 2015): saturate(remap(perlinWorley,
    // worleyFbm - 1, 1, 0, 1)). Both inputs are in 0 to 1.
    inline float shapeBase(float perlinWorley, float worleyFbm)
    {
        return saturate(remap(perlinWorley, worleyFbm - 1.f, 1.f, 0.f, 1.f));
    }

    // How hard the detail noise eats the cloud's edges (0 none, 1 all the way).
    constexpr float defaultErosion = 0.85f;

    // The density, 0 to 1, from the shape value (0 to 1), the detail value (0 to 1), the weather's coverage (0 clear
    // to 1 overcast) and the height gradient:
    //   base = shape * gradient
    //   d    = saturate(remap(base, 1 - coverage, 1, 0, 1))        only where coverage reaches: nothing at coverage 0
    //   d    = saturate(remap(d, detail * erosion, 1, 0, 1))        the detail eats the thin edges
    // Zero when coverage or gradient is zero. With coverage 1, detail 0 and gradient 1 it is `shape`.
    inline float cloudDensity(float shape, float detail, float coverage, float gradient, float erosion = defaultErosion)
    {
        if (coverage <= 0.f || gradient <= 0.f)
            return 0.f;
        const float base = saturate(shape) * saturate(gradient);
        float d = saturate(remap(base, 1.f - saturate(coverage), 1.f, 0.f, 1.f));
        d = saturate(remap(d, saturate(detail) * saturate(erosion), 1.f, 0.f, 1.f));
        return d;
    }

    // The extinction of a density of 1, per metre of path (a chosen value: cumulus is 0.03 to 0.1 per metre in the
    // literature; looked at in frames). The setting `volumetric clouds density` scales it.
    constexpr float extinctionPerMeter = 0.045f;

    // The optical depth (extinction times length) of `density` over `stepUnits` game units.
    inline float opticalDepth(float density, float densityScale, float stepUnits)
    {
        return std::max(density, 0.f) * std::max(densityScale, 0.f) * extinctionPerMeter
            * unitsToMeters(std::max(stepUnits, 0.f));
    }

    // The sum of the extinctions of equally long steps: the optical depth along a ray. `extinctions` are per metre,
    // `stepMeters` the length of each step.
    inline float opticalDepthAlong(const float* extinctions, std::size_t count, float stepMeters)
    {
        float sum = 0.f;
        for (std::size_t i = 0; i < count; ++i)
            sum += std::max(extinctions[i], 0.f);
        return sum * std::max(stepMeters, 0.f);
    }

    // --- Light -----------------------------------------------------------------------------------------------------

    // Beer's law: light fades exponentially with the optical depth it crosses.
    inline float beer(float opticalDepthValue)
    {
        return std::exp(-std::max(opticalDepthValue, 0.f));
    }

    // The closed form of a uniform slab: exp(-sigma * thickness).
    inline float slabTransmittance(float sigma, float thickness)
    {
        return std::exp(-std::max(sigma, 0.f) * std::max(thickness, 0.f));
    }

    // The powder term darkens the thin edges that Beer's law alone would light fully: 1 - exp(-2 d).
    inline float powder(float opticalDepthValue)
    {
        return 1.f - std::exp(-2.f * std::max(opticalDepthValue, 0.f));
    }

    // Beer-Powder (Schneider 2015): 2 exp(-d) (1 - exp(-2 d)). It is 0 at d = 0, rises to a peak and then tends to 0 as
    // d grows.
    inline float beerPowder(float opticalDepthValue)
    {
        return 2.f * beer(opticalDepthValue) * powder(opticalDepthValue);
    }

    // D5: the two lobes of a cloud droplet's scattering, forward (a bright lining against the sun) and back (a faint
    // glow away from it). Each is a Henyey-Greenstein lobe that integrates to 1; they are mixed by `forwardWeight`.
    constexpr float forwardAnisotropy = 0.8f;
    constexpr float backAnisotropy = -0.3f;
    constexpr float forwardWeight = 0.5f;

    // `cosTheta` is the dot product of the ray from the eye and the direction to the sun (1 looks at the sun).
    inline float dualLobePhase(
        float cosTheta, float gForward = forwardAnisotropy, float gBack = backAnisotropy, float weight = forwardWeight)
    {
        const float w = saturate(weight);
        return w * Render::Volume::henyeyGreenstein(cosTheta, gForward)
            + (1.f - w) * Render::Volume::henyeyGreenstein(cosTheta, gBack);
    }

    // The multiple-scattering approximation (Wrenninge 2015, "Art-directable multiple volumetric scattering"): the sum
    // over `octaves` of b^i * phase(cosTheta; g * c^i) * beer(d * a^i), with a = b = c = 0.5: the lobes' anisotropies
    // (g) shrink by c each octave. Each octave stands for light that scattered one more time: it reaches deeper (less
    // extinction), carries less energy, and has a flatter phase, tending to even scattering. One octave is the single
    // scattering `dualLobePhase(cosTheta) * beer(d)`.
    constexpr float multiScatterExtinction = 0.5f; // a
    constexpr float multiScatterEnergy = 0.5f; // b
    constexpr float multiScatterPhase = 0.5f; // c
    constexpr std::uint32_t maxOctaves = 8;

    inline float multiScatter(float opticalDepthToSun, float cosTheta, std::uint32_t octaves)
    {
        float sum = 0.f;
        float a = 1.f;
        float b = 1.f;
        float c = 1.f;
        const std::uint32_t count = std::min(octaves, maxOctaves);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            sum += b * dualLobePhase(cosTheta, forwardAnisotropy * c, backAnisotropy * c) * beer(opticalDepthToSun * a);
            a *= multiScatterExtinction;
            b *= multiScatterEnergy;
            c *= multiScatterPhase;
        }
        return sum;
    }

    // Bases are darker than tops because they see less of the sky: the ambient light is scaled from `minimum` at the
    // base to 1 at the top.
    constexpr float baseAmbientShare = 0.35f;

    inline float ambientHeightFactor(float fraction)
    {
        return baseAmbientShare + (1.f - baseAmbientShare) * saturate(fraction);
    }

    // The share of the cloud's colour that moves to the haze at `distanceMeters` when the air's e-folding length is
    // `hazeLengthMeters`: 1 - exp(-d / L). D9: distant clouds fade into the haze.
    inline float hazeBlend(float distanceMeters, float hazeLengthMeters)
    {
        return 1.f - std::exp(-std::max(distanceMeters, 0.f) / std::max(hazeLengthMeters, 1.f));
    }

    // The haze length, in metres, for a fog that ends at `fogEndUnits` game units: the clear weather's fog ends near a
    // hundred metres, which is a haze of tens of kilometres at the clouds' distance. A chosen ratio, by eye.
    constexpr float hazeLengthPerFogMeter = 400.f;

    inline float hazeLengthMeters(float fogEndUnits)
    {
        return std::max(unitsToMeters(fogEndUnits) * hazeLengthPerFogMeter, 1000.f);
    }

    // --- Rays and steps --------------------------------------------------------------------------------------------

    struct Span
    {
        bool hit = false;
        float enter = 0.f; // distance along the ray (same unit as the heights' unit)
        float exit = 0.f;
    };

    // Where a ray that starts at `originHeight` and has the z component `dirZ` of its unit direction crosses the flat
    // layer between `base` and `top` (all heights in one unit). Starting inside the layer, the span begins at 0. A
    // ray that looks away from the layer, or runs along it from outside, misses. `maxDistance` caps the exit (a ray
    // inside the layer that runs along it would otherwise never leave).
    inline Span layerSpan(float originHeight, float dirZ, float base, float top, float maxDistance)
    {
        Span span;
        // A NaN or infinite height or direction (or a NaN distance cap) is a miss, never a span.
        if (!std::isfinite(originHeight) || !std::isfinite(dirZ) || !std::isfinite(base) || !std::isfinite(top)
            || std::isnan(maxDistance))
            return span;
        constexpr float flat = 1e-5f;
        if (top <= base)
            return span;
        if (std::abs(dirZ) < flat)
        {
            if (originHeight < base || originHeight > top)
                return span;
            span.hit = true;
            span.enter = 0.f;
            span.exit = std::max(maxDistance, 0.f);
            return span;
        }
        // Never zero here (the flat case returned above); written so the compiler can see it.
        const float slope = (dirZ < 0.f ? -1.f : 1.f) * std::max(std::abs(dirZ), flat);
        float t0 = (base - originHeight) / slope;
        float t1 = (top - originHeight) / slope;
        if (t0 > t1)
            std::swap(t0, t1);
        if (t1 < 0.f)
            return span;
        span.enter = std::max(t0, 0.f);
        span.exit = std::min(t1, std::max(maxDistance, 0.f));
        span.hit = span.exit > span.enter;
        return span;
    }

    // The distance of the i-th of `steps` samples inside a span, with `offset` (0 to 1) as the shift of the whole
    // pattern inside a step: enter + (i + offset) * (exit - enter) / steps.
    inline float sampleDistance(const Span& span, std::uint32_t index, std::uint32_t steps, float offset)
    {
        if (steps == 0)
            return span.enter;
        return span.enter
            + (static_cast<float>(index) + saturate(offset)) * (span.exit - span.enter) / static_cast<float>(steps);
    }

    // The samples towards the sun grow longer with distance (near ones matter most): the i-th of `count` steps is
    // (i + 1) / (count (count + 1) / 2) of `totalLength`, so the steps add up to the total.
    inline float lightStepLength(std::uint32_t index, std::uint32_t count, float totalLength)
    {
        if (count == 0)
            return 0.f;
        const float sum = static_cast<float>(count) * static_cast<float>(count + 1) * 0.5f;
        return totalLength * static_cast<float>(index + 1) / sum;
    }

    // How far the sun rays are followed through the layer (metres): a fixed length, about a quarter of the layer's
    // thickness, since density beyond it adds little.
    constexpr float lightRayMeters = 600.f;

    // A ray whose transmittance has fallen below this ends early (the remaining light is almost gone).
    constexpr float minTransmittance = 0.01f;

    // Interleaved gradient noise (Jimenez 2014): a cheap, well spread value in [0, 1) from the pixel's integer
    // position, used to shift each ray's samples so the banding turns into fine noise.
    inline float interleavedGradientNoise(float x, float y)
    {
        const float inner = 0.06711056f * x + 0.00583715f * y;
        const float outer = 52.9829189f * (inner - std::floor(inner));
        return outer - std::floor(outer);
    }

    // The shift of the marched ray inside its block this frame, in [0, 1) on each axis: the same fixed Halton sequence
    // the fog uses, indexed by the frame counter and never by a clock.
    inline std::array<float, 2> rayJitter(std::uint32_t frame)
    {
        const auto j = Render::Volume::jitter(frame);
        return { j[0], j[1] };
    }

    // --- The bicubic upscale of the march image --------------------------------------------------------------------

    // The Catmull-Rom weights of the four taps at -1, 0, 1 and 2 around a fraction `t` (0 to 1) between taps 0 and 1:
    // they add up to 1, give tap 0 at t = 0 and tap 1 at t = 1, reproduce a straight ramp, and have small negative
    // lobes on the outer taps (the sharper look of a bicubic filter). A fraction outside 0 to 1 is clamped, and a
    // value that is not a number is 0. The shader repeats it (`catmullRomWeights` in cloud.slang).
    inline std::array<float, 4> catmullRomWeights(float t)
    {
        const float x = std::isfinite(t) ? std::clamp(t, 0.f, 1.f) : 0.f;
        const float x2 = x * x;
        const float x3 = x2 * x;
        return { -0.5f * x3 + x2 - 0.5f * x, 1.5f * x3 - 2.5f * x2 + 1.f, -1.5f * x3 + 2.f * x2 + 0.5f * x,
            0.5f * x3 - 0.5f * x2 };
    }

    // The bicubic result is kept between the two taps it lies between, so the negative lobes cannot ring past the
    // edge of a cloud or go below 0 (and a value that is not a number gives `a`).
    inline float clampToNearestTaps(float value, float a, float b)
    {
        if (!std::isfinite(value))
            return a;
        return std::clamp(value, std::min(a, b), std::max(a, b));
    }

    // --- Weather ---------------------------------------------------------------------------------------------------

    // D4: Morrowind's weather records carry no coverage value of their own, only the cloud picture (and its scroll
    // speed), so the coverage is read from the picture: the mean `thickness` of its pixels (0 thin to 1 thick, task
    // 0038's `Render::Cloud::thickness`) goes through this table, a piecewise linear curve. The first column is the
    // picture's mean thickness, the second the coverage. A chosen mapping, set by looking at the three vanilla
    // pictures (clear, cloudy, overcast) in frames; it can be tuned without changing a record.
    struct CoveragePoint
    {
        float thickness;
        float coverage;
    };
    constexpr std::array<CoveragePoint, 5> coverageTable{ { { 0.f, 0.15f }, { 0.1f, 0.3f }, { 0.3f, 0.5f },
        { 0.55f, 0.75f }, { 0.8f, 1.f } } };

    inline float coverageFromThickness(float meanThickness)
    {
        const float x = std::isfinite(meanThickness) ? meanThickness : 0.f;
        if (x <= coverageTable.front().thickness)
            return coverageTable.front().coverage;
        for (std::size_t i = 1; i < coverageTable.size(); ++i)
        {
            const CoveragePoint& a = coverageTable[i - 1];
            const CoveragePoint& b = coverageTable[i];
            if (x <= b.thickness)
                return remap(x, a.thickness, b.thickness, a.coverage, b.coverage);
        }
        return coverageTable.back().coverage;
    }

    // A weather change grows or thins the clouds smoothly: the coverage follows the weather manager's blend factor
    // (0 current, 1 next).
    inline float blendCoverage(float current, float next, float blendFactor)
    {
        const float t = saturate(blendFactor);
        return saturate(current + (next - current) * t);
    }

    // How fast the scroll runs: the weather record's cloud speed (1.25 clear, 3 thunderstorm) times this gives the
    // wind in metres per second of simulation time (no wall clock). A chosen value, set by looking at frames: real
    // cumulus drifts at 5 to 15 m/s, and the vanilla layers (speed * 0.003 of a 400 unit picture a second) are far
    // slower, so this is not derived from them. The caller keeps the sum in double and wraps it by the tile size.
    constexpr float windMetersPerSecondPerSpeed = 4.f;

    // The wind blows towards this horizontal direction (unit length), the same for every weather.
    constexpr std::array<float, 2> windDirection{ 0.9578263f, 0.2873479f };

    inline float windStep(float cloudSpeed, float seconds)
    {
        return std::max(cloudSpeed, 0.f) * windMetersPerSecondPerSpeed * std::max(seconds, 0.f);
    }

    // D13: the share of the sky that the volume draws, the rest being the vanilla layers. Storms (ash, blight and
    // blizzard have winds over the storm speed) keep their vanilla sky; so do weathers whose land fog is thick (the
    // record's land fog depth, 1 and over: fog, snow) fading in between 0.85 and 0.95.
    inline float volumeShareTarget(bool isStorm, float landFogDepth)
    {
        if (isStorm)
            return 0.f;
        return 1.f - smoothstep(0.85f, 0.95f, landFogDepth);
    }

    // The share moves to its target over this many seconds of simulation time.
    constexpr float shareSeconds = 3.f;

    inline float easeShare(float current, float target, float seconds)
    {
        const float step = std::max(seconds, 0.f) / shareSeconds;
        return current + std::clamp(target - current, -step, step);
    }

    // --- Where the noise is read -----------------------------------------------------------------------------------

    // D3: one tile of the shape texture (128 cubed) covers this many metres, and one tile of the detail texture (32
    // cubed) this many. The sampler repeats them. Chosen so the big forms are a few kilometres across.
    constexpr float shapeTileMeters = 12000.f;
    constexpr float detailTileMeters = 800.f;

    // The detail texture is read twice: at the coarse tile above and at this finer one (shifted, so the two lookups
    // are not the same cell), and the two are mixed by `combineDetail`. 12000 is a whole number of both tiles, which
    // `wrapWind` needs. A chosen value, set by looking at frames (task 0042's stage log, 2026-11-04): with only the
    // 1500 m tile the clouds read as smooth blobs.
    constexpr float detailFineTileMeters = 300.f;
    // The fine lookup's share of the mix, and the factor that puts back the spread that mixing two lookups takes out.
    constexpr float detailFineWeight = 0.4f;
    constexpr float detailContrast = 1.6f;

    // The detail value the density uses: 0.5 + (coarse (1 - w) + fine w - 0.5) * contrast, kept in 0 to 1 (a value that
    // is not a number counts as 0). Equal inputs of 0.5 give 0.5, so the mean erosion does not change.
    inline float combineDetail(float coarse, float fine)
    {
        const float c = std::isfinite(coarse) ? std::clamp(coarse, 0.f, 1.f) : 0.f;
        const float f = std::isfinite(fine) ? std::clamp(fine, 0.f, 1.f) : 0.f;
        const float mixed = c * (1.f - detailFineWeight) + f * detailFineWeight;
        return std::clamp(0.5f + (mixed - 0.5f) * detailContrast, 0.f, 1.f);
    }

    // The sun light is multiplied by this before the phase function (which integrates to 1 over the sphere, so 4 pi
    // would make an even scatterer reflect the whole sun). Half of that: with 4 pi the sunlit tops of cumulus clipped
    // to white in the tonemap (5 percent of a cloudy noon sky's cloud pixels and 85 percent of a clear one's).
    constexpr float sunPhaseScale = 6.28318530718f;

    constexpr std::uint32_t shapeSize = 128;
    constexpr std::uint32_t detailSize = 32;
    constexpr std::uint32_t curlSize = 128;

    // The fractional part, in [0, 1): where in a repeating texture a coordinate lands. Keeps the numbers small, since a
    // float loses precision far from the origin.
    inline float wrapCoordinate(float v)
    {
        // NaN and infinity have no fractional part; 0 keeps the result finite.
        if (!std::isfinite(v))
            return 0.f;
        const float f = v - std::floor(v);
        // A tiny negative input rounds to exactly 1 in a float; keep the result below 1.
        return f >= 1.f ? 0.f : f;
    }

    // The texture coordinate of a point (metres from the world origin) for a texture whose tile is `tileMeters`, with
    // the wind's offset: wrap((position + wind) / tileMeters).
    inline float noiseCoordinate(float positionMeters, float windMeters, float tileMeters)
    {
        return wrapCoordinate((positionMeters + windMeters) / tileMeters);
    }

    // --- The noise (a CPU reference of what the generation pass writes) ---------------------------------------------

    // The seed of every texture (D3: fixed, no art files).
    constexpr std::uint32_t noiseSeed = 0x0E0E1042u;

    // A 32 bit integer hash (the PCG output permutation of a mixed input), which the GPU repeats bit for bit.
    inline std::uint32_t hashUint(std::uint32_t v)
    {
        const std::uint32_t state = v * 747796405u + 2891336453u;
        const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // The hash of an integer lattice point: the coordinates are wrapped to `period` first, so the noise tiles.
    inline std::uint32_t hashCell(int x, int y, int z, int period, std::uint32_t seed)
    {
        const auto wrap = [period](int v) {
            const int m = v % period;
            return static_cast<std::uint32_t>(m < 0 ? m + period : m);
        };
        std::uint32_t h = hashUint(wrap(x) + hashUint(seed));
        h = hashUint(h ^ (wrap(y) * 0x9E3779B9u));
        h = hashUint(h ^ (wrap(z) * 0x85EBCA6Bu));
        return h;
    }

    // A hash as a float in [0, 1): the top 24 bits over 2^24, exact in a float.
    inline float hashToUnit(std::uint32_t h)
    {
        return static_cast<float>(h >> 8u) * (1.f / 16777216.f);
    }

    // Worley (cellular) noise tiling every `period` cells: the distance to the nearest of one random point a cell,
    // searched in the 27 cells around, as 1 - distance (so cell centres are bright), in [0, 1]. (x, y, z) are in cells.
    inline float worley(float x, float y, float z, int period, std::uint32_t seed)
    {
        const int cx = static_cast<int>(std::floor(x));
        const int cy = static_cast<int>(std::floor(y));
        const int cz = static_cast<int>(std::floor(z));
        float best = 1e9f;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int ix = cx + dx;
                    const int iy = cy + dy;
                    const int iz = cz + dz;
                    // Three hashes make the point: x from the cell's hash, y and z from its successors.
                    const std::uint32_t h = hashCell(ix, iy, iz, period, seed);
                    const float px = static_cast<float>(ix) + hashToUnit(h);
                    const float py = static_cast<float>(iy) + hashToUnit(hashUint(h + 1u));
                    const float pz = static_cast<float>(iz) + hashToUnit(hashUint(h + 2u));
                    const float ox = px - x;
                    const float oy = py - y;
                    const float oz = pz - z;
                    best = std::min(best, ox * ox + oy * oy + oz * oz);
                }
        // The nearest point is at most sqrt(3) away; one cell is a typical distance, so clamp there.
        return saturate(1.f - std::sqrt(best));
    }

    // Gradient (Perlin) noise tiling every `period` cells, in [0, 1]. The gradients are the twelve edge directions of a
    // cube picked by the lattice point's hash.
    inline float perlin(float x, float y, float z, int period, std::uint32_t seed)
    {
        static constexpr float gradients[12][3] = { { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 }, { 1, 0, 1 },
            { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 }, { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 } };
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int z0 = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const float fz = z - static_cast<float>(z0);
        const auto fade = [](float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); };
        const float u = fade(fx);
        const float v = fade(fy);
        const float w = fade(fz);
        float corner[2][2][2];
        for (int k = 0; k < 2; ++k)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                {
                    const auto& g = gradients[hashCell(x0 + i, y0 + j, z0 + k, period, seed) % 12u];
                    corner[k][j][i] = g[0] * (fx - static_cast<float>(i)) + g[1] * (fy - static_cast<float>(j))
                        + g[2] * (fz - static_cast<float>(k));
                }
        const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        const float x00 = lerp(corner[0][0][0], corner[0][0][1], u);
        const float x10 = lerp(corner[0][1][0], corner[0][1][1], u);
        const float x01 = lerp(corner[1][0][0], corner[1][0][1], u);
        const float x11 = lerp(corner[1][1][0], corner[1][1][1], u);
        const float value = lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
        // The extremes of this gradient set are about +-1; map to 0 to 1.
        return saturate(value * 0.5f + 0.5f);
    }

    // Perlin noise of several octaves (each double the frequency, half the weight), tiling, in [0, 1].
    inline float perlinFbm(float x, float y, float z, int period, std::uint32_t seed, int octaves)
    {
        float sum = 0.f;
        float weight = 0.5f;
        float total = 0.f;
        float scale = 1.f;
        for (int i = 0; i < octaves; ++i)
        {
            sum += weight
                * perlin(x * scale, y * scale, z * scale, period * static_cast<int>(scale),
                    seed + static_cast<std::uint32_t>(i));
            total += weight;
            weight *= 0.5f;
            scale *= 2.f;
        }
        return total > 0.f ? sum / total : 0.f;
    }

    // Perlin-Worley (Schneider 2015): the Perlin fBm remapped to start where the Worley noise ends, so billows get the
    // cell-like puffs of Worley on a smooth base: saturate(remap(perlin, worley, 1, 0, 1)).
    inline float perlinWorley(float perlinValue, float worleyValue)
    {
        return saturate(remap(perlinValue, worleyValue, 1.f, 0.f, 1.f));
    }

    // The frequencies (cells across a texture) of the shape texture's Worley channels, and of the detail texture's.
    constexpr int shapePerlinPeriod = 4;
    constexpr int shapeWorleyPeriods[3] = { 4, 8, 16 };
    constexpr int detailWorleyPeriods[3] = { 2, 4, 8 };
    constexpr int curlPeriod = 8;

    // What the generation pass stores at texel (ix, iy, iz) of a texture of `size` texels a side (sampled at the
    // texel's centre). Shape: R the Perlin-Worley value, G, B and A Worley noise at periods 4, 8 and 16. Detail: R, G
    // and B Worley noise at periods 2, 4 and 8, A their fBm. All in [0, 1].
    // The fBm of three Worley values: 0.625 low + 0.25 mid + 0.125 high.
    inline float worleyFbm(float lowFrequency, float midFrequency, float highFrequency)
    {
        return lowFrequency * 0.625f + midFrequency * 0.25f + highFrequency * 0.125f;
    }

    inline std::array<float, 4> shapeTexel(std::uint32_t ix, std::uint32_t iy, std::uint32_t iz, std::uint32_t size)
    {
        const float inv = 1.f / static_cast<float>(size);
        const float u = (static_cast<float>(ix) + 0.5f) * inv;
        const float v = (static_cast<float>(iy) + 0.5f) * inv;
        const float w = (static_cast<float>(iz) + 0.5f) * inv;
        const auto at = [&](int period) { return std::array<float, 3>{ u * period, v * period, w * period }; };
        std::array<float, 3> worleys{};
        for (int i = 0; i < 3; ++i)
        {
            const auto p = at(shapeWorleyPeriods[i]);
            worleys[i]
                = worley(p[0], p[1], p[2], shapeWorleyPeriods[i], noiseSeed + 10u + static_cast<std::uint32_t>(i));
        }
        const auto p = at(shapePerlinPeriod);
        const float perlinValue = perlinFbm(p[0], p[1], p[2], shapePerlinPeriod, noiseSeed, 5);
        const float pw = perlinWorley(perlinValue, worleys[0]);
        return { pw, worleys[0], worleys[1], worleys[2] };
    }

    inline std::array<float, 4> detailTexel(std::uint32_t ix, std::uint32_t iy, std::uint32_t iz, std::uint32_t size)
    {
        const float inv = 1.f / static_cast<float>(size);
        const float u = (static_cast<float>(ix) + 0.5f) * inv;
        const float v = (static_cast<float>(iy) + 0.5f) * inv;
        const float w = (static_cast<float>(iz) + 0.5f) * inv;
        std::array<float, 4> out{};
        for (int i = 0; i < 3; ++i)
        {
            const int p = detailWorleyPeriods[i];
            out[i] = worley(u * p, v * p, w * p, p, noiseSeed + 20u + static_cast<std::uint32_t>(i));
        }
        out[3] = worleyFbm(out[0], out[1], out[2]);
        return out;
    }
}

#endif
