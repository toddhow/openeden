#ifndef OPENMW_COMPONENTS_VFS_ARCHIVEFACTORY_H
#define OPENMW_COMPONENTS_VFS_ARCHIVEFACTORY_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

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

    /// Appends a factory to the process-wide list. Thread-safe (a mutex guards the list); still call it while the
    /// program starts, before registerArchives(), because a lookup sees the factories added before it copied the
    /// list. Archive kinds other than the Eden pack are added this way.
    void addArchiveFactory(ArchiveFactory factory);

    /// Adds a factory under a key unless a factory with that key is already in the list; returns whether it was
    /// added. For a group of factories that a start-up function registers and that may be asked to register twice.
    bool addArchiveFactoryOnce(const std::string& key, ArchiveFactory factory);

    /// How many factories are in the list.
    std::size_t archiveFactoryCount();

    /// Empties the list, keys included. For tests.
    void clearArchiveFactories();

    /// Asks the added factories in the order they were added, then the built-in Eden pack reader. VFS knows no
    /// other archive format: the Bethesda kinds are factories too (components/archivekinds).
    /// Throws std::runtime_error naming the file if nobody recognises it.
    std::unique_ptr<Archive> makeArchive(
        const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder);
}

#endif
