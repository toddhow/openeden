#ifndef OPENEDEN_RENDER_VOLUMEGPU_H
#define OPENEDEN_RENDER_VOLUMEGPU_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "volumemath.hpp"

/*
 * OpenEden (task 0041): what the volumetric fog's shaders read from the parameter block (view set binding 15). The block
 * is std140 and ten vec4 long: six vec4 and a mat4. `files/shaders/lib/view/volume.glsl` and
 * `files/shaders/comp/volume/volume.slang` declare the same block: change the three together (a test compares the member
 * order).
 */
namespace Render::Volume
{
    constexpr std::uint32_t paramsVec4s = 10;
    constexpr std::uint32_t paramsFloats = paramsVec4s * 4;

    // The compute shaders work in 8 by 8 columns of froxels.
    constexpr std::uint32_t groupSizeXY = 8;

    struct ParamsInput
    {
        GridSize grid = gridSize(Quality::Medium);
        bool enabled = true;
        float fogEnd = 7168.f; // game units: how far the grid reaches (see farDistance)
        float densityScale = 1.f;
        float heightScale = 0.f; // game units; 0 means the fog is as thick at any height
        float anisotropy = 0.5f;
        float sunGain = 0.f;
        float shadowMaxDistance = 0.f;
        float shadowFadeStart = 0.9f;
        std::uint32_t cascades = 0;
        float lampGain = 0.f;
        float shadowBias = 0.0005f;
        bool debug = false; // the fog holds the sun's shadow instead of light
        std::array<float, 3> jitter{ 0.5f, 0.5f, 0.5f };
        float historyWeight = 0.f;
        std::array<float, 16> toPreviousClip{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    };

    inline std::array<float, paramsFloats> buildParams(const ParamsInput& in)
    {
        std::array<float, paramsFloats> p{};
        const float reach = farDistance(in.fogEnd);
        p[0] = static_cast<float>(in.grid.x);
        p[1] = static_cast<float>(in.grid.y);
        p[2] = static_cast<float>(in.grid.z);
        p[3] = in.enabled ? 1.f : 0.f;
        p[4] = nearDistance;
        p[5] = reach;
        p[6] = std::log(reach / nearDistance);
        p[8] = std::max(in.densityScale, 0.f);
        p[9] = in.heightScale > 0.f ? 1.f / in.heightScale : 0.f;
        p[11] = std::clamp(in.anisotropy, -0.99f, 0.99f);
        p[12] = in.sunGain;
        p[13] = in.shadowMaxDistance;
        p[14] = in.shadowFadeStart;
        p[15] = static_cast<float>(in.cascades);
        p[16] = in.lampGain;
        p[17] = in.shadowBias;
        p[18] = in.debug ? 1.f : 0.f;
        p[20] = in.jitter[0];
        p[21] = in.jitter[1];
        p[22] = in.jitter[2];
        p[23] = std::clamp(in.historyWeight, 0.f, 1.f);
        for (std::size_t i = 0; i < 16; ++i)
            p[24 + i] = in.toPreviousClip[i];
        return p;
    }

    // For views without a volume: grid.w is 0, which the readers take as "off".
    inline std::array<float, paramsFloats> disabledParams()
    {
        return {};
    }

    constexpr std::uint32_t dispatchGroups(std::uint32_t count, std::uint32_t groupSize)
    {
        return (count + groupSize - 1) / groupSize;
    }

    constexpr std::size_t volumeTexels(GridSize grid)
    {
        return static_cast<std::size_t>(grid.x) * grid.y * grid.z;
    }

    // RGBA16F: eight bytes a texel.
    constexpr std::size_t volumeBytes(GridSize grid)
    {
        return volumeTexels(grid) * 8;
    }
}

#endif
