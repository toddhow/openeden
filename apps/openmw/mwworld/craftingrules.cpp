#include "craftingrules.hpp"

#include <components/debug/debuglog.hpp>

#include "cellref.hpp"

namespace MWWorld
{
    namespace
    {
        namespace sdk = openeden::sdk::game;

        // Inert rules: what a program without a game pack's crafting sees (craftingrules.hpp). Every answer is
        // "nothing": no item is accepted, nothing is made, nothing is known. Plain objects with no state.
        class InertAlchemy final : public sdk::AlchemySession
        {
        public:
            void setAlchemist(sdk::ItemHandle) override {}
            void clear() override {}
            void setPotionName(std::string_view) override {}
            int countPotionsToBrew() const override { return 0; }
            int addIngredient(sdk::ItemHandle) override { return -1; }
            void addApparatus(sdk::ItemHandle) override {}
            void removeIngredient(std::size_t) override {}
            void removeApparatus(std::size_t) override {}
            sdk::ItemHandle ingredientSlot(std::size_t) override { return sdk::kNoItem; }
            sdk::ItemHandle apparatusSlot(std::size_t) override { return sdk::kNoItem; }
            std::vector<sdk::AlchemyEffectLine> effects() const override { return {}; }
            std::string suggestPotionName() override { return {}; }
            sdk::AlchemyResult create(std::string_view, int& count) override
            {
                count = 0;
                return sdk::AlchemyResult::NoMortarAndPestle;
            }
        };

        class InertEnchanting final : public sdk::EnchantingSession
        {
        public:
            void setEnchanter(sdk::ItemHandle) override {}
            void setSelfEnchanting(bool) override {}
            void setOldItem(sdk::ItemHandle) override {}
            void setSoulGem(sdk::ItemHandle) override {}
            sdk::ItemHandle oldItem() override { return sdk::kNoItem; }
            sdk::ItemHandle soulGem() override { return sdk::kNoItem; }
            void setNewItemName(std::string_view) override {}
            void setEffects(std::span<const sdk::EffectLine>) override {}
            bool create() override { return false; }
            void nextCastStyle() override {}
            sdk::CastStyle castStyle() const override { return sdk::CastStyle::CastOnce; }
            float enchantPoints(bool) const override { return 0.f; }
            int effectiveCastCost() const override { return 0; }
            int enchantPrice() const override { return 0; }
            int maxEnchantValue() const override { return 0; }
            int gemCharge() const override { return 0; }
            int enchantChance() const override { return 0; }
            bool soulEmpty() const override { return true; }
            bool itemEmpty() const override { return true; }
        };

        class InertRepair final : public sdk::RepairSession
        {
        public:
            void setTool(sdk::ItemHandle) override {}
            sdk::ItemHandle tool() override { return sdk::kNoItem; }
            int toolUses() const override { return 0; }
            float toolQuality() const override { return 0.f; }
            int toolCount() const override { return 0; }
            void repair(sdk::ItemHandle) override {}
        };

        class InertCraftingRules final : public sdk::CraftingRules
        {
        public:
            std::unique_ptr<sdk::AlchemySession> beginAlchemy() override { return std::make_unique<InertAlchemy>(); }
            std::unique_ptr<sdk::EnchantingSession> beginEnchanting() override
            {
                return std::make_unique<InertEnchanting>();
            }
            std::unique_ptr<sdk::RepairSession> beginRepair() override { return std::make_unique<InertRepair>(); }
            bool rechargeWithGem(sdk::ItemHandle, sdk::ItemHandle) override { return false; }
            int gemSoul(sdk::ItemHandle) override { return 0; }
            float rechargedCharge(float charge, float, float) override { return charge; }
            bool knownAlchemyEffect(std::size_t, sdk::ItemHandle) override { return false; }
            std::vector<std::string> ingredientEffectNames(sdk::ItemHandle, float) override { return {}; }
            int apparatusSlotOf(sdk::ItemHandle) override { return -1; }
        };

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

    openeden::sdk::game::CraftingRules& activeCraftingRules()
    {
        if (installed() != nullptr)
            return *installed();
        static InertCraftingRules inert;
        return inert;
    }

    bool rechargeItemOverTime(const Ptr& item, float maxCharge, float duration)
    {
        const float charge = item.getCellRef().getEnchantmentCharge();
        if (charge == -1 || charge == maxCharge)
            return false;

        openeden::sdk::game::CraftingRules* rules = craftingRules();
        if (rules == nullptr)
        {
            // Main thread only (the per-frame item stores), so a plain flag is enough.
            static bool told = false;
            if (!told)
            {
                told = true;
                Log(Debug::Warning) << "No crafting rules are installed: enchanted items do not recharge over time";
            }
            return false;
        }

        item.getCellRef().setEnchantmentCharge(rules->rechargedCharge(charge, maxCharge, duration));
        return true;
    }
}
