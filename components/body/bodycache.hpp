#ifndef OPENEDEN_COMPONENTS_BODY_BODYCACHE_H
#define OPENEDEN_COMPONENTS_BODY_BODYCACHE_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage c): the cache of merged bodies as derived data.
//
// A bake writes ONE derived pack per run into <user data>/openeden/bodies/ and then points `current` at it:
//   merged-<digest>.edenpak   an Eden Archive (store-only, generic path-keyed entries, see bodypack.hpp):
//                             `manifest.txt` (CacheManifest below) and one `bodies/<name>.bodymesh` per body
//   current                   one line: the pack's file name. Written to a temporary name and renamed over `current`
//   status.json               a small report for people and for the future unified baker (task 0061)
// Publishing is atomic in the sense that matters: the pack is complete before `current` names it, and a bake killed at
// any point leaves the previous `current` and the previous pack untouched. A temporary file left behind is ignored.
//
// THE KEY of a body is the digest of everything its bytes depend on: the merger version, the reader version (the NIF
// reader, skin filter, placement and mirror rules and the adapter that turns shapes into parts: see bodyReaderVersion
// and its pinned test), the payload layout version, EVERYTHING the merger is given (the seams, the options and the
// limits: the key is computed through structured bindings of those structs, so a field added to one stops this
// library from compiling until the key learns about it; the merger's compile-time constants are included by name), the
// body's identity (race, sex, view), for every part its slot, record id, part type, flags, race, model path and the
// digest of the model FILE BYTES (a loose file has no archive digest), and the skeleton path and the digest of its
// bytes. A key therefore changes when one of those changes and only then, and it is per body: changing the model of a
// part of one body changes the keys of the bodies that use that part and no other. A stored entry whose key is not the
// one computed now is stale and ignored (the engine draws stock parts).
//
// Writing: a pack is written under a temporary name and renamed into place, unless a pack of that name (its name is
// the digest of its content) already reads back whole, in which case it is left alone, so a game that has it open is
// never written under. Every small file (the pointer, the status file) goes through writeFileAtomic. After a
// successful publish the baker keeps the new pack and the one the pointer named before (pruneOldPacks).
//
// Hostile input: the pack is read by the Eden Archive reader with its caps; the manifest reader is strict and caps
// its size and entries; payloads go through readBodyMesh. Determinism: no clock, no randomness, no threads; the same
// inputs give the same bytes on one machine. Threading: pure functions (publish and load touch the file system).

#include "bodymerge.hpp"
#include "bodypack.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Body
{
    /// Bumped when the merger's output for the same input can change (a rule, a tie-break, a constant).
    inline constexpr std::uint32_t bodyMergerVersion = 1;

    inline constexpr std::uint32_t cacheManifestVersion = 0;

    /// Bumped when the part reading changes its output for the same file: the NIF reader (components/bodynif), the
    /// skin filter, the placement and mirror rules, and the adapter that turns shapes into merger parts. A test pins
    /// the digest of a synthetic reading, so that a change cannot go unnoticed (testbodymesh.cpp, BodyCacheKey).
    inline constexpr std::uint32_t bodyReaderVersion = 1;

    namespace CacheLimits
    {
        inline constexpr std::size_t sMaxEntries = 4096;
        inline constexpr std::size_t sMaxManifestBytes = 4u << 20;
        inline constexpr std::size_t sMaxLineBytes = 1024;
        inline constexpr std::size_t sMaxPartsPerBody = 64;
    }

    /// One part of a body, as the key sees it.
    struct BodyPartKey
    {
        int mSlot = 0;
        std::string mRecordId;
        int mPartType = 0;
        int mFlags = 0;
        std::string mRace;
        std::string mModelPath;
        std::string mModelDigest; // lowercase hex of the file's bytes
        friend bool operator==(const BodyPartKey&, const BodyPartKey&) = default;
    };

    struct BodyKeyInput
    {
        std::string mRace;
        std::string mSex; // "male" or "female"
        std::string mView; // "third"
        std::vector<BodyPartKey> mParts; // any order: the key sorts a copy by slot
        std::string mSkeletonPath;
        std::string mSkeletonDigest; // lowercase hex of the file's bytes
        /// Everything the merger is given: the seams, the options and the limits (a body that hits a limit fails
        /// instead of merging, so the limits are part of what a stored entry means).
        std::vector<SeamDeclaration> mSeams;
        MergeOptions mMerge;
        MergeLimits mLimits;
        std::uint32_t mReaderVersion = bodyReaderVersion;
        std::uint32_t mMergerVersion = bodyMergerVersion;
        std::uint32_t mLayoutVersion = 0; // bodyMeshLayoutVersion
    };

    /// The body's key: 64 lowercase hex characters (BLAKE3 over a canonical text of the input).
    std::string computeBodyKey(const BodyKeyInput& input);

    /// The pack entry name of a body: `bodies/<race>-<sex>-<view>-<32 hex>.bodymesh`, lower case, a-z 0-9 and '-', a
    /// safe package path. The last part is a 128-bit digest of the exact (race, sex, view) text, so two bodies whose
    /// names differ only in punctuation, case or bytes beyond the shortened prefix do not collide; the baker also
    /// refuses a collision loudly.
    std::string bodyEntryName(std::string_view race, std::string_view sex, std::string_view view);

    struct CacheEntry
    {
        std::string mName; // the pack entry name
        std::string mRace;
        std::string mSex;
        std::string mView;
        std::string mKey;
        friend bool operator==(const CacheEntry&, const CacheEntry&) = default;
    };

    /// A body the bake could not merge: kept in the manifest so that a status line can say so.
    struct CacheFailure
    {
        std::string mRace;
        std::string mSex;
        std::string mView;
        std::string mReason; // one line
        friend bool operator==(const CacheFailure&, const CacheFailure&) = default;
    };

    struct CacheManifest
    {
        std::uint32_t mManifestVersion = cacheManifestVersion;
        std::uint32_t mMergerVersion = bodyMergerVersion;
        std::uint32_t mLayoutVersion = 0;
        std::vector<CacheEntry> mEntries; // sorted by name
        std::vector<CacheFailure> mFailures; // sorted by (race, sex, view)
        friend bool operator==(const CacheManifest&, const CacheManifest&) = default;
    };

    /// The manifest as text: tab-separated lines, "\n", one trailing newline (see the .cpp for the grammar).
    std::string writeManifest(const CacheManifest& manifest);

    /// Strict reader: any unknown line, duplicate name, over-long line, too many entries or control character is an
    /// error ("line 7: ...").
    std::optional<CacheManifest> readManifest(std::string_view text, std::string& error);

    /// True when the manifest has an entry of this name whose key equals `key`.
    bool isEntryCurrent(const CacheManifest& manifest, std::string_view name, std::string_view key);

    /// Writes `files` (the manifest and the payloads) as merged-<digest>.edenpak in `folder` (created if needed),
    /// then publishes it by an atomic rename of `current`. `packName` receives the file name. An earlier pack stays.
    bool publishCache(
        const std::filesystem::path& folder, const PackageFiles& files, std::string& packName, std::string& error);

    /// Reads the pack `current` names. Refuses a missing or unsafe pointer, a missing pack, a damaged pack.
    std::optional<PackageFiles> loadCurrentCache(const std::filesystem::path& folder, std::string& error);

    /// Writes `text` to `path` whole: to a uniquely named temporary file in the same folder, checked, then renamed over
    /// `path`. A reader sees the old file or the new one; a failure leaves the old one.
    bool writeFileAtomic(const std::filesystem::path& path, std::string_view text, std::string& error);

    /// Deletes every merged-<digest>.edenpak in `folder` whose name is not in `keep`. Returns how many were deleted.
    std::size_t pruneOldPacks(const std::filesystem::path& folder, const std::vector<std::string>& keep);

    /// The status file as canonical JSON text (state, counts, per-body failures).
    std::string writeStatusJson(const CacheManifest& manifest, std::string_view state);
}

#endif
