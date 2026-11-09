#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_REPACK_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_REPACK_H

// Repacking a mod's loose files into one Eden Archive pack (task 0057, stage d): the dry run, the content
// fingerprint and cache key, the staging and shim folders, the read-back verification and the cache panel's data.
// The packer itself (edenpack) is run by the caller as a child process; commandArguments says how.
//
// Originals are never modified: everything here writes only below the cache folder it is given. Threading: all of
// these walk or read files; call them from a worker thread.

#include "scanner.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ModOrganizer
{
    /// The padding a small file costs in a pack (spec 8.1; edenarchive/CLAUDE.md): up to 4095 bytes.
    inline constexpr std::uint64_t sPackBlockBytes = 4096;

    /// Top-level plugin and archive files stay on disk (the engine reads plugins and fallback archives from data
    /// folders, task 0064): they go to the shim, everything else to the pack.
    bool staysOnDisk(std::string_view relativePath);

    struct CollidingNames
    {
        std::string mNormalized;
        std::vector<std::string> mOriginals; // sorted
    };

    struct DryRun
    {
        std::uint64_t mPackFileCount = 0;
        std::uint64_t mPackSourceBytes = 0;
        /// An upper bound: every file rounded up to a whole block plus a fixed overhead for the index.
        std::uint64_t mEstimatedPackBytes = 0;
        bool mProbablyLarger = false; // the estimate is above the source size
        std::uint64_t mFreeBytes = 0; // on the target drive; 0 when unknown
        bool mFits = true;
        std::vector<std::string> mStayOnDisk; // sorted
        std::vector<CollidingNames> mCollisions;
        std::vector<ScanProblem> mProblems; // names that fail the extraction-safety check
        bool mTruncated = false;
        /// A mod with problems or collisions cannot be repacked.
        bool canRepack() const { return mProblems.empty() && mCollisions.empty() && !mTruncated; }
    };

    DryRun planRepack(const std::filesystem::path& dataRoot, const std::filesystem::path& targetFolder,
        const ScanLimits& limits = ScanLimits());

    struct Fingerprint
    {
        bool mOk = false;
        std::string mHex; // 64 hex characters: BLAKE3 over the sorted (normalized path, size, content hash) list
        std::uint64_t mFiles = 0;
        std::string mError;
    };

    /// Hashes every file below `dataRoot` that goes to the pack. `cancel` is checked between files.
    Fingerprint computeFingerprint(const std::filesystem::path& dataRoot, const std::atomic<bool>* cancel = nullptr);

    struct PackerSettings
    {
        std::string mVersion = "1";
        std::string mMode = "store";
        std::uint32_t mChunkSize = 0; // 0: the tool's default
        int mLevel = 0;
    };

    /// 32 hex characters naming the pack: BLAKE3(fingerprint, packer version, mode, chunk size, level).
    std::string repackKey(const std::string& fingerprintHex, const PackerSettings& settings);

    /// "<mod id>-<key>.edenpak"
    std::string packFileName(const std::string& modId, const std::string& key);

    /// edenpak's arguments: --store --normalize-names <input> <output>.
    std::vector<std::string> edenpackArguments(
        const PackerSettings& settings, const std::filesystem::path& input, const std::filesystem::path& output);

    struct StageResult
    {
        bool mOk = false;
        std::uint64_t mFiles = 0;
        std::string mError;
    };
    /// Creates `stageFolder` holding links (hard links, copies where the file system refuses) of the files that go
    /// to the pack, for edenpak to read. Refuses a folder that already exists and is not empty.
    StageResult buildPackStage(const std::filesystem::path& dataRoot, const std::filesystem::path& stageFolder);
    /// Same for the files that stay on disk (the shim data folder of a repacked mod).
    StageResult buildShim(const std::filesystem::path& dataRoot, const std::filesystem::path& shimFolder);

    struct VerifyResult
    {
        bool mOk = false;
        std::uint64_t mEntries = 0;
        std::string mError; // names the first path that differs
    };
    /// Opens the pack, and compares every packed entry's size and BLAKE3 hash with the source file, read back from
    /// the source. Also fails for a source file that has no entry and an entry that has no source file.
    VerifyResult verifyPack(const std::filesystem::path& pack, const std::filesystem::path& dataRoot,
        const std::atomic<bool>* cancel = nullptr);

    struct CacheItem
    {
        std::filesystem::path mPath;
        std::uint64_t mBytes = 0;
        bool mStale = false;
    };
    struct CacheUse
    {
        std::vector<CacheItem> mItems; // packs, shims and stages, sorted by path
        std::uint64_t mTotalBytes = 0;
        std::uint64_t mStaleBytes = 0;
    };
    /// True only for a folder whose last two components are "openeden" and "repack" (a guard against a mistyped
    /// setting pointing at the user's documents).
    bool isRepackCacheFolder(const std::filesystem::path& folder);
    /// `currentPacks` names the pack files (file names) that the library still uses; other packs are stale.
    CacheUse scanCache(const std::filesystem::path& cacheFolder, const std::vector<std::string>& currentPacks);
    /// Deletes the listed items, but only inside a valid cache folder; returns an error text or empty.
    std::string clearCache(const std::filesystem::path& cacheFolder, const std::vector<CacheItem>& items);
}

#endif
