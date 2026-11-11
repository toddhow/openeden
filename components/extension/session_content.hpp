#pragma once

// The session's content list as a game pack sees it (task 0107, stage N3a-2b; docs/design/neutral-world-model.md
// 10.11). The engine reads the header of each `content=` file once (apps/openmw/extensionhost.cpp), the entries are
// kept here as plain values, and WorldSourceServiceImpl::sessionContent hands a pack the ones its content loaders
// accept. No engine, renderer, Qt or game type. Reading a header is a formats-layer job (components/contentsource),
// which this engine-layer code may not include, so collectSessionContent takes the reader as a function.

#include <openeden/game/content.hpp>
#include <openeden/game/world_source_service.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openeden::extension
{
    /// One content file with the facts a content loader's probe needs.
    struct SessionContentEntry
    {
        std::string name;
        /// Absolute path, UTF-8.
        std::string path;
        /// "TES3" or "TES4" (the file layout family), as the packs' probes expect.
        std::string gameTag;
        std::uint32_t formatVersion = 0;
    };

    /// The entries `accepts` says yes to, in their order. Pure.
    [[nodiscard]] std::vector<SessionContentEntry> filterSessionContent(const std::vector<SessionContentEntry>& entries,
        const std::function<bool(const openeden::sdk::ContentHeader&)>& accepts);

    /// What collectSessionContent made of a content list.
    struct SessionContentCollection
    {
        /// The plugins that exist and have a readable header, in the list's order.
        std::vector<SessionContentEntry> entries;
        /// Names with a plugin extension (esm, esp, esl, omwgame, omwaddon) that `resolve` could not find.
        std::vector<std::string> missing;
        /// Names with a plugin extension whose file could not be opened as a plugin, with the reason.
        std::vector<std::string> unreadable;
    };

    /// The facts a header reader gives for one file.
    struct SessionContentHeader
    {
        std::string gameTag;
        std::uint32_t formatVersion = 0;
    };

    /// Reads one plugin's header: the facts, or nothing with `error` set to the reason. The host passes
    /// Content::readSessionTag (components/contentsource/sessiontag.hpp) adapted to this type.
    using SessionHeaderReader
        = std::function<std::optional<SessionContentHeader>(const std::filesystem::path&, std::string& error)>;

    /// Reads the header of each plugin in `names` (the engine's `content=` list). `resolve` turns a name into the
    /// file's path (the engine's file collections) or nothing. Names with another extension (builtin.omwscripts, an
    /// .omwscripts file) are not plugins and are skipped without a mention. Reads headers only. A null `readHeader`
    /// makes every plugin unreadable.
    [[nodiscard]] SessionContentCollection collectSessionContent(const std::vector<std::string>& names,
        const std::function<std::optional<std::filesystem::path>(const std::string&)>& resolve,
        const SessionHeaderReader& readHeader);
}
