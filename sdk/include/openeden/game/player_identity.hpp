#pragma once

#include <cstdint>
#include <string>

#include <openeden/version.hpp>

// The player's own name (task 0228): the pack lets the player choose a name (Skyrim SE's character creation), and the
// core writes it into the player's record so the save list, the HUD and the dialogue tags show it. The player is not a
// world object (the core makes him from a stub record), so the pack describes him through its own optional interface,
// found by dynamic_cast on the game's registered WorldCellSource (as PlayerBodySource is). The core POLLS: it asks
// every frame, the call never blocks, and it changes the record only when `revision` changed.
//
// Version policy: DRAFT 0.1. Additive changes (a trailing virtual with a default, a trailing field the core only
// reads) raise the minor number. Threading: main thread only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerIdentityVersion{ 0, 1 };

    struct PlayerIdentityState
    {
        /// 0 while the pack has no name for the player (the creation is not done, or the setting is off); otherwise a
        /// number that changes whenever the name changes (a finished creation, a loaded save).
        std::uint32_t revision = 0;
        /// The name, valid when `revision` is not 0. Plain text, never empty then.
        std::string name;
    };

    class PlayerIdentitySource
    {
    public:
        virtual ~PlayerIdentitySource() = default;

        /// Cheap and non-blocking. False when the pack has no player identity (the setting is off).
        [[nodiscard]] virtual bool playerIdentity(PlayerIdentityState& out) = 0;
    };
}
