#ifndef OPENEDEN_GAME_SKYRIMSE_MUSIC_H
#define OPENEDEN_GAME_SKYRIMSE_MUSIC_H

// Skyrim SE music (task 0209, milestone M-S7): the MUSC music types and MUST music tracks, the links that say which
// type a place has (CELL XCMO, LCTN NAM1, REGN RDMO, WRLD ZNAM), the combat type (DOBJ BTMS), and a pure chooser that
// turns a type into a plan of files and silences. Pack-own types for the records; the chooser speaks the SDK's
// MusicSource values (sdk/include/openeden/game/music_source.hpp). No engine type, no clock, no random source of its
// own: a choice is a function of the context, the seed and the pick counter.
//
// Layout status of every field is marked in music.cpp: VERIFIED (sizes and ranges measured on the five installed
// masters, aggregates in docs/tasks/0209-skyrim-music.md) or RECALLED-VERIFY.

#include "game_skyrimse/conditions.hpp"
#include "game_skyrimse/music_data.hpp"
#include "game_skyrimse/records.hpp"

#include <openeden/game/music_source.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse::music
{
    /// MUST CNAM values (VERIFIED: 283 single, 21 palette, 5 silent in the five masters).
    inline constexpr std::uint32_t kKindSingle = 0x6ED7E048u;
    inline constexpr std::uint32_t kKindPalette = 0x23F678C3u;
    inline constexpr std::uint32_t kKindSilent = 0xA1A9C4D5u;

    /// MUSC FNAM flag bits (RECALLED-VERIFY, consistent with the data).
    namespace TypeFlag
    {
        inline constexpr std::uint32_t PlaysOneSelection = 0x01;
        inline constexpr std::uint32_t AbruptTransition = 0x02;
        inline constexpr std::uint32_t CycleTracks = 0x04;
        inline constexpr std::uint32_t MaintainTrackOrder = 0x08;
        inline constexpr std::uint32_t DucksCurrentTrack = 0x20;
        inline constexpr std::uint32_t DoesNotQueue = 0x40;
    }

    enum class TrackKind : std::uint8_t
    {
        Unknown,
        Single,
        Palette,
        Silent,
    };

    struct Track
    {
        FormId mId = kNullFormId;
        std::string mEditorId;
        TrackKind mKind = TrackKind::Unknown;
        std::string mFile; ///< ANAM as stored ("Data\\Music\\x.wav")
        std::string mFinale; ///< BNAM as stored
        float mDuration = 0.f; ///< FLTV (a silent track's length; a palette's)
        float mFadeOut = 0.f; ///< DNAM
        std::vector<float> mCuePoints; ///< FNAM, seconds
        float mLoopStart = 0.f, mLoopEnd = 0.f; ///< LNAM
        std::uint32_t mLoopCount = 0;
        std::vector<FormId> mPalette; ///< SNAM; kNullFormId separates the groups
        std::vector<survey::Ctda> mConditions;
        bool mConditionsOk = true; ///< false when the CTDAs did not decode (the track never plays)
    };

    struct Type
    {
        FormId mId = kNullFormId;
        std::string mEditorId;
        std::uint32_t mFlags = 0; ///< FNAM
        std::uint16_t mPriority = 0; ///< PNAM +0
        std::uint16_t mDucking = 0; ///< PNAM +2
        float mFade = 0.f; ///< WNAM
        std::vector<FormId> mTracks; ///< TNAM (a repeated id is a heavier weight)
    };

    struct CellLink
    {
        FormId mCell = kNullFormId;
        FormId mWorld = kNullFormId; ///< kNullFormId for an interior
        bool mInterior = false;
        FormId mMusic = kNullFormId; ///< XCMO
        FormId mLocation = kNullFormId; ///< XLCN
        std::vector<FormId> mRegions; ///< XCLR
        std::string mEditorId;
    };

    struct LocationLink
    {
        FormId mId = kNullFormId;
        FormId mParent = kNullFormId; ///< PNAM
        FormId mMusic = kNullFormId; ///< NAM1
    };

    struct RegionLink
    {
        FormId mId = kNullFormId;
        FormId mMusic = kNullFormId; ///< RDMO
    };

    struct WorldLink
    {
        FormId mId = kNullFormId;
        FormId mMusic = kNullFormId; ///< ZNAM
        std::string mEditorId;
    };

    struct MusicLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxEntriesPerList = 4096;
        std::uint32_t mMaxStringBytes = 64 * 1024;
        std::uint32_t mMaxErrors = 256;
    };

    struct Records
    {
        std::string mName;
        std::vector<Type> mTypes;
        std::vector<Track> mTracks;
        std::vector<CellLink> mCells;
        std::vector<LocationLink> mLocations;
        std::vector<RegionLink> mRegions;
        std::vector<WorldLink> mWorlds;
        FormId mCombatType = kNullFormId; ///< DOBJ entry BTMS
        std::map<std::uint32_t, TypeCount> mCounts; ///< by record type
        std::uint64_t mOddSubrecords = 0;
        std::uint64_t mUnresolvedFormIds = 0;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    /// Walks an opened plugin and decodes MUSC, MUST, the music links of CELL, LCTN, REGN and WRLD and the combat entry
    /// of DOBJ. False only when the walk itself failed (mWalkError); records before it are kept.
    [[nodiscard]] bool parseMusicRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const MusicLimits& limits, Records& out);
    [[nodiscard]] bool parseMusicRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const MusicLimits& limits, Records& out, Content::Error& error);

    /// The records of every plugin of a load order, a later plugin replacing an earlier record of the same id and a
    /// deleted record removing it. Holds copies.
    class MusicIndex
    {
    public:
        void add(const Records& records);

        [[nodiscard]] const Type* type(FormId id) const { return find(mTypes, id); }
        [[nodiscard]] const Track* track(FormId id) const { return find(mTracks, id); }
        [[nodiscard]] const CellLink* cell(FormId id) const { return find(mCells, id); }
        [[nodiscard]] const LocationLink* location(FormId id) const { return find(mLocations, id); }
        [[nodiscard]] const RegionLink* region(FormId id) const { return find(mRegions, id); }
        [[nodiscard]] const WorldLink* world(FormId id) const { return find(mWorlds, id); }
        [[nodiscard]] FormId combatType() const { return mCombatType; }
        [[nodiscard]] std::size_t typeCount() const { return mTypes.size(); }
        [[nodiscard]] std::size_t trackCount() const { return mTracks.size(); }
        [[nodiscard]] std::size_t cellCount() const { return mCells.size(); }

    private:
        template <class T>
        static const T* find(const std::unordered_map<FormId, T>& map, FormId id)
        {
            const auto it = map.find(id);
            return it == map.end() ? nullptr : &it->second;
        }

        std::unordered_map<FormId, Type> mTypes;
        std::unordered_map<FormId, Track> mTracks;
        std::unordered_map<FormId, CellLink> mCells;
        std::unordered_map<FormId, LocationLink> mLocations;
        std::unordered_map<FormId, RegionLink> mRegions;
        std::unordered_map<FormId, WorldLink> mWorlds;
        FormId mCombatType = kNullFormId;
    };

    /// Whether a VFS path exists (main thread; empty: every path counts as existing).
    using FileProbe = std::function<bool(const std::string&)>;

    /// A music file name as the data stores it ("Data\\Music\\Tavern\\MUS_Tavern_01.wav", "\\Data\\Music\\...") as the
    /// VFS path the engine streams: lower case, forward slashes, no leading slash or "data/". The data names .wav
    /// files and the archive holds .xwm (VERIFIED: 320 of the 330 names resolve to an .xwm in Skyrim - Sounds.bsa), so
    /// the .xwm is tried first and then the stored name. "" for a name that is empty, has ".." or a drive letter, or
    /// that no candidate of `exists` finds.
    [[nodiscard]] std::string musicFilePath(std::string_view stored, const FileProbe& exists);

    /// The numbers a condition list may ask.
    struct ConditionFacts
    {
        float mGameHour = 12.f;
    };

    /// Which place a cell has music for: the type id and why. 0 when none.
    struct PlaceType
    {
        FormId mType = kNullFormId;
        const char* mReason = "";
    };

    /// The pure chooser over an index.
    class MusicChooser
    {
    public:
        explicit MusicChooser(const MusicIndex& index)
            : mIndex(index)
        {
        }

        /// The music type of a cell: its own XCMO, then its location's (and the parents') music, then the best region's
        /// (the lowest priority number, ties by the first listed), then its worldspace's. None when no link names a
        /// type that exists. RECALLED-VERIFY: the order (the data cannot show it).
        [[nodiscard]] PlaceType placeType(FormId cell) const;
        [[nodiscard]] FormId combatType() const { return mIndex.combatType(); }

        /// The type as the SDK describes it.
        [[nodiscard]] bool describe(FormId type, const char* reason, bool combat,
            openeden::sdk::game::MusicTypeDescription& out) const;

        /// The next plan of a type: a track whose conditions pass, chosen by the seed (or in order for a type that
        /// cycles or keeps its order), expanded to files that exist. False when nothing can play.
        [[nodiscard]] bool plan(FormId type, const ConditionFacts& facts, std::uint64_t seed, std::uint32_t pick,
            const FileProbe& exists, openeden::sdk::game::MusicPlan& out);

        [[nodiscard]] const conditions::Evaluator& evaluator() const { return mEvaluator; }
        /// Counters (for the log).
        std::uint64_t mPlans = 0;
        std::uint64_t mTracksRejectedByConditions = 0;
        std::uint64_t mTracksWithoutFile = 0;

    private:
        bool expand(const Track& track, int depth, std::uint64_t random, const FileProbe& exists,
            openeden::sdk::game::MusicPlan& out) const;

        const MusicIndex& mIndex;
        conditions::Evaluator mEvaluator;
    };

    /// The deterministic mixer behind every choice (splitmix64 finalizer of the seed, the ids and the counter).
    [[nodiscard]] std::uint64_t musicRandom(std::uint64_t seed, std::uint32_t typeId, std::uint32_t pick,
        std::uint32_t salt);

    /// Reads the music records of a load order on a worker and answers the SDK's questions once they are in.
    class MusicRuntime
    {
    public:
        MusicRuntime(std::vector<std::filesystem::path> plugins, LoadOrder order);
        ~MusicRuntime();
        MusicRuntime(const MusicRuntime&) = delete;
        MusicRuntime& operator=(const MusicRuntime&) = delete;

        /// Starts the worker (once).
        void start();
        /// True once the records are in (never blocks).
        [[nodiscard]] bool ready() const { return mReady.load(std::memory_order_acquire); }

        /// The context's place type, or the combat type when `inCombat`. False when the runtime is not ready or no
        /// type fits.
        [[nodiscard]] bool type(FormId cell, bool inCombat, openeden::sdk::game::MusicTypeDescription& out);
        [[nodiscard]] bool plan(FormId typeId, const ConditionFacts& facts, std::uint64_t seed, std::uint32_t pick,
            const FileProbe& exists, openeden::sdk::game::MusicPlan& out);

        [[nodiscard]] const MusicIndex& index() const { return mIndex; }
        [[nodiscard]] MusicChooser& chooser() { return *mChooser; }
        [[nodiscard]] std::uint64_t recordErrors() const { return mRecordErrors; }

    private:
        void work();

        std::vector<std::filesystem::path> mPlugins;
        LoadOrder mOrder;
        MusicIndex mIndex;
        std::unique_ptr<MusicChooser> mChooser;
        std::future<void> mFuture;
        std::atomic<bool> mReady{ false };
        bool mStarted = false;
        std::uint64_t mRecordErrors = 0;
    };
}

#endif
