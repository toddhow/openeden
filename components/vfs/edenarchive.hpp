#ifndef OPENMW_COMPONENTS_VFS_EDENARCHIVE_H
#define OPENMW_COMPONENTS_VFS_EDENARCHIVE_H

#include <filesystem>
#include <memory>

#include "archive.hpp"

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace VFS
{
    /// An ArchiveFactory (archivefactory.hpp) for Eden Archive packs (`.edenpak`, edenarchive/, task 0052).
    /// Returns nullptr when the file does not begin with the seven bytes `EDENPAK` (a pack of another generation,
    /// whose eighth byte differs, is claimed so that it is reported as unsupported rather than ignored). Throws
    /// std::runtime_error naming the file and the located error when the file is a pack that cannot be opened.
    /// Names inside a pack are already normalized, so the encoder is not used.
    std::unique_ptr<Archive> makeEdenArchive(const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder);

    /// Adds makeEdenArchive to the process-wide factory list (addArchiveFactory). Not needed by the game: makeArchive
    /// already tries makeEdenArchive after the added factories and before the Bethesda reader.
    void registerEdenArchiveFactory();
}

#endif
