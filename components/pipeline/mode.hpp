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
        GBUFFER,
        // OpenEden: a skinned draw that writes the G-buffer also writes the motion of its bones (task 0026, F3c).
        GBUFFER_MOTION,
        // OpenEden (task 0027): the draw also writes the material output and the roughness; added by
        // Pipeline::Graphics.
        GBUFFER_MATERIAL,
        // OpenEden (task 0027): physically based lighting, and its physical look (the vanilla look is PBR alone);
        // added by Pipeline::Graphics.
        PBR,
        PBR_PHYSICAL,
        // OpenEden (task 0027, tier 3): the model's texture has a packed material map (occlusion, roughness,
        // metalness).
        MATERIAL_MAP,
        // OpenEden (task 0028): the ambient light comes from the baked light probes; added by Pipeline::Graphics.
        GI,
        GI_SKY,
        // OpenEden (task 0029): the draw also writes the ambient part of its colour (the G-buffer's fifth extra
        // output, for ambient occlusion); added by Pipeline::Graphics.
        GBUFFER_AMBIENT,
        // OpenEden (task 0037): rain wetness, its puddles and the debug view; added by Pipeline::Graphics.
        WETNESS,
        WETNESS_PUDDLES,
        WETNESS_DEBUG,
        // OpenEden (task 0044, tier 3): the packed material map has the True PBR `_rmaos` layout, not `_orm`.
        MATERIAL_MAP_RMAOS,
        // OpenEden (task 0039): snow and ash cover and its debug view; added by Pipeline::Graphics.
        COVER,
        COVER_DEBUG,
        // OpenEden (task 0040): rain lit as scattering streaks, not as a surface (lib/light/precipitation.glsl); set by
        // MWRender::Rain.
        PRECIPITATION_LIT,
        // OpenEden (task 0045): parallax occlusion mapping. HEIGHT_MAP: the model's texture has a `_p` height file
        // (loader, binding HEIGHT_UNIT); PARALLAX_DISCARD: fragments whose displaced coordinates leave the texture are
        // dropped (loader, clamped textures only); PARALLAX_POM and its quality modes are added by Pipeline::Graphics.
        HEIGHT_MAP,
        PARALLAX_POM,
        PARALLAX_POM_LOW,
        PARALLAX_POM_HIGH,
        PARALLAX_DISCARD,
        // OpenEden (task 0059): the fragment is dropped by an ordered dither that thins out as the material's specular
        // alpha falls from 1 to 0 (default/main.frag), so a far object cluster can fade in over the live objects.
        LOD_FADE,
        // OpenEden (task 0077): the nearest point lights cast shadows (lib/light/pointshadow.glsl); added by
        // Pipeline::Graphics.
        POINT_SHADOW,
        // OpenEden (task 0096 stage c): far cluster foliage sways in the wind (default/main.vert). The vertex stage
        // reads the wind and the clock from the TEXMAT_BINDING uniform and each vertex's weight from its colour alpha.
        LOD_SWAY
    };

    const std::vector<Mode> vertexModes{ Mode::VERTEX, Mode::NORMAL, Mode::COLOR, Mode::TEXCOORD, Mode::PARTICLE,
        Mode::BILLBOARD, Mode::MORPH, Mode::SKIN, Mode::TEXMAT, Mode::ENV_MAP, Mode::INSTANCE, Mode::NORMAL_MAP,
        Mode::SOFT_PARTICLE, Mode::LOD_SWAY };
    const std::vector<Mode> fragmentModes{ Mode::NORMAL, Mode::COLOR, Mode::TEXCOORD, Mode::PARTICLE /*_TEXCOORD*/,
        Mode::DIFFUSE_MAP, Mode::DARK_MAP, Mode::DETAIL_MAP, Mode::DECAL_MAP, Mode::GLOW_MAP, Mode::BUMP_MAP,
        Mode::ENV_MAP, Mode::MATERIAL, Mode::NORMAL_MAP, Mode::PARALLAX, Mode::RECONSTRUCT_NORMAL_Z, Mode::SPECULAR_MAP,
        Mode::ALPHA_TO_COVERAGE, Mode::ADJUST_COVERAGE, Mode::SOFT_PARTICLE, Mode::PARTICLE_OCCLUSION,
        Mode::MATERIAL_MAP, Mode::MATERIAL_MAP_RMAOS, Mode::HEIGHT_MAP, Mode::PARALLAX_DISCARD, Mode::PRECIPITATION_LIT,
        Mode::LOD_FADE };

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
