#ifndef OPENEDEN_COMPONENTS_UI_RML_FILEINTERFACE_H
#define OPENEDEN_COMPONENTS_UI_RML_FILEINTERFACE_H

#include <cstdint>
#include <functional>
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

        /// Task 0163: a file the theme system makes up. Asked first for every Open with the normalised path (lower
        /// case, forward slashes); true means `out` is the whole file.
        using Generator = std::function<bool(const std::string& path, std::vector<char>& out)>;
        void setGenerator(Generator generator);

        /// Task 0163: asked for every `.rcss` and `.rml` read from the VFS (not for generated files) with its bytes;
        /// false refuses the file (Open returns the null handle). The guard logs why.
        using Guard = std::function<bool(const std::string& path, const std::vector<char>& bytes)>;
        void setGuard(Guard guard);

        /// Task 0163: true when a `.rcss` file that was opened from the VFS reads differently now than when it was
        /// last served or reported here (the new content is remembered, so the next call is false again).
        bool watchedFilesChanged();

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
        Generator mGenerator;
        Guard mGuard;
        std::unordered_map<std::string, std::size_t> mWatched; // path -> hash of the bytes last served
        std::unordered_map<std::uintptr_t, std::unique_ptr<Open_>> mOpen;
        std::uintptr_t mNext = 1;
    };
}

#endif
