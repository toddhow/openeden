#ifndef OPENEDEN_GAME_SKYRIMSE_MAP_H
#define OPENEDEN_GAME_SKYRIMSE_MAP_H

// The compass and the world map of a Skyrim SE session (task 0201, milestone M-S7). Off unless [extension.skyrimse] map.
//
// This file is the pack's side: the map markers of the data, the discovered state, the quest targets of the displayed
// objectives, and the script package `core.skyrimse.map` that the Lua compass and world map windows read. The windows
// themselves are Lua on the RmlUi library (files/data/openeden/skyrimse/compass.lua, worldmap.lua).
//
// Layouts (settled by the data, docs/tasks/0201-skyrim-map-and-compass.md section 3):
//  - A map marker is a REFR (base 0x10, the MapMarker static) whose subrecords include the empty XMRK, then FNAM (1 byte
//    of flags), FULL (a string id in a localised plugin) and TNAM (2 bytes: the marker type, then a byte that is 0 in
//    all 397 markers of the five masters). DATA is the usual 24 byte position and rotation. 362 of the 397 sit in a
//    worldspace's persistent cell, 35 in interiors (their world is none: they are not exterior markers).
//  - FNAM values in the data: 0 (377), 1 (9), 3 (11), 4 (3, Dawnguard). Names RECALLED-VERIFY (xEdit): bit 0 visible,
//    bit 1 can travel to, bit 2 show all hidden. This pack reads only bit 0: a marker with it set is shown from the
//    start, any other is shown once discovered. The type numbers are kept as stored (0 to 58 in the data); the names
//    of the types are not decoded.
//
// What is ours (the data has no rule for it): a marker is discovered when the player comes within kDiscoveryRadius
// (horizontal units) of it; undiscovered markers are hinted on the compass within kHintRange; the compass reaches
// kCompassRange. All three are named constants here and tunable.
//
// Threading: the parse runs on the dialogue worker; everything else on the main thread (the script bindings' Read
// handlers run on the thread that runs scripts, which is the main thread today, and never overlap the pack's phases).
// No engine, renderer or script-language type.

#include <game_skyrimse/journal.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/map_markers.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::map
{
    /// A quest target: where a displayed, not completed objective points.
    struct Target
    {
        FormId mQuest = kNullFormId;
        std::uint16_t mObjective = 0;
        FormId mWorld = kNullFormId; ///< the worldspace of an exterior target; none for an interior one
        FormId mCell = kNullFormId;
        float mX = 0.f, mY = 0.f, mZ = 0.f;
        std::string mText;
    };

    struct TargetInputs
    {
        const QuestIndex* mQuests = nullptr;
        const QuestState* mState = nullptr;
        const PlacedRefIndex* mPlaced = nullptr;
        journal::ObjectiveFn mObjective;
        journal::TextFn mText;
        /// The worldspace of a cell, and whether it is an interior; false when unknown.
        std::function<bool(FormId cell, FormId& world, bool& interior)> mCellWorld;
    };

    /// The targets of every running quest, in quest id then objective then target order, at most kMaxTargets. A target
    /// names an alias: a specific-reference alias or a unique actor (its first placed reference that starts enabled)
    /// gives the position; an alias nothing fills gives none. Pure.
    void buildTargets(const TargetInputs& inputs, std::vector<Target>& out);

    // ---- the save chunk -------------------------------------------------------------------------------------------

    /// The pack's chunk "map" (version 1): u32 count (at most kMaxDiscovered), then the ascending marker form ids.
    class MapChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "map";
        explicit MapChunk(Discovery& state)
            : mState(state)
        {
        }
        void setRestoredSink(std::function<void(const std::string&)> sink) { mRestored = std::move(sink); }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        Discovery& mState;
        std::function<void(const std::string&)> mRestored;
        std::function<void(const std::string&)> mSink;
    };

    // ---- the service ------------------------------------------------------------------------------------------------

    struct Access
    {
        const MarkerIndex* mMarkers = nullptr;
        const PlacedRefIndex* mPlaced = nullptr;
        const QuestIndex* mQuests = nullptr;
        const QuestState* mState = nullptr;
        journal::TextFn mText;
        std::function<bool(FormId cell, FormId& world, bool& interior)> mCellWorld;
        /// Task 0223: the exterior world an interior cell (by editor id) belongs to by its location; 0 when unknown.
        std::function<FormId(std::string_view editorId)> mInteriorWorld;
        /// The parent worldspace of a world that borrows its land (0 for none): a world with no markers of its own uses
        /// its parent's.
        std::function<FormId(FormId world)> mParentOf;
        /// One land row of the world map: pixels (r, g, b packed as 0xRRGGBB) of the cells minX..minX+width-1 at row y,
        /// -1 for a cell with no land. `bounds` is filled by the first call (cell grid minimum and size). Empty when the
        /// terrain is not reachable.
        std::function<bool(FormId world, std::int32_t& minX, std::int32_t& minY, std::int32_t& width,
            std::int32_t& height)>
            mLandBounds;
        std::function<bool(FormId world, std::int32_t y, std::vector<std::int32_t>& pixels)> mLandRow;
    };

    struct Hooks
    {
        std::function<bool(Access& out)> mAccess;
        journal::ObjectiveFn mObjective;
        std::function<std::uint64_t()> mObjectiveRevision;
        std::function<void(const std::string&)> mLog;
    };

    class MapService
    {
    public:
        explicit MapService(Hooks hooks);
        ~MapService();
        MapService(const MapService&) = delete;
        MapService& operator=(const MapService&) = delete;

        bool poll();
        [[nodiscard]] bool ready() const { return mReady; }
        [[nodiscard]] Discovery& discovery() { return mDiscovery; }
        [[nodiscard]] const Discovery& discovery() const { return mDiscovery; }
        /// Discovery, quest state and objective flags combined (0 before ready).
        [[nodiscard]] std::uint64_t revision() const;
        /// The targets, rebuilt first when the quest state or the objectives changed.
        [[nodiscard]] const std::vector<Target>& targets();
        /// The worlds whose markers answer for `world`: itself and the parents it borrows its land from (they share
        /// coordinates), at most four.
        [[nodiscard]] std::vector<FormId> markerWorlds(FormId world) const;
        /// Marks what is near (x, y) in `world` (the Mutate binding `visit`). Returns how many markers were new.
        std::size_t visit(FormId world, float x, float y);
        [[nodiscard]] const Access& access() const { return mAccess; }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        Access mAccess;
        Discovery mDiscovery;
        std::vector<Target> mTargets;
        std::uint64_t mTargetsRevision = ~0ull;
        bool mReady = false;
    };
}

#endif
