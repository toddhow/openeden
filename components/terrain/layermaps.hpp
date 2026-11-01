#ifndef OPENEDEN_COMPONENTS_TERRAIN_LAYERMAPS_H
#define OPENEDEN_COMPONENTS_TERRAIN_LAYERMAPS_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <components/pipeline/terrain.hpp>
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
    };

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
        // Task 0037: the terrain shader takes rain wetness.
        Pipeline::WetnessOptions wetness;

        bool enabled() const { return exists && (autoMaps.autoUseNormalMaps || autoMaps.autoUseSpecularMaps); }
    };

    struct TerrainLayerMaps
    {
        std::vector<TerrainLayer> layers;
        std::vector<std::string> normalMaps;
    };

    /// Finds each terrain layer's normal and specular maps (task 0012, V4i), as OpenMW's
    /// ESMTerrain::Storage::getLayerInfo at 442eec6e2f did. layerPaths are the layers' resolved diffuse paths; the
    /// settings' specular pattern is the terrain one. Normal maps shared by layers are listed once.
    TerrainLayerMaps findTerrainLayerMaps(const std::vector<std::string>& layerPaths,
        const vsgAdapters::AutoMapSettings& settings, const std::function<bool(std::string_view)>& exists);
}

#endif
