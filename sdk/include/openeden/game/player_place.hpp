#pragma once

#include <cstdint>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// Where the player stands in a game's world, told to the game (task 0211, milestone M-S6 "story events"). The game's
// story manager raises "change location" events when the player moves into another location; for an exterior the pack
// is not told which cell the player is in (the core owns the player), so the core reports the player's PLACE whenever
// it changes: an interior cell, or a grid square of a worldspace.
//
// ABI choice: like SceneSpeechSource, a NEW interface found by dynamic_cast on the game's registered ActorRules, so the
// frozen ActorRules / ActorRequestWriter are untouched. A pack that does not implement it is never told.
//
// Version policy: DRAFT 0.1. Additive changes raise the minor number. Threading: main thread only, from the core's
// world update (before the pack's actor update of the same frame). Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerPlaceVersion{ 0, 1 };

    struct PlayerPlace
    {
        /// True: the player is inside the interior cell `cell`; false: outside, on the grid square (gridX, gridY) of
        /// `world`.
        bool interior = false;
        /// The worldspace of an exterior place (kNoWorldId for an interior).
        WorldId world = kNoWorldId;
        /// The pack's id of an interior cell (kNoWorldId for an exterior).
        WorldId cell = kNoWorldId;
        std::int32_t gridX = 0;
        std::int32_t gridY = 0;
    };

    inline bool operator==(const PlayerPlace& a, const PlayerPlace& b)
    {
        return a.interior == b.interior && a.world == b.world && a.cell == b.cell && a.gridX == b.gridX
            && a.gridY == b.gridY;
    }

    /// Implemented by an ActorRules whose game reacts to the player's place.
    class PlayerPlaceSink
    {
    public:
        virtual ~PlayerPlaceSink() = default;

        /// The player's place changed (also once at the start, and after a load). Never called twice in a row with an
        /// equal place.
        virtual void onPlayerPlace(const PlayerPlace& place) = 0;
    };
}
