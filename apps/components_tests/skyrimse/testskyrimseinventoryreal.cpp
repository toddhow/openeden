// Task 0182: item stats and container contents on the five Skyrim SE masters (aggregates only: lines SKYRIMSE-INV).
// Settles the DATA layouts of inventory.hpp (every item record of a decoded type must have the size the header says,
// so any "odd" count above 0 is printed and fails the layout) and resolves every container's contents. Skipped without
// OPENEDEN_SKYRIMSE_DATA.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace game_skyrimse::inventory;

    const std::vector<std::string>& masters()
    {
        static const std::vector<std::string> names
            = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        return names;
    }

    TEST(SkyrimInventoryRealData, theMastersItemsAndContainersDecodeAndResolve)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        for (const std::string& n : masters())
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";

        LoadOrder order;
        std::string err;
        for (const std::string& n : masters())
            ASSERT_TRUE(order.add(n, false, err)) << err;

        const auto started = std::chrono::steady_clock::now();
        ItemTable table;
        std::uint64_t failed = 0, unresolved = 0;
        for (const std::string& n : masters())
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            InventoryRecords records;
            ASSERT_TRUE(parseInventoryRecords(*plugin, order, InventoryLimits(), records));
            failed += records.mFailed;
            unresolved += records.mUnresolvedFormIds;
            table.add(std::move(records));
            ParsedPlugin parsed;
            Content::Error parseError;
            ASSERT_TRUE(parseRecordsFile(data / n, order, ParseLimits(), parsed, parseError)) << parseError.describe();
            table.addLeveledLists(parsed.mLeveledLists);
        }
        const auto millis
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << "SKYRIMSE-INV items " << table.itemCount() << " containers " << table.containerCount() << " odd "
                  << table.oddSubrecords() << " failed " << failed << " unresolved " << unresolved << " in " << millis
                  << " ms\n";
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(table.oddSubrecords(), 0u) << "a DATA, ENIT or CNTO has a size the header does not describe";

        // Anchors: Gold001 (Skyrim.esm 0x0F) and the Iron Sword (0x12EB7).
        const ItemStat* gold = table.item(kGoldItem);
        ASSERT_NE(gold, nullptr);
        EXPECT_EQ(gold->mValue, 1);
        EXPECT_FLOAT_EQ(gold->mWeight, 0.f);
        const ItemStat* sword = table.item(0x00012EB7);
        ASSERT_NE(sword, nullptr);
        EXPECT_EQ(sword->mValue, 25);
        EXPECT_FLOAT_EQ(sword->mWeight, 9.f);

        std::uint64_t resolved = 0, empty = 0, unknown = 0, stacks = 0;
        // every container resolves to stacks of known items, with the seeded pick: twice equal
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / "Skyrim.esm", error);
            ASSERT_NE(plugin, nullptr);
            InventoryRecords records;
            ASSERT_TRUE(parseInventoryRecords(*plugin, order, InventoryLimits(), records));
            for (const ContainerStat& c : records.mContainers)
            {
                ResolveReport report;
                const std::vector<Stack> a = resolveContents(c.mItems, table, contentsSeed(c.mId), 1, &report);
                const std::vector<Stack> b = resolveContents(c.mItems, table, contentsSeed(c.mId), 1);
                EXPECT_EQ(a, b);
                ++resolved;
                stacks += a.size();
                empty += a.empty() ? 1 : 0;
                unknown += report.mUnknownForms;
            }
        }
        std::cout << "SKYRIMSE-INV Skyrim.esm containers " << resolved << " resolved at level 1 to " << stacks
                  << " stacks, " << empty << " empty, " << unknown << " unknown forms\n";
        EXPECT_GT(resolved, 300u); // Skyrim.esm holds 436 CONT records
    }
}
