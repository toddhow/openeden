#ifndef OPENEDEN_COMPONENTS_UI_RML_FILEINTERFACE_H
#define OPENEDEN_COMPONENTS_UI_RML_FILEINTERFACE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core/FileInterface.h>

namespace VFS
{
    class Manager;
}

namespace UiRml
{
    /// RmlUi's file interface over the game's virtual file system: documents, style sheets, images and fonts come
    /// from the same data folders and archives as everything else, with later data winning (note 0045 15.5).
    ///
    /// A file is read whole when opened and kept in memory until it is closed. Names are looked up case insensitively
    /// with either slash, as everywhere in the VFS; a name the VFS does not know is not looked for on the disk.
    /// Owner: the Overlay. The VFS manager must outlive it. Thread: any (the open table has a lock).
    class VfsFileInterface final : public Rml::FileInterface
    {
    public:
        explicit VfsFileInterface(const VFS::Manager& vfs);

        /// Reads a whole file; false when it does not exist or cannot be read. Used by the font engine too.
        bool readAll(const std::string& path, std::vector<char>& out) const;

        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;

        /// Number of files opened and not closed (for tests and leak checks).
        std::size_t openCount() const;

    private:
        struct Open_
        {
            std::vector<char> mData;
            std::size_t mPosition = 0;
        };
        Open_* find(Rml::FileHandle file) const;

        const VFS::Manager& mVfs;
        mutable std::mutex mMutex;
        std::unordered_map<std::uintptr_t, std::unique_ptr<Open_>> mOpen;
        std::uintptr_t mNext = 1;
    };
}

#endif
