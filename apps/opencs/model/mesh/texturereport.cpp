#include "texturereport.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace CSMMesh
{
    TextureReport buildTextureReport(const std::vector<std::string>& names,
        const std::function<std::optional<std::pair<std::uint32_t, std::uint32_t>>(const std::string&)>& lookup)
    {
        TextureReport report;
        std::set<std::string> seen;
        for (const std::string& name : names)
        {
            if (name.empty())
                continue;
            std::string key = name;
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!seen.insert(key).second)
                continue;

            TextureEntry entry;
            entry.name = name;
            if (const auto size = lookup(name))
            {
                entry.found = true;
                entry.width = size->first;
                entry.height = size->second;
            }
            else
                report.warnings.push_back("missing texture: " + name);
            report.entries.push_back(std::move(entry));
        }
        return report;
    }
}
