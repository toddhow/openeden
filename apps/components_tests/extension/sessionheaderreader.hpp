#ifndef OPENEDEN_COMPONENTS_TESTS_SESSIONHEADERREADER_H
#define OPENEDEN_COMPONENTS_TESTS_SESSIONHEADERREADER_H

#include <components/contentsource/sessiontag.hpp>
#include <components/extension/session_content.hpp>

// The header reader the host passes to collectSessionContent (apps/openmw/extensionhost.cpp), for tests. Tests are in
// the tools layer, which may include the formats layer; components/extension may not.
namespace SessionContentTest
{
    inline std::optional<openeden::extension::SessionContentHeader> readHeader(
        const std::filesystem::path& path, std::string& error)
    {
        const std::optional<Content::SessionTag> tag = Content::readSessionTag(path, error);
        if (!tag.has_value())
            return std::nullopt;
        return openeden::extension::SessionContentHeader{ tag->mTag, tag->mFormatVersion };
    }
}

#endif
