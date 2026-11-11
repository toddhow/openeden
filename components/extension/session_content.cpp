#include "session_content.hpp"

#include <components/files/conversion.hpp>
#include <components/misc/strings/lower.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace openeden::extension
{
    SessionContentEntry makeSessionContentEntry(
        const std::string& name, const std::string& path, const Content::Header& header)
    {
        SessionContentEntry entry;
        entry.name = name;
        entry.path = path;
        if (header.mFamily == Content::Family::Tes4)
        {
            entry.gameTag = "TES4";
            entry.formatVersion = header.mFormVersion;
        }
        else
        {
            entry.gameTag = "TES3";
            entry.formatVersion = 0;
        }
        return entry;
    }

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
        const std::function<std::optional<std::filesystem::path>(const std::string&)>& resolve)
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
            Content::Error error;
            const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(*path, error);
            if (plugin == nullptr)
            {
                result.unreadable.push_back(name + ": " + error.describe());
                continue;
            }
            result.entries.push_back(
                makeSessionContentEntry(name, Files::pathToUnicodeString(*path), plugin->header()));
        }
        return result;
    }
}
