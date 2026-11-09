#include "craftingrules.hpp"

#include <stdexcept>

#include "cellref.hpp"

namespace MWWorld
{
    namespace
    {
        std::unique_ptr<openeden::sdk::game::CraftingRules>& installed()
        {
            // Never destroyed before the windows that hold sessions made by it: a function-local static outlives them
            // only if it is made first; the executable installs it at start-up, long before any window.
            static std::unique_ptr<openeden::sdk::game::CraftingRules> rules;
            return rules;
        }
    }

    void installCraftingRules(std::unique_ptr<openeden::sdk::game::CraftingRules> rules)
    {
        installed() = std::move(rules);
    }

    openeden::sdk::game::CraftingRules* craftingRules()
    {
        return installed().get();
    }

    openeden::sdk::game::CraftingRules& requireCraftingRules()
    {
        if (installed() == nullptr)
            throw std::runtime_error("No crafting rules are installed: the game pack of this session provides none");
        return *installed();
    }

    bool rechargeItemOverTime(const Ptr& item, float maxCharge, float duration)
    {
        const float charge = item.getCellRef().getEnchantmentCharge();
        if (charge == -1 || charge == maxCharge)
            return false;

        openeden::sdk::game::CraftingRules* rules = craftingRules();
        if (rules == nullptr)
            return false;

        item.getCellRef().setEnchantmentCharge(rules->rechargedCharge(charge, maxCharge, duration));
        return true;
    }
}
