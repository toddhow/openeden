#include "session_content.hpp"

#include <components/files/conversion.hpp>
#include <components/misc/strings/lower.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace openeden::extension
{
    std::vector<SessionContentEntry> filterSessionContent(const std::vector<SessionContentEntry>& entries,
        const std::function<bool(const openeden::sdk::ContentHeader&)>& accepts)
    {
        std::vector<SessionContentEntry> kept;
        if (!accepts)
            return kept;
        for (const SessionContentEntry& entry : entries)
        {
            const openeden::sdk::ContentHeader header{ entry.gameTag, entry.formatVersion };
            if (accepts(header))
                kept.push_back(entry);
        }
        return kept;
    }

    namespace
    {
        bool hasPluginExtension(const std::string& name)
        {
            static constexpr std::array<std::string_view, 5> sExtensions{ ".esm", ".esp", ".esl", ".omwgame",
                ".omwaddon" };
            const std::string lower = Misc::StringUtils::lowerCase(name);
            return std::any_of(sExtensions.begin(), sExtensions.end(), [&lower](std::string_view extension) {
                return lower.size() >= extension.size()
                    && std::string_view(lower).substr(lower.size() - extension.size()) == extension;
            });
        }
    }

    SessionContentCollection collectSessionContent(const std::vector<std::string>& names,
        const std::function<std::optional<std::filesystem::path>(const std::string&)>& resolve,
        const SessionHeaderReader& readHeader)
    {
        SessionContentCollection result;
        for (const std::string& name : names)
        {
            if (!hasPluginExtension(name))
                continue;
            const std::optional<std::filesystem::path> path = resolve ? resolve(name) : std::nullopt;
            if (!path.has_value())
            {
                result.missing.push_back(name);
                continue;
            }
            std::string error;
            const std::optional<SessionContentHeader> header = readHeader ? readHeader(*path, error) : std::nullopt;
            if (!header.has_value())
            {
                result.unreadable.push_back(name + ": " + (error.empty() ? "no header reader" : error));
                continue;
            }
            result.entries.push_back(
                SessionContentEntry{ name, Files::pathToUnicodeString(*path), header->gameTag, header->formatVersion });
        }
        return result;
    }
}
