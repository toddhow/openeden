#ifndef OPENMW_COMPONENTS_VFS_REGISTER_ARCHIVES_H
#define OPENMW_COMPONENTS_VFS_REGISTER_ARCHIVES_H

#include <string>
#include <vector>

#include <components/files/collections.hpp>
#include <components/vfs/archive.hpp>

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace VFS
{
    class Manager;

    /// @brief Names the content files (plugins) at the top level of an archive: the engine reads those from data
    /// folders on disk, so they cannot be used from a pack (task 0064). Normalized names, in index order.
    std::vector<std::string> findContentFilesInArchive(Archive& archive);

    /// @brief Register BSA and file system archives based on the given OpenMW configuration.
    void registerArchives(VFS::Manager* vfs, const Files::Collections& collections,
        const std::vector<std::string>& archives, bool useLooseFiles, const ToUTF8::StatelessUtf8Encoder* encoder);
}

#endif
