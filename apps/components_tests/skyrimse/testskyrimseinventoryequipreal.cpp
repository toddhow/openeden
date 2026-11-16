// Task 0184: the potion effects and the Health restore rule on the five Skyrim SE masters (aggregates only: lines
// SKYRIMSE-EQUIP). Settles, by the data, the three recalled facts of SkyrimWorldView::healthOfEffect: the effect
// RestoreHealth (Skyrim.esm 0x0003EB15) is a ValueModifier (archetype 0) on the Health actor value (24), and neither
// the hostile (0x1) nor the detrimental (0x4) flag is set on it; and that every EFID is followed by an EFIT of 12 bytes
// (no odd subrecord). Skipped without OPENEDEN_SKYRIMSE_DATA.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace game_skyrimse::inventory;

    constexpr FormId kRestoreHealth = 0x0003EB15;
    constexpr FormId kRestoreHealthPotion = 0x0003EADD; // RestoreHealth01: 25 points

    TEST(SkyrimEquipRealData, thePotionEffectsDecodeAndTheRestoreHealthEffectIsAnInstantHealthModifier)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> masters
            = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        for (const std::string& n : masters)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        LoadOrder order;
        std::string err;
        for (const std::string& n : masters)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        ItemTable table;
        CombatIndex combat;
        for (const std::string& n : masters)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            InventoryRecords records;
            ASSERT_TRUE(parseInventoryRecords(*plugin, order, InventoryLimits(), records));
            table.add(std::move(records));
            CombatRecords combatRecords;
            ASSERT_TRUE(parseCombatRecords(*plugin, order, ExteriorLimits(), combatRecords));
            combat.add(combatRecords);
        }
        EXPECT_EQ(table.oddSubrecords(), 0u);

        const MagicEffectRecord* heal = combat.magicEffect(kRestoreHealth);
        ASSERT_NE(heal, nullptr);
        std::cout << "SKYRIMSE-EQUIP RestoreHealth: has data " << heal->mHasData << " archetype " << heal->mArchetype
                  << " primary value " << heal->mPrimaryValue << " flags 0x" << std::hex << heal->mFlags << std::dec
                  << "\n";
        EXPECT_TRUE(heal->mHasData);
        EXPECT_EQ(heal->mArchetype, 0u);
        EXPECT_EQ(heal->mPrimaryValue, 24);
        EXPECT_EQ(heal->mFlags & 0x5u, 0u);

        const ItemStat* potion = table.item(kRestoreHealthPotion);
        ASSERT_NE(potion, nullptr);
        ASSERT_EQ(potion->mEffects.size(), 1u);
        EXPECT_EQ(potion->mEffects[0].mEffect, kRestoreHealth);
        EXPECT_FLOAT_EQ(potion->mEffects[0].mMagnitude, 25.f);
        EXPECT_EQ(potion->mEffects[0].mDuration, 0u);
        EXPECT_EQ(potion->mCategory, Category::Potion);

        // the iron dagger and the iron cuirass: weapon and armour categories
        const ItemStat* dagger = table.item(0x0001397E);
        ASSERT_NE(dagger, nullptr);
        EXPECT_EQ(dagger->mCategory, Category::Weapon);
        const ItemStat* cuirass = table.item(0x00012E49);
        ASSERT_NE(cuirass, nullptr);
        EXPECT_EQ(cuirass->mCategory, Category::Armor);
    }
}
