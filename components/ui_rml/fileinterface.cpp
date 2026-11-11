#include "fileinterface.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace UiRml
{
    VfsFileInterface::VfsFileInterface(const VFS::Manager& vfs)
        : mVfs(vfs)
    {
    }

    bool VfsFileInterface::readAll(const std::string& path, std::vector<char>& out) const
    {
        try
        {
            const Files::IStreamPtr stream = mVfs.find(VFS::Path::Normalized(path));
            if (stream == nullptr)
                return false;
            out.assign(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>());
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    Rml::FileHandle VfsFileInterface::Open(const Rml::String& path)
    {
        auto file = std::make_unique<Open_>();
        if (!readAll(path, file->mData))
            return 0;
        std::lock_guard lock(mMutex);
        const std::uintptr_t id = mNext++;
        mOpen.emplace(id, std::move(file));
        return static_cast<Rml::FileHandle>(id);
    }

    void VfsFileInterface::Close(Rml::FileHandle file)
    {
        std::lock_guard lock(mMutex);
        mOpen.erase(static_cast<std::uintptr_t>(file));
    }

    VfsFileInterface::Open_* VfsFileInterface::find(Rml::FileHandle file) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mOpen.find(static_cast<std::uintptr_t>(file));
        return it == mOpen.end() ? nullptr : it->second.get();
    }

    size_t VfsFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
    {
        Open_* open = find(file);
        if (open == nullptr)
            return 0;
        const std::size_t count = std::min(size, open->mData.size() - open->mPosition);
        if (count > 0)
            std::memcpy(buffer, open->mData.data() + open->mPosition, count);
        open->mPosition += count;
        return count;
    }

    bool VfsFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
    {
        Open_* open = find(file);
        if (open == nullptr)
            return false;
        long long base = 0;
        switch (origin)
        {
            case SEEK_SET:
                base = 0;
                break;
            case SEEK_CUR:
                base = static_cast<long long>(open->mPosition);
                break;
            case SEEK_END:
                base = static_cast<long long>(open->mData.size());
                break;
            default:
                return false;
        }
        const long long target = base + offset;
        if (target < 0 || target > static_cast<long long>(open->mData.size()))
            return false;
        open->mPosition = static_cast<std::size_t>(target);
        return true;
    }

    size_t VfsFileInterface::Tell(Rml::FileHandle file)
    {
        const Open_* open = find(file);
        return open == nullptr ? 0 : open->mPosition;
    }

    size_t VfsFileInterface::Length(Rml::FileHandle file)
    {
        const Open_* open = find(file);
        return open == nullptr ? 0 : open->mData.size();
    }

    std::size_t VfsFileInterface::openCount() const
    {
        std::lock_guard lock(mMutex);
        return mOpen.size();
    }
}
