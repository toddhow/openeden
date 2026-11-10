#include "scanner.hpp"
#include <components/files/conversion.hpp>

#include <components/vfs/entryname.hpp>

#include <algorithm>
#include <array>
#include <fstream>

namespace ModOrganizer
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string lowerAscii(std::string text)
        {
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return text;
        }

        std::string extensionOf(const std::string& lowerName)
        {
            const std::size_t dot = lowerName.rfind('.');
            return dot == std::string::npos ? std::string() : lowerName.substr(dot + 1);
        }

        bool isPluginExtension(const std::string& e)
        {
            return e == "esm" || e == "esp" || e == "omwgame" || e == "omwaddon" || e == "omwscripts";
        }

        bool isDataFolderName(const std::string& lower)
        {
            static const std::array<const char*, 18> names
                = { "meshes", "textures", "icons", "sound", "music", "video", "bookart", "splash", "fonts", "scripts",
                      "shaders", "lua", "l10n", "animations", "mygui", "distantland", "trees", "grass" };
            return std::any_of(names.begin(), names.end(), [&](const char* n) { return lower == n; });
        }

        std::string nameOf(const fs::path& p)
        {
            const auto name = p.filename().u8string();
            return std::string(name.begin(), name.end());
        }

        // True when the folder directly holds a plugin, an archive or one of the known data sub-folders.
        bool looksLikeDataRoot(const fs::path& folder)
        {
            std::error_code ec;
            for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code e2;
                if (it->is_symlink(e2))
                    continue;
                const std::string lower = lowerAscii(nameOf(it->path()));
                if (it->is_directory(e2))
                {
                    if (isDataFolderName(lower))
                        return true;
                }
                else if (it->is_regular_file(e2))
                {
                    const std::string ext = extensionOf(lower);
                    if (isPluginExtension(ext) || ext == "bsa" || ext == "ba2")
                        return true;
                }
            }
            return false;
        }

        std::vector<std::string> sortedSubfolders(const fs::path& folder)
        {
            std::vector<std::string> names;
            std::error_code ec;
            for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code e2;
                if (!it->is_symlink(e2) && it->is_directory(e2))
                    names.push_back(nameOf(it->path()));
            }
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
                return lowerAscii(a) < lowerAscii(b) || (lowerAscii(a) == lowerAscii(b) && a < b);
            });
            return names;
        }

        struct Walker
        {
            const ScanLimits& mLimits;
            ScanResult& mResult;

            void problem(const std::string& path, std::string message)
            {
                if (mResult.mProblems.size() < mLimits.mMaxProblems)
                    mResult.mProblems.push_back({ path, std::move(message) });
                else
                    ++mResult.mProblemsNotListed;
            }

            void walk(const fs::path& folder, const std::string& relative, std::size_t depth)
            {
                std::error_code ec;
                std::vector<fs::directory_entry> entries;
                for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                     !ec && it != end; it.increment(ec))
                    entries.push_back(*it);
                if (ec)
                    problem(relative, "cannot read the folder: " + ec.message());
                std::sort(
                    entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                        return nameOf(a.path()) < nameOf(b.path());
                    });
                for (const fs::directory_entry& entry : entries)
                {
                    if (mResult.mTruncated)
                        return;
                    const std::string name = nameOf(entry.path());
                    const std::string rel = relative.empty() ? name : relative + "/" + name;
                    std::error_code e2;
                    if (entry.is_symlink(e2))
                    {
                        problem(rel, "a link is not followed");
                        continue;
                    }
                    if (const auto reason = VFS::Path::checkExtractionSafe(name))
                        problem(rel, *reason);
                    if (entry.is_directory(e2))
                    {
                        if (depth + 1 > mLimits.mMaxDepth)
                        {
                            problem(rel, "nested deeper than " + std::to_string(mLimits.mMaxDepth) + " folders");
                            continue;
                        }
                        walk(entry.path(), rel, depth + 1);
                        continue;
                    }
                    if (!entry.is_regular_file(e2))
                        continue;
                    if (mResult.mFileCount >= mLimits.mMaxFiles)
                    {
                        mResult.mTruncated = true;
                        return;
                    }
                    ++mResult.mFileCount;
                    const std::uintmax_t size = entry.file_size(e2);
                    if (e2)
                        problem(rel, "cannot read the size: " + e2.message());
                    else
                        mResult.mTotalBytes += size;
                    if (depth == 0)
                    {
                        const std::string ext = extensionOf(lowerAscii(name));
                        if (isPluginExtension(ext))
                            mResult.mPlugins.push_back(name);
                        else if (ext == "bsa" || ext == "ba2")
                            mResult.mArchives.push_back(name);
                        else if (ext == "edenpak")
                            mResult.mPacks.push_back(name);
                    }
                }
            }
        };
    }

    ScanResult scanDataRoot(const fs::path& root, const ScanLimits& limits)
    {
        ScanResult result;
        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            result.mRootMissing = true;
            return result;
        }
        Walker walker{ limits, result };
        walker.walk(root, "", 0);
        const auto byLower = [](const std::string& a, const std::string& b) {
            return lowerAscii(a) < lowerAscii(b) || (lowerAscii(a) == lowerAscii(b) && a < b);
        };
        std::sort(result.mPlugins.begin(), result.mPlugins.end(), byLower);
        std::sort(result.mArchives.begin(), result.mArchives.end(), byLower);
        std::sort(result.mPacks.begin(), result.mPacks.end(), byLower);
        return result;
    }

    std::string detectDataPath(const fs::path& folder)
    {
        std::error_code ec;
        if (!fs::is_directory(folder, ec) || looksLikeDataRoot(folder))
            return {};
        for (const std::string& first : sortedSubfolders(folder))
        {
            if (looksLikeDataRoot(folder / Files::pathFromUnicodeString(first)))
                return first;
        }
        for (const std::string& first : sortedSubfolders(folder))
        {
            for (const std::string& second : sortedSubfolders(folder / Files::pathFromUnicodeString(first)))
            {
                if (looksLikeDataRoot(
                        folder / Files::pathFromUnicodeString(first) / Files::pathFromUnicodeString(second)))
                    return first + "/" + second;
            }
        }
        return {};
    }

    bool looksLikeEdenPack(const fs::path& file)
    {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
            return false;
        std::ifstream in(file, std::ios::binary);
        std::array<char, 7> magic{};
        in.read(magic.data(), magic.size());
        return in.gcount() == 7 && std::string_view(magic.data(), 7) == "EDENPAK";
    }
}
