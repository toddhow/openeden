#include "bodypack.hpp"

#include "bodypackage.hpp"

#include <edenarchive/archive.hpp>
#include <edenarchive/hash.hpp>
#include <edenarchive/source.hpp>
#include <edenarchive/writer.hpp>

#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace Body
{
    namespace
    {
        // Packing is store-only with the library's default chunk size and no pack flags, on one thread, so the pack
        // (and its manifest digest) depends only on the files (bodypack.hpp).
        EdenArchive::WriteOptions packOptions()
        {
            EdenArchive::WriteOptions options;
            options.mStoreOnly = true;
            options.mThreads = 1;
            return options;
        }

        std::string utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        bool toEntries(const PackageFiles& files, std::vector<EdenArchive::InputEntry>& entries, std::string& error)
        {
            if (files.mFiles.size() > PackLimits::sMaxFiles)
            {
                error = "the package has more than " + std::to_string(PackLimits::sMaxFiles) + " files";
                return false;
            }
            std::uint64_t total = 0;
            entries.reserve(files.mFiles.size());
            for (const auto& [path, content] : files.mFiles)
            {
                total += content.size();
                if (total > PackLimits::sMaxTotalBytes)
                {
                    error = "the package is larger than " + std::to_string(PackLimits::sMaxTotalBytes) + " bytes";
                    return false;
                }
                EdenArchive::InputEntry entry;
                entry.mPath = path;
                entry.mType = 0;
                entry.mCompression = EdenArchive::Compression::Store;
                entry.mData = content;
                entries.push_back(std::move(entry));
            }
            return true;
        }

        std::string oneLine(std::string text)
        {
            for (char& c : text)
            {
                if (c == '\n' || c == '\r')
                {
                    c = ' ';
                }
            }
            return text;
        }
    }

    std::optional<PackageFiles> readPackageFolder(const std::filesystem::path& folder, std::string& error)
    {
        try
        {
            std::error_code code;
            if (!std::filesystem::is_directory(folder, code))
            {
                error = "'" + utf8(folder) + "' is not a folder";
                return std::nullopt;
            }
            PackageFiles files;
            std::uint64_t total = 0;
            std::filesystem::recursive_directory_iterator iterator(
                folder, std::filesystem::directory_options::skip_permission_denied, code);
            if (code)
            {
                error = "cannot list '" + utf8(folder) + "': " + code.message();
                return std::nullopt;
            }
            for (; iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(code))
            {
                if (code)
                {
                    error = "cannot list '" + utf8(folder) + "': " + code.message();
                    return std::nullopt;
                }
                const std::filesystem::directory_entry& entry = *iterator;
                if (entry.is_symlink(code) || !entry.is_regular_file(code))
                {
                    continue;
                }
                const std::u8string generic = std::filesystem::relative(entry.path(), folder, code).generic_u8string();
                const std::string key(generic.begin(), generic.end());
                if (!isSafePackagePath(key))
                {
                    error = "the file name '" + oneLine(key) + "' is not a safe package path";
                    return std::nullopt;
                }
                if (files.mFiles.size() >= PackLimits::sMaxFiles)
                {
                    error = "the folder has more than " + std::to_string(PackLimits::sMaxFiles) + " files";
                    return std::nullopt;
                }
                const std::uintmax_t size = entry.file_size(code);
                if (code)
                {
                    error = "cannot read the size of '" + oneLine(key) + "': " + code.message();
                    return std::nullopt;
                }
                total += size;
                if (total > PackLimits::sMaxTotalBytes)
                {
                    error = "the folder holds more than " + std::to_string(PackLimits::sMaxTotalBytes) + " bytes";
                    return std::nullopt;
                }
                std::ifstream stream(entry.path(), std::ios::binary);
                std::vector<std::uint8_t> content(static_cast<std::size_t>(size));
                stream.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()));
                if (!stream || stream.gcount() != static_cast<std::streamsize>(content.size()))
                {
                    error = "cannot read '" + oneLine(key) + "'";
                    return std::nullopt;
                }
                files.mFiles.emplace(key, std::move(content));
            }
            return files;
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot read the folder: ") + e.what());
            return std::nullopt;
        }
    }

    bool writePackageFile(const PackageFiles& files, const std::filesystem::path& output, std::string& error)
    {
        try
        {
            std::vector<EdenArchive::InputEntry> entries;
            if (!toEntries(files, entries, error))
            {
                return false;
            }
            const EdenArchive::Error result = EdenArchive::writePack(entries, output, packOptions());
            if (result)
            {
                error = oneLine(result.describe(utf8(output)));
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot write the pack: ") + e.what());
            return false;
        }
    }

    std::optional<PackageFiles> readPackageFile(const std::filesystem::path& pack, std::string& error)
    {
        try
        {
            EdenArchive::Error result;
            const std::unique_ptr<EdenArchive::Archive> archive
                = EdenArchive::Archive::open(pack, EdenArchive::OpenOptions{}, result);
            if (archive == nullptr)
            {
                error = oneLine(result.describe(utf8(pack)));
                return std::nullopt;
            }
            const std::size_t count = archive->entryCount();
            if (count > PackLimits::sMaxFiles)
            {
                error = "the pack has more than " + std::to_string(PackLimits::sMaxFiles) + " entries";
                return std::nullopt;
            }
            std::uint64_t total = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const EdenArchive::EntryInfo entry = archive->entry(i);
                if (!entry.mHasPath)
                {
                    error = "the pack has an entry without a path";
                    return std::nullopt;
                }
                if (entry.mSize > PackLimits::sMaxTotalBytes - total)
                {
                    error = "the pack holds more than " + std::to_string(PackLimits::sMaxTotalBytes) + " bytes";
                    return std::nullopt;
                }
                total += entry.mSize;
                if (!isSafePackagePath(entry.mPath))
                {
                    error = "the pack has an entry whose name is not a safe package path";
                    return std::nullopt;
                }
            }
            PackageFiles files;
            EdenArchive::DecodeBudget budget;
            budget.mRemaining = PackLimits::sMaxTotalBytes;
            for (std::size_t i = 0; i < count; ++i)
            {
                const EdenArchive::EntryInfo entry = archive->entry(i);
                std::vector<std::uint8_t> content;
                const EdenArchive::Error read = archive->readEntry(i, content, &budget);
                if (read)
                {
                    error = oneLine(read.describe(utf8(pack)));
                    return std::nullopt;
                }
                if (!files.mFiles.emplace(std::string(entry.mPath), std::move(content)).second)
                {
                    error = "the pack has two entries with the same name";
                    return std::nullopt;
                }
            }
            return files;
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot read the pack: ") + e.what());
            return std::nullopt;
        }
    }

    std::string packageDigest(const PackageFiles& files, std::string& error)
    {
        try
        {
            std::vector<EdenArchive::InputEntry> entries;
            if (!toEntries(files, entries, error))
            {
                return {};
            }
            std::vector<std::uint8_t> bytes;
            const EdenArchive::Error written = EdenArchive::writePackToMemory(entries, bytes, packOptions());
            if (written)
            {
                error = oneLine(written.describe());
                return {};
            }
            EdenArchive::Error opened;
            const std::unique_ptr<EdenArchive::Archive> archive = EdenArchive::Archive::open(
                EdenArchive::makeMemorySource(std::move(bytes)), EdenArchive::OpenOptions{}, opened);
            if (archive == nullptr)
            {
                error = oneLine(opened.describe());
                return {};
            }
            return EdenArchive::toHex(archive->info().mManifestDigest);
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot compute the digest: ") + e.what());
            return {};
        }
    }
}
