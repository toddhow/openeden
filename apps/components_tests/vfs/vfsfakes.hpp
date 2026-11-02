#ifndef OPENMW_APPS_COMPONENTS_TESTS_VFS_VFSFAKES_H
#define OPENMW_APPS_COMPONENTS_TESTS_VFS_VFSFAKES_H

// Test doubles for the VFS tests of task 0050 that need the new interfaces of the task (File::getContentHash and
// the default File::size and File::readRange). Written from the spec's "Interface fixed for implementation".

#include "vfstestutil.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace VFSTest
{
    inline VFS::ContentHash makeHash(std::uint8_t seed)
    {
        VFS::ContentHash hash;
        hash.fill(seed);
        return hash;
    }

    /// An in-memory file with an optional content hash. It leaves size() and readRange() to File's defaults.
    class FakeFile : public VFS::File
    {
    public:
        explicit FakeFile(
            std::string content, std::optional<VFS::ContentHash> hash = std::nullopt, std::string stem = "fake")
            : mContent(std::move(content))
            , mHash(hash)
            , mStem(std::move(stem))
        {
        }

        Files::IStreamPtr open() override { return std::make_unique<std::stringstream>(mContent, std::ios_base::in); }

        std::filesystem::file_time_type getLastModified() const override { return {}; }

        std::string getStem() const override { return mStem; }

        std::optional<VFS::ContentHash> getContentHash() const override { return mHash; }

        const std::string& content() const { return mContent; }

    private:
        const std::string mContent;
        const std::optional<VFS::ContentHash> mHash;
        const std::string mStem;
    };

    /// A file that implements only what every File must (open, time, stem). Every other member is File's default.
    /// The content can be replaced to model a file that changes between calls; open() calls are counted.
    class OpenOnlyFile : public VFS::File
    {
    public:
        explicit OpenOnlyFile(std::string content)
            : mContent(std::move(content))
        {
        }

        Files::IStreamPtr open() override
        {
            ++mOpenCount;
            std::lock_guard lock(mMutex);
            return std::make_unique<std::stringstream>(mContent, std::ios_base::in);
        }

        std::filesystem::file_time_type getLastModified() const override { return {}; }

        std::string getStem() const override { return "openonly"; }

        void setContent(std::string content)
        {
            std::lock_guard lock(mMutex);
            mContent = std::move(content);
        }

        std::size_t openCount() const { return mOpenCount; }

    private:
        std::mutex mMutex;
        std::string mContent;
        std::atomic<std::size_t> mOpenCount{ 0 };
    };

    /// An archive of a third kind, built in memory. Files are created with add() and are owned by the archive.
    class FakeArchive : public VFS::Archive
    {
    public:
        explicit FakeArchive(std::string description)
            : mDescription(std::move(description))
        {
        }

        FakeFile& add(std::string_view name, std::string content, std::optional<VFS::ContentHash> hash = std::nullopt)
        {
            auto file = std::make_unique<FakeFile>(std::move(content), hash);
            FakeFile& result = *file;
            mFiles.insert_or_assign(VFS::Path::Normalized(name), std::move(file));
            return result;
        }

        FakeFile* find(std::string_view name) const
        {
            const auto it = mFiles.find(VFS::Path::Normalized(name));
            return it == mFiles.end() ? nullptr : it->second.get();
        }

        void listResources(VFS::FileMap& out) override
        {
            for (const auto& [name, file] : mFiles)
                out[name] = file.get();
        }

        bool contains(VFS::Path::NormalizedView file) const override { return mFiles.find(file) != mFiles.end(); }

        std::string getDescription() const override { return mDescription; }

    private:
        std::string mDescription;
        std::map<VFS::Path::Normalized, std::unique_ptr<FakeFile>, std::less<>> mFiles;
    };

    /// Adds a new FakeArchive to the manager and returns it; the manager owns it.
    inline FakeArchive& addFakeArchive(VFS::Manager& vfs, std::string description)
    {
        auto archive = std::make_unique<FakeArchive>(std::move(description));
        FakeArchive& result = *archive;
        vfs.addArchive(std::move(archive));
        return result;
    }
}

#endif
