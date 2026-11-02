#ifndef OPENMW_COMPONENTS_VFS_ENTRYNAME_H
#define OPENMW_COMPONENTS_VFS_ENTRYNAME_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace VFS::Path
{
    /// The longest entry name, in bytes, accepted in a pack. A proposal; the specification (task 0051) fixes the
    /// real limit and the reader takes it as a parameter.
    inline constexpr std::size_t defaultMaxEntryNameLength = 1024;

    /// Checks a name read from a pack. Returns std::nullopt when the name is acceptable, otherwise a short reason.
    /// Acceptable: not empty; at most `maxLength` bytes; already normalized (Path::isNormalized: lower case, '/' as
    /// separator, no doubled separators); valid UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF);
    /// no control character below 0x20 (NUL included); no leading or trailing '/'; no empty, '.' or '..' component.
    std::optional<std::string> checkPackEntryName(
        std::string_view name, std::size_t maxLength = defaultMaxEntryNameLength);

    /// Checks a name that a tool is about to write to disk as a relative path. Returns std::nullopt when the file
    /// stays inside the target folder on Windows and on Linux, otherwise a short reason. Rejects what
    /// checkPackEntryName rejects (except the length limit, and the name need not be normalized), and also: a
    /// component with a drive prefix or any of : < > " | ? * \ in it, a component that ends in '.' or a space, and a
    /// component whose part before the first '.' is a Windows reserved device name (con, prn, aux, nul, com1 to
    /// com9, lpt1 to lpt9), whatever its case.
    std::optional<std::string> checkExtractionSafe(std::string_view name);

    /// True if the bytes are well-formed UTF-8 (RFC 3629).
    bool isValidUtf8(std::string_view text);
}

#endif
