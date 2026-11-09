#ifndef OPENEDEN_COMPONENTS_WORLDLOD_BAKESTATUS_H
#define OPENEDEN_COMPONENTS_WORLDLOD_BAKESTATUS_H

// The status file and the command file of the background LOD baker (task 0058, stage a). A PROPOSAL until the baker
// (stage S8 of the world LOD design) exists; reader and writer live here so that baker, game and launcher cannot
// drift. Both files are canonical JSON ("openeden.lod.status" v1, "openeden.lod.command" v1).
//
// The status file is untrusted: a size cap, field length caps, at most 20 error entries, nothing in it is opened or
// executed. Threading: pure functions except the file helpers, which do blocking I/O: not on the GUI thread.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WorldLod
{
    inline constexpr std::size_t sMaxStatusBytes = 256 * 1024;
    inline constexpr std::size_t sMaxErrors = 20;
    inline constexpr std::size_t sMaxTextBytes = 1024;

    enum class BakerState
    {
        Idle,
        Building,
        Paused,
        Complete,
        Failed,
    };

    struct BakeError
    {
        std::string mUnit;
        std::string mMessage;
        bool operator==(const BakeError&) const = default;
    };

    struct CellRef
    {
        std::string mWorldspace;
        std::int64_t mX = 0;
        std::int64_t mY = 0;
        bool operator==(const CellRef&) const = default;
    };

    struct BakeStatus
    {
        std::string mToolVersion;
        std::int64_t mPid = 0;
        std::string mProcessStartTime;
        BakerState mState = BakerState::Idle;
        std::string mPauseReason;
        std::int64_t mHeartbeat = 0; // seconds since the Unix epoch
        std::string mProfile;
        std::string mFingerprint; // 64 hex or empty
        std::string mGeneration; // 64 hex or empty
        std::int64_t mUnitsTotal = 0;
        std::int64_t mUnitsDone = 0;
        std::int64_t mUnitsFailed = 0;
        std::int64_t mLevel = 0;
        std::int64_t mLevelsTotal = 0;
        std::string mActivity;
        CellRef mLastCell;
        std::int64_t mEtaSeconds = -1;
        std::int64_t mCacheBytes = 0;
        std::vector<BakeError> mErrors;
        std::int64_t mErrorsTotal = 0;
        std::int64_t mLastCommandId = 0;
        std::string mLogPath;

        bool operator==(const BakeStatus&) const = default;
    };

    struct StatusError
    {
        std::string mFile;
        std::size_t mLine = 0;
        std::string mPointer;
        std::string mMessage;
        bool mNewerVersion = false; // show state Unknown, do not parse further
        std::string format() const;
    };

    std::optional<BakeStatus> readStatus(std::string_view text, std::string_view fileName, StatusError& error);
    std::string writeStatus(const BakeStatus& status);

    enum class CommandKind
    {
        Pause,
        Resume,
        Stop,
    };

    struct Command
    {
        std::int64_t mId = 0;
        CommandKind mKind = CommandKind::Pause;
        bool operator==(const Command&) const = default;
    };

    std::optional<Command> readCommand(std::string_view text, std::string_view fileName, StatusError& error);
    std::string writeCommand(const Command& command);

    /// What the page shows (task 0058, "The states the page shows").
    enum class PageState
    {
        Off,
        NotBuilt,
        Building,
        Paused,
        UpToDate,
        OutOfDate,
        Failed,
        Unknown,
    };

    struct Judgement
    {
        PageState mState = PageState::Unknown;
        std::string mReason; // why Unknown or Failed, plain text
    };

    struct JudgeInput
    {
        bool mFeatureEnabled = true;
        bool mStatusFileExists = false;
        std::optional<BakeStatus> mStatus; // empty when unreadable
        std::string mUnreadableReason;
        std::string mActiveFingerprint; // the active profile's, from the shared fingerprint function
        std::int64_t mNow = 0; // seconds since the Unix epoch
        std::int64_t mHeartbeatTimeout = 30;
    };

    /// A stale heartbeat on a "building" or "paused" file is Failed (the baker stopped without finishing), never
    /// Building. Complete with a fingerprint equal to the active one is UpToDate, otherwise OutOfDate.
    Judgement judge(const JudgeInput& input);

    /// The shown estimate: nothing until at least `minUnits` are done and the baker gives a positive value.
    std::optional<std::int64_t> shownEtaSeconds(const BakeStatus& status, std::int64_t minUnits = 50);

    /// Reads a file with the size cap. Returns nullopt with `error` filled when it cannot be read.
    std::optional<std::string> readSmallFile(const std::filesystem::path& path, std::size_t maxBytes, std::string& error);
    /// Writes through "<path>.tmp" and a rename, so that a reader sees the old or the new file, never half.
    bool writeFileAtomically(const std::filesystem::path& path, std::string_view text, std::string& error);

    /// Cache guards (task 0058, "Cache size and clearing").
    /// True only for a folder whose last two components are "openeden" and "lod".
    bool isLodCacheFolder(const std::filesystem::path& folder);
    struct CacheListing
    {
        std::vector<std::filesystem::path> mFiles; // files that may be deleted, sorted
        std::uint64_t mBytes = 0;
        std::vector<std::string> mSkipped; // links and names not of the cache's naming
    };
    /// Regular files directly below or in sub-folders of the cache; names that are not "status.json", "command.json",
    /// "manifest*", "*.tmp", "*.edenpak", "*.json" and "*.lock" are left out. Links are never followed.
    CacheListing listCacheFiles(const std::filesystem::path& folder);
    /// Refuses unless isLodCacheFolder, a baker is not alive and the game is not running.
    std::string deleteCache(const std::filesystem::path& folder, const CacheListing& listing, bool bakerAlive, bool gameRunning);
}

#endif
