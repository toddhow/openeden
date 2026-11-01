#ifndef OPENEDEN_COMPONENTS_VSGADAPTERS_NIF_AUTOMAPS_H
#define OPENEDEN_COMPONENTS_VSGADAPTERS_NIF_AUTOMAPS_H

#include <functional>
#include <string>
#include <string_view>

#include <vsg/vk/vulkan.h>

#include <components/render/materialmath.hpp>

namespace vsgAdapters
{
    /// The `[Shaders]` settings that find normal and specular maps next to a model's diffuse texture.
    struct AutoMapSettings
    {
        bool autoUseNormalMaps = false;
        bool autoUseSpecularMaps = false;
        std::string normalMapPattern = "_n";
        std::string normalHeightMapPattern = "_nh";
        std::string specularMapPattern = "_spec";
        // OpenEden (task 0027, tier 3): a packed occlusion, roughness, metalness file; off unless `pbr material maps`.
        bool autoUseMaterialMaps = false;
        std::string materialMapPattern = std::string(Render::Pbr::materialMapSuffix);
        // Task 0044: the True PBR layout, looked for before the `_orm` one.
        std::string rmaosMapPattern = std::string(Render::Pbr::rmaosMapSuffix);
    };

    struct AutoMaps
    {
        std::string normalMap;
        // The normal map's alpha holds height, for parallax.
        bool normalHeight = false;
        std::string specularMap;
        std::string materialMap;
        // Which channel layout `materialMap` has (task 0044).
        Render::Pbr::MapLayout materialLayout = Render::Pbr::MapLayout::Orm;
    };

    struct MaterialMapFile
    {
        std::string path;
        Render::Pbr::MapLayout layout = Render::Pbr::MapLayout::Orm;
    };

    /// The packed material map next to a diffuse texture (task 0044): `_rmaos` if it exists, else `_orm`; nothing
    /// unless settings.autoUseMaterialMaps.
    MaterialMapFile findMaterialMap(std::string_view diffusePath, const AutoMapSettings& settings,
        const std::function<bool(std::string_view)>& exists);

    /// The diffuse path with its last "." replaced by pattern + "." (Misc::StringUtils::replaceLast, as OpenMW's
    /// ShaderVisitor named the maps); empty if the path has no ".".
    std::string autoMapPath(std::string_view diffusePath, std::string_view pattern);

    /// Finds the normal and specular maps for a diffuse texture's resolved path (task 0012, V4h), as OpenMW's
    /// Shader::ShaderVisitor::applyStateSet at 442eec6e2f: the normal-height map before the plain normal map, unless
    /// the found map is the model's own bump texture (bumpPath), then the specular map.
    AutoMaps findAutoMaps(std::string_view diffusePath, std::string_view bumpPath, const AutoMapSettings& settings,
        const std::function<bool(std::string_view)>& exists);

    /// True for two-channel formats (RG and BC5), whose normal maps reconstruct Z and have no height
    /// (OpenMW's mReconstructNormalZ).
    bool isTwoChannelFormat(VkFormat format);
}

#endif
