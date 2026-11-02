// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include <edenarchive/source.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace EdenArchive
{
    namespace
    {
        Error ioError(const std::string& what, const std::filesystem::path& path)
        {
            Error error;
            error.mClass = ErrorClass::Io;
            error.mField = "file";
            // generic_u8string: path.string() throws on Windows for a name the ANSI code page cannot hold.
            const std::u8string text = path.generic_u8string();
            error.mMessage = what + " '" + std::string(reinterpret_cast<const char*>(text.data()), text.size()) + "'";
            return error;
        }

        class MemorySource final : public Source
        {
        public:
            explicit MemorySource(std::vector<std::uint8_t> bytes)
                : mBytes(std::move(bytes))
            {
            }

            std::uint64_t size() const noexcept override { return mBytes.size(); }

            bool read(std::uint64_t offset, void* destination, std::size_t size) const noexcept override
            {
                if (offset > mBytes.size() || size > mBytes.size() - offset)
                    return false;
                if (size != 0)
                    std::memcpy(destination, mBytes.data() + offset, size);
                return true;
            }

        private:
            std::vector<std::uint8_t> mBytes;
        };

#ifdef _WIN32
        // One handle, opened once. ReadFile with an explicit offset in OVERLAPPED reads at that position; the handle
        // is a synchronous one, so concurrent calls are serialised by the system and each uses its own offset.
        class FileSource final : public Source
        {
        public:
            FileSource(HANDLE handle, std::uint64_t size)
                : mHandle(handle)
                , mSize(size)
            {
            }
            ~FileSource() override { CloseHandle(mHandle); }

            std::uint64_t size() const noexcept override { return mSize; }

            bool read(std::uint64_t offset, void* destination, std::size_t size) const noexcept override
            {
                if (offset > mSize || size > mSize - offset)
                    return false;
                std::size_t total = 0;
                while (total < size)
                {
                    constexpr std::size_t maxChunk = std::size_t{ 1 } << 30; // ReadFile takes a 32-bit count
                    const DWORD chunk = static_cast<DWORD>(std::min(size - total, maxChunk));
                    const unsigned long long at = offset + total;
                    OVERLAPPED overlapped{};
                    overlapped.Offset = static_cast<DWORD>(at & 0xFFFFFFFFull);
                    overlapped.OffsetHigh = static_cast<DWORD>(at >> 32);
                    DWORD got = 0;
                    if (!ReadFile(mHandle, static_cast<char*>(destination) + total, chunk, &got, &overlapped)
                        || got == 0)
                        return false;
                    total += got;
                }
                return true;
            }

        private:
            HANDLE mHandle;
            std::uint64_t mSize;
        };
#else
        class FileSource final : public Source
        {
        public:
            FileSource(int fd, std::uint64_t size)
                : mFd(fd)
                , mSize(size)
            {
            }
            ~FileSource() override { ::close(mFd); }

            std::uint64_t size() const noexcept override { return mSize; }

            bool read(std::uint64_t offset, void* destination, std::size_t size) const noexcept override
            {
                if (offset > mSize || size > mSize - offset)
                    return false;
                std::size_t total = 0;
                while (total < size)
                {
                    const ssize_t got = ::pread(
                        mFd, static_cast<char*>(destination) + total, size - total, static_cast<off_t>(offset + total));
                    if (got < 0 && errno == EINTR)
                        continue;
                    if (got <= 0)
                        return false;
                    total += static_cast<std::size_t>(got);
                }
                return true;
            }

        private:
            int mFd;
            std::uint64_t mSize;
        };
#endif
    }

    namespace
    {
        std::shared_ptr<const Source> openFileSourceUnguarded(const std::filesystem::path& path, Error& error)
        {
            error = Error{};
#ifdef _WIN32
            // Sharing includes delete, so a tool can replace the file by rename while a program has it open.
            HANDLE handle
                = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                error = ioError("cannot open", path);
                return nullptr;
            }
            LARGE_INTEGER size;
            if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0)
            {
                CloseHandle(handle);
                error = ioError("cannot get the size of", path);
                return nullptr;
            }
            return std::make_shared<FileSource>(handle, static_cast<std::uint64_t>(size.QuadPart));
#else
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
            {
                error = ioError("cannot open", path);
                return nullptr;
            }
            struct stat info;
            if (::fstat(fd, &info) != 0 || info.st_size < 0 || !S_ISREG(info.st_mode))
            {
                ::close(fd);
                error = ioError("cannot get the size of", path);
                return nullptr;
            }
            return std::make_shared<FileSource>(fd, static_cast<std::uint64_t>(info.st_size));
#endif
        }
    }

    std::shared_ptr<const Source> openFileSource(const std::filesystem::path& path, Error& error)
    {
        try
        {
            return openFileSourceUnguarded(path, error);
        }
        catch (...)
        {
            error = Error{};
            error.mClass = ErrorClass::Io;
            error.mField = "file";
            error.mMessage = "cannot open";
            return nullptr;
        }
    }

    std::shared_ptr<const Source> makeMemorySource(const std::vector<std::uint8_t>& bytes)
    {
        return std::make_shared<MemorySource>(bytes);
    }

    std::shared_ptr<const Source> makeMemorySource(std::vector<std::uint8_t>&& bytes)
    {
        return std::make_shared<MemorySource>(std::move(bytes));
    }
}
