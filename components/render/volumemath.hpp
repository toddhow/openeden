#ifndef OPENEDEN_RENDER_VOLUMEMATH_H
#define OPENEDEN_RENDER_VOLUMEMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

/*
 * OpenEden (task 0041): the pure maths of the volumetric fog. No VSG or game types.
 *
 * The fog is a grid of small boxes that follow the camera's view ("froxels", frustum voxels). The compute shaders
 * `volume.slang` in `files/shaders/comp/volume` repeats the slice depth function, the phase function, the integration
 * step and the jitter: change them together (a test reads the shader's constants back).
 */
namespace Render::Volume
{
    struct GridSize
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
    };

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

    // Task 0041, D2: the grid size of each quality tier.
    inline GridSize gridSize(Quality quality)
    {
        switch (quality)
        {
            case Quality::Low:
                return { 80, 45, 32 };
            case Quality::High:
                return { 240, 135, 96 };
            case Quality::Medium:
                break;
        }
        return { 160, 90, 64 };
    }

    // The first slice's depth (game units): nearer than this the fog is left out. Eight units is about a hand's width.
    constexpr float nearDistance = 8.f;
    // The farthest the grid reaches (game units). Beyond the fog's end the fog is opaque anyway; this caps a very long
    // view distance so the slices stay thin.
    constexpr float maxRange = 8192.f;
    // The shortest the grid is (an interior with a very short fog still gets a usable range).
    constexpr float minRange = 256.f;

    // How far the grid reaches for a fog that ends at `fogEnd`.
    inline float farDistance(float fogEnd)
    {
        return std::clamp(fogEnd, minRange, maxRange);
    }

    // Slices are spaced exponentially: thin near the camera where detail shows, thick far away.
    // `slice` is a fractional coordinate from 0 (at `nearD`) to `slices` (at `farD`).
    inline float sliceDepth(float slice, std::uint32_t slices, float nearD, float farD)
    {
        return nearD * std::pow(farD / nearD, slice / static_cast<float>(slices));
    }

    // The inverse of `sliceDepth`, clamped to the grid.
    inline float sliceCoordinate(float depth, std::uint32_t slices, float nearD, float farD)
    {
        if (depth <= nearD)
            return 0.f;
        if (depth >= farD)
            return static_cast<float>(slices);
        return std::log(depth / nearD) / std::log(farD / nearD) * static_cast<float>(slices);
    }

    constexpr float pi = 3.14159265358979f;

    // Henyey-Greenstein phase function: how much light a particle scatters towards the eye. `cosTheta` is the dot
    // product of the view ray (from the eye) and the direction to the sun, so 1 looks straight at the sun; `g` is the
    // lobe (-1 backwards, 0 even, 1 forwards). It integrates to 1 over the sphere. Same formula as
    // `sunGlowHenyeyGreenstein` in lib/view/fog.glsl.
    inline float henyeyGreenstein(float cosTheta, float g)
    {
        const float c = std::clamp(cosTheta, -1.f, 1.f);
        const float k = std::clamp(g, -0.99f, 0.99f);
        const float denominator = 1.f + k * k - 2.f * k * c;
        return (1.f - k * k) / (4.f * pi * denominator * std::sqrt(denominator));
    }

    inline float transmittance(float extinction, float distance)
    {
        return std::exp(-std::max(extinction, 0.f) * std::max(distance, 0.f));
    }

    struct Accum
    {
        std::array<float, 3> scattered{ 0.f, 0.f, 0.f }; // light scattered towards the eye so far
        float transmittance = 1.f; // the share of what is behind that still gets through
    };

    // One slice of the front to back walk. `scatterPerLength` is the light scattered towards the eye per unit of length
    // (scattering coefficient times incoming light times phase); `extinction` the fog's density; `dz` the slice's
    // thickness. The step is exact for a slice of uniform fog (Hillaire 2015, "Physically based sky, atmosphere and
    // cloud rendering in Frostbite", the improved integration).
    inline void integrateSlice(Accum& a, const std::array<float, 3>& scatterPerLength, float extinction, float dz)
    {
        const float sigma = std::max(extinction, 0.f);
        const float length = std::max(dz, 0.f);
        const float step = std::exp(-sigma * length);
        // (1 - exp(-sigma dz)) / sigma, which tends to dz as sigma tends to 0.
        const float weight = sigma < 1e-6f ? length : (1.f - step) / sigma;
        for (int i = 0; i < 3; ++i)
            a.scattered[i] += a.transmittance * scatterPerLength[i] * weight;
        a.transmittance *= step;
    }

    // The extinction that matches the engine's exponential fog (lib/view/fog.glsl: 1 - exp(-2 (d - start/2) / (end -
    // start/2))).
    inline float fogExtinction(float fogStart, float fogEnd)
    {
        return 2.f / std::max(fogEnd - fogStart * 0.5f, 1.f);
    }

    // Fog is thinner higher up: 1 at and below `baseZ`, falling off with `scaleHeight` above it.
    inline float heightFalloff(float z, float baseZ, float scaleHeight)
    {
        return std::exp(-std::max(z - baseZ, 0.f) / std::max(scaleHeight, 1e-3f));
    }

    // How quickly the fog thins above the camera (game units for the density to fall to 1/e): a mild lean towards fog
    // that lies low, not a layer. A named constant; the settings do not expose it.
    constexpr float defaultHeightScale = 4096.f;

    // How bright the sun's scattered light is against the fog's own colour, before the phase function (a chosen value,
    // set by looking at frames: shafts show without turning a clear day into a glare).
    constexpr float defaultSunGain = 0.2f;

    // Past the grid the shader keeps the fog going with the same extinction, so the horizon joins the old fog.
    inline float tailTransmittance(float extinction, float distance, float farD)
    {
        return std::exp(-extinction * std::max(distance - farD, 0.f));
    }

    // A fixed low-discrepancy sequence (Halton, bases 2, 3 and 5) indexed by the frame counter, with no clock: the
    // grid's sample positions shift a little each frame, and the temporal blend averages them.
    constexpr std::uint32_t jitterPeriod = 16;

    inline float halton(std::uint32_t index, std::uint32_t base)
    {
        float result = 0.f;
        float fraction = 1.f;
        while (index > 0)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    inline std::array<float, 3> jitter(std::uint32_t frame)
    {
        const std::uint32_t index = frame % jitterPeriod + 1;
        return { halton(index, 2), halton(index, 3), halton(index, 5) };
    }

    inline bool useVolumetrics(bool setting, bool hdr)
    {
        return setting && hdr;
    }

    // D6: no temporal blend at the lowest tier.
    inline bool temporalEnabled(Quality quality, bool setting)
    {
        return setting && quality != Quality::Low;
    }

    // How bright a lamp's scattered light is against the lamp's own colour (a chosen value, set by looking at frames).
    constexpr float defaultLampGain = 0.5f;

    // A move of the camera longer than this between two frames (game units) is a jump (a teleport or a new cell), and
    // the history is thrown away. The same limit the velocity pass uses (Velocity::cameraJumpDistance is the camera's,
    // in double precision; this one only needs to be of the same size).
    constexpr double cameraJumpDistance = 256.0;

    inline bool cameraJumped(double px, double py, double pz, double cx, double cy, double cz)
    {
        const double dx = cx - px;
        const double dy = cy - py;
        const double dz = cz - pz;
        return dx * dx + dy * dy + dz * dz > cameraJumpDistance * cameraJumpDistance;
    }

    // How much of the previous frame's value the blend keeps.
    constexpr float historyWeight = 0.9f;

    inline float blendHistory(float history, float current, float weight)
    {
        const float w = std::clamp(weight, 0.f, 1.f);
        return current + (history - current) * w;
    }
}

#endif
