#ifndef OPENEDEN_RENDER_CLOUDVOLUMEGPU_H
#define OPENEDEN_RENDER_CLOUDVOLUMEGPU_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "cloudcurvemath.hpp"
#include "cloudvolumemath.hpp"

/*
 * OpenEden (task 0042): what the volumetric clouds' compute shaders read from the parameter block (compute set binding
 * 10, a uniform buffer the cloud passes own: it is not in the view set). The block is std140 and ten vec4 long: six
 * vec4 and a mat4. `files/shaders/comp/cloud/cloud.slang` declares the same block: change them together (a test
 * compares the member order and the constants).
 */
namespace Render::CloudVolume
{
    constexpr std::uint32_t paramsVec4s = 10;
    constexpr std::uint32_t paramsFloats = paramsVec4s * 4;

    // The compute shaders work in 8 by 8 pixels (the noise in 4 by 4 by 4 texels).
    constexpr std::uint32_t groupSizeXY = 8;
    constexpr std::uint32_t noiseGroupSize3D = 4;

    // The debug view the resolve pass can draw instead of the clouds (`volumetric clouds debug`).
    enum class DebugView
    {
        None = 0,
        Transmittance = 1,
        ShapeNoise = 2,
        DetailNoise = 3,
        CurlNoise = 4
    };

    inline DebugView parseDebugView(std::string_view name)
    {
        if (name == "transmittance")
            return DebugView::Transmittance;
        if (name == "shape")
            return DebugView::ShapeNoise;
        if (name == "detail")
            return DebugView::DetailNoise;
        if (name == "curl")
            return DebugView::CurlNoise;
        return DebugView::None;
    }

    // How much of the previous frame's picture the blend keeps (0.9 is the fog's too: volumemath.hpp).
    constexpr float historyWeight = 0.9f;

    // Share of the sun's light and the ambient light the cloud takes, before the setting's own scale (chosen by
    // looking at frames).
    constexpr float defaultSunGain = 1.f;
    constexpr float defaultAmbientGain = 1.f;

    // The farthest a ray is followed through the layer (metres): a flat layer is hit at 100 km at one degree above
    // the horizon; beyond this the haze has taken all the colour anyway.
    constexpr float maxRayMeters = 120000.f;

    // The ground shadow map (D6): shadowMapSize texels a side, covering plus and minus shadowHalfExtentMeters round the
    // camera on the layer's middle plane, plus a column of two texels for its placement (so the image is one texel
    // wider than it is high). The fragment shaders repeat the size (lib/view/volumecloudshadow.glsl).
    constexpr std::uint32_t shadowMapSize = 256;
    constexpr float shadowHalfExtentMeters = 20000.f;
    constexpr std::uint32_t shadowImageWidth = shadowMapSize + 1;

    struct ParamsInput
    {
        std::array<std::uint32_t, 2> marchSize{ 1, 1 };
        std::array<std::uint32_t, 2> fullSize{ 1, 1 };
        float coverage = 0.5f;
        float densityScale = 1.f;
        std::array<float, 2> windMeters{ 0.f, 0.f }; // wrapped by the tile (see wrapWind)
        std::uint32_t steps = 40;
        std::uint32_t lightSteps = 5;
        std::array<float, 2> jitter{ 0.5f, 0.5f };
        float historyWeight = 0.f;
        float erosion = defaultErosion;
        float sunGain = defaultSunGain;
        float ambientGain = defaultAmbientGain;
        float hazeLengthMeters = 40000.f;
        DebugView debug = DebugView::None;
        std::uint32_t frame = 0;
        float shadowShare = 0.f; // the share of the sky the volume draws (0: it casts no shadow)
        float shadowStrength = 0.f; // 0 to 1: how dark the shadow of a thick cloud is
        std::array<float, 16> toPreviousClip{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        // The farthest a ray is followed, in metres (maxRayMeters for the flat layer, curvedMaxRayMeters for the
        // shell).
        float rayLimitMeters = maxRayMeters;
        // The curved layer's planet radius in metres; 0 keeps the flat layer.
        float planetRadiusMeters = 0.f;
        // The large scale variety of the shape lookup and the coverage (cloudcurvemath.hpp).
        bool variety = false;
    };

    // The extra vec4 after the ten of the main block (the block grew by it; `paramsVec4s` stays the main block's):
    // x the planet radius in metres (0: flat layer), y 1 with the variety, z and w unused.
    constexpr std::uint32_t curveParamsVec4s = 1;
    constexpr std::uint32_t curveParamsFloats = curveParamsVec4s * 4;

    inline std::array<float, curveParamsFloats> buildCurveParams(const ParamsInput& in)
    {
        std::array<float, curveParamsFloats> p{};
        const bool planet = std::isfinite(in.planetRadiusMeters) && in.planetRadiusMeters >= minPlanetRadiusMeters;
        p[0] = planet ? in.planetRadiusMeters : 0.f;
        p[1] = in.variety ? 1.f : 0.f;
        return p;
    }

    // The wind offset wrapped by `tile` (a whole number of every noise tile, so the clouds do not jump at the wrap).
    inline float wrapWind(double meters, float tile)
    {
        const double period = static_cast<double>(tile);
        double wrapped = std::fmod(meters, period);
        if (wrapped < 0.0)
            wrapped += period;
        return static_cast<float>(wrapped);
    }

    // The wind offset kept below the shape tile (a whole number of detail tiles), so a float keeps
    // its precision in a long session: the caller adds in double and wraps here.
    inline float wrapWind(double meters)
    {
        const double tile = static_cast<double>(shapeTileMeters);
        double wrapped = std::fmod(meters, tile);
        if (wrapped < 0.0)
            wrapped += tile;
        return static_cast<float>(wrapped);
    }

    inline std::array<float, paramsFloats> buildParams(const ParamsInput& in)
    {
        std::array<float, paramsFloats> p{};
        p[0] = static_cast<float>(in.marchSize[0]);
        p[1] = static_cast<float>(in.marchSize[1]);
        p[2] = static_cast<float>(in.fullSize[0]);
        p[3] = static_cast<float>(in.fullSize[1]);
        p[4] = metersToUnits(layerBaseMeters);
        p[5] = metersToUnits(layerTopMeters);
        p[6] = metersToUnits(in.rayLimitMeters);
        p[7] = std::clamp(in.coverage, 0.f, 1.f);
        p[8] = std::max(in.densityScale, 0.f);
        p[9] = in.windMeters[0];
        p[10] = in.windMeters[1];
        p[11] = static_cast<float>(std::max<std::uint32_t>(in.steps, 1));
        p[12] = static_cast<float>(in.lightSteps);
        p[13] = std::clamp(in.jitter[0], 0.f, 1.f);
        p[14] = std::clamp(in.jitter[1], 0.f, 1.f);
        p[15] = std::clamp(in.historyWeight, 0.f, 1.f);
        p[16] = std::clamp(in.erosion, 0.f, 1.f);
        p[17] = std::max(in.sunGain, 0.f);
        p[18] = std::max(in.ambientGain, 0.f);
        p[19] = std::max(in.hazeLengthMeters, 1.f);
        p[20] = static_cast<float>(static_cast<int>(in.debug));
        p[21] = static_cast<float>(in.frame % 65536u);
        p[22] = std::clamp(in.shadowShare, 0.f, 1.f);
        p[23] = std::clamp(in.shadowStrength, 0.f, 1.f);
        for (std::size_t i = 0; i < 16; ++i)
            p[24 + i] = in.toPreviousClip[i];
        return p;
    }

    constexpr std::uint32_t dispatchGroups(std::uint32_t count, std::uint32_t groupSize)
    {
        return (count + groupSize - 1) / groupSize;
    }

    // The bytes of the images (for the cost note): the march image and the full size pair at RGBA16F (8 bytes a
    // texel), the shape texture at RGBA8, the detail at RGBA8, the curl at RGBA8.
    constexpr std::size_t imageBytes(std::array<std::uint32_t, 2> marchSize, std::array<std::uint32_t, 2> fullSize)
    {
        const std::size_t march = static_cast<std::size_t>(marchSize[0]) * marchSize[1] * 8;
        const std::size_t full = static_cast<std::size_t>(fullSize[0]) * fullSize[1] * 8 * 2;
        const std::size_t noise = (static_cast<std::size_t>(shapeSize) * shapeSize * shapeSize
                                      + static_cast<std::size_t>(detailSize) * detailSize * detailSize
                                      + static_cast<std::size_t>(curlSize) * curlSize)
            * 4;
        return march + full + noise;
    }
}

#endif
