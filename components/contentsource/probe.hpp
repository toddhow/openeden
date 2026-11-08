#ifndef OPENEDEN_COMPONENTS_CONTENTSOURCE_PROBE_H
#define OPENEDEN_COMPONENTS_CONTENTSOURCE_PROBE_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "plugin.hpp"

// What a content list (the launcher's data files page, task 0083 stage 5) shows about a plugin, made from the header
// that Content::Plugin has already read. The list used to open every plugin with the typed Morrowind or TES4 reader
// just to get these five facts; this reads no record and knows no game.
namespace Content
{
    /// Converts a header string from the code page the content files were written in to UTF-8. The header strings are
    /// stored raw by Content::Header (they are bytes of a legacy encoding for TES3 and the older TES4-family files).
    using TextConverter = std::function<std::string(std::string_view)>;

    struct PluginInfo
    {
        Family mFamily = Family::Tes3;
        /// HEDR's version number, as the typed readers' esmVersionF() gave it.
        float mVersion = 0.0f;
        std::string mAuthor;
        std::string mDescription;
        /// Master file names in the order the header lists them.
        std::vector<std::string> mMasters;
    };

    /// `convert` is applied to the author, the description and every master name; an empty function leaves them as
    /// stored.
    PluginInfo probe(const Header& header, const TextConverter& convert);
}

#endif
