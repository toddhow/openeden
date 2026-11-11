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
// Main thread only, deterministic (the same plugins give the same lists and the same objects), and nothing throws out
// of it: failures are located Content::Error values or the `error` text of loadCell.

#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/records.hpp>

#include <openeden/game/world_view.hpp>
#include <openeden/vfs.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    /// Where files that are not plugins (the string tables) come from. `path` is lower case with backslashes, as an
    /// archive names it (`strings\skyrim_english.strings`).
    class FileSource
    {
    public:
        virtual ~FileSource() = default;
        [[nodiscard]] virtual bool read(std::string_view path, std::string& bytes) const = 0;
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

    private:
        const openeden::sdk::VfsService& mVfs;
    };

    class SkyrimWorldView final : public openeden::sdk::game::WorldView, public ComponentSource
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

        [[nodiscard]] const Stats& stats() const { return mStats; }
        /// The string table language in use.
        [[nodiscard]] const std::string& language() const { return mOptions.mLanguage; }
        /// Located problems found while indexing and reading (capped).
        [[nodiscard]] const std::vector<Content::Error>& errors() const { return mErrors; }

        // ---- openeden::sdk::game::WorldView ----
        /// The door-to-cell index (reference id to the id of the cell it stands in), for teleport doors only.
        [[nodiscard]] const RefCellIndex& teleportDoorCells() const { return mRefCells; }

        /// The owner, lock and teleport of a placement of the cell the LAST loadCell returned (own placements and the
        /// world's persistent ones it borrowed). Null for any other cell or placement. Main thread only.
        [[nodiscard]] const PlacementComponents* find(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldId placement) const override;

        [[nodiscard]] std::string_view gameId() const override { return "skyrimse"; }
        [[nodiscard]] std::size_t cellCount() const override { return mCellOrder.size(); }
        [[nodiscard]] std::size_t worldCount() const override { return mWorldOrder.size(); }
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

        void noteError(Content::Error error);
        void indexPlugin(std::uint32_t pluginIndex);
        void resolveNames(PluginData& plugin, const StringTable* table);
        [[nodiscard]] std::shared_ptr<const Materialised> materialise(FormId cell, std::string& error);
        [[nodiscard]] bool readCellRefs(
            const CellSource& source, const CellEntry& entry, std::vector<PlacedRef>& out, std::string& error);
        [[nodiscard]] openeden::sdk::game::PlacedObject makeObject(const PlacedRef& ref, bool& baseMissing) const;

        Options mOptions;
        const FileSource* mFiles = nullptr;
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
        std::map<FormId, PlacementComponents> mLastComponents; ///< its placements' components, borrowed ones included
        std::vector<Content::Error> mErrors;
        Stats mStats;
        bool mFinished = false;
    };
}

#endif
