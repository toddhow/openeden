#include "neutralinventory.hpp"

#include <cstdio>

namespace MWWorld
{
    NeutralInventoryDecision decideNeutralInventory(
        openeden::sdk::game::ObjectKind kind, bool byPlayer, bool settingOn)
    {
        if (!settingOn || !byPlayer || kind != openeden::sdk::game::ObjectKind::Container)
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
}
