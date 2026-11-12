#pragma once

#include <string>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// The name the PLAYER sees for an object (task 0150): the tooltip and the activation caption over an actor, a door or a
// container. WorldObject::baseEditorId is an editor id ("BanneredMareHulda"), not a display name; the display name is the
// base record's own name field (Skyrim SE: FULL, looked up in the string tables of a localised plugin).
//
// ABI choice: like ActorAppearanceSource and DialogueSource, a NEW interface found by dynamic_cast on the game's
// registered WorldCellSource, so the frozen WorldCellSource and WorldObject (1.x) are untouched. A pack that does not
// implement it just shows editor ids, as before.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a default body) raise the minor number.
// Threading: main thread only. Plain values only.

namespace openeden::sdk::game
{
    inline constexpr Version kDisplayNameSourceVersion{ 0, 1 };

    class DisplayNameSource
    {
    public:
        virtual ~DisplayNameSource() = default;

        /// The display name of the base form `base` (WorldObject::base). Empty when the base has no name, is unknown, or
        /// its name cannot be read; the core then uses the editor id.
        [[nodiscard]] virtual std::string displayNameOf(WorldId base) = 0;
    };
}
