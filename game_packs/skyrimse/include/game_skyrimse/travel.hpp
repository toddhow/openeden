#ifndef OPENEDEN_GAME_SKYRIMSE_TRAVEL_H
#define OPENEDEN_GAME_SKYRIMSE_TRAVEL_H

// Fast travel from the world map in a Skyrim SE session (task 0218, milestone M-S7). Off unless
// [extension.skyrimse] fast travel (which also needs [extension.skyrimse] map, the markers and the discovered state).
//
// The pack's side: when the player may travel, where the travel ends, how long it takes, and the script package
// `core.skyrimse.travel` that the Lua world map window and the global travel script read. The teleport itself and the
// passing of the game clock are done by a global script (`player:teleport`, `world.advanceTime`); the engine carries the
// player's followers with a script teleport the way it does with a door (mwworld/neutralfollowers.*).
//
// What the data settles (docs/tasks/0218-skyrim-fast-travel.md section 2, Skyrim.esm):
//  - 353 of the 397 map markers carry an XLKR of 8 bytes: a null keyword and a reference. The reference is a static
//    "landing" place next to the marker (a REFR whose editor id often says MapLandingMarker); the median distance from
//    the marker is 1,873 units (25 to 8,770). The 44 markers with no XLKR have no landing.
//  - The marker FNAM flags are 0, 1 or 3 in the data; bit 1 (can travel to) is set in 11 only, so a travel destination is
//    a marker that is discovered or has that bit. RECALLED-VERIFY: Skyrim sets "visible" and "can travel to" when the
//    player discovers a marker.
// RECALLED-VERIFY: that Skyrim itself arrives at the marker's linked reference (UESP, the CK's "Fast Travel Marker"
// text). Where there is none the arrival is the marker's own position.
//
// What is ours (no rule in the data): one game hour per kUnitsPerGameHour units of straight line distance, at least 1
// and at most kMaxTravelHours hours; the arrival faces the marker it belongs to.
//
// Threading: main thread only (the bindings' Read handlers run on the thread that runs scripts, the main thread
// today; the Mutate call runs there in call order). No engine, renderer or script-language type.

#include <game_skyrimse/map.hpp>
#include <game_skyrimse/map_markers.hpp>
#include <game_skyrimse/placed_refs.hpp>

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace game_skyrimse::travel
{
    inline constexpr std::string_view kNamespace = "skyrimse.travel";
    /// Straight line distance covered by one game hour of travel. Ours.
    inline constexpr double kUnitsPerGameHour = 9000.0;
    inline constexpr int kMinTravelHours = 1;
    inline constexpr int kMaxTravelHours = 48;

    /// Why the player may not travel (the order is the order the checks run in).
    enum class Refusal : std::uint8_t
    {
        None,
        NotReady, ///< the map's or the combat's data is not read yet
        Interior, ///< the player stands in an interior (Skyrim: no fast travel indoors)
        UnknownMarker, ///< no such marker in the world the player stands in
        NotDiscovered, ///< the marker is neither discovered nor marked "can travel to"
        InCombat, ///< the player was in combat a moment ago
        EnemiesNear, ///< a hostile is alive and engaged
        NoDestination, ///< no place to arrive at (the marker has no usable position)
    };
    /// "" for None, else a lower-case word: "not_ready", "interior", "unknown", "undiscovered", "combat", "enemies",
    /// "no_destination".
    [[nodiscard]] std::string_view refusalName(Refusal refusal);

    struct Inputs
    {
        bool mReady = false;
        bool mInterior = false;
        bool mKnown = false;
        bool mDiscovered = false;
        bool mInCombat = false;
        bool mEnemiesNear = false;
        bool mHasDestination = false;
    };
    /// Pure: the first reason that holds.
    [[nodiscard]] Refusal decideTravel(const Inputs& inputs);

    /// Game hours a straight line distance takes: ceil(distance / kUnitsPerGameHour), cut to 1 to kMaxTravelHours; a
    /// distance that is not finite (or below 0) is the minimum. Pure.
    [[nodiscard]] int travelHours(double distance);

    /// The heading from (fromX, fromY) to (toX, toY) in the engine's convention (0 faces +Y, clockwise, radians in
    /// (-pi, pi]); 0 for two equal points or numbers that are not finite. Pure.
    [[nodiscard]] double yawToward(double fromX, double fromY, double toX, double toY);

    struct Destination
    {
        float mX = 0.f, mY = 0.f, mZ = 0.f;
        double mYaw = 0.0;
        bool mFromLink = false; ///< the marker's linked reference gave the place
    };
    /// Where the travel to `marker` ends: the position of `link` when it is given and `linkUsable` (it stands in the
    /// marker's world, outdoors), else the marker's own position. The heading faces the marker from a landing place
    /// (0 when the arrival is the marker). Pure.
    [[nodiscard]] Destination chooseDestination(const map::Marker& marker, const PlacedEntry* link, bool linkUsable);

    struct Plan
    {
        Destination mWhere;
        double mDistance = 0.0; ///< from where the player stands
        int mHours = kMinTravelHours;
    };

    struct Hooks
    {
        std::function<bool()> mReady; ///< the combat's data is read
        std::function<bool()> mInCombat; ///< the player fought a moment ago
        std::function<bool()> mEnemiesNear; ///< a live hostile is engaged
        std::function<std::size_t()> mFollowerCount; ///< how many followers the pack has (a trace; the engine carries them)
        std::function<void(const std::string&)> mLog;
    };

    class TravelService
    {
    public:
        /// `map` must outlive the service (it is the pack's map service).
        TravelService(map::MapService& map, Hooks hooks);
        ~TravelService();
        TravelService(const TravelService&) = delete;
        TravelService& operator=(const TravelService&) = delete;

        /// The destination of `marker` seen from (fromX, fromY): false when the marker is unknown.
        [[nodiscard]] bool plan(FormId world, FormId marker, double fromX, double fromY, Plan& out);
        [[nodiscard]] Refusal refusal(FormId world, bool interior, FormId marker, double fromX, double fromY);
        /// The Mutate call: refused (counted, logged) unless travel is allowed now; otherwise counted, logged and the
        /// arrival's neighbourhood is discovered. Returns whether it ran.
        bool commit(FormId world, bool interior, FormId marker, double fromX, double fromY);

        // ---- task 0223: travel to a placed reference (Game.FastTravel of the carriage drivers' Papyrus) -------------
        /// The arrival at the placed reference `destination` (an exterior REFR, e.g. a CarriageDestinationMarker) seen
        /// from (fromX, fromY): its position, facing the way the player travels. False for no such reference, a deleted
        /// one, one in an interior, or one outside the player's world (`world`) and its parents.
        [[nodiscard]] bool planToRef(FormId world, FormId destination, double fromX, double fromY, Plan& out);
        [[nodiscard]] Refusal refusalToRef(FormId world, bool interior, FormId destination, double fromX, double fromY);
        /// Like commit: refused (counted, logged) unless travel is allowed now.
        bool commitToRef(FormId world, bool interior, FormId destination, double fromX, double fromY);
        /// Game.FastTravel(destination): remembers the request for the global travel script (which reads
        /// `request_seq` and `request_ref`). False for a null form id. A later request replaces an earlier unread one.
        bool queueRequest(FormId destination);
        [[nodiscard]] std::uint32_t requestSequence() const { return mRequestSeq; }
        [[nodiscard]] FormId requestedRef() const { return mRequested; }
        [[nodiscard]] std::size_t performed() const { return mPerformed; }
        [[nodiscard]] std::size_t refused() const { return mRefused; }

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        map::MapService& mMap;
        Hooks mHooks;
        std::size_t mPerformed = 0;
        std::size_t mRefused = 0;
        std::uint32_t mRequestSeq = 0;
        FormId mRequested = kNullFormId;
    };
}

#endif
