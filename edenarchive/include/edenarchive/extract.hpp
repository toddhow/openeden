// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_EXTRACT_HPP
#define EDENARCHIVE_EXTRACT_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "archive.hpp"

namespace EdenArchive
{
    /// The extraction rules of section 15.4 for one stored path: returns std::nullopt when the path may be written
    /// below an output folder on Windows and on Linux, otherwise a short reason. (OpenEden's own tools pass
    /// VFS::Path::checkExtractionSafe instead; a unit test shows the two agree.)
    std::optional<std::string> checkExtractionSafe(std::string_view path);

    struct UnpackOptions
    {
        bool mOverwrite = false; // refuse to replace an existing file unless set
        /// The name check; empty means checkExtractionSafe above.
        std::function<std::optional<std::string>(std::string_view)> mNameCheck;
    };

    /// Writes every entry that has a path below `outputFolder` (created if missing). Refuses (class Name) a path that
    /// fails the name check, that is also a directory prefix of another path, or that would collide with another path
    /// on a case-insensitive file system; refuses (class Io) to overwrite without mOverwrite, and never follows a
    /// symbolic link or junction below the output folder. Entries that have only a key are skipped. Each file is
    /// written to a temporary name and renamed; the content hash is verified. Stops at the first error. Everything
    /// is checked before the first file is written.
    Error unpack(
        const Archive& archive, const std::filesystem::path& outputFolder, const UnpackOptions& options) noexcept;
}

#endif
