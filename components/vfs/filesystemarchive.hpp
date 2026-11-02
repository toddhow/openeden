#ifndef OPENMW_COMPONENTS_RESOURCE_FILESYSTEMARCHIVE_H
#define OPENMW_COMPONENTS_RESOURCE_FILESYSTEMARCHIVE_H

#include "archive.hpp"
#include "file.hpp"

#include <filesystem>
#include <string>

namespace VFS
{

    class FileSystemArchiveFile : public File
    {
    public:
        FileSystemArchiveFile(const std::filesystem::path& path);

        Files::IStreamPtr open() override;

        std::filesystem::file_time_type getLastModified() const override;

        std::string getStem() const override;

        std::uint64_t size() override;

        /// A positional read on a handle opened for the call: thread-safe, and the file may be replaced between calls.
        void readRange(std::uint64_t offset, std::size_t size, void* destination) override;

    private:
        std::filesystem::path mPath;
    };

    class FileSystemArchive : public Archive
    {
    public:
        FileSystemArchive(const std::filesystem::path& path);

        void listResources(FileMap& out) override;

        bool contains(Path::NormalizedView file) const override;

        std::string getDescription() const override;

    private:
        std::map<VFS::Path::Normalized, FileSystemArchiveFile, std::less<>> mIndex;
        std::filesystem::path mPath;
    };

}

#endif
