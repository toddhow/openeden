#include "layermaps.hpp"

#include <map>

namespace Terrain
{
    TerrainLayerMaps findTerrainLayerMaps(const std::vector<std::string>& layerPaths,
        const vsgAdapters::AutoMapSettings& settings, const std::function<bool(std::string_view)>& exists)
    {
        TerrainLayerMaps result;
        std::map<std::string, int, std::less<>> normalMapIndices;
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
            result.layers.push_back(std::move(layer));
        }
        return result;
    }
}
