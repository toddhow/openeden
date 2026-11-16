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
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerItemSourceVersion{ 0, 1 };

    class PlayerItemSource
    {
    public:
        virtual ~PlayerItemSource() = default;

        /// True when the player holds at least one of the item (a base form id, WorldId as the pack's own ids).
        [[nodiscard]] virtual bool playerHasItem(WorldId item) const = 0;
    };
}
