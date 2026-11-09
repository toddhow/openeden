#include "conflicts.hpp"

#include <components/vfs/archive.hpp>
#include <components/vfs/archivefactory.hpp>
#include <components/vfs/edenarchive.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>

namespace ModOrganizer
{
    namespace
    {
        namespace fs = std::filesystem;

        // Gives an archive a description of our own so that the manager's answers map back to mods.
        class LabeledArchive : public VFS::Archive
        {
        public:
            LabeledArchive(std::unique_ptr<VFS::Archive> inner, std::string label)
                : mInner(std::move(inner))
                , mLabel(std::move(label))
            {
            }

            void listResources(VFS::FileMap& out) override { mInner->listResources(out); }
            bool contains(VFS::Path::NormalizedView file) const override { return mInner->contains(file); }
            std::string getDescription() const override { return mLabel; }

        private:
            std::unique_ptr<VFS::Archive> mInner;
            std::string mLabel;
        };

        std::string lowerAscii(std::string text)
        {
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return text;
        }

        // Looks an archive up like Files::Collections::getPath: last data folder first, names case-insensitive.
        fs::path findArchive(const Layout& layout, const std::string& name)
        {
            const std::string wanted = lowerAscii(name);
            for (auto it = layout.mData.rbegin(); it != layout.mData.rend(); ++it)
            {
                std::error_code ec;
                const fs::path folder = fs::u8path(it->mPath);
                if (!fs::is_directory(folder, ec))
                    continue;
                for (fs::directory_iterator dir(folder, ec), end; !ec && dir != end; dir.increment(ec))
                {
                    const auto u8 = dir->path().filename().u8string();
                    if (lowerAscii(std::string(u8.begin(), u8.end())) == wanted)
                        return dir->path();
                }
            }
            return {};
        }

        std::string labelOf(const DataEntry& entry)
        {
            if (entry.mModId.empty())
                return "base";
            return (entry.mIsShim ? "shim:" : "mod:") + entry.mModId;
        }

        // Builds the manager. Sources that cannot be opened are skipped and named in `errors`.
        std::unique_ptr<VFS::Manager> buildManager(const Layout& layout, std::vector<std::string>& errors)
        {
            auto vfs = std::make_unique<VFS::Manager>();
            for (const std::string& name : layout.mArchives)
            {
                try
                {
                    const fs::path path = findArchive(layout, name);
                    if (path.empty())
                        throw std::runtime_error("not found in any data folder");
                    vfs->addArchive(std::make_unique<LabeledArchive>(VFS::makeArchive(path, nullptr), "archive:" + name));
                }
                catch (const std::exception& e)
                {
                    errors.push_back("Archive \"" + name + "\": " + e.what());
                }
            }
            for (const DataEntry& entry : layout.mData)
            {
                try
                {
                    const fs::path path = fs::u8path(entry.mPath);
                    std::error_code ec;
                    std::unique_ptr<VFS::Archive> archive;
                    if (entry.mIsPack || fs::is_regular_file(path, ec))
                    {
                        archive = VFS::makeEdenArchive(path, nullptr);
                        if (archive == nullptr)
                            throw std::runtime_error("is a file but not an Eden Archive");
                    }
                    else
                    {
                        if (!fs::is_directory(path, ec))
                            throw std::runtime_error("the folder does not exist");
                        archive = std::make_unique<VFS::FileSystemArchive>(path);
                    }
                    vfs->addArchive(std::make_unique<LabeledArchive>(std::move(archive), labelOf(entry)));
                }
                catch (const std::exception& e)
                {
                    errors.push_back("\"" + entry.mPath + "\" (" + (entry.mModId.empty() ? "base" : entry.mModId)
                        + "): " + e.what());
                }
            }
            vfs->buildIndex();
            return vfs;
        }

        std::string modLabel(const std::string& source)
        {
            // A shim and the folder or pack of one mod are the same mod.
            if (source.rfind("shim:", 0) == 0)
                return "mod:" + source.substr(5);
            return source;
        }

        std::map<std::string, std::string> winners(const Layout& layout)
        {
            std::vector<std::string> errors;
            const auto vfs = buildManager(layout, errors);
            std::map<std::string, std::string> result;
            for (const VFS::Path::Normalized& name : vfs->getRecursiveDirectoryIterator())
                result[name.value()] = modLabel(vfs->getArchive(name));
            return result;
        }
    }

    std::string sourceModId(const std::string& source)
    {
        for (const char* prefix : { "mod:", "shim:" })
        {
            const std::string p = prefix;
            if (source.rfind(p, 0) == 0)
                return source.substr(p.size());
        }
        return {};
    }

    ConflictReport computeConflicts(const Layout& layout, bool includeIdentical)
    {
        ConflictReport report;
        const auto vfs = buildManager(layout, report.mErrors);
        const std::vector<VFS::Override> overrides = includeIdentical ? vfs->getOverrides() : vfs->getConflicts();
        for (const VFS::Override& item : overrides)
        {
            ConflictFile file;
            file.mPath = item.mPath.value();
            file.mWinner = vfs->getArchiveDescription(item.mWinner.mArchive);
            for (const VFS::Provider& loser : item.mLosers)
                file.mLosers.push_back(vfs->getArchiveDescription(loser.mArchive));
            file.mIdentical = item.mIdentical;
            report.mFiles.push_back(std::move(file));
        }
        for (const VFS::SourceOverrideCount& count : vfs->getOverrideCounts())
            report.mCounts.push_back(
                { count.mDescription, count.mOverridden, count.mOverriding, count.mIdenticalDuplicates });
        return report;
    }

    std::vector<std::string> differentWinners(const Layout& a, const Layout& b)
    {
        const auto first = winners(a);
        const auto second = winners(b);
        std::vector<std::string> different;
        for (const auto& [path, winner] : first)
        {
            const auto it = second.find(path);
            if (it == second.end() || it->second != winner)
                different.push_back(path);
        }
        for (const auto& [path, winner] : second)
            if (first.find(path) == first.end())
                different.push_back(path);
        std::sort(different.begin(), different.end());
        return different;
    }
}
