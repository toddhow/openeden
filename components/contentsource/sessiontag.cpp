#include "sessiontag.hpp"

#include <memory>

#include "plugin.hpp"

namespace Content
{
    std::optional<SessionTag> readSessionTag(const std::filesystem::path& path, std::string& error)
    {
        Content::Error failure;
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, failure);
        if (plugin == nullptr)
        {
            error = failure.describe();
            return std::nullopt;
        }
        SessionTag tag;
        if (plugin->header().mFamily == Content::Family::Tes4)
        {
            tag.mTag = "TES4";
            tag.mFormatVersion = plugin->header().mFormVersion;
        }
        else
        {
            tag.mTag = "TES3";
            tag.mFormatVersion = 0;
        }
        return tag;
    }
}
