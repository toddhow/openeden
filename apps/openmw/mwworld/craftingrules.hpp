#ifndef OPENMW_MWWORLD_CRAFTINGRULES_H
#define OPENMW_MWWORLD_CRAFTINGRULES_H

#include <memory>

#include <openeden/game/crafting.hpp>

#include "ptr.hpp"

namespace MWWorld
{
    /// The crafting rules of the game in play (task 0085 move 2, stage 2c). The executable installs the game pack's rules
    /// before the first window is made; the core owns the object from then on. Main thread only.
    ///
    /// A program that installs nothing has no crafting: craftingRules() is null, the windows throw when they are made
    /// (requireCraftingRules()), and the recharge of items over time does nothing.
    void installCraftingRules(std::unique_ptr<openeden::sdk::game::CraftingRules> rules);

    [[nodiscard]] openeden::sdk::game::CraftingRules* craftingRules();

    /// Throws std::runtime_error naming what is missing when nothing is installed.
    [[nodiscard]] openeden::sdk::game::CraftingRules& requireCraftingRules();

    /// Recharges an enchanted item over time: the per-frame path of ContainerStore::rechargeItems and
    /// CellStore::rechargeItems (task 0085 move 2: a non-virtual helper of the core). It returns false, and does not ask
    /// the rules, when no change is due (the item has no charge, -1, or is already full), so the rules are called once
    /// per item whose charge really changes and never for the rest. With no rules installed nothing recharges.
    bool rechargeItemOverTime(const Ptr& item, float maxCharge, float duration);
}

#endif
