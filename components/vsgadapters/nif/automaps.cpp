#include "automaps.hpp"

namespace vsgAdapters
{
    std::string autoMapPath(std::string_view diffusePath, std::string_view pattern)
    {
        const size_t dot = diffusePath.rfind('.');
        if (dot == std::string_view::npos)
            return {};
        std::string path(diffusePath.substr(0, dot));
        path += pattern;
        path += diffusePath.substr(dot);
        return path;
    }

    AutoMaps findAutoMaps(std::string_view diffusePath, std::string_view bumpPath, const AutoMapSettings& settings,
        const std::function<bool(std::string_view)>& exists)
    {
        AutoMaps maps;
        if (diffusePath.empty())
            return maps;

        if (settings.autoUseNormalMaps)
        {
            std::string normalMap = autoMapPath(diffusePath, settings.normalHeightMapPattern);
            bool normalHeight = !normalMap.empty() && exists(normalMap);
            if (!normalHeight)
            {
                normalMap = autoMapPath(diffusePath, settings.normalMapPattern);
                if (!normalMap.empty() && !exists(normalMap))
                    normalMap.clear();
            }
            // A map already used as the bump map is probably not an actual normal map.
            if (!normalMap.empty() && normalMap != bumpPath)
            {
                maps.normalMap = std::move(normalMap);
                maps.normalHeight = normalHeight;
            }
        }

        if (settings.autoUseSpecularMaps)
        {
            std::string specularMap = autoMapPath(diffusePath, settings.specularMapPattern);
            if (!specularMap.empty() && exists(specularMap))
                maps.specularMap = std::move(specularMap);
        }

        if (settings.autoUseMaterialMaps)
        {
            MaterialMapFile found = findMaterialMap(diffusePath, settings, exists);
            maps.materialMap = std::move(found.path);
            maps.materialLayout = found.layout;
        }
        maps.heightMap = findHeightMap(diffusePath, settings, exists);
        // The march needs the surface's tangent frame, which comes with a normal map: look for one next to a height
        // file even when normal maps are not asked for.
        if (!maps.heightMap.empty() && maps.normalMap.empty() && !settings.autoUseNormalMaps)
        {
            std::string normalMap = autoMapPath(diffusePath, settings.normalMapPattern);
            if (!normalMap.empty() && normalMap != bumpPath && exists(normalMap))
                maps.normalMap = std::move(normalMap);
        }
        return maps;
    }

    std::string findHeightMap(std::string_view diffusePath, const AutoMapSettings& settings,
        const std::function<bool(std::string_view)>& exists)
    {
        if (!settings.autoUseHeightMaps)
            return {};
        std::string path = autoMapPath(diffusePath, settings.heightMapPattern);
        if (path.empty() || !exists(path))
            return {};
        return path;
    }

    MaterialMapFile findMaterialMap(std::string_view diffusePath, const AutoMapSettings& settings,
        const std::function<bool(std::string_view)>& exists)
    {
        MaterialMapFile result;
        if (!settings.autoUseMaterialMaps)
            return result;
        // The True PBR layout first, then task 0027's.
        std::string rmaos = autoMapPath(diffusePath, settings.rmaosMapPattern);
        if (!rmaos.empty() && exists(rmaos))
        {
            result.path = std::move(rmaos);
            result.layout = Render::Pbr::MapLayout::Rmaos;
            return result;
        }
        std::string orm = autoMapPath(diffusePath, settings.materialMapPattern);
        if (!orm.empty() && exists(orm))
        {
            result.path = std::move(orm);
            result.layout = Render::Pbr::MapLayout::Orm;
        }
        return result;
    }

    bool isTwoChannelFormat(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8_SNORM:
            case VK_FORMAT_R8G8_USCALED:
            case VK_FORMAT_R8G8_SSCALED:
            case VK_FORMAT_R8G8_UINT:
            case VK_FORMAT_R8G8_SINT:
            case VK_FORMAT_R8G8_SRGB:
            case VK_FORMAT_R16G16_UNORM:
            case VK_FORMAT_R16G16_SNORM:
            case VK_FORMAT_R16G16_USCALED:
            case VK_FORMAT_R16G16_SSCALED:
            case VK_FORMAT_R16G16_UINT:
            case VK_FORMAT_R16G16_SINT:
            case VK_FORMAT_R16G16_SFLOAT:
            case VK_FORMAT_BC5_UNORM_BLOCK:
            case VK_FORMAT_BC5_SNORM_BLOCK:
                return true;
            default:
                return false;
        }
    }
}
