#ifndef OPENEDEN_GAME_SKYRIMSE_MAP_MARKERS_H
#define OPENEDEN_GAME_SKYRIMSE_MAP_MARKERS_H

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

#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>


#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::map
{
    inline constexpr float kDiscoveryRadius = 4096.f; ///< ours: one exterior cell
    inline constexpr float kHintRange = 8192.f; ///< ours: undiscovered markers appear on the compass inside this
    inline constexpr float kCompassRange = 16384.f; ///< ours: discovered markers appear on the compass inside this
    inline constexpr std::size_t kMaxMarkers = 16384;
    inline constexpr std::size_t kMaxDiscovered = 16384;
    inline constexpr std::size_t kMaxTargets = 64;
    inline constexpr std::size_t kPageSize = 256; ///< a binding array holds at most this many elements
    inline constexpr std::string_view kNamespace = "skyrimse.map";

    namespace MarkerFlags
    {
        inline constexpr std::uint8_t Visible = 0x01;
        inline constexpr std::uint8_t CanTravel = 0x02;
        inline constexpr std::uint8_t ShowAllHidden = 0x04;
    }

    struct Marker
    {
        FormId mRef = kNullFormId;
        FormId mWorld = kNullFormId; ///< the worldspace; none for an interior marker (not listed)
        float mX = 0.f, mY = 0.f, mZ = 0.f;
        std::uint8_t mFlags = 0;
        std::uint8_t mType = 0;
        LString mName;
        std::uint32_t mPlugin = 0; ///< index in the load order of the plugin whose record won (for the string table)
        bool mDeleted = false;
        /// The reference of the XLKR with a null keyword (task 0218): the place fast travel arrives at; 0 when none.
        FormId mLinked = kNullFormId;
    };

    struct MarkerLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint64_t mMaxMarkers = 1ull * 1024 * 1024;
        std::uint32_t mMaxErrors = 256;
    };

    struct MarkerRecords
    {
        std::string mName;
        std::vector<Marker> mMarkers;
        std::uint64_t mFailed = 0;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::vector<Content::Error> mErrors;
        std::optional<Content::Error> mWalkError;
    };

    /// Reads the map markers of one plugin: every REFR with an XMRK subrecord inside a worldspace's children group.
    /// False with `out.mWalkError` for a plugin that is not Skyrim SE's or whose walk failed.
    [[nodiscard]] bool parseMarkers(
        const Content::Plugin& plugin, const LoadOrder& order, const MarkerLimits& limits, MarkerRecords& out);

    /// The markers of a load order: a later record of the same reference replaces an earlier one, a deleted one removes
    /// it. Add plugins in load order (the plugin index is the position among the calls), then finish() once.
    class MarkerIndex
    {
    public:
        void add(MarkerRecords&& records);
        void finish();
        [[nodiscard]] const std::vector<Marker>& all() const { return mMarkers; } ///< sorted by reference id
        [[nodiscard]] const Marker* find(FormId reference) const;
        [[nodiscard]] std::size_t size() const { return mMarkers.size(); }
        [[nodiscard]] bool hasWorld(FormId world) const;

    private:
        std::vector<Marker> mMarkers;
        std::uint32_t mNextPlugin = 0;
    };

    /// The discovered markers. Not thread safe. Deterministic.
    class Discovery
    {
    public:
        [[nodiscard]] bool discovered(FormId marker) const;
        /// A marker is shown when its Visible flag is set or it is discovered.
        [[nodiscard]] bool shown(const Marker& marker) const
        {
            return (marker.mFlags & MarkerFlags::Visible) != 0 || discovered(marker.mRef);
        }
        /// Marks every marker of `world` whose horizontal distance to (x, y) is at most `radius` (a marker already
        /// shown by its flag is marked too, so that the chunk is the whole truth). Returns how many were new.
        std::size_t visit(const MarkerIndex& index, FormId world, float x, float y, float radius);
        /// The same over several worlds (a world and the parents it borrows its land from share coordinates).
        std::size_t visit(
            const MarkerIndex& index, std::span<const FormId> worlds, float x, float y, float radius);
        [[nodiscard]] std::size_t count() const { return mFound.size(); }
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }
        [[nodiscard]] const std::vector<FormId>& found() const { return mFound; } ///< ascending

        /// Replaces the state with a list of markers (sorted, deduplicated, capped).
        void restore(std::vector<FormId> found);

    private:
        std::vector<FormId> mFound; ///< ascending
        std::uint64_t mRevision = 0;
    };

    /// One drawn compass or map marker, in the order a query answers.
    struct View
    {
        const Marker* mMarker = nullptr;
        float mDistance = 0.f;
        bool mDiscovered = false;
    };

    /// The markers of `world` for a script: shown ones within `range` of (x, y) and undiscovered ones within
    /// `hintRange`, nearest first (ties by reference id), at most `count` from `skip` on. Pure.
    void query(const MarkerIndex& index, const Discovery& state, FormId world, float x, float y, float range,
        float hintRange, std::size_t skip, std::size_t count, std::vector<View>& out);
    /// The same over several worlds.
    void query(const MarkerIndex& index, const Discovery& state, std::span<const FormId> worlds, float x, float y,
        float range, float hintRange, std::size_t skip, std::size_t count, std::vector<View>& out);

}

#endif
