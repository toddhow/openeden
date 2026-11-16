#pragma once

#include <cstddef>
#include <cstdint>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// Followers (task 0196, milestone M-S7): an actor the player has recruited keeps near the player, goes with the player
// through teleport doors and fights on the player's side. The pack owns who follows (the rules: recruiting,
// dismissing, what a follower does); the core only needs three answers from it.
//
// ABI choice: like ActorRules, a NEW interface found by dynamic_cast on the game's registered WorldCellSource, so the
// frozen WorldCellSource is untouched and a pack without followers simply does not implement it. No SDK major or minor
// change: the SDK stays 4.0.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default) raise the minor
// number. Threading: main thread only, called from the core's door action, the physics update and the load.
//
// Followers are named by the placement id of the actor (WorldObject::id), the same id ActorHandle carries. The id
// stays the same when the core moves the actor to another cell, so a follower is the same follower after a door.

namespace openeden::sdk::game
{
    inline constexpr Version kFollowerRulesVersion{ 0, 1 };

    /// Whose side an actor fights on, as far as the follower rules know. Combat reads it; the combat AI itself is not
    /// part of this interface.
    enum class FollowerSide : std::uint8_t
    {
        None = 0, ///< not a follower: whatever side the actor's own rules give it
        Player = 1, ///< a follower: allied to the player, an enemy of the player's enemies
    };

    class FollowerRules
    {
    public:
        virtual ~FollowerRules() = default;

        /// Writes the placement ids of the current followers (ascending, at most `capacity`) to `out` and returns how
        /// many there are (which may be more than `capacity`). The core carries these with the player through a door.
        [[nodiscard]] virtual std::size_t followers(WorldId* out, std::size_t capacity) const = 0;

        /// True when the actor of this placement follows the player.
        [[nodiscard]] virtual bool isFollower(WorldId reference) const = 0;

        /// The combat hook: the side of an actor. The default follows isFollower.
        [[nodiscard]] virtual FollowerSide sideOf(WorldId reference) const
        {
            return isFollower(reference) ? FollowerSide::Player : FollowerSide::None;
        }

        /// The cell the follower's placement lives in (its home), or kNoWorldId when the pack does not know. After a
        /// load the core finds a follower that is not beside the player in that cell and brings it to the player.
        [[nodiscard]] virtual WorldId homeCell(WorldId reference) const
        {
            (void)reference;
            return kNoWorldId;
        }

        /// The core moved a follower to another cell (a door the player used). Informational: the actor is removed and
        /// added again by the actor hooks, and the pack keeps it a follower by its placement id. Default ignores it.
        virtual void onFollowerCarried(WorldId reference)
        {
            (void)reference;
        }
    };
}
