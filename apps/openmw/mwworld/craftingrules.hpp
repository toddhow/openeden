#ifndef OPENMW_MWWORLD_CRAFTINGRULES_H
#define OPENMW_MWWORLD_CRAFTINGRULES_H

#include <memory>

#include <openeden/game/crafting.hpp>

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
}

#endif
