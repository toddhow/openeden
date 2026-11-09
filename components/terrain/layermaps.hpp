#ifndef OPENEDEN_COMPONENTS_TERRAIN_LAYERMAPS_H
#define OPENEDEN_COMPONENTS_TERRAIN_LAYERMAPS_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <components/pipeline/terrain.hpp>
#include <components/render/materialoverrides.hpp>
#include <components/vsgadapters/nif/automaps.hpp>

namespace Terrain
{
    struct TerrainLayer
    {
        // The diffuse map, replaced by the specular pattern's map when one exists.
        std::string diffuseMap;
        // Index into TerrainLayerMaps::normalMaps, or -1.
        int normalMapIndex = -1;
        bool parallax = false;
        // The diffuse map's alpha is the specular strength.
        bool specular = false;
        // Task 0044: index into TerrainLayerMaps::materialMaps, or -1, and that file's channel layout.
        int materialMapIndex = -1;
        Render::Pbr::MapLayout materialLayout = Render::Pbr::MapLayout::Orm;
        // Task 0045: index into TerrainLayerMaps::heightMaps, or -1.
        int heightMapIndex = -1;
    };

    // The terrain shader's per-layer flags word (files/shaders/terrain/main.frag repeats it): the normal map's index in
    // the low 16 bits (0xffff: none), parallax (bit 16), specular (bit 17), the `_rmaos` layout (bit 18), the material
    // map's index in bits 20 to 31 (0xfff: none).
    constexpr uint32_t layerNormalMask = 0xffffu;
    constexpr uint32_t layerParallaxFlag = 1u << 16;
    constexpr uint32_t layerSpecularFlag = 1u << 17;
    constexpr uint32_t layerRmaosFlag = 1u << 18;
    constexpr uint32_t layerMaterialShift = 20;
    constexpr uint32_t layerMaterialMask = 0xfffu;

    inline uint32_t packLayerFlags(
        int normalIndex, bool parallax, bool specular, int materialIndex, Render::Pbr::MapLayout layout)
    {
        uint32_t value = normalIndex >= 0 ? static_cast<uint32_t>(normalIndex) & layerNormalMask : layerNormalMask;
        if (parallax && normalIndex >= 0)
            value |= layerParallaxFlag;
        if (specular)
            value |= layerSpecularFlag;
        const uint32_t material
            = materialIndex >= 0 ? static_cast<uint32_t>(materialIndex) & layerMaterialMask : layerMaterialMask;
        value |= material << layerMaterialShift;
        if (materialIndex >= 0 && layout == Render::Pbr::MapLayout::Rmaos)
            value |= layerRmaosFlag;
        return value;
    }

    /// The `[Shaders]` terrain map settings (specularMapPattern holds the terrain specular pattern) and how to tell
    /// whether a texture path exists.
    struct TerrainMapSettings
    {
        vsgAdapters::AutoMapSettings autoMaps;
        std::function<bool(std::string_view)> exists;
        // openeden (task 0026): colour outputs of the main pass beyond the scene's own; the terrain writes them.
        unsigned int gbufferOutputs = 0;
        // Task 0036: the terrain shader takes the exterior sky-visibility field.
        bool giSky = false;
        // Task 0048: the terrain writes its ambient part for the ambient occlusion pass (`ao terrain`).
        bool aoTerrain = false;
        // Task 0077: the terrain (lit with the material model) takes the point lights' shadows.
        bool pointShadow = false;
        // Task 0037: the terrain shader takes rain wetness.
        Pipeline::WetnessOptions wetness;
        // Task 0039: the terrain shader takes snow and ash cover.
        Pipeline::CoverOptions cover;
        // Task 0044: the terrain is lit with the material model and reads the layers' material maps (autoMaps must ask
        // for them and for normal maps). `layerValues` gives a layer texture's tier 2 values (the two scales and a
        // reflectance); it may run on loader threads.
        bool pbr = false;
        // Task 0045: the layers' `_p` height files are marched (1 low, 2 medium, 3 high; 0 off), with `parallax scale`
        // folded into each layer's depth. The layers' `displacementScale` comes from layerValues, so it needs the
        // material table.
        unsigned int parallaxQuality = 0;
        float parallaxScale = 1.f;
        std::function<Render::Pbr::OverrideValues(std::string_view texturePath)> layerValues;

        // Task 0044: the material maps need the per-pixel path as well.
        bool enabled() const
        {
            return exists
                && (autoMaps.autoUseNormalMaps || autoMaps.autoUseSpecularMaps || autoMaps.autoUseMaterialMaps
                    || autoMaps.autoUseHeightMaps);
        }
    };

    /// Task 0080: true when two builders given these layer lists and settings would build equal layer arrays, so the
    /// second may use the first's (the LOD terrain's Builder and the live one).
    inline bool layerArraysShareable(const std::vector<std::string>& layersA, const TerrainMapSettings& settingsA,
        const std::vector<std::string>& layersB, const TerrainMapSettings& settingsB)
    {
        if (layersA != layersB || settingsA.enabled() != settingsB.enabled())
            return false;
        if (!settingsA.enabled())
            return true; // the plain diffuse array, nothing else decides it
        const vsgAdapters::AutoMapSettings& a = settingsA.autoMaps;
        const vsgAdapters::AutoMapSettings& b = settingsB.autoMaps;
        return settingsA.pbr == settingsB.pbr && settingsA.parallaxQuality == settingsB.parallaxQuality
            && settingsA.parallaxScale == settingsB.parallaxScale && a.autoUseNormalMaps == b.autoUseNormalMaps
            && a.autoUseSpecularMaps == b.autoUseSpecularMaps && a.normalMapPattern == b.normalMapPattern
            && a.normalHeightMapPattern == b.normalHeightMapPattern && a.specularMapPattern == b.specularMapPattern
            && a.autoUseMaterialMaps == b.autoUseMaterialMaps && a.materialMapPattern == b.materialMapPattern
            && a.rmaosMapPattern == b.rmaosMapPattern && a.autoUseHeightMaps == b.autoUseHeightMaps
            && a.heightMapPattern == b.heightMapPattern;
    }

    /// Task 0080 follow-up: whether the LOD terrain may adopt its tiles on this update. Adopting with layer sharing
    /// calls `Builder::waitForLayers` on the live builder, which blocks the calling (main) thread until the live
    /// terrain's asynchronous layer build (task 0089) is done, seconds with a texture mod. So when sharing is wanted
    /// the adoption waits until the live layers are ready and is retried on a later update; the tiles are not needed
    /// before then. Without sharing nothing waits on the live builder.
    inline bool mayAdoptTiles(bool tilesLoaded, bool sharingWanted, bool sourceLayersReady)
    {
        return tilesLoaded && (!sharingWanted || sourceLayersReady);
    }

    struct TerrainLayerMaps
    {
        std::vector<TerrainLayer> layers;
        std::vector<std::string> normalMaps;
        // Task 0044: the `_rmaos` / `_orm` files of the layers, each listed once.
        std::vector<std::string> materialMaps;
        // Task 0045: the `_p` height files of the layers, each listed once.
        std::vector<std::string> heightMaps;
    };

    /// Finds each terrain layer's normal and specular maps (task 0012, V4i), as OpenMW's
    /// ESMTerrain::Storage::getLayerInfo at 442eec6e2f did. layerPaths are the layers' resolved diffuse paths; the
    /// settings' specular pattern is the terrain one. Normal maps shared by layers are listed once.
    TerrainLayerMaps findTerrainLayerMaps(const std::vector<std::string>& layerPaths,
        const vsgAdapters::AutoMapSettings& settings, const std::function<bool(std::string_view)>& exists);
}

#endif
