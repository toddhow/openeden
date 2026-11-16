#pragma once

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// Does the PLAYER carry an item (task 0184, milestone M-S6). The engine decides a lock (a key opens a door or a
// container) and the pack owns the inventory, so the engine asks the pack through this interface: a key held in the
// inventory satisfies the lock's key check, as a key from the session's key ring does.
//
// ABI choice: like DisplayNameSource, a NEW interface found by dynamic_cast on the game's registered
// WorldCellSource, so the frozen WorldCellSource (1.x) is untouched. A pack that does not implement it, or has no inventory switched on,
// answers false (the key ring alone decides, as before).
//
// Task 0203 (version 0.2): two more trailing virtuals with default bodies, for lock picking: whether the pack lets the
// player pick locks, and which locks it picked (the engine then treats them as open).
//
// Version policy: DRAFT 0.2. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerItemSourceVersion{ 0, 2 };

    class PlayerItemSource
    {
    public:
        virtual ~PlayerItemSource() = default;

        /// True when the player holds at least one of the item (a base form id, WorldId as the pack's own ids).
        [[nodiscard]] virtual bool playerHasItem(WorldId item) const = 0;

        /// Task 0203: true when the pack runs the lock picking minigame; the engine then offers it for a lock the player
        /// has no key for instead of refusing. Default: false (the refusal, as before).
        [[nodiscard]] virtual bool lockpickingOn() const { return false; }

        /// Task 0203: true when the player picked the lock of this placement (a placement's WorldId); the engine treats
        /// such a lock as open. Default: false.
        [[nodiscard]] virtual bool lockPicked(WorldId placement) const
        {
            (void)placement;
            return false;
        }
    };
}
