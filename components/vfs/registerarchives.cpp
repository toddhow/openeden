#include "registerarchives.hpp"

#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>

#include <components/vfs/archivefactory.hpp>
#include <components/vfs/edenarchive.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

namespace VFS
{
    namespace
    {
        // The file kinds the engine reads from data folders on disk, not through the VFS (task 0064).
        bool isContentFileExtension(std::string_view extension)
        {
            for (const std::string_view known : { "esm", "esp", "omwgame", "omwaddon", "omwscripts", "project" })
            {
                if (extension == known)
                {
                    return true;
                }
            }
            return false;
        }

        // Counts at Info level, the full list only when the debug level is asked for (archive doc 12): a count costs
        // a walk over the overridden paths only, and the list costs a log line for each of them.
        void logOverrides(const VFS::Manager& vfs)
        {
            const std::vector<SourceOverrideCount> counts = vfs.getOverrideCounts();
            std::size_t overridden = 0;
            std::size_t identical = 0;
            for (const SourceOverrideCount& count : counts)
            {
                overridden += count.mOverridden;
                identical += count.mIdenticalDuplicates;
            }
            if (overridden == 0 && identical == 0)
                return;

            Log(Debug::Info) << "Data sources: " << counts.size()
                             << ", paths provided by more than one source: " << (overridden + identical)
                             << " (identical content: " << identical << ")";
            for (const SourceOverrideCount& count : counts)
            {
                if (count.mOverridden != 0)
                    Log(Debug::Info) << "  " << count.mDescription << ": " << count.mOverridden
                                     << " paths overridden by later sources";
            }

            if (Log::sMinDebugLevel < Debug::Debug)
                return;
            for (const Override& item : vfs.getOverrides())
            {
                Log log(Debug::Debug);
                log << "  " << item.mPath.value() << (item.mIdentical ? " (same content) " : " ") << "<- "
                    << vfs.getArchiveDescription(item.mWinner.mArchive);
                for (const Provider& loser : item.mLosers)
                    log << " > " << vfs.getArchiveDescription(loser.mArchive);
            }
        }
    }

    std::vector<std::string> findContentFilesInArchive(Archive& archive)
    {
        FileMap files;
        archive.listResources(files);
        std::vector<std::string> result;
        for (const auto& [name, file] : files)
        {
            const std::string& text = name.value();
            if (text.find('/') != std::string::npos)
            {
                continue;
            }
            const std::size_t dot = text.rfind('.');
            if (dot != std::string::npos && isContentFileExtension(std::string_view(text).substr(dot + 1)))
            {
                result.push_back(text);
            }
        }
        return result;
    }

    void registerArchives(VFS::Manager* vfs, const Files::Collections& collections,
        const std::vector<std::string>& archives, bool useLooseFiles, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        const Files::PathContainer& dataDirs = collections.getPaths();

        for (std::vector<std::string>::const_iterator archive = archives.begin(); archive != archives.end(); ++archive)
        {
            if (collections.doesExist(*archive))
            {
                // Last BSA has the highest priority
                const auto archivePath = collections.getPath(*archive);
                Log(Debug::Info) << "Adding BSA archive " << archivePath;
                vfs->addArchive(makeArchive(archivePath, encoder));
            }
            else
            {
                throw std::runtime_error("Archive '" + *archive + "' not found");
            }
        }

        if (useLooseFiles)
        {
            std::set<std::filesystem::path> seen;
            for (const auto& dataDir : dataDirs)
            {
                if (seen.insert(dataDir).second)
                {
                    if (std::filesystem::is_regular_file(dataDir))
                    {
                        // A data pack stands at the position of its line, like a folder (task 0064).
                        Log(Debug::Info) << "Adding data archive " << dataDir;
                        std::unique_ptr<Archive> pack = makeEdenArchive(dataDir, encoder);
                        if (pack == nullptr)
                        {
                            throw std::runtime_error("The data entry '" + Files::pathToUnicodeString(dataDir)
                                + "' is a file but not an Eden Archive");
                        }
                        for (const std::string& name : findContentFilesInArchive(*pack))
                        {
                            Log(Debug::Warning)
                                << "Eden Archive '" << dataDir << "' contains the content file '" << name
                                << "'; content files are read from data folders only, so keep it in a "
                                   "folder (task 0064)";
                        }
                        vfs->addArchive(std::move(pack));
                        continue;
                    }
                    Log(Debug::Info) << "Adding data directory " << dataDir;
                    // Last data dir has the highest priority
                    vfs->addArchive(std::make_unique<FileSystemArchive>(dataDir));
                }
                else
                    Log(Debug::Info) << "Ignoring duplicate data directory " << dataDir;
            }
        }

        vfs->buildIndex();
        logOverrides(*vfs);
    }

}
