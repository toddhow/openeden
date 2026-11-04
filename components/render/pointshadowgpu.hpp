#ifndef OPENEDEN_RENDER_POINTSHADOWGPU_H
#define OPENEDEN_RENDER_POINTSHADOWGPU_H

#include <algorithm>
#include <cstddef>
#include <vector>

#include <vsg/maths/vec4.h>

#include "pointshadowmath.hpp"

namespace Render::PointShadow
{
    /*
     * The layout of the point-light shadow data buffer (binding VIEW_POINT_SHADOW_DATA_BINDING, vec4 elements):
     * [0] = (slotCount, resolution, tanHalf, near), [1] = (normalOffsetTexels, depthBiasTexels, pcfRadiusTexels, taps),
     * then one (slot or -1, fade, 0, 0) per light index of the light buffer, then one (view-space light x, y, z, far)
     * per slot. files/shaders/lib/light/pointshadow.* read it; change them together.
     */

    constexpr std::size_t headerVec4s = 2;

    inline std::size_t lightEntryOffset()
    {
        return headerVec4s;
    }

    inline std::size_t slotOffset(std::size_t maxLights)
    {
        return headerVec4s + maxLights;
    }

    inline std::size_t bufferVec4s(std::size_t maxLights)
    {
        return slotOffset(maxLights) + static_cast<std::size_t>(maxSlots);
    }

    struct Params
    {
        int slotCount = 0;
        double resolution = 512;
        double tanHalf = 1.0;
        double near = nearPlane;
        double normalOffsetTexels = 1.0;
        double depthBiasTexels = 1.0;
        double pcfRadiusTexels = 1.5;
        int taps = 8;
        double biasScale = 1.5;
    };

    inline void pack(const Params& params, std::size_t maxLights, const std::vector<int>& slotOfLight,
        const std::vector<float>& fadeOfLight, const std::vector<vsg::vec4>& slotViewPositionAndFar,
        std::vector<vsg::vec4>& out)
    {
        out.assign(bufferVec4s(maxLights), vsg::vec4(0.f, 0.f, 0.f, 0.f));
        out[0] = vsg::vec4(static_cast<float>(params.slotCount), static_cast<float>(params.resolution),
            static_cast<float>(params.tanHalf), static_cast<float>(params.near));
        out[1] = vsg::vec4(static_cast<float>(params.normalOffsetTexels * params.biasScale),
            static_cast<float>(params.depthBiasTexels * params.biasScale), static_cast<float>(params.pcfRadiusTexels),
            static_cast<float>(params.taps));
        for (std::size_t i = 0; i < maxLights; ++i)
        {
            const int slot = i < slotOfLight.size() ? slotOfLight[i] : -1;
            const float fade = slot >= 0 && i < fadeOfLight.size() ? fadeOfLight[i] : 0.f;
            out[lightEntryOffset() + i] = vsg::vec4(static_cast<float>(slot), fade, 0.f, 0.f);
        }
        const std::size_t count = std::min(slotViewPositionAndFar.size(), static_cast<std::size_t>(maxSlots));
        for (std::size_t k = 0; k < count; ++k)
            out[slotOffset(maxLights) + k] = slotViewPositionAndFar[k];
    }
}

#endif
