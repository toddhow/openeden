#include "edenarchive.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include <edenarchive/archive.hpp>

#include <components/files/conversion.hpp>

#include "archivefactory.hpp"
#include "file.hpp"
#include "filemap.hpp"
#include "pathutil.hpp"

namespace VFS
{
    namespace
    {
        // A seekable, read-only stream over bytes it owns, for the legacy code paths that want a std::istream.
        class BytesStreamBuf : public std::streambuf
        {
        public:
            explicit BytesStreamBuf(std::vector<std::uint8_t> bytes)
                : mBytes(std::move(bytes))
            {
                char* begin = reinterpret_cast<char*>(mBytes.data());
                setg(begin, begin, begin + mBytes.size());
            }

        protected:
            pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode) override
            {
                const off_type size = static_cast<off_type>(mBytes.size());
                off_type base = 0;
                if (direction == std::ios_base::cur)
                    base = static_cast<off_type>(gptr() - eback());
                else if (direction == std::ios_base::end)
                    base = size;
                const off_type target = base + offset;
                if (target < 0 || target > size)
                    return pos_type(off_type(-1));
                setg(eback(), eback() + target, egptr());
                return pos_type(target);
            }

            pos_type seekpos(pos_type position, std::ios_base::openmode mode) override
            {
                return seekoff(off_type(position), std::ios_base::beg, mode);
            }

        private:
            std::vector<std::uint8_t> mBytes;
        };

        class BytesStream final : private BytesStreamBuf, public std::istream
        {
        public:
            explicit BytesStream(std::vector<std::uint8_t> bytes)
                : BytesStreamBuf(std::move(bytes))
                , std::istream(static_cast<BytesStreamBuf*>(this))
            {
            }
        };

        // The most that File::open() will decode into memory (1 GiB); see EdenArchiveFile::open.
        constexpr std::uint64_t maxOpenSize = std::uint64_t{ 1 } << 30;

        class EdenArchiveFile;

        // The entries of one pack. The Archive is immutable after opening, so any thread may read it.
        class EdenPackArchive final : public Archive
        {
        public:
            explicit EdenPackArchive(const std::filesystem::path& path);

            void listResources(FileMap& out) override;
            bool contains(Path::NormalizedView file) const override;
            std::string getDescription() const override { return "Eden Archive: " + Files::pathToUnicodeString(mPath); }

            const EdenArchive::Archive& pack() const { return *mPack; }
            const std::filesystem::path& path() const { return mPath; }

        private:
            std::filesystem::path mPath;
            std::unique_ptr<EdenArchive::Archive> mPack;
            std::vector<EdenArchiveFile> mFiles;
        };

        [[noreturn]] void fail(const EdenPackArchive& pack, const std::string& what, const EdenArchive::Error& error)
        {
            throw std::runtime_error(what + " in " + Files::pathToUnicodeString(pack.path()) + ": "
                + error.describe());
        }

        class EdenArchiveFile final : public File
        {
        public:
            EdenArchiveFile(const EdenPackArchive* owner, std::size_t index)
                : mOwner(owner)
                , mIndex(index)
            {
            }

            // The whole entry, verified, as a stream (the legacy interface). Range reads below do not use it.
            Files::IStreamPtr open() override
            {
                std::vector<std::uint8_t> bytes;
                // The legacy interface holds the whole entry in memory, so it is capped; larger entries are read in
                // ranges (File::readRange), which has no such limit.
                EdenArchive::DecodeBudget budget;
                budget.mRemaining = maxOpenSize;
                if (const EdenArchive::Error error = mOwner->pack().readEntry(mIndex, bytes, &budget))
                    fail(*mOwner, "Failed to read '" + std::string(mOwner->pack().entry(mIndex).mPath) + "'", error);
                return std::make_unique<BytesStream>(std::move(bytes));
            }

            std::filesystem::file_time_type getLastModified() const override
            {
                std::error_code ec;
                const std::filesystem::file_time_type time = std::filesystem::last_write_time(mOwner->path(), ec);
                return ec ? std::filesystem::file_time_type::min() : time;
            }

            std::string getStem() const override
            {
                std::string_view name = mOwner->pack().entry(mIndex).mPath;
                auto index = name.find_last_of('/');
                if (index != std::string_view::npos)
                    name = name.substr(index + 1);
                index = name.find_last_of('.');
                if (index != std::string_view::npos && index != 0)
                    name = name.substr(0, index);
                return std::string(name);
            }

            std::size_t index() const { return mIndex; }

            std::uint64_t size() override { return mOwner->pack().entry(mIndex).mSize; }

            void readRange(std::uint64_t offset, std::size_t size, void* destination) override
            {
                if (const EdenArchive::Error error = mOwner->pack().readRange(mIndex, offset, size, destination))
                    fail(*mOwner,
                        "Failed to read a range of '" + std::string(mOwner->pack().entry(mIndex).mPath) + "'", error);
            }

            // The hash the pack stores for the entry. It is the pack author's statement until the entry has been
            // read once: reading a range verifies the chunks it touches, not this value (specification 14.3).
            std::optional<ContentHash> getContentHash() const override
            {
                return mOwner->pack().entry(mIndex).mContentHash;
            }

        private:
            const EdenPackArchive* mOwner;
            std::size_t mIndex;
        };

        EdenPackArchive::EdenPackArchive(const std::filesystem::path& path)
            : mPath(path)
        {
            EdenArchive::Error error;
            mPack = EdenArchive::Archive::open(path, EdenArchive::OpenOptions{}, error);
            if (!mPack)
                throw std::runtime_error("Failed to open the Eden Archive '" + Files::pathToUnicodeString(path)
                    + "': " + error.describe());
            // Created after the pack: the files point back at this object, which does not move (it is held by pointer).
            mFiles.reserve(mPack->entryCount());
            for (std::size_t i = 0; i < mPack->entryCount(); ++i)
            {
                if (mPack->entry(i).mHasPath)
                    mFiles.emplace_back(this, i);
            }
        }

        void EdenPackArchive::listResources(FileMap& out)
        {
            for (EdenArchiveFile& file : mFiles)
                out[Path::Normalized(std::string_view(mPack->entry(file.index()).mPath))] = &file;
        }

        bool EdenPackArchive::contains(Path::NormalizedView file) const
        {
            return mPack->findPath(file.value()).has_value();
        }

    }

    std::unique_ptr<Archive> makeEdenArchive(
        const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* /*encoder*/)
    {
        // Decide by the magic, never by the extension: the seven ASCII bytes, so that a pack of another container
        // generation is reported as unsupported instead of being ignored.
        {
            std::ifstream stream(path, std::ios::binary);
            char magic[7] = {};
            if (!stream.read(magic, sizeof(magic)) || std::memcmp(magic, "EDENPAK", sizeof(magic)) != 0)
                return nullptr;
        }
        return std::make_unique<EdenPackArchive>(path);
    }

    void registerEdenArchiveFactory()
    {
        addArchiveFactory(&makeEdenArchive);
    }
}
