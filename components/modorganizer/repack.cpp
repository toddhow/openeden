#include "repack.hpp"

#include <components/vfs/entryname.hpp>

#include <edenarchive/archive.hpp>
#include <edenarchive/hash.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace ModOrganizer
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::uint64_t sIndexOverheadBytes = 64 * 1024;
        constexpr std::uint64_t sPerFileOverheadBytes = 256;

        std::string lowerAscii(std::string text)
        {
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return text;
        }

        std::string u8(const fs::path& p)
        {
            const auto s = p.u8string();
            return std::string(s.begin(), s.end());
        }

        struct SourceFile
        {
            std::string mRelative; // '/' separators, original case
            fs::path mPath;
            std::uint64_t mSize = 0;
        };

        struct Listing
        {
            std::vector<SourceFile> mPack;
            std::vector<SourceFile> mStay;
            std::vector<ScanProblem> mProblems;
            bool mTruncated = false;
        };

        // Regular files below the root, no links, sorted by relative name.
        Listing listFiles(const fs::path& root, const ScanLimits& limits)
        {
            Listing listing;
            std::error_code ec;
            std::size_t count = 0;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code e2;
                const auto generic = it->path().lexically_relative(root).generic_u8string();
                const std::string relative(generic.begin(), generic.end());
                if (it->is_symlink(e2))
                {
                    it.disable_recursion_pending();
                    listing.mProblems.push_back({ relative, "a link is not followed" });
                    continue;
                }
                if (static_cast<std::size_t>(it.depth()) >= limits.mMaxDepth)
                    it.disable_recursion_pending();
                if (!it->is_regular_file(e2))
                    continue;
                if (++count > limits.mMaxFiles)
                {
                    listing.mTruncated = true;
                    break;
                }
                SourceFile file{ relative, it->path(), it->file_size(e2) };
                if (const auto reason = VFS::Path::checkExtractionSafe(relative))
                    listing.mProblems.push_back({ relative, *reason });
                (staysOnDisk(relative) ? listing.mStay : listing.mPack).push_back(std::move(file));
            }
            const auto byName = [](const SourceFile& a, const SourceFile& b) { return a.mRelative < b.mRelative; };
            std::sort(listing.mPack.begin(), listing.mPack.end(), byName);
            std::sort(listing.mStay.begin(), listing.mStay.end(), byName);
            return listing;
        }

        // False when cancelled or unreadable.
        bool hashFile(const fs::path& path, EdenArchive::Hasher& hasher, const std::atomic<bool>* cancel)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::vector<char> buffer(1 << 20);
            while (in)
            {
                if (cancel != nullptr && cancel->load())
                    return false;
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                if (in.gcount() > 0)
                    hasher.update(buffer.data(), static_cast<std::size_t>(in.gcount()));
            }
            return in.eof();
        }

        void putU64(EdenArchive::Hasher& hasher, std::uint64_t v)
        {
            unsigned char bytes[8];
            for (int i = 0; i < 8; ++i)
                bytes[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
            hasher.update(bytes, 8);
        }

        StageResult linkFiles(const fs::path& dataRoot, const fs::path& folder, bool wantStay)
        {
            StageResult result;
            std::error_code ec;
            if (fs::exists(folder, ec) && !fs::is_empty(folder, ec))
            {
                result.mError = "The folder already exists and is not empty: " + u8(folder);
                return result;
            }
            const Listing listing = listFiles(dataRoot, ScanLimits());
            if (listing.mTruncated)
            {
                result.mError = "The mod has too many files.";
                return result;
            }
            fs::create_directories(folder, ec);
            if (ec)
            {
                result.mError = "Cannot create " + u8(folder) + ": " + ec.message();
                return result;
            }
            for (const SourceFile& file : wantStay ? listing.mStay : listing.mPack)
            {
                const fs::path target = folder / fs::u8path(file.mRelative);
                fs::create_directories(target.parent_path(), ec);
                fs::create_hard_link(file.mPath, target, ec);
                if (ec)
                {
                    ec.clear();
                    fs::copy_file(file.mPath, target, fs::copy_options::none, ec);
                }
                if (ec)
                {
                    result.mError = "Cannot link or copy " + file.mRelative + ": " + ec.message();
                    return result;
                }
                ++result.mFiles;
            }
            result.mOk = true;
            return result;
        }

        std::uint64_t sizeOf(const fs::path& path)
        {
            std::error_code ec;
            if (fs::is_regular_file(path, ec))
                return fs::file_size(path, ec);
            std::uint64_t total = 0;
            for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
            {
                std::error_code e2;
                if (it->is_regular_file(e2))
                    total += it->file_size(e2);
            }
            return total;
        }
    }

    bool staysOnDisk(std::string_view relativePath)
    {
        if (relativePath.find('/') != std::string_view::npos)
            return false;
        const std::string lower = lowerAscii(std::string(relativePath));
        const std::size_t dot = lower.rfind('.');
        if (dot == std::string::npos)
            return false;
        const std::string ext = lower.substr(dot + 1);
        return ext == "esm" || ext == "esp" || ext == "omwgame" || ext == "omwaddon" || ext == "omwscripts"
            || ext == "project" || ext == "bsa" || ext == "ba2";
    }

    DryRun planRepack(const fs::path& dataRoot, const fs::path& targetFolder, const ScanLimits& limits)
    {
        DryRun run;
        const Listing listing = listFiles(dataRoot, limits);
        run.mProblems = listing.mProblems;
        run.mTruncated = listing.mTruncated;
        std::map<std::string, std::vector<std::string>> byName;
        for (const SourceFile& file : listing.mPack)
        {
            ++run.mPackFileCount;
            run.mPackSourceBytes += file.mSize;
            run.mEstimatedPackBytes += (file.mSize + sPackBlockBytes - 1) / sPackBlockBytes * sPackBlockBytes
                + sPerFileOverheadBytes;
            byName[lowerAscii(file.mRelative)].push_back(file.mRelative);
        }
        run.mEstimatedPackBytes += sIndexOverheadBytes;
        run.mProbablyLarger = run.mEstimatedPackBytes > run.mPackSourceBytes;
        for (const SourceFile& file : listing.mStay)
            run.mStayOnDisk.push_back(file.mRelative);
        for (auto& [normalized, originals] : byName)
        {
            if (originals.size() > 1)
            {
                std::sort(originals.begin(), originals.end());
                run.mCollisions.push_back({ normalized, originals });
            }
        }
        std::error_code ec;
        fs::path probe = targetFolder;
        while (!probe.empty() && !fs::exists(probe, ec) && probe.has_parent_path() && probe.parent_path() != probe)
            probe = probe.parent_path();
        const fs::space_info space = fs::space(probe, ec);
        if (!ec)
        {
            run.mFreeBytes = space.available;
            run.mFits = run.mEstimatedPackBytes <= run.mFreeBytes;
        }
        return run;
    }

    Fingerprint computeFingerprint(const fs::path& dataRoot, const std::atomic<bool>* cancel)
    {
        Fingerprint result;
        const Listing listing = listFiles(dataRoot, ScanLimits());
        if (listing.mTruncated)
        {
            result.mError = "The mod has too many files.";
            return result;
        }
        std::map<std::string, const SourceFile*> unique;
        for (const SourceFile& file : listing.mPack)
        {
            if (!unique.emplace(lowerAscii(file.mRelative), &file).second)
            {
                result.mError = "Two files have the same name after lower-casing: " + file.mRelative;
                return result;
            }
        }
        EdenArchive::Hasher total;
        for (const auto& [name, file] : unique)
        {
            EdenArchive::Hasher one;
            if (!hashFile(file->mPath, one, cancel))
            {
                result.mError = (cancel != nullptr && cancel->load()) ? "Cancelled." : "Cannot read " + file->mRelative;
                return result;
            }
            total.update(name.data(), name.size());
            total.update("\0", 1);
            putU64(total, file->mSize);
            const EdenArchive::Hash hash = one.finalize();
            total.update(hash.data(), hash.size());
            ++result.mFiles;
        }
        result.mHex = EdenArchive::toHex(total.finalize());
        result.mOk = true;
        return result;
    }

    std::string repackKey(const std::string& fingerprintHex, const PackerSettings& settings)
    {
        EdenArchive::Hasher hasher;
        const auto field = [&](const std::string& text) {
            hasher.update(text.data(), text.size());
            hasher.update("\0", 1);
        };
        field(fingerprintHex);
        field(settings.mVersion);
        field(settings.mMode);
        field(std::to_string(settings.mChunkSize));
        field(std::to_string(settings.mLevel));
        return EdenArchive::toHex(hasher.finalize()).substr(0, 32);
    }

    std::string packFileName(const std::string& modId, const std::string& key)
    {
        return modId + "-" + key + ".edenpak";
    }

    std::vector<std::string> edenpackArguments(
        const PackerSettings& settings, const fs::path& input, const fs::path& output)
    {
        std::vector<std::string> args;
        args.push_back(settings.mMode == "optimise" ? "--optimise" : "--store");
        if (settings.mMode == "optimise" && settings.mLevel > 0)
        {
            args.push_back("--level");
            args.push_back(std::to_string(settings.mLevel));
        }
        if (settings.mChunkSize != 0)
        {
            args.push_back("--chunk-size");
            args.push_back(std::to_string(settings.mChunkSize));
        }
        args.push_back("--normalize-names");
        args.push_back(u8(input));
        args.push_back(u8(output));
        return args;
    }

    StageResult buildPackStage(const fs::path& dataRoot, const fs::path& stageFolder)
    {
        return linkFiles(dataRoot, stageFolder, false);
    }

    StageResult buildShim(const fs::path& dataRoot, const fs::path& shimFolder)
    {
        return linkFiles(dataRoot, shimFolder, true);
    }

    VerifyResult verifyPack(const fs::path& pack, const fs::path& dataRoot, const std::atomic<bool>* cancel)
    {
        VerifyResult result;
        EdenArchive::Error error;
        const auto archive = EdenArchive::Archive::open(pack, EdenArchive::OpenOptions(), error);
        if (!archive)
        {
            result.mError = "Cannot open the pack: " + error.describe();
            return result;
        }
        const Listing listing = listFiles(dataRoot, ScanLimits());
        std::map<std::string, const SourceFile*> sources;
        for (const SourceFile& file : listing.mPack)
            sources.emplace(lowerAscii(file.mRelative), &file);

        std::set<std::string> seen;
        for (std::size_t i = 0; i < archive->entryCount(); ++i)
        {
            if (cancel != nullptr && cancel->load())
            {
                result.mError = "Cancelled.";
                return result;
            }
            const EdenArchive::EntryInfo entry = archive->entry(i);
            const std::string name(entry.mPath);
            if (!entry.mHasPath)
            {
                result.mError = "The pack has an entry without a path.";
                return result;
            }
            const auto source = sources.find(name);
            if (source == sources.end())
            {
                result.mError = "The pack has an entry with no source file: " + name;
                return result;
            }
            seen.insert(name);
            if (entry.mSize != source->second->mSize)
            {
                result.mError = "Size differs: " + source->second->mRelative;
                return result;
            }
            EdenArchive::Hasher hasher;
            if (!hashFile(source->second->mPath, hasher, cancel))
            {
                result.mError = "Cannot read the source file: " + source->second->mRelative;
                return result;
            }
            if (hasher.finalize() != entry.mContentHash)
            {
                result.mError = "Content differs: " + source->second->mRelative;
                return result;
            }
            const EdenArchive::Error verified = archive->verifyEntry(i);
            if (!verified.ok())
            {
                result.mError = "The packed data of " + source->second->mRelative + " is damaged: " + verified.describe();
                return result;
            }
            ++result.mEntries;
        }
        for (const auto& [name, file] : sources)
        {
            if (!seen.count(name))
            {
                result.mError = "The pack has no entry for the source file: " + file->mRelative;
                return result;
            }
        }
        result.mOk = true;
        return result;
    }

    bool isRepackCacheFolder(const fs::path& folder)
    {
        fs::path p = folder.lexically_normal();
        if (p.filename().empty())
            p = p.parent_path();
        if (lowerAscii(u8(p.filename())) != "repack")
            return false;
        p = p.parent_path();
        return lowerAscii(u8(p.filename())) == "openeden";
    }

    CacheUse scanCache(const fs::path& cacheFolder, const std::vector<std::string>& currentPacks)
    {
        CacheUse use;
        std::error_code ec;
        for (fs::directory_iterator it(cacheFolder, ec), end; !ec && it != end; it.increment(ec))
        {
            CacheItem item;
            item.mPath = it->path();
            item.mBytes = sizeOf(it->path());
            const std::string name = u8(it->path().filename());
            std::error_code e2;
            if (it->is_regular_file(e2))
                item.mStale = lowerAscii(name).ends_with(".edenpak")
                    && std::find(currentPacks.begin(), currentPacks.end(), name) == currentPacks.end();
            else
                item.mStale = name.rfind("stage-", 0) == 0;
            use.mTotalBytes += item.mBytes;
            if (item.mStale)
                use.mStaleBytes += item.mBytes;
            use.mItems.push_back(std::move(item));
        }
        std::sort(use.mItems.begin(), use.mItems.end(),
            [](const CacheItem& a, const CacheItem& b) { return a.mPath < b.mPath; });
        return use;
    }

    std::string clearCache(const fs::path& cacheFolder, const std::vector<CacheItem>& items)
    {
        if (!isRepackCacheFolder(cacheFolder))
            return "Refused: the folder is not named .../openeden/repack.";
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(cacheFolder, ec);
        // Check every item first: nothing is deleted when one of them lies outside.
        for (const CacheItem& item : items)
        {
            const fs::path parent = fs::weakly_canonical(item.mPath, ec).parent_path();
            if (parent != root)
                return "Refused: " + u8(item.mPath) + " is not directly inside the cache folder.";
        }
        for (const CacheItem& item : items)
        {
            fs::remove_all(item.mPath, ec);
            if (ec)
                return "Could not delete " + u8(item.mPath) + ": " + ec.message();
        }
        return {};
    }
}
