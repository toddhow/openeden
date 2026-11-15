#ifndef OPENMW_MWWORLD_NEUTRALINVENTORY_H
#define OPENMW_MWWORLD_NEUTRALINVENTORY_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/game/world_model.hpp>

// Opening a Skyrim container's window (task 0182, milestone M-S6): the pure part.
//
// Activating a container does not open a MyGUI window. The engine only tells the pack's Lua scripts that a container (or,
// later, a corpse) was activated, with ids as hex text; the pack owns what is inside, the player script draws it. The
// ids are text because a load-order id of a plugin from the eighth on does not fit a Lua integer-safe 32 bit value and a
// script event carries numbers as doubles and strings only.

namespace MWWorld
{
    /// The name of the event the global script `scripts/openeden/skyrimse/inventory_global.lua` listens to.
    inline constexpr std::string_view kNeutralInventoryEvent = "openeden_skyrimse_container_opened";

    /// What to do when an object of a neutral cell is activated.
    enum class NeutralInventoryDecision
    {
        NotApplicable, ///< not a container, or the player did not activate it, or the setting is off: as before
        Open, ///< tell the scripts
    };

    [[nodiscard]] NeutralInventoryDecision decideNeutralInventory(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn);

    /// An id as lower-case hex of at least 8 digits (the form the pack's bindings read).
    [[nodiscard]] std::string neutralHexId(std::uint64_t id);

    struct NeutralInventoryEventField
    {
        std::string mName;
        std::string mText;
    };

    /// The fields of the event: placement, base, kind ("container") and name, in that order.
    [[nodiscard]] std::vector<NeutralInventoryEventField> neutralInventoryEventFields(
        std::uint64_t placement, std::uint64_t base, std::string_view kind, std::string_view name);
}

#endif
