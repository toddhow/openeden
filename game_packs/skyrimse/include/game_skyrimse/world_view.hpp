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
#include <game_skyrimse/barter.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/furniture.hpp>
#include <game_skyrimse/map_markers.hpp>
#include <game_skyrimse/menus.hpp>
#include <game_skyrimse/mining.hpp>
#include <game_skyrimse/music_data.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/progression.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/scene_runner.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/vmad.hpp>
#include <game_skyrimse/werewolf.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/actor_mechanics.hpp>
#include <openeden/game/dialogue_source.hpp>
#include <openeden/game/display_name_source.hpp>
#include <openeden/game/player_body.hpp>
#include <openeden/game/player_identity.hpp>
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
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace character
    {
        struct Access;
    }

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
                                  public openeden::sdk::game::PlayerBodySource,
                                  public openeden::sdk::game::PlayerIdentitySource,
                                  public openeden::sdk::game::ActorMechanicsSource,
                                  public openeden::sdk::game::DialogueSource,
                                  public openeden::sdk::game::DisplayNameSource,
                                  public openeden::sdk::game::WeatherSource,
                                  public music::MusicDataSource,
                                  public PackageSource,
                                  public FurnitureSource,
                                  public CombatData
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

        /// A placed reference that carries scripts (task 0176): its form ids are load order ids.
        struct ReferenceScripts
        {
            FormId mReference = kNullFormId;
            FormId mBase = kNullFormId;
            bool mActor = false;
            std::vector<vmad::Script> mScripts;
            /// Task 0256: the placement is a primitive volume (XPRM), with its place, for the trigger tracker.
            bool mHasPrimitive = false;
            RefPrimitive mPrimitive;
            Vec3 mPosition;
            Vec3 mRotation; ///< task 0274: radians as stored (a box volume is turned about Z)
            FormId mCell = kNullFormId;
        };

        /// What the Papyrus host reads of the dialogue data (task 0176). Pointers stay valid while the view lives.
        struct PapyrusAccess
        {
            const QuestIndex* mQuests = nullptr;
            /// the conversation runtime's quest state: scripts change it, dialogue reads it
            QuestState* mState = nullptr;
            std::function<const vmad::Vmad*(FormId quest)> mVmads;
            /// task 0185: the text of a localised string of a record of plugin `plugin` (the runtime's own resolver;
            /// main thread only; an empty string when it cannot be found)
            std::function<std::string(const LString&, std::uint32_t plugin, StringTableKind)> mText;
            /// task 0187: fills quest aliases (specific references, unique actors, find-matching by conditions)
            QuestAliasFiller* mAliases = nullptr;
            /// task 0187: the story manager: raises an event and returns the quests it starts (the caller starts them)
            std::function<std::vector<FormId>(const StoryEvent& event)> mStory;
            /// task 0194: the scenes, their conditions (with fn_550 answered by `mSetSceneLookup`'s function), the line
            /// a topic gives an actor, and the package records
            const SceneIndex* mScenes = nullptr;
            std::function<bool(const std::vector<survey::Ctda>&, FormId scene, FormId quest)> mEvaluate;
            std::function<bool(FormId topic, FormId speakerReference, std::uint32_t pick, SceneLine& out)> mLine;
            /// task 0231: the line an actor says unprompted for a DIAL subtype ("HELO", "ATCK" ...), see
            /// DialogueRuntime::barkLine; main thread
            /// task 0243: `speakerIsVictim`: the speaker is the victim of the crime it remarks on
            std::function<bool(std::string_view subtype, FormId speakerReference, std::uint32_t pick,
                bool speakerIsVictim, SceneLine& out)>
                mBark;
            /// task 0235: the player's stance (sneaking, what is drawn: 0 nothing, 1 weapon, 2 spell) for the
            /// conditions of the dialogue runtime; main thread
            std::function<void(bool sneaking, std::uint8_t drawState)> mSetStance;
            /// task 0261: do the conditions of a quest's log entry hold now (the conversation runtime's world: the last
            /// speaker stands for the player's place); main thread
            std::function<bool(const std::vector<survey::Ctda>& list, FormId quest)> mQuestConditions;
            /// task 0261: a script's live value of a quest variable wins in the conversation's conditions; main thread
            std::function<void(SkyrimConditionWorld::LiveQuestVariable)> mSetLiveQuestVariable;
            std::function<void(std::function<bool(FormId scene, std::uint32_t action)>)> mSetSceneLookup;
            std::function<const PackageRecord*(FormId package)> mPackage;
            /// the base form of a placed reference (0 when unknown)
            std::function<FormId(FormId reference)> mBaseOf;
            /// task 0217: the display name of what fills the alias `aliasName` of `quest` (an actor's or reference's
            ///   FULL, a
            /// location's FULL); "" when the alias is not filled or what fills it has no name. Main thread.
            std::function<std::string(FormId quest, std::string_view aliasName)> mAliasName;
            /// task 0217: the locations (the Papyrus data names a form a Location, and finds the location of a
            // reference)
            const LocationIndex* mLocations = nullptr;
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
        /// Task 0291: the water height of an interior cell (any cell, not only the last one loaded).
        [[nodiscard]] std::optional<float> findInteriorWater(openeden::sdk::game::WorldId cell) const override;

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
        bool packagesForReference(
            FormId reference, FormId base, std::vector<const PackageRecord*>& out) const override; // task 0274
        [[nodiscard]] std::uint64_t packageListRevision() const override; // task 0274
        [[nodiscard]] bool tracesPackages() const override { return static_cast<bool>(mPackageTraceLog); } // task 0334
        void packageChosen(FormId reference, FormId base, const PackageRecord* package,
            const std::string& note) const override; // task 0334
        /// Task 0274: the quest whose alias gives this package (the first one that lists it), 0 when none does.
        [[nodiscard]] FormId aliasPackageQuest(FormId package) const;
        [[nodiscard]] PackageBehavior behaviorOf(const PackageRecord& package) const override;
        bool conditionsPass(const PackageRecord& package, FormId actorBase, FormId actorReference) override;
        [[nodiscard]] bool locate(FormId reference, double& x, double& y, double& z) const override;
        // Stage S6f: activities (patrol, follow), the linked references and patrol idles of placements.
        [[nodiscard]] PackageActivity activityOf(const PackageRecord& package) const override;
        [[nodiscard]] bool targetOf(const PackageRecord& package, PackageTarget& out) const override;
        [[nodiscard]] bool forceGreetTopic(const PackageRecord& package, FormId& out) const override;
        /// Task 0310: a reference with a primitive (a trigger volume) is listed with the scripted references even when it
        /// carries no script (a quest alias holds its script); set before any cell is read
        void setPrimitiveTriggers(bool on) { mPrimitiveTriggers = on; }
        [[nodiscard]] bool linkedRef(FormId reference, FormId keyword, FormId& out) const override;
        /// Task 0299: the stored angles of a scripted placement of a cell this view materialised (a trap's facing).
        [[nodiscard]] bool referenceRotation(FormId reference, double (&xyz)[3]) const;
        [[nodiscard]] float patrolIdle(FormId reference) const override;
        /// Task 0277: the reference stands in a cell other than the player's, an interior being on one side.
        [[nodiscard]] bool targetIsElsewhere(FormId reference) const override;
        [[nodiscard]] std::uint32_t packagesRevision() const override { return mPackagesRevision; }

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

        // ---- CombatData (task 0171, stage S5-3): the numbers of a fight from the actor and combat records ----
        bool fighter(openeden::sdk::game::WorldId base, FighterProfile& out) override;
        bool fighterGear(
            openeden::sdk::game::WorldId base, const std::vector<ArmourPiece>& worn, FighterGear& out) override;
        bool fighterFor(openeden::sdk::game::WorldId base, std::uint32_t variation, FighterProfile& out) override;
        FighterProfile playerFighter() override;
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override;
        const WeaponRecord* weaponById(FormId id) override; // task 0295
        CombatSettings settings() override;
        /// Task 0254: iHoursToRespawnCell (240 when the data has none or the records are not read yet).
        [[nodiscard]] double hoursToRespawnCell() const;
        // Task 0197
        openeden::sdk::game::ActorRelation factionRelation(const FighterProfile& a, const FighterProfile& b) override;
        BlockSettings blockSettings() override;
        /// Task 0178 (stage S5-6): the AMMO by editor id, its PROJ and the arrow GMSTs.
        const AmmoRecord* ammoByEditorId(std::string_view editorId) override;
        const ProjectileRecord* projectileOf(const AmmoRecord& ammo) override;
        ProjectileSettings projectileSettings() override;
        DualCastSettings dualCastSettings() override; // task 0300
        /// Task 0186 (stage S5-8): spells by editor id, a PROJ by form id and the magic GMSTs.
        const SpellRecord* spellByEditorId(std::string_view editorId) override;
        const ImageModifierRecord* imageModifierByEditorId(std::string_view editorId) override; // task 0257
        // Task 0205: the shouts.
        const ShoutRecord* shoutById(FormId id) override;
        const ShoutRecord* shoutByEditorId(std::string_view editorId) override;
        shouts::Settings shoutSettings() override;
        /// The shouts and words of the load order, or null while the combat records are being read.
        [[nodiscard]] const ShoutIndex* shoutIndex() const;
        const ProjectileRecord* projectileById(FormId id) override;
        const AmmoRecord* ammoById(FormId id) override; // task 0299
        std::string_view artModelById(FormId id) override; ///< task 0191
        const ExplosionRecord* explosionById(FormId id) override; ///< task 0242
        MagicSettings magicSettings() override;
        /// Review fix of task 0171: false until the combat records (and the actor records) are read, on a worker
        /// started by the first call; never blocks. The four answers above read nothing themselves: they answer
        /// "nothing" until this is true.
        bool combatReady() override;
        /// Task 0231: the GMSTs of the load order (the bark timers read them); null until the combat records are read
        /// (this asks for them like combatReady()).
        [[nodiscard]] const SettingsTable* gameSettings();
        const PerkLibrary* perkLibrary() override; // task 0179
        bool raceIsChild(FormId race) override; // task 0238
        bool globalDefault(FormId global, float& value) override { return globalValue(global, value); } // task 0238
        FormId perkByEditorId(std::string_view editorId) override;
        const SoundLibrary* soundLibrary() override; // task 0193
        bool soundFileExists(const std::string& vfsPath) override;

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
        void setHostileFactions(bool on) { mHostileFactions = on; } ///< task 0316
        [[nodiscard]] std::uint16_t standInPlayerLevel() const { return mStandInPlayerLevel; }
        /// Task 0321: the level and the race of an actor base (NPC_) as the effective record gives them (templates
        /// followed; a template that is a leveled list is resolved with pick 0). False when the id is no NPC_.
        [[nodiscard]] bool actorLevelOf(FormId base, std::int32_t& level);
        [[nodiscard]] bool actorRaceOf(FormId base, FormId& race);
        /// Task 0212 (`creatures`): creature races are described and fight as creatures. Set before the first actor.
        void setCreatures(bool on) { mCreatures = on; }
        /// Task 0253: the dragon race is described and fights as a dragon (needs setCreatures).
        void setDragons(bool on) { mDragons = on; }
        /// Task 0253: the NPC_ of this editor id (any case; the last plugin wins), or kNullFormId. Main thread.
        [[nodiscard]] FormId npcByEditorId(std::string_view editorId);
        /// Task 0220 (`[extension.skyrimse] held gear`): describeActor also names the actor's weapon and shield.
        void setHeldGear(bool on) { mHeldGear = on; }
        [[nodiscard]] bool heldGear() const { return mHeldGear; }

        // ---- openeden::sdk::game::PlayerBodySource (task 0222) ----
        /// `[extension.skyrimse] player body`: the player has a Skyrim body (race Nord, the sex of `player female`).
        void setPlayerBody(bool on) { mPlayerBodyOn = on; }
        [[nodiscard]] bool playerBodyOn() const { return mPlayerBodyOn; }
        /// What the player has equipped, as the pack's inventory holds it (the ARMO items and the one WEAP). A change
        /// moves the revision the core polls; the same lists again do not.
        void setPlayerEquipment(std::vector<FormId> armour, FormId weapon);
        /// Task 0259: the player is a werewolf (his body is the beast's, in third person only; the revision moves).
        void setBeastForm(bool on);
        /// Task 0279: the player is a vampire, so his body is drawn with the vampire race of his own.
        void setVampireLook(bool on);
        [[nodiscard]] bool beastForm() const { return mBeastForm; }
        /// What the beast's claws do for a player of this level (false until the combat records are read; never waits).
        [[nodiscard]] bool beastNumbers(std::uint32_t playerLevel, werewolf::BeastNumbers& out);
        /// True when the base resolves to an NPC_ of a human race (a person: a werewolf's feed gives it the full time).
        [[nodiscard]] bool baseIsPerson(openeden::sdk::game::WorldId base);
        /// The revision and (through the cell source) the swing state. Never blocks: the revision is 0 until the actor
        /// records are read (a worker reads them, adopted here).
        bool playerBody(openeden::sdk::game::PlayerBodyState& out) override;
        bool describePlayerBody(openeden::sdk::game::ActorAppearance& out, std::string& error) override;
        /// Task 0245 (`[extension.skyrimse] first person body`, needs `player body`): the player's first-person arms.
        void setFirstPersonBody(bool on) { mFirstPersonBodyOn = on; }
        [[nodiscard]] bool firstPersonBodyOn() const { return mFirstPersonBodyOn; }
        bool describeFirstPersonBody(openeden::sdk::game::ActorAppearance& out, std::string& error) override;
        // ---- openeden::sdk::game::PlayerIdentitySource (task 0228) ----
        /// The name the player chose: the core writes it into his record. An empty name is no name.
        void setPlayerName(std::string name);
        [[nodiscard]] const std::string& playerName() const { return mPlayerName; }
        bool playerIdentity(openeden::sdk::game::PlayerIdentityState& out) override;
        [[nodiscard]] bool creatures() const { return mCreatures; }
        /// The speech skill and the gold of the stand-in player the dialogue conditions read (task 0155; the pack
        /// settings `player speech` and `player gold`). Applied when the dialogue runtime is built, the level is
        /// `standInPlayerLevel()` then.
        void setStandInPlayerSpeech(std::uint8_t speech) { mStandInSpeech = speech; }
        void setStandInPlayerGold(float gold) { mStandInGold = gold; }
        /// The stand-in player's sex (task 0156; the pack setting `player female`).
        void setStandInPlayerFemale(bool female) { mStandInFemale = female; }
        [[nodiscard]] bool standInPlayerFemale() const { return mStandInFemale; } // task 0306: book tags

        /// Task 0200: the records the player's progression is built from (false until the combat records are read:
        /// combatReady() starts that).
        [[nodiscard]] bool progressionInputs(
            const progression::SkillTable*& skills, const PerkLibrary*& perks, progression::Settings& settings);
        /// Task 0200: the player's real level and skills for the speech and bribe checks (replaces the stand-in's
        /// level and skills; applied at once to a running conversation runtime).
        void setProgressionPlayer(
            std::uint16_t level, const std::array<std::uint8_t, progression::kSkillCount>& skills);

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

        // ---- task 0184 (equip, use, gold)
        /// A WEAP by form id. False while the combat records are not read (ask again; the combat update starts that
        /// read); true with `out` null for an id that is no weapon. Main thread.
        [[nodiscard]] bool lookupWeapon(FormId id, const WeaponRecord*& out);
        /// Task 0300: an AMMO by form id, the same way (false while the combat records are not read; true with `out`
        /// null for an id that is no ammunition) and the projectile it fires (null when it names none).
        [[nodiscard]] bool lookupAmmo(FormId id, const AmmoRecord*& out);
        /// The armour piece a worn ARMO gives the armour rating (rating, slots, type, keywords), false when the id is
        /// no armour. Reads the actor index like describeActor (main thread).
        [[nodiscard]] bool armourPieceById(FormId id, ArmourPiece& out);
        /// How much health an effect of a potion restores at once: its magnitude when the effect is a value modifier
        /// on Health that is neither hostile nor detrimental and lasts no time, else 0 (also while the records are
        /// not read). RECALLED-VERIFY: the flag bits and 24 as the Health actor value.
        [[nodiscard]] float healthOfEffect(FormId effect, float magnitude, std::uint32_t duration);
        /// Task 0199: the numbers of an MGEF the crafting rules need (flags, base cost, archetype, primary actor
        /// value). False when the combat records are not read yet or the effect is unknown.
        [[nodiscard]] bool magicEffectData(
            FormId effect, std::uint32_t& flags, float& baseCost, std::uint32_t& archetype, std::int32_t& primaryValue);
        /// Where the player's gold comes from (the inventory's Gold001) for the speech and bribe checks; empty: the
        /// stand-in's. Applied when the dialogue runtime is built (set it before the first conversation).
        void setPlayerGoldSource(std::function<float()> source) { mGoldSource = std::move(source); }
        /// Task 0270: what the player wears and how many of an item he carries, for GetEquipped and GetItemCount on the
        /// player in the conversation's conditions (the Amulet of Mara). Applied like the gold source.
        void setPlayerWornSource(std::function<bool(FormId)> source) { mWornSource = std::move(source); }
        void setPlayerItemSource(std::function<float(FormId)> source) { mItemSource = std::move(source); }

        /// Task 0198: Game.GetFormFromFile. The load-order id of local id `local` of the plugin `lowerFile` (lower
        /// case); 0 when the index is not built or the plugin is not in the order. Main thread.
        [[nodiscard]] FormId formFromFile(std::string_view lowerFile, std::uint32_t local) const;

        // ---- task 0182 (inventory)
        /// What the inventory reader needs, copied out once the index is built (false before: ask again, nothing
        /// waits). `leveled` gets every plugin's leveled lists in load order (the reader keeps the later one).
        [[nodiscard]] bool inventoryInputs(
            std::vector<std::filesystem::path>& paths, LoadOrder& order, std::vector<LeveledList>& leveled) const;
        /// The CNTO entries of an NPC_ base with its default outfit's items (after templates); false when unknown.
        /// Reads the actor index like describeActor (main thread).
        [[nodiscard]] bool npcInventory(FormId base, std::vector<InventoryEntry>& out);

        // ---- task 0189 (barter)
        /// The vendor data of an NPC_ base: its first faction (templates followed, SNAM order) whose FACT has a buy
        /// list and a chest, with the chest REFR's base from the placed-reference table and the list's keywords from
        /// the form lists. Pending until the conversation records are read (there is always a conversation first:
        /// barter is a dialogue service); NotVendor for an NPC with no such faction or a chest the data does not know.
        /// Main thread; the actor index is read on the first call like describeActor.
        [[nodiscard]] barter::VendorLookup vendorOf(FormId npcBase, barter::VendorInfo& out);
        /// A merchant's GetOffersServicesNow is true whatever its package (see SkyrimConditionWorld); set before the
        /// first conversation or any time after.
        void setVendorServicesLookup(std::function<bool(FormId speakerBase)> lookup);
        /// Task 0326: the placed references that died in this session (the civil war's kill count).
        void setDeadLookup(std::function<bool(FormId reference)> lookup);
        /// Task 0239: what the follower scripts changed, read by the dialogue conditions. Not owned.
        void setFollowerOverlay(const FollowerOverlay* overlay);
        /// Task 0277: where an actor stands now (the pack's package brain); the view supplies where a loaded reference
        /// stands. Conversation conditions that measure a distance use them (the Companions' yard greeting).
        void setLiveActorPlace(std::function<bool(FormId reference, double& x, double& y, double& z)> lookup);
        /// Task 0258: the arrest conversation. `mWants(reference)`: should a conversation with the actor placed as
        /// `reference` open as the guard's forced greeting? The others tell the crime rules that it began, ended or
        /// could not begin. Every function may be empty (then no conversation is forced).
        struct ArrestDialogueHooks
        {
            std::function<bool(FormId reference)> mWants;
            std::function<void(FormId reference)> mBegan;
            std::function<void(FormId reference)> mEnded;
            std::function<void(FormId reference)> mFailed;
        };
        void setArrestDialogue(ArrestDialogueHooks hooks);
        /// Task 0264: a ForceGreet package's conversation. `mTopic(reference)`: the topic whose first passing INFO is
        /// the greeting of the conversation that opens with the actor placed as `reference` (0: none is wanted);
        /// `mBegan` and `mEnded` tell the force-greet system that it opened and closed. Every function may be empty.
        struct ForcedGreetHooks
        {
            std::function<FormId(FormId reference)> mTopic;
            std::function<void(FormId reference)> mBegan;
            std::function<void(FormId reference)> mEnded;
            /// Task 0271: asked before `mEnded` when the greeting cannot open: true lets the plain conversation open.
            std::function<bool(FormId reference)> mPlainWhenNone;
        };
        void setForcedGreeting(ForcedGreetHooks hooks);
        /// What the crime rules tell the conversations' conditions (applied now and to a runtime built later).
        void setCrimeConditionSource(CrimeConditionSource source);
        /// Can the guard placed as `reference` (of base `base`) open the forced greeting now? False while the dialogue
        /// data is not built yet (nothing blocks the main thread).
        [[nodiscard]] bool forcedGreetingAvailable(FormId base, FormId reference, std::string* why = nullptr);
        /// Task 0271: where the player stands, for the package conditions that run on him.
        /// Task 0327: the scenes' condition world (the quest world) is told too.
        void setPlayerPlace(bool interior, FormId cell, FormId world);
        /// Task 0312: the world the player stands in (none indoors or before the place is known) and the world of an
        /// exterior cell (none for an interior or an unknown cell).
        [[nodiscard]] FormId playerWorld() const { return mPlayerPlaceKnown && !mPlayerInterior ? mPlayerWorld : kNullFormId; }
        [[nodiscard]] FormId worldOfCell(FormId cell);
        /// Task 0312: the location the player stands in now (an interior's, or the exterior square's), none when unknown.
        [[nodiscard]] FormId playerLocationNow() const;
        /// Task 0271: the packages quest aliases give (ALPC) join an actor's list (off by default).
        void setAliasPackages(bool on) { mAliasPackagesOn = on; }
        /// Task 0330: whether the 3D of a reference is loaded (fn_555 HasLoaded3D for a scene's phase), set by the pack.
        void setLoadedLookup(SkyrimConditionWorld::LoadedLookup lookup) { mLoadedLookup = std::move(lookup); }
        /// Task 0330: an interior's water is judged by its content even when the engine asks before the cell is built
        /// (a start cell). Off: a cell not built yet is judged by its record alone (task 0291's rule).
        void setJudgeWaterEarly(bool on) { mJudgeWaterEarly = on; }
        /// Task 0330: the placement's record says it starts dead (known once its cell has been read).
        [[nodiscard]] bool startsDead(FormId reference) const { return mStartsDeadRefs.count(reference) != 0; }
        /// Task 0302 (draugr ambushes): the package conditions also see what the game's scripts did to an actor's
        /// Variable01 (the follower overlay), so a package that tests it stops passing when the script lets the actor out.
        void setAmbush(bool on) { mAmbushOn = on; }
        [[nodiscard]] bool aliasPackagesOn() const { return mAliasPackagesOn; }
        /// Task 0271 (a developer aid, `OPENEDEN_PACKAGE_TRACE=<decimal base id>`): the packages of this NPC base and
        /// whether each one's conditions pass are written to `log` as they are asked.
        void setPackageTrace(FormId base, std::function<void(const std::string&)> log)
        {
            mPackageTraceBase = base;
            mPackageTraceLog = std::move(log);
        }
        /// Task 0261: see SkyrimConditionWorld::setRankMinusOneMembers.
        void setRankMinusOneMembers(std::set<FormId> factions);
        // ---- task 0219 (training, rooms)
        /// Training and renting are named "open:<name>" in the conversation (set before the first conversation or
        /// any time after).
        void setOpenServices(bool on);
        /// Task 0298: a Blocking dialogue branch starts a conversation instead of Hello (DialogueRuntime::setBlockingBranches).
        void setBlockingBranches(bool on);
        void setFragmentFlush(std::function<void()> flush); ///< task 0304
        void setDialogueTrace(std::function<void(const std::string&)> trace); ///< task 0304
        void setQuestGreetings(bool on); ///< task 0304: a quest's nameless branch start greets (College of Winterhold)
        void setWalkAwayTopics(bool on); ///< task 0315: ending a conversation runs the walk-away topic's fragment
        void setWalkAwayInfos(bool on); ///< task 0326: and the last topic's own walk-away INFO (flag 0x80)
        /// Task 0308: a ForceGreet package whose topic is the subtype HELO makes the actor say its greeting.
        void setHelloGreetings(bool on) { mHelloGreetings = on; }
        /// Task 0335: GetDetected(player) holds for an actor in the player's interior (SkyrimConditionWorld::
        /// setDetectsPlayerInCell); the quest world is told where the player stands for it.
        void setDetectsPlayer(bool on) { mDetectsPlayer = on; }
        void setMovedActorsOutdoors(bool on) { mMovedActorsOutdoors = on; } // task 0329
        /// The class data of an NPC_ base (templates followed): CLAS DATA's trained skill and maximum training level.
        /// Ready (with `hasData` false for a class with no data), Pending until the actor index is built, NotVendor
        /// when the NPC or its class is unknown. Main thread; reads the actor index like vendorOf.
        struct ClassTraining
        {
            bool mHasData = false;
            std::uint8_t mSkill = 0;
            std::uint8_t mMaxLevel = 0;
        };
        [[nodiscard]] barter::VendorLookup trainingOf(FormId npcBase, ClassTraining& out);
        /// The bed REFR of an innkeeper: the `Bed` property of its base's RentRoomScript (VMAD of the NPC_). Ready, or
        /// NotVendor when the NPC has no such script or property; Pending until the index is built.
        [[nodiscard]] barter::VendorLookup rentBedOf(FormId npcBase, FormId& bed);
        // ---- task 0204 (crime)
        /// Who owns a placement of a cell loadCell has returned (an NPC_ or a FACT form id; the placements of every
        /// cell loaded so far are kept, bounded). Nothing for an unowned or unknown placement. Main thread.
        [[nodiscard]] std::optional<openeden::sdk::game::OwnerComponent> ownerOfPlacement(FormId placement) const;
        /// Task 0261: the cell a placed reference stands in (0 when unknown: the placed-reference table is not ready,
        /// or the reference is a created one), and a cell's form id by its editor id (0 when unknown). Main thread.
        [[nodiscard]] FormId cellOfPlacement(FormId placement) const;
        [[nodiscard]] FormId cellIdByEditorId(std::string_view editorId);
        /// Task 0325: the editor id of a cell (lower case, as the index keeps it), empty when unknown or before the
        /// index is built. Main thread.
        [[nodiscard]] std::string cellEditorIdOf(FormId cell);
        /// Task 0221: the harvest data of a FLOR or TREE base that names an ingredient, and its name (the FULL, else
        /// the editor id); false for any other base or before the index is built. Main thread.
        [[nodiscard]] bool plantOf(FormId base, HarvestData& out, std::string& name);
        /// Task 0338: the voice type of a TACT base (a talking activator), else 0. Main thread; waits for the index.
        [[nodiscard]] FormId talkingActivatorVoice(FormId base);
        /// Task 0224: the resource data of an ore vein (an ACTI with MineOreScript) or a chopping block (a FURN with
        /// ResourceFurnitureScript), its name (FULL, else the editor id) and its activate text; false for any other
        /// base or before the index is built. Main thread.
        [[nodiscard]] bool resourceOf(
            FormId base, mining::ResourceInfo& out, std::string& name, std::string& activateText);
        /// Task 0224: the entries of an FLST (needs the dialogue worker's data, started by this call); false while
        /// that is not ready or when the form is not a list. Main thread.
        [[nodiscard]] bool formListEntries(FormId list, std::vector<FormId>& out);
        /// Task 0224: the DESC text of a MESG record (a localised one is looked up in the plugin's string tables,
        /// read at the first ask); empty when it cannot be read. Main thread.
        [[nodiscard]] std::string messageTextOf(FormId message);
        /// Task 0237: a MESG's text, button texts and whether it is a message box. False when the form is not a message
        /// or before the index is built. Main thread.
        struct MessageData
        {
            std::string mText;
            std::vector<std::string> mButtons;
            bool mBox = false;
        };
        [[nodiscard]] bool messageOf(FormId message, MessageData& out);
        /// Task 0237: the name (FULL, else the editor id) of an ACTI that carries the standing stone or shrine script
        /// (`powerShrineScript`, `TempleBlessingScript`), else empty. Main thread.
        [[nodiscard]] std::string stoneNameOf(FormId base);
        /// Task 0237: the Papyrus class of a base form the other lookups do not class: "Spell" for a SPEL, "Message"
        /// for a MESG (a script's `Spell.Cast` and `Message.Show` need the class on the VALUE); empty for anything
        /// else.
        [[nodiscard]] std::string scriptClassOf(FormId form);
        /// Task 0221: with this on, displayNameOf answers a harvestable plant's prompt ("Harvest Canis Root") instead
        /// of its name. Set before the first cell is built.
        void setHarvestPrompts(bool on) { mHarvestPrompts = on; }
        /// Task 0208: the XCNT of a loaded placed stack, 0 when it has none. Main thread.
        [[nodiscard]] std::int32_t itemCountOfPlacement(FormId placement) const;
        /// True when the form is a FACT (else an NPC_). Needs the actor index (built by the combat or the first actor).
        [[nodiscard]] bool isFactionForm(FormId form) const;
        /// Task 0274: the horse a placed actor starts mounted on (its XHOR), else kNullFormId.
        [[nodiscard]] FormId horseOf(FormId reference) const;
        /// Task 0274: an NPC_ base (the class `ActorBase`: `GetActorBase() == TriggerActor` casts to it).
        [[nodiscard]] bool isActorBaseForm(FormId form) const;
        /// The crime data of a faction (null: none or no crime values) and its name (the FULL, else the editor id).
        [[nodiscard]] const CrimeData* crimeDataOf(FormId faction) const;
        [[nodiscard]] std::string factionNameOf(FormId faction) const;
        /// Where the JAIL marker of a crime faction sends the player: the XTEL of that REFR (its destination cell's
        /// editor id, position and heading). False when the marker or its teleport is not known.
        [[nodiscard]] bool jailDestination(FormId faction, crime::JailDestination& out);
        // ---- task 0213 (trespass)
        /// What the CELL records of the load order say of a cell (the latest plugin that has a DATA or an XOWN wins
        /// each). False for a cell the view does not know. Main thread.
        [[nodiscard]] bool cellAccessOf(FormId cell, crime::CellAccess& out) const;
        /// The same, by editor id (case-insensitive); False when no such cell.
        [[nodiscard]] bool cellAccessByName(std::string_view editorId, crime::CellAccess& out) const;
        // ---- task 0210 (waiting and sleeping)
        /// True when the base form's model is a bed: its furniture markers include a sleep marker (the model is read
        /// once and cached). False for a base that is not a model record, has no model or cannot be read. Main thread.
        [[nodiscard]] bool furnitureIsBed(FormId base);
        /// The speech skill of the stand-in player (the pack setting `player speech`).
        [[nodiscard]] std::uint8_t standInPlayerSpeech() const { return mStandInSpeech; }

        // ---- task 0201 (map and compass): the pack's own hooks, one pair
        /// Asks the dialogue worker to read the map markers too (before the worker starts: set it in the pack's
        /// start()). Costs one more walk of each plugin.
        void setWantMapMarkers(bool want) { mWantMapMarkers = want; }
        struct MapAccess
        {
            const map::MarkerIndex* mMarkers = nullptr;
            const PlacedRefIndex* mPlaced = nullptr;
            std::function<bool(FormId cell, FormId& world, bool& interior)> mCellWorld;
            std::function<FormId(std::string_view editorId)> mInteriorWorld; ///< task 0223
        };
        /// True once the dialogue data (and so the markers) is read; never waits. Main thread.
        [[nodiscard]] bool mapAccess(MapAccess& out);

        // ---- task 0207 (menus and loading screens): the LSCR records and the quest world's evaluator
        /// Asks the dialogue worker to read the loading screens too (before the worker starts: set it in the pack's
        /// start()).
        void setWantLoadingScreens(bool want) { mWantLoadingScreens = want; }
        /// True once the dialogue data is read; never waits. Main thread. The evaluator answers conditions as the quest
        /// world does (the player's current state).
        [[nodiscard]] bool loadingAccess(menus::Access& out);

        // ---- task 0225 (character creation)
        /// The player's look when he made one: race, sex and the head parts he chose (HDPT type and part, kNullFormId
        /// for "none"). A change moves the revision the core polls (the body is made again); the same look again does
        /// not. Until it is called the body is the Nord of `player female`.
        void setPlayerLook(FormId race, bool female, std::vector<std::pair<std::uint32_t, FormId>> headChoice);
        /// The actor records and a text resolver for the character menu: true once the actor index is there (the combat
        /// worker reads it; this call never waits). Main thread only.
        [[nodiscard]] bool characterAccess(character::Access& out);

        // ---- task 0176 (Papyrus wiring)
        /// Fills \a out and returns true once the dialogue and quest data are read (it starts the read on the first
        /// call and never waits: false means "ask again next frame"). Main thread.
        [[nodiscard]] bool papyrusAccess(PapyrusAccess& out);
        /// Tasks 0217 and 0228: the display name of what fills the alias `aliasName` of `quest` ("" when nothing does),
        /// shared by the journal and the conversation tags; and its sex (1 female, 0 male, -1 unknown).
        [[nodiscard]] std::string aliasDisplayName(FormId quest, std::string_view aliasName);
        [[nodiscard]] int aliasFemale(FormId quest, std::string_view aliasName);
        /// Task 0306: the scripts a base record's VMAD names (a reference created of it gets them); null when none.
        [[nodiscard]] const std::vector<vmad::Script>* scriptsOfBase(FormId base) const { return baseScriptsOf(base); }
        /// The scripts of the placed references of a cell (its own placements, not the world's persistent ones).
        /// Main thread.
        /// Task 0181: a placement's own scripts followed by those of its base form (see vmad::combineScripts).
        [[nodiscard]] std::vector<ReferenceScripts> scriptsOfCell(FormId cell);
        /// Task 0181: does the form (a placed reference: its base form's keywords) have the keyword? Base records
        /// (ACTI, DOOR, items ...) by their KWDA; an NPC by its own keywords and its race's, once the actor index
        /// exists. False when the data cannot say. Main thread.
        [[nodiscard]] bool formHasKeyword(FormId form, FormId keyword) const;
        /// A script set a quest's stage: the package brain's own copy of the quest state follows.
        void setPackageQuestStage(FormId quest, std::uint32_t stage);
        /// Task 0305: what an alias of a running quest holds now (a script's ForceRefTo / ForceLocationTo; the Papyrus
        /// host answers) and a number that moves when that may have changed. With them an alias that no data fills
        /// (DLC1VQ00's Messenger) gives the actor it holds its packages (ALPC), and a package condition run on that
        /// quest's aliases (GetInCurrentLocAlias) reads them.
        using AliasLookup = std::function<FormId(FormId quest, std::uint32_t alias)>;
        void setAliasLookup(AliasLookup lookup, std::function<std::uint32_t()> revision)
        {
            mAliasLookup = std::move(lookup);
            mAliasRevision = std::move(revision);
        }
        /// Task 0314: what a script FORCED into an alias (ForceRefTo) only. An alias with no fill data and no
        /// conditions (BYOH's Orphan01..07) is filled by nothing but a script, so the packages it lists go only to what
        /// a script forced into it: the filler's "any reference" fill of such an alias gave a Riften orphan's packages
        /// to Brynjolf. Without this lookup `AliasLookup` is used for them too (the older behaviour).
        void setForcedAliasLookup(AliasLookup lookup)
        {
            mForcedAliasLookup = std::move(lookup);
        }
        /// Task 0306: a quest started with no stage (the story manager's start): the package brain's copy runs it.
        void startPackageQuest(FormId quest);
        /// Task 0187: where the script fragments of a shown dialogue line go (applied now if the runtime exists, else
        /// when it is made). Main thread.
        void setInfoFragmentSink(InfoFragmentSink sink);
        /// Task 0187: is the form a dialogue INFO with scripts (the class of a TIF fragment script's form)?
        [[nodiscard]] bool isScriptedInfo(FormId info) const;
        /// Task 0194: is the form a scene (SCEN)? (the class of a scene's fragment script's form)
        [[nodiscard]] bool isScene(FormId form) const;
        /// Task 0223: the default value of a GLOB (a Papyrus GlobalVariable). False when the form is no global or the
        /// dialogue data is not read yet.
        [[nodiscard]] bool globalValue(FormId global, float& value) const;
        /// Task 0223: the exterior world a player in the interior cell `editorId` belongs to by its location (its
        /// parent chain up to the first location that lists a grid square): kNullFormId when none can be found.
        [[nodiscard]] FormId worldOfInterior(std::string_view editorId) const;
        /// Task 0202: the description of a BASE form as an object that is not placed anywhere yet (PlaceAtMe): its
        /// kind,
        /// editor id, model path and bounds, and the identity transform; the caller sets id, position, rotation and
        /// scale.
        /// False for a form that is not a known base (a placed reference, a quest, a missing id). Main thread.
        [[nodiscard]] bool describeBase(FormId base, openeden::sdk::game::PlacedObject& out) const;
        /// Task 0250: the NPC_ a summon effect's associated form names: the form itself when it is an NPC_, or the
        /// entry a leveled NPC list picks for `level` (`pick` chooses among equals; nested lists are followed to depth
        /// 4); 0 when it names neither. Reads the actor index (main thread only).
        [[nodiscard]] FormId summonBase(FormId associated, std::uint16_t level, std::uint32_t pick);
        /// Task 0268: an actor a script's MoveTo made again (a created reference) speaks as its base does: the scene
        /// lines look the speaker up by reference. Main thread.
        void noteCreatedActor(FormId reference, FormId base) { mCreatedActorBases[reference] = base; }
        [[nodiscard]] FormId createdActorBase(FormId reference) const
        {
            const auto it = mCreatedActorBases.find(reference);
            return it != mCreatedActorBases.end() ? it->second : kNullFormId;
        }
        /// Task 0187: the quest of a scripted INFO (its QNAM, else its topic's); 0 for any other form.
        [[nodiscard]] FormId questOfInfo(FormId info) const;
        [[nodiscard]] FormId questOfScene(FormId scene) const; ///< task 0264
        /// Task 0187: the location (LCTN, by the cell's XLCN) and interior flag of a cell the view has indexed; false
        /// when the cell is not known. Main thread.
        [[nodiscard]] bool cellLocation(FormId cell, FormId& location, bool& interior) const;
        /// Task 0211: the location of a cell the view has indexed (its XLCN, else for an exterior the location whose
        /// cell list holds its grid square); kNullFormId for none or before the dialogue data is ready. Main thread.
        [[nodiscard]] FormId locationOfCell(FormId cell) const;
        /// Task 0276: where the player is, once a frame, for the scene conditions (GetDistance to an alias). Main thread.
        void setPlayerPosition(double x, double y, double z);
        /// Task 0276: is the location the same as `ancestor` or inside it (ObjectReference.IsInLocation)? False before
        /// the dialogue data is ready. Main thread.
        [[nodiscard]] bool locationWithin(FormId location, FormId ancestor) const;
        /// Task 0211: the location of a grid square of a worldspace (the cell XLCN when the data has the cell, else the
        /// location lists). Main thread.
        [[nodiscard]] FormId locationOfGridSquare(FormId world, std::int32_t x, std::int32_t y) const;
        /// Task 0211: where the player stands, for the alias filler's closest-first rule. Main thread.
        void setPlayerSite(std::function<QuestAliasFiller::PlayerSite()> site);
        /// Task 0230: a measurement switch. False leaves GetIsEditorLocAlias (fn_567) unanswered, as before the task;
        /// it must be set before the quest evaluator is built (the first dialogue or Papyrus use).
        void setAnswerEditorLocAlias(bool answer) { mAnswerEditorLocAlias = answer; }
        /// Task 0217: what scripts store on locations (Location.SetKeywordData; fn_606 and fn_651 read it). Owned by
        /// the view, so the Papyrus host and the condition worlds share it. Main thread.
        [[nodiscard]] LocationKeywordData& keywordData() { return mKeywordData; }
        /// Task 0217: the location the player stands in now (the site the driver reports); kNullFormId when unknown.
        [[nodiscard]] FormId playerLocation() const { return mPlayerSite ? mPlayerSite().mLocation : kNullFormId; }
        /// Task 0217: a developer's trace of the story manager's walks (applied now if the manager exists, else when
        // made).
        void setStoryTrace(StoryManager::Trace trace);
        /// Task 0217: what a save keeps of the story manager, the ambient quests that ran and the keyword data. The
        /// manager part is empty before the dialogue data is built.
        [[nodiscard]] StoryState storyState() const;
        /// Applies a loaded state; false (nothing applied) while the story manager does not exist yet: ask again.
        bool restoreStoryState(const StoryState& state);
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

        // ---- music (task 0209): the runtime reads the music records of every plugin on a worker ----
        music::MusicRuntime* musicRuntime() override;
        bool musicFileExists(const std::string& vfsPath) override;

    private:
        [[nodiscard]] const QuestAlias* findAliasByName(FormId quest, std::string_view aliasName) const;
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
            /// Task 0303: the LTMP of the latest source that has one (0 when none).
            FormId mLightingTemplate = kNullFormId;
            /// Task 0291: the interior water of the latest source that has any (`CellRecord::interiorWaterHeight`).
            std::optional<float> mInteriorWater;
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
            std::vector<ReferenceScripts> mScripts; ///< placements with VMAD scripts, by id (task 0176)
            /// Task 0293: placements that have an enable parent (XESP): child -> (parent, opposite of the parent).
            std::map<FormId, std::pair<FormId, bool>> mEnableParents;
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
        static void foldEnableParents(
            std::vector<openeden::sdk::game::PlacedObject>& objects, const Materialised& own,
            const Materialised* persistent);
        /// The LIGH data of a base form, if the base is a light that has some.
        [[nodiscard]] const LightData* lightOfBase(FormId base) const;

        Options mOptions;
        const FileSource* mFiles = nullptr;
        std::unique_ptr<music::MusicRuntime> mMusic; ///< task 0209; created by the first musicRuntime() call
        std::unique_ptr<WeatherIndex> mWeatherIndex; ///< built by the first weather ask; null before
        std::unique_ptr<WeatherTable> mWeatherTable; ///< over mWeatherIndex
        std::function<bool(const std::string&)> mWeatherProbe; ///< does a texture exist (set with the table)
        bool ensureWeather();
        std::unique_ptr<ActorIndex> mActorIndex; ///< built by the first describeActor; null before
        bool mStandInFemale = false;
        std::uint16_t mStandInPlayerLevel = 1; ///< kStandInPlayerLevel
        bool mHostileFactions = false; ///< task 0316: `combat hostile factions`
        bool mCreatures = false; ///< task 0212
        bool mDragons = false; ///< task 0253
        std::map<std::string, FormId> mNpcByEditorId; ///< task 0253: lower case, built by the first npcByEditorId
        bool mNpcByEditorIdBuilt = false;
        bool mHeldGear = false; ///< task 0220
        bool mPlayerBodyOn = false; ///< task 0222
        bool mFirstPersonBodyOn = false; ///< task 0245
        bool mBeastForm = false; ///< task 0259
        bool mVampireLook = false; ///< task 0279
        bool mPlayerLookSet = false; ///< task 0225
        FormId mPlayerRace = kNullFormId; ///< task 0225
        bool mPlayerFemale = false; ///< task 0225
        std::vector<std::pair<std::uint32_t, FormId>> mPlayerHeadChoice; ///< task 0225
        std::map<std::pair<std::uint32_t, int>, std::unique_ptr<StringTable>> mCharacterTables; ///< task 0225
        std::vector<FormId> mPlayerArmour; ///< task 0222: sorted
        FormId mPlayerWeapon = kNullFormId; ///< task 0222
        std::string mPlayerName; ///< task 0228
        std::uint32_t mPlayerNameRevision = 0;
        std::uint32_t mPlayerBodyRevision = 0; ///< task 0222: counts changes of the equipment
        std::uint8_t mStandInSpeech = 15; ///< kStandInSkill
        float mStandInGold = 100.f; ///< kStandInGold
        bool mHaveProgressionPlayer = false; // task 0200
        std::uint16_t mProgressionLevel = 1;
        std::array<std::uint8_t, progression::kSkillCount> mProgressionSkills{};
        std::function<float()> mGoldSource; // task 0184
        std::function<bool(FormId)> mWornSource; // task 0270
        std::function<float(FormId)> mItemSource; // task 0270
        bool ensureActorIndex();
        std::unique_ptr<CombatIndex> mCombatIndex; ///< WEAP, RACE combat fields and GMSTs of every plugin (task 0171)
        // Review fix of task 0171: the combat records (and the actor records, when nothing else has read them yet) are
        // read by ONE worker started by the first combatReady(); the main thread only adopts the result. State: 0 not
        // started, 1 reading, 2 read (waiting to be adopted), 3 adopted.
        std::thread mCombatThread;
        std::atomic<int> mCombatState{ 0 };
        std::unique_ptr<CombatIndex> mCombatBuilt;
        std::unique_ptr<ActorIndex> mActorBuilt;
        std::vector<Content::Error> mCombatErrors;
        std::uint64_t mCombatMillis = 0;
        struct DialogueData; ///< the dialogue and quest indexes (task 0141); defined in the .cpp
        struct DialogueState; ///< the data, the string tables and the runtime
        std::unique_ptr<DialogueState> mDialogue;
        std::function<std::optional<FormId>(FormId)> mCurrentPackageLookup;
        std::function<bool(FormId)> mVendorServicesLookup; ///< task 0189
        const FollowerOverlay* mFollowerOverlay = nullptr; ///< task 0239
        std::function<bool(FormId, double&, double&, double&)> mLiveActorPlace; ///< task 0277
        bool mMovedActorsOutdoors = false; ///< task 0329
        AliasLookup mAliasLookup; ///< task 0305
        AliasLookup mForcedAliasLookup; ///< task 0314
        std::function<std::uint32_t()> mAliasRevision;
        std::uint32_t mPackagesRevision = 0; ///< task 0277: moves when a quest's stage may change the alias packages
        ArrestDialogueHooks mArrest; ///< task 0258
        CrimeConditionSource mCrimeSource; ///< task 0258
        bool mCrimeSourceSet = false;
        FormId mForcedReference = kNullFormId; ///< the open conversation is the forced one with this actor
        ForcedGreetHooks mForcedGreet; ///< task 0264
        FormId mForcedGreetReference = kNullFormId; ///< task 0264: the open conversation is a ForceGreet package's
        bool mAliasPackagesOn = false; ///< task 0271
        SkyrimConditionWorld::LoadedLookup mLoadedLookup; ///< task 0330
        bool mJudgeWaterEarly = false; ///< task 0330
        std::set<FormId> mStartsDeadRefs; ///< task 0330: placements whose record starts them dead
        std::set<FormId> mTalkedTo; ///< task 0324: references whose conversation began (GetTalkedToPC for packages)
        bool mPrimitiveTriggers = false; ///< task 0310
        bool mAmbushOn = false; ///< task 0302
        bool mPlayerPlaceKnown = false;
        bool mPlayerInterior = false;
        FormId mPlayerCell = kNullFormId;
        FormId mPlayerWorld = kNullFormId;
        FormId mPackageTraceBase = kNullFormId;
        std::function<void(const std::string&)> mPackageTraceLog;
        bool mOpenServices = false; ///< task 0219
        bool mBlockingBranches = false; ///< task 0298
        std::function<bool(FormId)> mDeadLookup; ///< task 0326
        bool mQuestGreetings = false; ///< task 0304
        bool mWalkAwayTopics = false; ///< task 0315
        bool mWalkAwayInfos = false; ///< task 0326
        std::function<void(const std::string&)> mDialogueTrace;
        std::function<void()> mFragmentFlush;
        bool mHelloGreetings = false; ///< task 0308
        bool mDetectsPlayer = false; ///< task 0335
        std::set<FormId> mRankMinusOneMembers{ kJobInnkeeperFaction }; ///< task 0261, 0309
        std::function<QuestAliasFiller::PlayerSite()> mPlayerSite; ///< task 0211
        bool mAnswerEditorLocAlias = true; ///< task 0230: false leaves fn_567 unanswered (measurement only)
        LocationKeywordData mKeywordData; ///< task 0217
        StoryManager::Trace mStoryTrace; ///< task 0217
        static constexpr std::int32_t kLoadedRing = 2; ///< the engine loads the player square and two rings round it
        InfoFragmentSink mInfoFragmentSink; ///< task 0187
        bool mPackageWired = true;
        std::atomic<std::uint64_t> mPlacedRefsMillis{ 0 };
        void fillSpeakerCell(DialogueSpeaker& speaker, FormId cell) const; ///< interior flag and location of a cell
        bool ensureDialogue();
        void startDialoguePrefetch();
        [[nodiscard]] static std::unique_ptr<DialogueData> buildDialogueData(
            const std::vector<std::pair<std::filesystem::path, std::string>>& plugins, const LoadOrder& order,
            bool mapMarkers, bool loadingScreens);
        bool mWantLoadingScreens = false; ///< task 0207: read the LSCR records with the dialogue data
        std::map<std::uint32_t, std::unique_ptr<StringTable>> mMessageTables; ///< task 0224: by plugin and kind
        bool mHarvestPrompts = false; ///< task 0221: displayNameOf answers a plant's prompt
        bool mWantMapMarkers = false; ///< task 0201: read the map markers with the dialogue data
        std::thread mDialogueThread; ///< reads the conversation records after the first actor was described
        std::unique_ptr<DialogueData> mPrefetched; ///< the thread's result; valid once mDialogueReady is set
        std::atomic<bool> mDialogueReady{ false };
        LoadOrder mOrder;
        std::vector<PluginData> mPlugins;
        std::unordered_map<FormId, BaseRef> mBases;
        /// The VMAD scripts of a base form (null when it has none or is not a scripted kind). Task 0181.
        [[nodiscard]] const std::vector<vmad::Script>* baseScriptsOf(FormId base) const;
        std::unordered_map<FormId, CellEntry> mCells;
        std::map<FormId, WorldEntry> mWorlds;
        std::map<FormId, FormId> mCreatedActorBases; ///< task 0268: created actor reference -> its base
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
        std::unordered_map<FormId, std::array<float, 3>> mRefRotations; ///< task 0299: scripted placements only
        std::unordered_map<FormId, float> mPatrolIdles;
        MotionTable mMotion;
        bool mMotionTried = false;
        void ensureMotion();
        std::unordered_map<FormId, FormId> mActorCell;
        /// Markers per lower-case model path; an empty list for a model that has none or cannot be read.
        std::unordered_map<std::string, std::vector<FurnitureMarker>> mMarkerCache;
        const std::vector<FurnitureMarker>& markersOf(const std::string& model);
        std::optional<openeden::sdk::game::CellLighting> mLastLighting; ///< its interior lighting, if the cell has some
        /// Task 0291 (follow-up): what each interior cell loadCell returned says about its water plane (capped).
        std::map<openeden::sdk::game::WorldId, InteriorWaterEvidence> mWaterEvidence;
        static InteriorWaterEvidence gatherWaterEvidence(const std::vector<openeden::sdk::game::PlacedObject>& objects,
            const std::map<FormId, PlacementComponents>& components); ///< task 0330
        std::map<FormId, PlacementComponents> mLastComponents; ///< its placements' components, borrowed ones included
        std::unordered_map<FormId, openeden::sdk::game::OwnerComponent>
            mOwners; ///< task 0204: every loaded owned placement
        std::unordered_map<FormId, std::int32_t> mItemCounts; ///< task 0208: XCNT of every loaded placed stack
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
