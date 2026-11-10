#ifndef COMPONENTS_FILES_COLLECTION_HPP
#define COMPONENTS_FILES_COLLECTION_HPP

#include <filesystem>

#include "multidircollection.hpp"

namespace Files
{
    /// True when `path` is a regular file whose name ends in ".edenpak" (any case): an Eden Archive that a `data=` line
    /// may name in place of a folder (task 0064). Says nothing about the file's content; the VFS checks the magic.
    bool isDataPackFile(const std::filesystem::path& path);

    /// True when a `data=` line can name `path`: an existing folder, or a pack file (isDataPackFile). The launcher
    /// keeps exactly these lines when it validates the data paths (task 0057: it dropped pack lines before).
    bool isUsableDataEntry(const std::filesystem::path& path);

    class Collections
    {
    public:
        Collections();

        ///< Directories are listed with increasing priority.
        Collections(const Files::PathContainer& directories);

        ///< Return a file collection for the given extension.
        const MultiDirCollection& getCollection(std::string_view extension) const;

        std::filesystem::path getPath(std::string_view file) const;
        ///< Return full path (including filename) of \a file.
        ///
        /// If the file does not exist in any of the collection's
        /// directories, an exception is thrown. \a file must include the
        /// extension.

        bool doesExist(std::string_view file) const;
        ///< \return Does a file with the given name exist?

        const Files::PathContainer& getPaths() const;

    private:
        Files::PathContainer mDirectories;

        mutable std::map<std::string, MultiDirCollection, Misc::StringUtils::CiComp> mCollections;
    };
}

#endif
