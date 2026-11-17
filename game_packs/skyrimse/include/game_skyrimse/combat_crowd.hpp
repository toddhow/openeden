#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_CROWD_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_CROWD_H

// Task 0192 (combat on many actors, test rules only): which neutral actors fight in a crowd test.
//
// `combat test crowd = N` makes the N actors nearest to the player hostile to it; `combat test duels = M` makes the
// next 2M nearest fight each other in M pairs (each is the other's only enemy). The choice is pure: a list of the
// actors that are near, the player's place and the two counts give the same answer every time (distance, then
// placement id, then session break every tie), so a repeat run picks the same fighters. Nothing here reads a clock or
// a record. Standard library and the SDK's handle type only.

#include <openeden/game/actor_rules.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace game_skyrimse::Crowd
{
    /// Actors farther than this (horizontal game units) from the player never join a crowd test.
    inline constexpr double kMaxDistance = 3000.0;
    /// The most actors one crowd test picks (hostile plus duelists), a bound on what a setting can ask for.
    inline constexpr std::size_t kMaxPicked = 256;

    struct Candidate
    {
        openeden::sdk::game::ActorHandle mHandle;
        double mX = 0.0;
        double mY = 0.0;
    };

    struct Choice
    {
        std::vector<openeden::sdk::game::ActorHandle> mHostile; ///< hostile to the player
        /// Duelists, both directions: {a, b} and {b, a} are both listed (each is the other's opponent).
        std::vector<std::pair<openeden::sdk::game::ActorHandle, openeden::sdk::game::ActorHandle>> mOpponents;
    };

    /// Picks the `hostile` nearest candidates (within kMaxDistance of (px, py)) as hostile to the player, then pairs
    /// the next `2 * duels` nearest: walking them nearest first, each unpaired one is paired with the nearest unpaired
    /// other (to it), so two actors that stand side by side fight each other. A non-finite candidate or player place
    /// is skipped / gives nothing; counts are cut to kMaxPicked. The candidates are reordered.
    [[nodiscard]] Choice choose(
        std::vector<Candidate>& candidates, double px, double py, std::size_t hostile, std::size_t duels);
}

#endif
