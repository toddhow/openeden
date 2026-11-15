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
