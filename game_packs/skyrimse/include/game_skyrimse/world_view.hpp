#ifndef OPENEDEN_GAME_SKYRIMSE_WORLD_VIEW_H
#define OPENEDEN_GAME_SKYRIMSE_WORLD_VIEW_H

// A Skyrim-specific world store behind the game pack, exposed through the SDK's WorldView contract of plain values
// (task 0086, world model option 1; docs/design/game-packs.md). No engine, renderer, Jolt, Qt or OSG type.
//
// What it holds: for each plugin of the load order, the INDEX of its records (worlds, cells without their references,
// base forms with a model) decoded by parseRecords with ParseLimits::mSkipReferences, so no placed reference is a heap
// object until a cell is asked for. loadCell() reads that cell's bytes (its record and its children group, from every
// plugin that touches the cell), decodes them, merges them in load order (a later plugin's placement replaces an
// earlier one with the same id, a record flagged deleted removes it) and keeps the result in a small cache.
//
// Overriding follows the load order: a cell, a world or a base form with the same resolved id in a later plugin wins;
// fields a later record lacks fall back to the earlier ones. An exterior cell is also given the placements of its
// world's persistent cell that stand inside its grid square (4096 units).
//
// Threading (task 0109, decision 2): the queries are for ONE thread at a time (the main thread in the game). The index
// can be built on a worker with startLoad(): while it runs, every query waits (bounded, Options::mIndexWait) for the
// worker to finish and then sees the finished index (a mutex and an atomic flag order the worker's writes before the
// reader's reads); after the bound it answers "nothing found" and never reads the half-built index. The result does
// not depend on the thread or the timing: the worker runs the same addPlugin / finish calls in the same order.
// Deterministic (the same plugins give the same lists and the same objects), and nothing throws out of it: failures
// are located Content::Error values or the `error` text of loadCell.

#include <game_skyrimse/anim_motion.hpp>
#include <game_skyrimse/furniture.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/records.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/actor_mechanics.hpp>
#include <openeden/game/dialogue_source.hpp>
#include <openeden/game/display_name_source.hpp>
#include <openeden/game/weather_source.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/vfs.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    class ActorIndex;
    class PlacedRefIndex;
    struct DialogueSpeaker;
    class WeatherIndex;
    class WeatherTable;
    /// Where files that are not plugins (the string tables) come from. `path` is lower case with backslashes, as an
    /// archive names it (`strings\skyrim_english.strings`).
    class FileSource
    {
    public:
        virtual ~FileSource() = default;
        [[nodiscard]] virtual bool read(std::string_view path, std::string& bytes) const = 0;
        /// Whether a file exists. The default reads it (a source with a cheaper test overrides).
        [[nodiscard]] virtual bool exists(std::string_view path) const
        {
            std::string bytes;
            return read(path, bytes);
        }
    };

    /// Loose files under a Data folder, then Skyrim - Interface.bsa, then the BSAs whose name starts with the plugin's
    /// base name (a mod's own archive). Reads the real files; used by tools.
    class DataFolderFiles final : public FileSource
    {
    public:
        explicit DataFolderFiles(std::filesystem::path dataFolder)
            : mData(std::move(dataFolder))
        {
        }
        [[nodiscard]] bool read(std::string_view path, std::string& bytes) const override;

    private:
        std::filesystem::path mData;
    };

    /// The engine's files through the SDK's VfsService (Host::getVfsService()): loose files of the data folders and
    /// the BSAs the session registered, so a string table in Skyrim - Interface.bsa is found without this pack opening
    /// an archive. Not owned; the service must outlive it. An empty answer is "not found" (a string table is never
    /// empty). Task 0107.
    class VfsFileSource final : public FileSource
    {
    public:
        explicit VfsFileSource(const openeden::sdk::VfsService& vfs)
            : mVfs(vfs)
        {
        }
        [[nodiscard]] bool read(std::string_view path, std::string& bytes) const override;
        [[nodiscard]] bool exists(std::string_view path) const override;

    private:
        const openeden::sdk::VfsService& mVfs;
    };

    class SkyrimWorldView final : public openeden::sdk::game::WorldView,
                                  public ComponentSource,
                                  public TerrainProvider,
                                  public openeden::sdk::game::ActorAppearanceSource,
                                  public openeden::sdk::game::ActorMechanicsSource,
                                  public openeden::sdk::game::DialogueSource,
                                  public openeden::sdk::game::DisplayNameSource,
                                  public openeden::sdk::game::WeatherSource,
                                  public PackageSource,
                                  public FurnitureSource
    {
    public:
        struct Options
        {
            /// The string table language ("english"), a setting of the pack.
            std::string mLanguage = "english";
            ParseLimits mLimits;
            /// Materialised cells kept for repeated asks.
            std::size_t mMaxCachedCells = 16;
            /// Bytes read from a plugin for one cell (record plus children group).
            std::uint64_t mMaxCellBytes = 256ull * 1024 * 1024;
            /// Objects of one cell kept; further ones are dropped and counted in Stats::mObjectsDropped.
            std::size_t mMaxObjectsPerCell = 500000;
            /// How long a query waits for a running startLoad() before it gives up and answers "nothing". Generous: the
            /// index of the five masters takes about 2 s; a wait only happens when a query comes before it is done.
            std::chrono::milliseconds mIndexWait{ 30000 };
            /// Told about waits and timeouts (any thread; the pack passes its logger, which has its own lock).
            /// May be empty.
            std::function<void(const std::string&)> mNotice;
        };

        /// What startLoad's worker did, handed to the completion function.
        struct LoadReport
        {
            std::size_t mGiven = 0;
            std::size_t mTaken = 0;
            std::vector<std::string> mProblems;
            std::chrono::milliseconds mElapsed{ 0 };
            /// True when the load was cut short by cancelAndJoin(); the index is then incomplete.
            bool mAborted = false;
        };

        struct Stats
        {
            std::size_t mPlugins = 0;
            std::size_t mPluginsRefused = 0;
            std::size_t mCells = 0;
            std::size_t mWorlds = 0;
            std::size_t mBaseForms = 0;
            /// References decoded while indexing; zero by design (a test pins it).
            std::uint64_t mReferencesInIndex = 0;
            /// How many times loadCell read plugin bytes and decoded references, and how many it got.
            std::uint64_t mCellReads = 0;
            std::uint64_t mReferencesDecoded = 0;
            std::uint64_t mBytesRead = 0;
            std::uint64_t mCacheHits = 0;
            std::uint64_t mObjectsDropped = 0;
            std::size_t mStringTablesMissing = 0;
            std::uint64_t mUnresolvedFormIds = 0;
            std::uint64_t mRecordErrors = 0;
            /// Teleport doors the index pass found, and what makeComponents could not use (task 0099, N4).
            std::uint64_t mTeleportDoorsIndexed = 0;
            ComponentStats mComponents;
        };

        SkyrimWorldView();
        /// `files` is not owned and must outlive the view (null: names of localised plugins stay empty).
        SkyrimWorldView(Options options, const FileSource* files);
        ~SkyrimWorldView() override;

        SkyrimWorldView(const SkyrimWorldView&) = delete;
        SkyrimWorldView& operator=(const SkyrimWorldView&) = delete;

        /// Replaces the options and the file source (the pack does it in start(), when the host's VFS and the language
        /// setting are known). Refused (false) once a plugin was added. `files` is not owned and must outlive the view.
        [[nodiscard]] bool configure(Options options, const FileSource* files);

        /// Appends one plugin to the load order and indexes it. False with `error` (the first line starts with the
        /// file name) for a file that cannot be opened, is not a Skyrim SE plugin, or cannot take a place in the
        /// load order; the plugin is then not part of the view. The index of a damaged file keeps the records before
        /// the damage and the problem is added to errors().
        [[nodiscard]] bool addPlugin(const std::filesystem::path& path, std::string& error);

        /// addPlugin for each path in order, then finish(). Returns how many plugins were taken.
        std::size_t load(const std::vector<std::filesystem::path>& paths, std::vector<std::string>& problems);

        /// Builds the sorted lists and the lookups. Must be called after the last addPlugin (load does).
        void finish();

        /// load() on a worker thread. Returns at once; false (and nothing started) if a load is already running or a
        /// plugin was added. `onBuilt` runs ON THE WORKER after finish() and BEFORE the index is published to other
        /// threads: the worker may use the view freely inside it (a test aid uses it to probe a cell), and other
        /// threads' queries keep waiting until it returns. It must not throw (a throw is caught and reported to the
        /// notice function). Thread: any one caller; queries before completion wait as described at the top.
        [[nodiscard]] bool startLoad(
            std::vector<std::filesystem::path> paths, std::function<void(const LoadReport&)> onBuilt);

        /// True when no startLoad() is running (the index may be read). Any thread.
        [[nodiscard]] bool isReady() const { return !mBuilding.load(std::memory_order_acquire); }

        /// Asks a running startLoad() to stop after the plugin it is on, then waits for the worker to end. Safe to
        /// call when nothing runs, and more than once. The destructor calls it. Not callable from the worker.
        void cancelAndJoin();

        /// True once cancelAndJoin() has asked the worker to stop. Any thread (lets a test wait for the request instead
        /// of sleeping).
        [[nodiscard]] bool cancelRequested() const { return mCancel.load(); }

        /// Task 0125: whether startLoad's worker built the terrain index (LAND, LTEX of every plugin) itself, right
        /// after the cell index and after isReady() turned true, so that the first exterior door does not build it on
        /// the main thread (it took 2.8 s). Any thread; true once the worker has finished that build.
        [[nodiscard]] bool terrainBuiltOnWorker() const { return mTerrainFromWorker.load(std::memory_order_acquire); }

        /// How many queries had to wait for the worker, and the longest wait in milliseconds. Any thread.
        [[nodiscard]] std::uint64_t indexWaits() const { return mWaits.load(); }
        [[nodiscard]] std::uint64_t longestIndexWaitMillis() const { return mLongestWaitMillis.load(); }

        /// Waits for a running startLoad() like a query; an empty Stats after the timeout.
        [[nodiscard]] const Stats& stats() const;
        /// The string table language in use.
        [[nodiscard]] const std::string& language() const { return mOptions.mLanguage; }
        /// Located problems found while indexing and reading (capped).
        [[nodiscard]] const std::vector<Content::Error>& errors() const;

        // ---- openeden::sdk::game::WorldView ----
        /// The door-to-cell index (reference id to the id of the cell it stands in), for teleport doors only.
        [[nodiscard]] const RefCellIndex& teleportDoorCells() const;

        /// The owner, lock and teleport of a placement of the cell the LAST loadCell returned (own placements and the
        /// world's persistent ones it borrowed). Null for any other cell or placement. Main thread only.
        [[nodiscard]] const PlacementComponents* find(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldId placement) const override;
        /// The interior lighting of the cell the last loadCell returned (its XCLL); none for any other cell.
        [[nodiscard]] std::optional<openeden::sdk::game::CellLighting> findCellLighting(
            openeden::sdk::game::WorldId cell) const override;

        /// The terrain index over the same plugins in the same order, built on the first call (task 0120). Waits for a
        /// running startLoad() like a query. Null after that wait times out. Main thread only.
        [[nodiscard]] const SkyrimTerrainIndex* terrainIndex() const override;

        [[nodiscard]] std::string_view gameId() const override { return "skyrimse"; }
        [[nodiscard]] std::size_t cellCount() const override;
        [[nodiscard]] std::size_t worldCount() const override;
        std::size_t listCells(std::size_t first, std::size_t maxCount,
            std::vector<openeden::sdk::game::WorldCellInfo>& out) const override;
        std::size_t listWorlds(std::vector<openeden::sdk::game::WorldInfo>& out) const override;
        [[nodiscard]] std::optional<openeden::sdk::game::WorldCellInfo> findCellByEditorId(
            std::string_view editorId) const override;
        [[nodiscard]] std::optional<openeden::sdk::game::WorldCellInfo> findCellByGrid(
            openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const override;
        [[nodiscard]] std::optional<openeden::sdk::game::WorldInfo> findWorldByEditorId(
            std::string_view editorId) const override;
        bool loadCell(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCellContent& out, std::string& error) override;

        // ---- PackageSource (task 0135, S5) ----
        // The packages of an NPC, their conditions and the places of the references of the cells loaded so far. The
        // records are read on the dialogue worker (the same plugin pass); ready() is false until it is done and never
        // blocks. Main thread only.
        [[nodiscard]] bool ready() const override;
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override;
        [[nodiscard]] PackageBehavior behaviorOf(const PackageRecord& package) const override;
        bool conditionsPass(const PackageRecord& package, FormId actorBase, FormId actorReference) override;
        [[nodiscard]] bool locate(FormId reference, double& x, double& y, double& z) const override;
        // Stage S6f: activities (patrol, follow), the linked references and patrol idles of placements.
        [[nodiscard]] PackageActivity activityOf(const PackageRecord& package) const override;
        [[nodiscard]] bool targetOf(const PackageRecord& package, PackageTarget& out) const override;
        [[nodiscard]] bool linkedRef(FormId reference, FormId keyword, FormId& out) const override;
        [[nodiscard]] float patrolIdle(FormId reference) const override;

        // ---- FurnitureSource (task 0135, S6b) ----
        // Sit and sleep markers of the furniture placements of the cells loadCell returned, read from the models
        // (BSFurnitureMarkerNode, through the FileSource, cached per model). Main thread only.
        void slotsNear(FormId actorReference, double x, double y, double z, double radius,
            openeden::sdk::game::FurniturePose pose, double fromX, double fromY, double fromZ,
            std::vector<FurnitureSlot>& out) override;

        // ---- openeden::sdk::game::ActorAppearanceSource (task 0121, N5-1) ----
        /// The appearance of the NPC_ `base` (see makeActorAppearance). The actor records of every plugin are read
        /// on the first call and kept (a second read of each plugin file, beside the world index); main thread only.
        bool describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
            openeden::sdk::game::ActorAppearance& out, std::string& error) override;

        // ---- openeden::sdk::game::ActorMechanicsSource (task 0154, stage S7) ----
        /// Numbers and relations from the actor index (FACT, RACE, CLAS, NPC_ of every plugin; read with the first
        /// describeActor or the first of these asks). Main thread only.
        bool actorStats(openeden::sdk::game::WorldId base, std::uint32_t variation, std::uint16_t playerLevel,
            openeden::sdk::game::ActorStats& out, std::string& error) override;
        bool actorDisposition(openeden::sdk::game::WorldId base, std::uint32_t variation,
            openeden::sdk::game::ActorDisposition& out, std::string& error) override;
        openeden::sdk::game::ActorRelation relationBetween(openeden::sdk::game::WorldId baseA, std::uint32_t variationA,
            openeden::sdk::game::WorldId baseB, std::uint32_t variationB) override;
        /// The level an actor that scales with the player scales to when the caller passes 0 (the pack setting
        /// `player level`; kStandInPlayerLevel by default).
        void setStandInPlayerLevel(std::uint16_t level) { mStandInPlayerLevel = level; }
        [[nodiscard]] std::uint16_t standInPlayerLevel() const { return mStandInPlayerLevel; }
        /// The speech skill and the gold of the stand-in player the dialogue conditions read (task 0155; the pack
        /// settings `player speech` and `player gold`). Applied when the dialogue runtime is built, the level is
        /// `standInPlayerLevel()` then.
        void setStandInPlayerSpeech(std::uint8_t speech) { mStandInSpeech = speech; }
        void setStandInPlayerGold(float gold) { mStandInGold = gold; }
        /// The stand-in player's sex (task 0156; the pack setting `player female`).
        void setStandInPlayerFemale(bool female) { mStandInFemale = female; }

        // ---- task 0160
        /// What package an actor runs now (by the placed reference's id), nullopt when unknown, 0 for none. Used by
        /// GetIsCurrentPackage and GetOffersServicesNow in a conversation. Set it before the first conversation (the
        /// pack gives the package brain's answer); null: those conditions stay unanswered.
        void setCurrentPackageLookup(std::function<std::optional<FormId>(FormId reference)> lookup);
        /// Whether the package conditions (conditionsPass) see locations, quest variables and other placed references
        /// (default true). False is the world of task 0135 / 0156: no cell, no location, no quest variable.
        void setPackageConditionsWired(bool wired) { mPackageWired = wired; }
        /// The placed-reference table, once the dialogue worker has built it (null before; main thread). Its build took
        /// `placedRefsBuildMillis()`.
        [[nodiscard]] const PlacedRefIndex* placedRefs() const;
        [[nodiscard]] std::uint64_t placedRefsBuildMillis() const { return mPlacedRefsMillis.load(); }

        // ---- openeden::sdk::game::DisplayNameSource (task 0150) ----
        /// FULL of the base form (NPC_, DOOR, CONT, ACTI ..., items), through the string tables of a localised plugin;
        /// empty when the record has none. Main thread only.
        std::string displayNameOf(openeden::sdk::game::WorldId base) override;

        // ---- openeden::sdk::game::DialogueSource (task 0141, M-S4 slice 1) ----
        /// The dialogue, quest and actor records of every plugin are read on the first call and kept (another read of
        /// each plugin file); string tables are read when a text needs them. One conversation at a time; main thread.
        bool beginConversation(openeden::sdk::game::WorldId base, openeden::sdk::game::WorldId reference,
            openeden::sdk::game::WorldId cell, openeden::sdk::game::DialogueTurn& out, std::string& error) override;
        bool chooseTopic(std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error) override;
        void endConversation() override;

        // ---- openeden::sdk::game::WeatherSource (task 0127, stage W-S1) ----
        /// The weather records of every plugin are read on the first call and kept (a second read of each plugin file,
        /// as the actor index does); main thread only.
        bool describeWeathers(std::vector<openeden::sdk::game::WeatherDescription>& out, std::string& error) override;
        bool describeTimeOfDay(openeden::sdk::game::TimeOfDayDescription& out) override;
        bool weatherListForCell(openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWeatherList& out) override;

    private:
        struct PluginData
        {
            std::filesystem::path mPath;
            std::string mName;
            ParsedPlugin mParsed;
        };

        enum class BaseTable : std::uint8_t
        {
            Static,
            Model,
            Npc,
            Item,
            Leveled,
        };

        struct BaseRef
        {
            std::uint32_t mPlugin = 0;
            BaseTable mTable = BaseTable::Static;
            std::uint32_t mIndex = 0;
        };

        struct CellSource
        {
            std::uint32_t mPlugin = 0;
            std::uint32_t mIndex = 0; ///< into the plugin's ParsedPlugin::mCells
        };

        struct CellEntry
        {
            std::vector<CellSource> mSources; ///< in load order
            openeden::sdk::game::WorldCellInfo mInfo;
            bool mWorldPersistent = false;
            /// The XCLL of the latest source that has one (interior lighting; task 0113).
            std::optional<CellLightingData> mLighting;
        };

        struct WorldEntry
        {
            openeden::sdk::game::WorldInfo mInfo;
            FormId mPersistentCell = kNullFormId;
        };

        struct Materialised
        {
            std::vector<openeden::sdk::game::PlacedObject> mObjects;
            std::uint32_t mUnresolvedBases = 0;
            std::map<FormId, PlacementComponents> mComponents; ///< by placement id; only placements that have any
        };

        /// True when the caller may read or change the index: nothing is building, or the caller is the worker, or the
        /// worker finishes within Options::mIndexWait. False after a timeout (the caller must answer "nothing").
        [[nodiscard]] bool awaitIndex() const;
        bool addPluginUnchecked(const std::filesystem::path& path, std::string& error);
        void noteError(Content::Error error);
        void indexPlugin(std::uint32_t pluginIndex);
        void resolveNames(PluginData& plugin, const StringTable* table);
        [[nodiscard]] std::shared_ptr<const Materialised> materialise(FormId cell, std::string& error);
        [[nodiscard]] bool readCellRefs(
            const CellSource& source, const CellEntry& entry, std::vector<PlacedRef>& out, std::string& error);
        [[nodiscard]] openeden::sdk::game::PlacedObject makeObject(const PlacedRef& ref, bool& baseMissing) const;
        /// The LIGH data of a base form, if the base is a light that has some.
        [[nodiscard]] const LightData* lightOfBase(FormId base) const;

        Options mOptions;
        const FileSource* mFiles = nullptr;
        std::unique_ptr<WeatherIndex> mWeatherIndex; ///< built by the first weather ask; null before
        std::unique_ptr<WeatherTable> mWeatherTable; ///< over mWeatherIndex
        std::function<bool(const std::string&)> mWeatherProbe; ///< does a texture exist (set with the table)
        bool ensureWeather();
        std::unique_ptr<ActorIndex> mActorIndex; ///< built by the first describeActor; null before
        bool mStandInFemale = false;
        std::uint16_t mStandInPlayerLevel = 1; ///< kStandInPlayerLevel
        std::uint8_t mStandInSpeech = 15; ///< kStandInSkill
        float mStandInGold = 100.f; ///< kStandInGold
        bool ensureActorIndex();
        struct DialogueData; ///< the dialogue and quest indexes (task 0141); defined in the .cpp
        struct DialogueState; ///< the data, the string tables and the runtime
        std::unique_ptr<DialogueState> mDialogue;
        std::function<std::optional<FormId>(FormId)> mCurrentPackageLookup;
        bool mPackageWired = true;
        std::atomic<std::uint64_t> mPlacedRefsMillis{ 0 };
        void fillSpeakerCell(DialogueSpeaker& speaker, FormId cell) const; ///< interior flag and location of a cell
        bool ensureDialogue();
        void startDialoguePrefetch();
        [[nodiscard]] static std::unique_ptr<DialogueData> buildDialogueData(
            const std::vector<std::pair<std::filesystem::path, std::string>>& plugins, const LoadOrder& order);
        std::thread mDialogueThread; ///< reads the conversation records after the first actor was described
        std::unique_ptr<DialogueData> mPrefetched; ///< the thread's result; valid once mDialogueReady is set
        std::atomic<bool> mDialogueReady{ false };
        LoadOrder mOrder;
        std::vector<PluginData> mPlugins;
        std::unordered_map<FormId, BaseRef> mBases;
        std::unordered_map<FormId, CellEntry> mCells;
        std::map<FormId, WorldEntry> mWorlds;
        std::vector<FormId> mCellOrder;
        std::vector<FormId> mWorldOrder;
        std::map<std::string, FormId> mCellByEditorId; ///< lower case
        std::map<std::tuple<FormId, std::int32_t, std::int32_t>, FormId> mCellByGrid;
        std::map<std::string, FormId> mWorldByEditorId;
        std::deque<std::pair<FormId, std::shared_ptr<const Materialised>>> mCache;
        RefCellIndex mRefCells; ///< teleport doors: reference id to cell id (later plugins win)
        FormId mLastCell = kNullFormId; ///< the cell of the last successful loadCell
        /// Where every reference of every cell loadCell returned stands (task 0135 S5: package locations). Only grows.
        std::unordered_map<FormId, std::array<double, 3>> mPlacements;
        /// Task 0135 S6b: the furniture placements of each cell loadCell returned (replaced on a reload), and the cell
        /// of every actor placement. Main thread only.
        struct FurnitureRef
        {
            FormId mId = kNullFormId;
            std::string mModel; ///< as the data stores it
            double mX = 0, mY = 0, mZ = 0, mRotZ = 0, mScale = 1;
        };
        std::unordered_map<FormId, std::vector<FurnitureRef>> mFurnitureByCell;
        // Stage S6f: XLKR links and XPRD idle times of the placements of every cell materialised so far (bounded), and
        // the root motion of the furniture clips, read from the animation data files the first time a seat is asked
        // for.
        std::unordered_map<FormId, std::vector<RefLink>> mRefLinks;
        std::unordered_map<FormId, float> mPatrolIdles;
        MotionTable mMotion;
        bool mMotionTried = false;
        void ensureMotion();
        std::unordered_map<FormId, FormId> mActorCell;
        /// Markers per lower-case model path; an empty list for a model that has none or cannot be read.
        std::unordered_map<std::string, std::vector<FurnitureMarker>> mMarkerCache;
        const std::vector<FurnitureMarker>& markersOf(const std::string& model);
        std::optional<openeden::sdk::game::CellLighting> mLastLighting; ///< its interior lighting, if the cell has some
        std::map<FormId, PlacementComponents> mLastComponents; ///< its placements' components, borrowed ones included
        std::vector<Content::Error> mErrors;
        Stats mStats;
        bool mFinished = false;
        mutable std::unique_ptr<SkyrimTerrainIndex> mTerrain; ///< built by the worker after the cell index, else lazily
        // Task 0125: 1 while startLoad's worker is building mTerrain after the cell index (set before mBuilding
        // clears), 2 once it has. terrainIndex() waits for 1 to end (mBuildMutex, mBuildCv).
        mutable std::atomic<int> mTerrainState{ 0 };
        std::atomic<bool> mTerrainFromWorker{ false };
        void buildTerrainIndex() const;

        // The worker (startLoad). mBuilding is set before the thread starts and cleared by the worker (under the
        // mutex, release order) after the index is complete; readers use acquire. mWorkerId lets the worker pass its
        // own gate.
        std::thread mWorker;
        std::atomic<bool> mBuilding{ false };
        std::atomic<bool> mCancel{ false };
        std::atomic<bool> mFailed{ false }; ///< the build was cancelled or threw: queries answer "nothing"
        std::atomic<std::thread::id> mWorkerId{};
        mutable std::mutex mBuildMutex;
        mutable std::condition_variable mBuildCv;
        mutable std::atomic<std::uint64_t> mWaits{ 0 };
        mutable std::atomic<std::uint64_t> mLongestWaitMillis{ 0 };
        mutable std::atomic<bool> mTimeoutReported{ false };
        mutable std::atomic<bool> mWaitReported{ false };
    };
}

#endif
