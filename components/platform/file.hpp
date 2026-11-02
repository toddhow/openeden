#ifndef OPENMW_COMPONENTS_PLATFORM_FILE_HPP
#define OPENMW_COMPONENTS_PLATFORM_FILE_HPP

#include <cstdlib>
#include <filesystem>

namespace Platform::File
{

    enum class Handle : intptr_t
    {
        Invalid = -1
    };

    enum class SeekType
    {
        Begin,
        Current,
        End
    };

    Handle open(const std::filesystem::path& filename);

    void close(Handle handle);

    size_t size(Handle handle);

    void seek(Handle handle, size_t position, SeekType type = SeekType::Begin);
    size_t tell(Handle handle);

    size_t read(Handle handle, void* data, size_t size);

    /// Reads up to `size` bytes starting at the absolute byte `position` and returns how many were read (fewer than
    /// `size` only at the end of the file). Does not depend on, and may change, the handle's current position, so
    /// several threads may call it on one handle at once where the platform allows (Windows and POSIX do).
    size_t readAt(Handle handle, size_t position, void* data, size_t size);

    class ScopedHandle
    {
        Handle mHandle{ Handle::Invalid };

    public:
        ScopedHandle() noexcept = default;
        ScopedHandle(ScopedHandle& other) = delete;
        ScopedHandle(Handle handle) noexcept
            : mHandle(handle)
        {
        }
        ScopedHandle(ScopedHandle&& other) noexcept
            : mHandle(other.mHandle)
        {
            other.mHandle = Handle::Invalid;
        }
        ScopedHandle& operator=(const ScopedHandle& other) = delete;
        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (mHandle != Handle::Invalid)
                close(mHandle);
            mHandle = other.mHandle;
            other.mHandle = Handle::Invalid;
            return *this;
        }
        ~ScopedHandle()
        {
            if (mHandle != Handle::Invalid)
                close(mHandle);
        }

        operator Handle() const { return mHandle; }
    };
}

#endif // OPENMW_COMPONENTS_PLATFORM_FILE_HPP
