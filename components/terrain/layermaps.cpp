#include "layermaps.hpp"

#include <map>

namespace Terrain
{
    TerrainLayerMaps findTerrainLayerMaps(const std::vector<std::string>& layerPaths,
        const vsgAdapters::AutoMapSettings& settings, const std::function<bool(std::string_view)>& exists)
    {
        TerrainLayerMaps result;
        std::map<std::string, int, std::less<>> normalMapIndices;
        std::map<std::string, int, std::less<>> materialMapIndices;
        std::map<std::string, int, std::less<>> heightMapIndices;
        result.layers.reserve(layerPaths.size());
        for (const std::string& path : layerPaths)
        {
            TerrainLayer layer;
            layer.diffuseMap = path;
            if (!path.empty() && settings.autoUseNormalMaps)
            {
                std::string normalMap = vsgAdapters::autoMapPath(path, settings.normalHeightMapPattern);
                layer.parallax = !normalMap.empty() && exists(normalMap);
                if (!layer.parallax)
                {
                    normalMap = vsgAdapters::autoMapPath(path, settings.normalMapPattern);
                    if (!normalMap.empty() && !exists(normalMap))
                        normalMap.clear();
                }
                if (!normalMap.empty())
                {
                    auto [it, inserted]
                        = normalMapIndices.emplace(normalMap, static_cast<int>(result.normalMaps.size()));
                    if (inserted)
                        result.normalMaps.push_back(normalMap);
                    layer.normalMapIndex = it->second;
                }
            }
            if (!path.empty() && settings.autoUseSpecularMaps)
            {
                std::string specularMap = vsgAdapters::autoMapPath(path, settings.specularMapPattern);
                if (!specularMap.empty() && exists(specularMap))
                {
                    layer.diffuseMap = std::move(specularMap);
                    layer.specular = true;
                }
            }
            if (!path.empty() && settings.autoUseMaterialMaps)
            {
                vsgAdapters::MaterialMapFile found = vsgAdapters::findMaterialMap(path, settings, exists);
                if (!found.path.empty())
                {
                    auto [it, inserted]
                        = materialMapIndices.emplace(found.path, static_cast<int>(result.materialMaps.size()));
                    if (inserted)
                        result.materialMaps.push_back(found.path);
                    layer.materialMapIndex = it->second;
                    layer.materialLayout = found.layout;
                }
            }
            if (!path.empty())
            {
                std::string heightMap = vsgAdapters::findHeightMap(path, settings, exists);
                if (!heightMap.empty())
                {
                    auto [it, inserted]
                        = heightMapIndices.emplace(heightMap, static_cast<int>(result.heightMaps.size()));
                    if (inserted)
                        result.heightMaps.push_back(std::move(heightMap));
                    layer.heightMapIndex = it->second;
                }
            }
            result.layers.push_back(std::move(layer));
        }
        return result;
    }
}
