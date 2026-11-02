#ifndef OPENMW_COMPONENTS_VFS_RANGEREAD_H
#define OPENMW_COMPONENTS_VFS_RANGEREAD_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "file.hpp"

namespace VFS
{
    /// True when [offset, offset + size) lies inside a region of `regionSize` bytes. Written so that nothing can
    /// overflow, whatever the three numbers are.
    [[nodiscard]] constexpr bool isRangeInside(
        std::uint64_t offset, std::uint64_t size, std::uint64_t regionSize) noexcept
    {
        return offset <= regionSize && size <= regionSize - offset;
    }

    /// Reads `size` bytes at `offset` of a region of a disk file with a positional read on a handle opened for the
    /// call only, so any number of threads may call it at once.
    /// The region is `regionSize` bytes long and starts at byte `regionStart` of the file at `path`.
    /// Throws std::runtime_error (naming `label`, the file's description) when the range is not inside the region,
    /// when the region is not inside the file as it is now, or when the file gives fewer bytes than asked.
    void readFileRange(const std::filesystem::path& path, std::uint64_t regionStart, std::uint64_t regionSize,
        std::uint64_t offset, std::size_t size, void* destination, const std::string& label);

    /// The same for a whole file: the region is the file as it is when the call opens it.
    void readFileRange(const std::filesystem::path& path, std::uint64_t offset, std::size_t size, void* destination,
        const std::string& label);

    /// Size in bytes of the disk file at `path`. Throws std::runtime_error naming the path on failure.
    std::uint64_t getFileSize(const std::filesystem::path& path);
}

#endif
