#include "assimpscene.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>

#include <components/misc/strings/algorithm.hpp>

namespace Resource
{
    const aiMatrix4x4 sAssimpYUpToZUp(1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1);

    namespace
    {
        // assimp 6.0.5's COLLADA parser (ReadControllerWeightsVCount in ColladaParser.cpp) does not skip whitespace at
        // the start of a skin's <vcount>. With "<vcount> 2 2 ...", as Blender's exporter writes it, it reads a first
        // count of 0, which shifts every vertex's bone weights to the next vertex. Returns the content with that
        // whitespace removed, or an empty string if nothing needed removing.
        std::string removeLeadingVCountWhitespace(std::string_view content)
        {
            constexpr std::string_view tag = "<vcount>";
            std::string fixed;
            std::size_t copied = 0;
            for (std::size_t pos = content.find(tag); pos != std::string_view::npos; pos = content.find(tag, pos))
            {
                pos += tag.size();
                std::size_t textStart = pos;
                while (textStart < content.size()
                    && (content[textStart] == ' ' || content[textStart] == '\t' || content[textStart] == '\r'
                        || content[textStart] == '\n'))
                    ++textStart;
                if (textStart == pos)
                    continue;
                if (fixed.empty())
                    fixed.reserve(content.size());
                fixed.append(content.substr(copied, pos - copied));
                copied = textStart;
            }
            if (!fixed.empty())
                fixed.append(content.substr(copied));
            return fixed;
        }
    }

    const aiScene* readAssimpScene(Assimp::Importer& importer, std::string_view content, std::string_view extension,
        unsigned int postProcessFlags, std::string& error)
    {
        const std::string fixed = removeLeadingVCountWhitespace(content);
        if (!fixed.empty())
            content = fixed;
        // Without this, assimp gives a model with no meshes a generated skeleton mesh (SkeletonMeshBuilder).
        importer.SetPropertyBool(AI_CONFIG_IMPORT_NO_SKELETON_MESHES, true);
        // Name nodes by their COLLADA name rather than their id, as OSG's COLLADA reader did.
        importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_USE_COLLADA_NAMES, true);
        const std::string hint(extension);
        const aiScene* scene
            = importer.ReadFileFromMemory(content.data(), content.size(), postProcessFlags, hint.c_str());
        if (scene == nullptr || scene->mRootNode == nullptr)
        {
            error = importer.GetErrorString();
            return nullptr;
        }
        return scene;
    }

    bool hasSkinnedMeshes(const aiScene& scene)
    {
        for (unsigned int i = 0; i < scene.mNumMeshes; ++i)
            if (scene.mMeshes[i]->HasBones())
                return true;
        return false;
    }

    std::vector<std::string> getColladaDescriptions(std::string_view content)
    {
        constexpr std::string_view open = "<Description>";
        constexpr std::string_view close = "</Description>";
        std::vector<std::string> descriptions;
        for (std::size_t start = content.find(open); start != std::string_view::npos; start = content.find(open, start))
        {
            start += open.size();
            const std::size_t end = content.find(close, start);
            if (end == std::string_view::npos)
                break;
            std::string description(content.substr(start, end - start));
            Misc::StringUtils::trim(description);
            descriptions.emplace_back(std::move(description));
            start = end + close.size();
        }
        return descriptions;
    }
}
