#ifndef OPENEDEN_COMPONENTS_CONTENTSOURCE_SESSIONTAG_H
#define OPENEDEN_COMPONENTS_CONTENTSOURCE_SESSIONTAG_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// The two facts a content loader's probe is asked about a plugin (task 0107, layer fix): the file layout family as a
// tag and the TES4 form version. Read from the header only; no record, no game rule. Formats layer: the engine layer
// (components/extension) may not include this, so its collector takes a reader function and the host passes this one.
namespace Content
{
    struct SessionTag
    {
        /// "TES3" or "TES4" (the TES4 family: Oblivion, Skyrim, Fallout), as the packs' probes expect.
        std::string mTag;
        /// The form version in the TES4 record header; 0 for TES3.
        std::uint32_t mFormatVersion = 0;
    };

    /// Opens `path` as a plugin and returns its tag, or nothing with `error` set to a located reason.
    /// Thread safety: no shared state; safe to call from any thread.
    [[nodiscard]] std::optional<SessionTag> readSessionTag(const std::filesystem::path& path, std::string& error);
}

#endif
