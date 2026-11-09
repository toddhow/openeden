#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_SCANNER_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_SCANNER_H

// Scans a mod folder (untrusted input) for the plugin files, archives and packs the engine will find in it, and for
// the folder that is its data root (task 0057, stage a). Reads directory entries only: no file content except the
// seven magic bytes of a pack. Links are not followed. Output is sorted, so it does not depend on the order the file
// system lists entries in.
//
// Threading: pure functions of the file system; call from a worker thread, never block the GUI thread with them.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ModOrganizer
{
    struct ScanLimits
    {
        std::size_t mMaxFiles = 2000000;
        std::size_t mMaxDepth = 64;
        std::size_t mMaxProblems = 100;
    };

    struct ScanProblem
    {
        std::string mPath; // relative to the scanned root, '/' separators
        std::string mMessage;
    };

    struct ScanResult
    {
        bool mRootMissing = false;
        bool mTruncated = false; // a limit was reached; the counts are lower bounds
        std::vector<std::string> mPlugins; // top level: .esm .esp .omwgame .omwaddon .omwscripts
        std::vector<std::string> mArchives; // top level: .bsa .ba2
        std::vector<std::string> mPacks; // top level: .edenpak
        std::uint64_t mFileCount = 0;
        std::uint64_t mTotalBytes = 0;
        std::vector<ScanProblem> mProblems; // names that fail VFS::Path::checkExtractionSafe, links, depth
        std::size_t mProblemsNotListed = 0;
    };

    ScanResult scanDataRoot(const std::filesystem::path& root, const ScanLimits& limits = ScanLimits());

    /// The relative sub-path of `folder` that holds the game data ("" when `folder` itself does, "Data Files" for a
    /// mod that wraps its data in one more folder). Looks at most two levels down, prefers the shallowest, then the
    /// first by lower-case name. Returns "" when nothing looks like a data root.
    std::string detectDataPath(const std::filesystem::path& folder);

    /// True for a regular file whose first seven bytes are "EDENPAK".
    bool looksLikeEdenPack(const std::filesystem::path& file);
}

#endif
