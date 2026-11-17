#ifndef OPENMW_MWWORLD_NEUTRALINVENTORY_H
#define OPENMW_MWWORLD_NEUTRALINVENTORY_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/game/world_model.hpp>

// Opening a Skyrim container's window (task 0182, milestone M-S6): the pure part.
//
// Activating a container does not open a MyGUI window. The engine only tells the pack's Lua scripts that a container
// (or, later, a corpse) was activated, with ids as hex text; the pack owns what is inside, the player script draws it.
// The ids are text because a load-order id of a plugin from the eighth on does not fit a Lua integer-safe 32 bit value
// and a script event carries numbers as doubles and strings only.

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

    /// Task 0184: a dead actor the player activates opens the loot window (the pack's `actor` owner) instead of a
    /// conversation. Only the player, only a dead Actor, only with the setting on.
    [[nodiscard]] NeutralInventoryDecision decideNeutralLoot(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn, bool dead);

    /// Task 0184: a lock's key is held when the session's key ring has it or the pack's inventory does (the optional
    /// lookup may be empty). Key 0 is no key. The inventory is only asked when the ring says no.
    [[nodiscard]] bool neutralKeyHeld(openeden::sdk::game::WorldId key,
        const std::function<bool(openeden::sdk::game::WorldId)>& keyRing,
        const std::function<bool(openeden::sdk::game::WorldId)>& inventory);

    /// Task 0208: a placed item the player activates is picked up (the pack's scripts do it), and a living actor a
    /// sneaking player activates has its pockets picked. Both only with the setting on; the pack decides what is an
    /// item.
    inline constexpr std::string_view kNeutralPickupEvent = "openeden_skyrimse_item_activated";
    inline constexpr std::string_view kNeutralPickpocketEvent = "openeden_skyrimse_pickpocket_opened";
    [[nodiscard]] NeutralInventoryDecision decideNeutralPickup(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn);
    [[nodiscard]] NeutralInventoryDecision decideNeutralPickpocket(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn, bool dead, bool sneaking);

    /// Task 0221: a placed flora or tree the player activates is harvested (the pack's scripts do it: the pack decides
    /// from the base whether it names an ingredient). Only with `[Game] neutral inventory`.
    inline constexpr std::string_view kNeutralHarvestEvent = "openeden_skyrimse_harvest_activated";
    [[nodiscard]] NeutralInventoryDecision decideNeutralHarvest(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn);
    /// Whether an object of this kind shows a tooltip (its name, or the pack's prompt) when the player looks at it:
    /// actors, doors and containers always, furniture, items, flora and trees with the neutral inventory on.
    [[nodiscard]] bool neutralKindHasToolTip(openeden::sdk::game::ObjectKind kind, bool inventorySettingOn);

    /// Task 0224: a placed Activator the player activates (an ore vein: the pack's scripts decide from the base whether
    /// it is a resource; nothing happens when none listens). Only with `[Game] neutral inventory`. A Furniture (the
    /// chopping block) already sends the bench event, which the mining script also answers.
    inline constexpr std::string_view kNeutralResourceEvent = "openeden_skyrimse_resource_activated";
    [[nodiscard]] NeutralInventoryDecision decideNeutralResource(
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

    /// Task 0189: the merchant's barter line (a dialogue turn whose service is "barter") opens the barter window
    /// instead of the conversation box when the neutral inventory is on.
    inline constexpr std::string_view kNeutralBarterEvent = "openeden_skyrimse_barter_opened";
    [[nodiscard]] bool decideNeutralBarter(std::string_view service, bool settingOn);

    /// Task 0219: a dialogue turn whose service is "open:training", "open:rent" or "open:gift" (the pack marks these
    /// when [extension.skyrimse] services is on) opens that service's window instead of the conversation box when the
    /// neutral inventory is on. The result is the bare service name ("training", "rent" or "gift"), "" for anything
    /// else (barter has its own event).
    inline constexpr std::string_view kNeutralServiceEvent = "openeden_skyrimse_service_opened";
    [[nodiscard]] std::string decideNeutralService(std::string_view service, bool settingOn);
    /// The fields of the service event: the service, the speaker's placement and base (hex text) and its name.
    [[nodiscard]] std::vector<NeutralInventoryEventField> neutralServiceEventFields(
        std::string_view service, std::uint64_t placement, std::uint64_t npcBase, std::string_view name);

    /// Task 0199: activating a Furniture opens the crafting window when the neutral inventory is on. The pack decides
    /// from the furniture's base whether it is a bench (the engine knows no keywords); the event carries the fields of
    /// `neutralInventoryEventFields` with kind "bench".
    inline constexpr std::string_view kNeutralBenchEvent = "openeden_skyrimse_bench_opened";
    [[nodiscard]] NeutralInventoryDecision decideNeutralBench(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn);
    /// The fields of the barter event: the speaker's placement and base (hex text) and its name, in that order.
    [[nodiscard]] std::vector<NeutralInventoryEventField> neutralBarterEventFields(
        std::uint64_t placement, std::uint64_t npcBase, std::string_view name);

    /// Task 0203: a locked door or container the player activates offers the pack's lock picking minigame (an event to
    /// the scripts) instead of the refusal, when `[Game] neutral inventory` and `[GUI] rmlui` are on (the window needs
    /// both), the pack runs lock picking and the lock is locked (no key held). Anything else keeps the refusal.
    inline constexpr std::string_view kNeutralLockpickEvent = "openeden_skyrimse_lock_activated";
    [[nodiscard]] NeutralInventoryDecision decideNeutralLockpick(openeden::sdk::game::ObjectKind kind, bool byPlayer,
        bool inventorySettingOn, bool rmluiOn, bool packLockpickingOn, bool locked);
    /// The fields of the lock event: placement and base (hex text), kind ("door" or "container"), level (decimal text,
    /// the raw XLOC level) and name, in that order.
    [[nodiscard]] std::vector<NeutralInventoryEventField> neutralLockpickEventFields(
        std::uint64_t placement, std::uint64_t base, std::string_view kind, std::int32_t level, std::string_view name);
    /// A lock is open when the session's state says so or the pack says the player picked it (asked only then).
    [[nodiscard]] bool neutralLockAlreadyOpen(bool sessionOpen,
        const std::function<bool(openeden::sdk::game::WorldId)>& packPicked, openeden::sdk::game::WorldId placement);
}

#endif
