#include <game_morrowind/game_rules.hpp>

#include <game_morrowind/crafting_rules.hpp>

namespace game_morrowind
{
    namespace
    {
        class MorrowindRules final : public openeden::sdk::game::GameRules
        {
        public:
            [[nodiscard]] std::string_view gameId() const noexcept override { return "morrowind"; }

            [[nodiscard]] std::unique_ptr<openeden::sdk::game::CraftingRules> makeCraftingRules() override
            {
                return game_morrowind::makeCraftingRules();
            }
        };
    }

    std::unique_ptr<openeden::sdk::game::GameRules> makeGameRules()
    {
        return std::make_unique<MorrowindRules>();
    }
}
