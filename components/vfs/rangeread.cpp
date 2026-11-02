#include "rangeread.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include <components/files/conversion.hpp>
#include <components/platform/file.hpp>

namespace VFS
{
    namespace
    {
        [[noreturn]] void fail(const std::string& label, const std::string& what)
        {
            throw std::runtime_error("Failed to read a range of " + label + ": " + what);
        }
    }

    std::uint64_t getFileSize(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec)
            throw std::runtime_error(
                "Failed to get the size of '" + Files::pathToUnicodeString(path) + "': " + ec.message());
        return size;
    }

    namespace
    {
        void readOpened(Platform::File::Handle handle, std::uint64_t regionStart, std::uint64_t regionSize,
            std::uint64_t offset, std::size_t size, void* destination,
            const std::string& label)
        {
            if (!isRangeInside(offset, size, regionSize))
                fail(label,
                    "offset " + std::to_string(offset) + " and size " + std::to_string(size)
                        + " are outside the content of " + std::to_string(regionSize) + " bytes");

            if (size == 0)
                return;

            // Only the bytes asked for must exist: an entry that points past the end of its archive can still be
            // read where it does lie inside the file, and a read that reaches the missing part is a short read below.
            // The addition cannot overflow for a region the caller got from a 32-bit header, but check anyway.
            if (!isRangeInside(regionStart, offset, std::numeric_limits<std::uint64_t>::max()))
                fail(label, "the entry lies beyond the largest possible file");

            const std::uint64_t position = regionStart + offset;
            if (position > std::numeric_limits<std::size_t>::max())
                fail(label, "the position does not fit into this platform's file offsets");

            const std::size_t got = Platform::File::readAt(handle, static_cast<std::size_t>(position), destination, size);
            if (got != size)
                fail(label, "expected " + std::to_string(size) + " bytes, got " + std::to_string(got));
        }
    }

    void readFileRange(const std::filesystem::path& path, std::uint64_t regionStart, std::uint64_t regionSize,
        std::uint64_t offset, std::size_t size, void* destination, const std::string& label)
    {
        // Check what can be checked before touching the disk, so a bad request costs no system call.
        if (!isRangeInside(offset, size, regionSize))
            fail(label,
                "offset " + std::to_string(offset) + " and size " + std::to_string(size)
                    + " are outside the content of " + std::to_string(regionSize) + " bytes");
        if (size == 0)
            return;

        Platform::File::ScopedHandle handle(Platform::File::open(path));
        readOpened(handle, regionStart, regionSize, offset, size, destination, label);
    }

    void readFileRange(const std::filesystem::path& path, std::uint64_t offset, std::size_t size, void* destination,
        const std::string& label)
    {
        // std::filesystem for the size: Platform::File::size does not support files over 4 GiB on Windows.
        const std::uint64_t fileSize = getFileSize(path);
        Platform::File::ScopedHandle handle(Platform::File::open(path));
        readOpened(handle, 0, fileSize, offset, size, destination, label);
    }
}
