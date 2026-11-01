#ifndef VSGOPENMW_PIPELINE_MODE_H
#define VSGOPENMW_PIPELINE_MODE_H

#include <vector>

namespace Pipeline
{
    /*
     * Controls shader variations.
     */
    enum class Mode
    {
        VERTEX,
        NORMAL,
        COLOR,
        SKIN,
        TEXCOORD,
        MATERIAL,
        TEXMAT,
        MORPH,
        PARTICLE,
        BILLBOARD,
        DIFFUSE_MAP,
        DARK_MAP,
        DETAIL_MAP,
        DECAL_MAP,
        GLOW_MAP,
        BUMP_MAP,
        ENV_MAP,
        // OpenEden: per-instance offset, scale and rotation, with groundcover's wind and stomping (task 0012).
        INSTANCE,
        // OpenEden: normal and specular maps (task 0012, V4h). NORMAL_MAP adds a per-vertex tangent and the normal map;
        // PARALLAX reads height from its alpha; RECONSTRUCT_NORMAL_Z is for two-channel maps.
        NORMAL_MAP,
        PARALLAX,
        RECONSTRUCT_NORMAL_Z,
        SPECULAR_MAP,
        // OpenEden: alpha to coverage through the sample mask, and the mip-level alpha scale (task 0012, V4j); added by
        // Pipeline::Graphics from its settings (coverageModes).
        ALPHA_TO_COVERAGE,
        ADJUST_COVERAGE,
        // OpenEden: particles fading where they meet the opaque scene (task 0012, V4k).
        SOFT_PARTICLE,
        // OpenEden: weather particles discarded under a roof (task 0012, V4l).
        PARTICLE_OCCLUSION,
        // OpenEden: the draw writes the G-buffer's extra colour outputs (task 0026); added by Pipeline::Graphics, never
        // by a loader.
        GBUFFER
    };

    const std::vector<Mode> vertexModes{ Mode::VERTEX, Mode::NORMAL, Mode::COLOR, Mode::TEXCOORD, Mode::PARTICLE,
        Mode::BILLBOARD, Mode::MORPH, Mode::SKIN, Mode::TEXMAT, Mode::ENV_MAP, Mode::INSTANCE, Mode::NORMAL_MAP,
        Mode::SOFT_PARTICLE };
    const std::vector<Mode> fragmentModes{ Mode::NORMAL, Mode::COLOR, Mode::TEXCOORD, Mode::PARTICLE /*_TEXCOORD*/,
        Mode::DIFFUSE_MAP, Mode::DARK_MAP, Mode::DETAIL_MAP, Mode::DECAL_MAP, Mode::GLOW_MAP, Mode::BUMP_MAP,
        Mode::ENV_MAP, Mode::MATERIAL, Mode::NORMAL_MAP, Mode::PARALLAX, Mode::RECONSTRUCT_NORMAL_Z, Mode::SPECULAR_MAP,
        Mode::ALPHA_TO_COVERAGE, Mode::ADJUST_COVERAGE, Mode::SOFT_PARTICLE, Mode::PARTICLE_OCCLUSION };

    using Modes = std::vector<bool>;

    inline void addMode(Mode mode, Modes& vec)
    {
        auto i = static_cast<size_t>(mode);
        if (i >= vec.size())
            vec.resize(i + 1);
        vec[i] = true;
    }

    inline void removeMode(Mode mode, Modes& vec)
    {
        auto i = static_cast<size_t>(mode);
        if (i < vec.size())
            vec[i] = false;
        // Trailing unset modes are dropped, so the same set of modes always compares equal (pipeline cache keys).
        while (!vec.empty() && !vec.back())
            vec.pop_back();
    }

    inline bool hasMode(Mode mode, const Modes& vec)
    {
        auto i = static_cast<size_t>(mode);
        return i < vec.size() && vec[i];
    }

    inline Modes modes(const std::vector<Mode>& vec)
    {
        Modes ret;
        for (auto mode : vec)
            addMode(mode, ret);
        return ret;
    }
}

#endif
