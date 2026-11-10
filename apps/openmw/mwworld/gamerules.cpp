#include "gamerules.hpp"

#include "craftingrules.hpp"

namespace MWWorld
{
    void installGameRules(openeden::sdk::game::GameRules& rules)
    {
        installCraftingRules(rules.makeCraftingRules());
    }
}
