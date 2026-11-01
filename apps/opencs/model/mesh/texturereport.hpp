#ifndef CSM_MESH_TEXTUREREPORT_H
#define CSM_MESH_TEXTUREREPORT_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CSMMesh
{
    struct TextureEntry
    {
        std::string name;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool found = false;
    };

    struct TextureReport
    {
        std::vector<TextureEntry> entries;
        std::vector<std::string> warnings;
    };

    /// The Textures panel's data (task 0043): distinct names (case-insensitive; the first spelling and order are
    /// kept; empty names skipped), each looked up once. \param lookup the size of the texture, or nothing if the
    /// virtual file system has none. A missing one gets the warning "missing texture: <name>".
    TextureReport buildTextureReport(const std::vector<std::string>& names,
        const std::function<std::optional<std::pair<std::uint32_t, std::uint32_t>>(const std::string&)>& lookup);
}

#endif
