#include "registerarchives.hpp"

#include <filesystem>
#include <set>
#include <stdexcept>
#include <vector>

#include <components/debug/debuglog.hpp>

#include <components/vfs/archivefactory.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

namespace VFS
{
    namespace
    {
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
