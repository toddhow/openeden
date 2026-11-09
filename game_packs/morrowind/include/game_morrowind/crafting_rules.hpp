#ifndef GAME_MORROWIND_CRAFTING_RULES_H
#define GAME_MORROWIND_CRAFTING_RULES_H

#include <memory>

#include <openeden/game/crafting.hpp>

namespace game_morrowind
{
    /// Morrowind's crafting rules (alchemy, enchanting, repair, recharge) as the game-pack contract of
    /// docs/design/game-packs.md section 5.5 (task 0085 move 2, stage 2c). The object wraps the unchanged
    /// MWMechanics::Alchemy, Enchanting, Repair and rechargeItem of this pack, so the rules, their numbers and the
    /// order of their random draws are exactly the ones that were in apps/openmw/mwmechanics; it adds no draw of its
    /// own.
    ///
    /// Items come in as openeden::sdk::game::ItemHandle and are mapped back with MWWorld::resolveItem; every session
    /// owns the MWWorld::ItemHandleScope of the handles it returns. Main thread only.
    [[nodiscard]] std::unique_ptr<openeden::sdk::game::CraftingRules> makeCraftingRules();
}

#endif
