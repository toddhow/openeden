#ifndef OPENMW_MWWORLD_NEUTRALFOLLOWERS_H
#define OPENMW_MWWORLD_NEUTRALFOLLOWERS_H

#include <cstddef>
#include <string>
#include <vector>

#include <components/esm/position.hpp>
#include <components/esm3/cellref.hpp>

#include "ptr.hpp"

// Followers of a game pack's neutral world (task 0196, milestone M-S7): the engine's half.
//
// The pack decides who follows the player (openeden::sdk::game::FollowerRules, found by dynamic_cast on the game's
// registered source). The engine's part is carrying them: when the player uses a teleport door, every follower that is
// in an active cell and near enough is moved to the destination cell, beside the arrival point, just before the
// player goes (the order Morrowind's ActionTeleport uses for its followers). `World::moveObject` keeps the same
// LiveCellRef (it only records that the reference now belongs to the new cell), so the physics and render entries stay
// valid; the cell the follower leaves is inactive for it, so the actor is taken out of the scene and the pack hears
// onActorRemoved, and when the destination loads the actor is added again and the pack's placement id is still the
// follower's.
//
// Main thread only.

namespace MWWorld
{
    /// How far from the player a follower may be and still go through the door with him (game units). An estimate.
    inline constexpr float kFollowerCarryRange = 1500.f;
    /// Where the carried followers stand: this far in front of the arrival point and spread sideways by
    /// kFollowerArrivalSpread each. Estimates.
    inline constexpr float kFollowerArrivalForward = 130.f;
    inline constexpr float kFollowerArrivalSpread = 110.f;
    /// The most followers carried at once (the pack's own limit is 16).
    inline constexpr std::size_t kMaxCarriedFollowers = 16;

    /// The positions of `count` carried followers around `arrival` (pure). The arrival's yaw is `rot[2]`, clockwise
    /// from +Y as the engine stores it. Follower i of n stands kFollowerArrivalForward in front of the arrival and
    /// (i - (n - 1) / 2) * kFollowerArrivalSpread to its right, with the arrival's z and facing. An empty result for
    /// count 0 or a position that is not finite.
    [[nodiscard]] std::vector<ESM::Position> planFollowerArrivals(const ESM::Position& arrival, std::size_t count);

    /// The live neutral actors of the active cells that the pack says follow the player and that are within
    /// kFollowerCarryRange of him and not dead, in ascending placement id (so the result does not depend on the order
    /// cells are held in). Asks the game's FollowerRules; empty when the game has none.
    [[nodiscard]] std::vector<Ptr> collectNeutralFollowers(
        const std::string& gameId, const Ptr& player, float range = kFollowerCarryRange);

    /// Moves the followers to the cell `cell` around `arrival` (the world's moveObject; the player is not touched) and
    /// tells the pack. Returns how many were moved.
    std::size_t carryNeutralFollowers(
        const std::string& gameId, const std::vector<Ptr>& followers, CellStore& cell, const ESM::Position& arrival);

    /// A script moves the player to `destination` (task 0218, fast travel): the followers of the player's current neutral
    /// cell go with him, collected and carried exactly as a teleport door does it. Call it before the player moves.
    /// Returns how many were carried; 0 when the player is not in a neutral cell or the game has no followers.
    std::size_t carryNeutralFollowersWithPlayer(CellStore& destination, const ESM::Position& arrival);

    /// After a load (the pack's chunks restored, the player's cell active): every follower the pack names that is not
    /// in an active cell is looked up in its home cell (FollowerRules::homeCell; an interior the pack can build) and
    /// moved to the player's cell around the player, as a door would have carried it. The neutral save does not keep
    /// actors (they are rebuilt from the pack), so this is what keeps a carried follower with the player. Returns how
    /// many were brought; one that cannot be found is logged. Main thread.
    std::size_t restoreNeutralFollowers();
}

#endif
