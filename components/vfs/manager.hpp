#ifndef OPENMW_COMPONENTS_RESOURCEMANAGER_H
#define OPENMW_COMPONENTS_RESOURCEMANAGER_H

#include <components/files/istreamptr.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "file.hpp"
#include "filemap.hpp"
#include "pathutil.hpp"

namespace VFS
{
    class Archive;
    class RecursiveDirectoryRange;

    /// One source (an archive) that provides a path. A Provider (and so an Override) points into the manager's
    /// archives: it is valid until the next reset(), addArchive() or buildIndex(). mArchive is its index in registration order: 0 was
    /// registered first and has the lowest priority.
    struct Provider
    {
        std::size_t mArchive;
        File* mFile;
    };

    /// A path that more than one archive provides. The last registered provider wins.
    struct Override
    {
        Path::Normalized mPath;
        Provider mWinner;
        std::vector<Provider> mLosers; // highest priority loser first
        // True when every provider, the winner included, has a content hash and all hashes are equal.
        bool mIdentical;
    };

    struct SourceOverrideCount
    {
        std::size_t mArchive;
        std::string mDescription;
        std::size_t mOverridden; // paths of a real conflict on which this archive loses
        std::size_t mOverriding; // paths of a real conflict on which this archive wins
        std::size_t mIdenticalDuplicates; // paths of identical-content sets that this archive provides
    };

    /// @brief The main class responsible for loading files from a virtual file system.
    /// @par Various archive types (e.g. directories on the filesystem, or compressed archives)
    /// can be registered, and will be merged into a single file tree. If the same filename is
    /// contained in multiple archives, the last added archive will have priority.
    /// @par Thread-safety contract (task 0050, archive doc 12):
    /// The manager has two phases. While it is being set up (the constructor, reset(), addArchive() and
    /// buildIndex()) it must be used by one thread only, and no other thread may call anything on it, on a File it
    /// returned or on a stream it opened, because buildIndex() replaces the index those point into. After
    /// buildIndex() has returned the manager is immutable: every const method (exists, find, get, getArchive,
    /// getLastModified, getStem, getSize, readRange, getRecursiveDirectoryIterator, getOverrides, getConflicts,
    /// getOverrideCounts, getArchiveCount, getArchiveDescription), File::open(), File::size() and File::readRange()
    /// may be called from any number of threads at once. Each stream returned by find() or get() is a new object
    /// with its own file handle; one stream must not be used by two threads. Adding an archive later means calling
    /// buildIndex() again, which is a setup-phase call: the caller must make sure nothing else is running.
    class Manager
    {
    public:
        Manager();

        ~Manager();

        // Empty the file index and unregister archives.
        void reset();

        /// Register the given archive. All files contained in it will be added to the index on the next buildIndex()
        /// call.
        void addArchive(std::unique_ptr<Archive>&& archive);

        /// Build the file index. Should be called when all archives have been registered.
        void buildIndex();

        /// Does a file with this name exist?
        /// @note May be called from any thread once the index has been built.
        bool exists(const Path::Normalized& name) const;

        bool exists(Path::NormalizedView name) const;

        // Returns open file if exists or nullptr.
        Files::IStreamPtr find(Path::NormalizedView name) const;

        /// Retrieve a file by name.
        /// @note Throws an exception if the file can not be found.
        /// @note May be called from any thread once the index has been built.
        Files::IStreamPtr get(const Path::Normalized& name) const;

        Files::IStreamPtr get(Path::NormalizedView name) const;

        std::string getArchive(const Path::Normalized& name) const;

        /// Recursively iterate over the elements of the given path
        /// In practice it return all files of the VFS starting with the given path
        /// @note the path is normalized
        /// @note May be called from any thread once the index has been built.
        RecursiveDirectoryRange getRecursiveDirectoryIterator(std::string_view path) const;

        RecursiveDirectoryRange getRecursiveDirectoryIterator(VFS::Path::NormalizedView path) const;

        RecursiveDirectoryRange getRecursiveDirectoryIterator() const;

        /// Size in bytes of the winning file of the given name.
        /// @note Throws an exception if the file can not be found. Thread-safe after buildIndex().
        std::uint64_t getSize(VFS::Path::NormalizedView name) const;

        /// Copy `size` bytes at `offset` of the winning file of the given name into `destination`; see
        /// File::readRange for what is valid. Does not open a stream.
        /// @note Throws an exception if the file can not be found or the range is not valid. Thread-safe after
        /// buildIndex().
        void readRange(VFS::Path::NormalizedView name, std::uint64_t offset, std::size_t size, void* destination) const;

        /// Every path that more than one registered archive provides, ordered by path, with the winner and the
        /// overridden providers. The same archives in the same order always give the same list.
        std::vector<Override> getOverrides() const;

        /// getOverrides() without the sets whose providers all have the same known content hash.
        std::vector<Override> getConflicts() const;

        /// One element per registered archive, in registration order.
        std::vector<SourceOverrideCount> getOverrideCounts() const;

        std::size_t getArchiveCount() const { return mArchives.size(); }

        /// @note Throws std::out_of_range for an index that is not a registered archive.
        std::string getArchiveDescription(std::size_t index) const;

        std::filesystem::file_time_type getLastModified(VFS::Path::NormalizedView name) const;
        // Equivalent to std::filesystem::path::stem. The result isn't normalized.
        std::string getStem(VFS::Path::NormalizedView name) const;

    private:
        std::vector<std::unique_ptr<Archive>> mArchives;

        FileMap mIndex;

        // Only the paths provided by more than one archive; providers in ascending priority, the last is the winner
        // and equals the file in mIndex. Built by buildIndex(), so it costs memory in proportion to the conflicts.
        std::map<Path::Normalized, std::vector<Provider>, std::less<>> mProviders;

        inline Files::IStreamPtr findNormalized(std::string_view normalizedPath) const;

        /// Retrieve a file by name (name is already normalized).
        /// @note Throws an exception if the file can not be found.
        /// @note May be called from any thread once the index has been built.
        Files::IStreamPtr getNormalized(std::string_view normalizedName) const;
    };

}

#endif
