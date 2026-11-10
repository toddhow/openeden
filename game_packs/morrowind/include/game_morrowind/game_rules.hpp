#ifndef GAME_MORROWIND_GAME_RULES_H
#define GAME_MORROWIND_GAME_RULES_H

#include <memory>

#include <openeden/game/game_rules.hpp>

namespace game_morrowind
{
    /// The Morrowind pack's GameRules (task 0084 leftovers, stage I1): the single object through which the core gets
    /// this pack's rules. Today it hands out the crafting rules of makeCraftingRules(), unchanged. The journal and the
    /// dialogue manager still reach the core through MWDialogue::installFactories (they are made from engine types
    /// that the SDK may not carry; see the 0085 stage log). Main thread only.
    [[nodiscard]] std::unique_ptr<openeden::sdk::game::GameRules> makeGameRules();
}

#endif
