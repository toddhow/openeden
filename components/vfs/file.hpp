#ifndef OPENMW_COMPONENTS_VFS_FILE_H
#define OPENMW_COMPONENTS_VFS_FILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <components/files/istreamptr.hpp>

namespace VFS
{
    /// The BLAKE3-256 digest of a file's content (archive doc 7 and 14.5).
    using ContentHash = std::array<std::uint8_t, 32>;

    class File
    {
    public:
        virtual ~File() = default;

        virtual Files::IStreamPtr open() = 0;

        virtual std::filesystem::file_time_type getLastModified() const = 0;

        virtual std::string getStem() const = 0;

        /// Size of the content in bytes (for a compressed archive entry: the uncompressed size).
        /// The default opens a stream and seeks to its end; subclasses that know the size should override it.
        /// @note Safe to call from several threads at once, like open().
        virtual std::uint64_t size();

        /// Copies exactly `size` bytes of the content, starting at byte `offset`, into `destination`.
        /// Valid when offset <= this->size() and size <= this->size() - offset; a zero-length read is valid at any
        /// offset up to and including the size. Anything else, and a short read (the file shrank, or an archive
        /// entry points past the end of its archive), throws std::runtime_error. A failed call may leave
        /// `destination` partly written, but never touches memory outside [destination, destination + size).
        /// The default opens a stream, seeks and reads; subclasses override it with a positional read.
        /// @note Synchronous. Safe to call from several threads at once on the same object.
        virtual void readRange(std::uint64_t offset, std::size_t size, void* destination);

        /// The content hash stored with the file, if its container stores one. Loose files and legacy archive
        /// entries have none; hashing them reads the whole file, so only explicit tools do it, never the game.
        virtual std::optional<ContentHash> getContentHash() const { return std::nullopt; }
    };
}

#endif
