#ifndef OPENMW_MWWORLD_CRAFTINGRULES_H
#define OPENMW_MWWORLD_CRAFTINGRULES_H

#include <memory>

#include <openeden/game/crafting.hpp>

#include "ptr.hpp"

namespace MWWorld
{
    /// The crafting rules of the game in play (task 0085 move 2, stage 2c). The executable installs the game pack's
    /// rules before the first window is made; the core owns the object from then on. Main thread only.
    ///
    /// A program that installs nothing has no crafting rules (craftingRules() is null). It still runs: the code that
    /// needs rules asks activeCraftingRules(), which then answers with INERT rules. Inert rules know no alchemy effect,
    /// show no ingredient effect names, accept no ingredient or tool, make nothing, recharge nothing and have no
    /// apparatus, so a potion tooltip or an item filter shows no effects and the crafting windows open empty and
    /// unusable. Nothing throws. Installing rules (the executable does, game_morrowind::makeCraftingRules()) replaces
    /// them.
    ///
    /// The installation is not tied to game=: until the game pack lifecycle owns it (with GameRules), a program that
    /// wants Morrowind's crafting installs the Morrowind pack's rules itself, as apps/openmw/main.cpp does. openmw-lib
    /// never names a pack.
    void installCraftingRules(std::unique_ptr<openeden::sdk::game::CraftingRules> rules);

    /// The installed rules, or null.
    [[nodiscard]] openeden::sdk::game::CraftingRules* craftingRules();

    /// The installed rules, or the inert ones. Never null, never throws.
    [[nodiscard]] openeden::sdk::game::CraftingRules& activeCraftingRules();

    /// Recharges an enchanted item over time: the per-frame path of ContainerStore::rechargeItems and
    /// CellStore::rechargeItems (task 0085 move 2: a non-virtual helper of the core). It returns false, and does not
    /// ask the rules, when no change is due (the item has no charge, -1, or is already full), so the rules are called
    /// once per item whose charge really changes and never for the rest. With no rules installed nothing recharges, and
    /// the first call says so in the log.
    bool rechargeItemOverTime(const Ptr& item, float maxCharge, float duration);
}

#endif
