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

namespace CSMMesh
{
    namespace
    {
        std::uint32_t le32(const std::vector<unsigned char>& b, std::size_t at)
        {
            return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
        }

        std::uint32_t be32(const std::vector<unsigned char>& b, std::size_t at)
        {
            return b[at + 3] | (b[at + 2] << 8) | (b[at + 1] << 16) | (static_cast<std::uint32_t>(b[at]) << 24);
        }

        std::optional<std::pair<std::uint32_t, std::uint32_t>> sized(std::uint32_t width, std::uint32_t height)
        {
            if (width == 0 || height == 0)
                return std::nullopt;
            return std::make_pair(width, height);
        }
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> imageSizeFromHeader(
        const std::vector<unsigned char>& b)
    {
        if (b.size() >= 4 && b[0] == 'D' && b[1] == 'D' && b[2] == 'S' && b[3] == ' ')
            return b.size() >= 20 ? sized(le32(b, 16), le32(b, 12)) : std::nullopt;
        static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
        if (b.size() >= 8 && std::equal(pngSignature, pngSignature + 8, b.begin()))
            return b.size() >= 24 ? sized(be32(b, 16), be32(b, 20)) : std::nullopt;
        if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M')
        {
            if (b.size() < 26)
                return std::nullopt;
            const std::int32_t height = static_cast<std::int32_t>(le32(b, 22));
            const std::uint32_t absHeight
                = height < 0 ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(height)) : height;
            return sized(le32(b, 18), absHeight);
        }
        // A TGA file has no magic number: check the fields before the size.
        if (b.size() >= 18 && (b[1] == 0 || b[1] == 1)
            && (b[2] == 1 || b[2] == 2 || b[2] == 3 || b[2] == 9 || b[2] == 10 || b[2] == 11))
            return sized(b[12] | (b[13] << 8), b[14] | (b[15] << 8));
        return std::nullopt;
    }
}
