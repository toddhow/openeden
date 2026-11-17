// Task 0199: the crafting records of the installed Skyrim.esm (gated by OPENEDEN_SKYRIMSE_DATA, which names the Data
// folder). Prints aggregates only. The numbers are those of an independent Python walker (task 0199 section 1); a COBJ
// with no CNAM (7) is not a recipe.
#include <game_skyrimse/crafting.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;

    TEST(SkyrimCraftingRealData, skyrimEsmCraftingRecordsDecodeAsTheSurveyFound)
    {
        const char* dir = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::filesystem::path path = std::filesystem::path(dir) / "Skyrim.esm";
        Content::Error error;
        const auto plugin = Content::Plugin::openFile(path, error);
        ASSERT_NE(plugin, nullptr) << error.mMessage;
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("Skyrim.esm", false, err));
        CraftingRecords records;
        ASSERT_TRUE(parseCraftingRecords(*plugin, order, CraftingLimits(), records));
        CraftingTable table;
        table.add(std::move(records));
        table.finish();
        std::cout << "SKYRIMSE-CRAFTING ingredients " << table.ingredientCount() << ", recipes " << table.recipeCount()
                  << ", enchantments " << table.enchantmentCount() << ", furniture " << table.furnitureCount() << "\n";
        EXPECT_EQ(table.ingredientCount(), 94u);
        EXPECT_EQ(table.recipeCount(), 594u);
        EXPECT_EQ(table.enchantmentCount(), 604u);
        const FormId forge = table.keyword("CraftingSmithingForge");
        ASSERT_EQ(forge, 0x00088105u);
        EXPECT_EQ(table.keyword("CraftingSmithingSharpeningWheel"), 0x00088108u);
        EXPECT_EQ(table.keyword("WICraftingAlchemy"), 0x0004F6E6u);
        EXPECT_EQ(table.keyword("WICraftingEnchanting"), 0x0004F6DDu);
        EXPECT_EQ(table.recipesFor(BenchKind::SharpeningWheel, true).size(), 158u);
        EXPECT_EQ(table.recipesFor(BenchKind::ArmorTable, true).size(), 220u);
        EXPECT_EQ(table.recipesFor(BenchKind::Forge, true).size(), 158u);
        // the grindstone, an anvil and the alchemy lab of the survey
        EXPECT_EQ(table.benchesOf(0x0006E9C2u), std::vector<BenchKind>({ BenchKind::SharpeningWheel }));
        EXPECT_EQ(table.benchesOf(0x0001A2ADu), std::vector<BenchKind>({ BenchKind::Forge }));
        EXPECT_EQ(table.benchesOf(0x000BAD0Cu), std::vector<BenchKind>({ BenchKind::Alchemy }));
        EXPECT_EQ(table.benchesOf(0x000BAD0Du), std::vector<BenchKind>({ BenchKind::Enchanting }));
        // an Iron Sword of Fear carries its enchantment
        EXPECT_NE(table.enchantmentOf(0x00089716u), kNullFormId);
        // a grand soul gem, filled
        ASSERT_NE(table.soulGem(0x0002E4FFu), nullptr);
        EXPECT_EQ(table.soulGem(0x0002E4FFu)->mCapacity, 5);
        EXPECT_EQ(table.soulGem(0x0002E4FFu)->mSoul, 5);
    }
}
