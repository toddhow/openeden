#include "neutralinventory.hpp"

#include <cstdio>
#include <string>

namespace MWWorld
{
    NeutralInventoryDecision decideNeutralInventory(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn)
    {
        if (!settingOn || !byPlayer || kind != openeden::sdk::game::ObjectKind::Container)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    NeutralInventoryDecision decideNeutralBench(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn)
    {
        if (!settingOn || !byPlayer || kind != openeden::sdk::game::ObjectKind::Furniture)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    NeutralInventoryDecision decideNeutralLoot(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn, bool dead)
    {
        if (!settingOn || !byPlayer || !dead || kind != openeden::sdk::game::ObjectKind::Actor)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    NeutralInventoryDecision decideNeutralPickup(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn)
    {
        if (!settingOn || !byPlayer || kind != openeden::sdk::game::ObjectKind::Item)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    NeutralInventoryDecision decideNeutralPickpocket(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn, bool dead, bool sneaking)
    {
        if (!settingOn || !byPlayer || dead || !sneaking || kind != openeden::sdk::game::ObjectKind::Actor)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    bool neutralKeyHeld(openeden::sdk::game::WorldId key,
        const std::function<bool(openeden::sdk::game::WorldId)>& keyRing,
        const std::function<bool(openeden::sdk::game::WorldId)>& inventory)
    {
        if (key == 0)
            return false;
        if (keyRing && keyRing(key))
            return true;
        return inventory && inventory(key);
    }

    bool decideNeutralBarter(std::string_view service, bool settingOn)
    {
        return settingOn && service == "barter";
    }

    std::vector<NeutralInventoryEventField> neutralBarterEventFields(
        std::uint64_t placement, std::uint64_t npcBase, std::string_view name)
    {
        return { { "placement", neutralHexId(placement) }, { "base", neutralHexId(npcBase) },
            { "name", std::string(name) } };
    }

    std::string neutralHexId(std::uint64_t id)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%08llx", static_cast<unsigned long long>(id));
        return buffer;
    }

    std::vector<NeutralInventoryEventField> neutralInventoryEventFields(
        std::uint64_t placement, std::uint64_t base, std::string_view kind, std::string_view name)
    {
        return { { "placement", neutralHexId(placement) }, { "base", neutralHexId(base) },
            { "kind", std::string(kind) }, { "name", std::string(name) } };
    }

    NeutralInventoryDecision decideNeutralLockpick(openeden::sdk::game::ObjectKind kind, bool byPlayer,
        bool inventorySettingOn, bool rmluiOn, bool packLockpickingOn, bool locked)
    {
        const bool lockable = kind == openeden::sdk::game::ObjectKind::Door
            || kind == openeden::sdk::game::ObjectKind::Container;
        if (!lockable || !byPlayer || !inventorySettingOn || !rmluiOn || !packLockpickingOn || !locked)
            return NeutralInventoryDecision::NotApplicable;
        return NeutralInventoryDecision::Open;
    }

    std::vector<NeutralInventoryEventField> neutralLockpickEventFields(std::uint64_t placement, std::uint64_t base,
        std::string_view kind, std::int32_t level, std::string_view name)
    {
        return { { "placement", neutralHexId(placement) }, { "base", neutralHexId(base) },
            { "kind", std::string(kind) }, { "level", std::to_string(level) }, { "name", std::string(name) } };
    }

    bool neutralLockAlreadyOpen(bool sessionOpen,
        const std::function<bool(openeden::sdk::game::WorldId)>& packPicked, openeden::sdk::game::WorldId placement)
    {
        return sessionOpen || (packPicked && packPicked(placement));
    }
}
