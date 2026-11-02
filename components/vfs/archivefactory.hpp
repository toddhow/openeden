#ifndef OPENMW_COMPONENTS_VFS_ARCHIVEFACTORY_H
#define OPENMW_COMPONENTS_VFS_ARCHIVEFACTORY_H

#include <filesystem>
#include <functional>
#include <memory>

#include "archive.hpp"

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace VFS
{
    /// Turns the archive file at `path` into an Archive, or returns nullptr when the file is not of this factory's
    /// kind. A factory decides by the file's magic bytes, never by its extension alone. It throws only for a file
    /// that is of its kind but broken, and the message names the file.
    using ArchiveFactory = std::function<std::unique_ptr<Archive>(
        const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder)>;

    /// Appends a factory to the process-wide list. Not thread-safe: call it while the program starts, before
    /// registerArchives(). Future archive kinds (the Eden pack, task 0052) are added this way.
    void addArchiveFactory(ArchiveFactory factory);

    /// Empties the list. For tests.
    void clearArchiveFactories();

    /// Asks the added factories in the order they were added, then the built-in Bethesda reader (BSA and BA2).
    /// Throws std::runtime_error naming the file if nobody recognises it.
    std::unique_ptr<Archive> makeArchive(
        const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder);
}

#endif
