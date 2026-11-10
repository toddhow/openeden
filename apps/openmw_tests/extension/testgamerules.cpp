// Task 0084 (leftovers, lane I, stage I1): the single place a game pack hands its rules to the core
// (sdk/include/openeden/game/game_rules.hpp), the core's installer for them, and the Morrowind pack's implementation.
#include <openeden/game/game_rules.hpp>

#include <apps/openmw/mwworld/craftingrules.hpp>
#include <apps/openmw/mwworld/gamerules.hpp>

#include <game_morrowind/game_rules.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    namespace sdkgame = openeden::sdk::game;

    // Counts what the core asked of it; hands out the crafting rules it was given (or none).
    class FakeRules final : public sdkgame::GameRules
    {
    public:
        explicit FakeRules(std::unique_ptr<sdkgame::CraftingRules> crafting = nullptr)
            : mCrafting(std::move(crafting))
        {
        }

        [[nodiscard]] std::string_view gameId() const noexcept override { return "fake"; }

        std::unique_ptr<sdkgame::CraftingRules> makeCraftingRules() override
        {
            ++mCraftingCalls;
            return std::move(mCrafting);
        }

        int mCraftingCalls = 0;

    private:
        std::unique_ptr<sdkgame::CraftingRules> mCrafting;
    };

    // Some other rules object so that the test can tell which one was installed.
    class MarkerCrafting final : public sdkgame::CraftingRules
    {
    public:
        // Only the identity matters here: nothing in this test begins a session.
        [[nodiscard]] std::unique_ptr<sdkgame::AlchemySession> beginAlchemy() override { return nullptr; }
        [[nodiscard]] std::unique_ptr<sdkgame::EnchantingSession> beginEnchanting() override { return nullptr; }
        [[nodiscard]] std::unique_ptr<sdkgame::RepairSession> beginRepair() override { return nullptr; }
        bool rechargeWithGem(sdkgame::ItemHandle, sdkgame::ItemHandle) override { return false; }
        [[nodiscard]] int gemSoul(sdkgame::ItemHandle) override { return 0; }
        [[nodiscard]] float rechargedCharge(float charge, float, float) override { return charge; }
        [[nodiscard]] bool knownAlchemyEffect(std::size_t, sdkgame::ItemHandle) override { return false; }
        [[nodiscard]] std::vector<std::string> ingredientEffectNames(sdkgame::ItemHandle, float) override { return {}; }
        [[nodiscard]] int apparatusSlotOf(sdkgame::ItemHandle) override { return -1; }
    };

    class GameRulesInstallTest : public testing::Test
    {
    protected:
        void TearDown() override { MWWorld::installCraftingRules(nullptr); }
    };

    TEST(GameRulesHeader, theVersionContractIdAndPolicyAreFixed)
    {
        EXPECT_EQ(sdkgame::kGameRulesVersion, (openeden::sdk::Version{ 1, 0 }));
        EXPECT_EQ(sdkgame::GameRules::kId, "openeden.game.rules");
        EXPECT_EQ(sdkgame::GameRules::kVersion, sdkgame::kGameRulesVersion);
        EXPECT_TRUE(std::is_abstract_v<sdkgame::GameRules>);
        EXPECT_TRUE(std::has_virtual_destructor_v<sdkgame::GameRules>);
    }

    TEST(GameRulesHeader, anAddedFunctionHasADefaultSoOlderPacksStillCompile)
    {
        // A pack that only names its game compiles: every other member has a default (no crafting rules).
        struct Minimal final : sdkgame::GameRules
        {
            [[nodiscard]] std::string_view gameId() const noexcept override { return "minimal"; }
        };
        Minimal minimal;
        EXPECT_EQ(minimal.makeCraftingRules(), nullptr);
    }

    TEST_F(GameRulesInstallTest, installingRulesAsksThePackOnceAndTheCoreOwnsTheCraftingRules)
    {
        FakeRules rules(std::make_unique<MarkerCrafting>());
        ASSERT_EQ(MWWorld::craftingRules(), nullptr);
        MWWorld::installGameRules(rules);
        EXPECT_EQ(rules.mCraftingCalls, 1);
        EXPECT_NE(MWWorld::craftingRules(), nullptr);
        EXPECT_NE(dynamic_cast<MarkerCrafting*>(MWWorld::craftingRules()), nullptr);
    }

    TEST_F(GameRulesInstallTest, rulesWithNoCraftingLeaveTheCoreWithItsInertRulesAndNothingThrows)
    {
        FakeRules rules;
        EXPECT_NO_THROW(MWWorld::installGameRules(rules));
        EXPECT_EQ(rules.mCraftingCalls, 1);
        EXPECT_EQ(MWWorld::craftingRules(), nullptr);
        EXPECT_NO_THROW((void)MWWorld::activeCraftingRules());
    }

    TEST_F(GameRulesInstallTest, rulesWithNoCraftingReplaceRulesInstalledBefore)
    {
        MWWorld::installCraftingRules(std::make_unique<MarkerCrafting>());
        FakeRules rules;
        MWWorld::installGameRules(rules);
        // The new game's answer wins, even when the answer is "none": a game without crafting must not keep
        // another game's.
        EXPECT_EQ(MWWorld::craftingRules(), nullptr);
    }

    TEST(GameRulesMorrowind, thePackNamesItsGameAndHandsOutTheCraftingRulesTheExecutableInstalledBefore)
    {
        std::unique_ptr<sdkgame::GameRules> rules = game_morrowind::makeGameRules();
        ASSERT_NE(rules, nullptr);
        EXPECT_EQ(rules->gameId(), "morrowind");
        std::unique_ptr<sdkgame::CraftingRules> crafting = rules->makeCraftingRules();
        EXPECT_NE(crafting, nullptr);
    }
}
