// Task 0189: the vendor data and the item keywords on the player's own Skyrim SE masters (aggregates and official
// identifiers only; lines SKYRIMSE-BARTER). The expected ids were read by an independent Python walker (its own plugin
// reader): ServicesWhiterunBanneredMare 0x9CAFC has list 0x914F0 (food and raw food) and chest REFR 0x9CAFB; the Iron
// Sword (0x12EB7) carries VendorItemWeapon (0x8F958), Cabbage (0x64B3F) VendorItemFood (0x8CDEA). Skipped without
// OPENEDEN_SKYRIMSE_DATA.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); the five official masters are read, in order
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/barter.hpp>
#include <game_skyrimse/inventory.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    TEST(SkyrimBarterRealData, theMastersVendorFactionsAndItemKeywordsDecode)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        std::map<FormId, FactionRecord> factions; // later plugins win
        inventory::ItemTable table;
        std::uint64_t odd = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << error.describe();
            ActorRecords records;
            ASSERT_TRUE(parseActorRecords(*file, order, ActorLimits(), records));
            for (const char* sub : { "FACT.VEND", "FACT.VENC", "FACT.VENV" })
                if (const auto it = records.mOddBy.find(sub); it != records.mOddBy.end())
                    odd += it->second;
            for (const FactionRecord& f : records.mFactionRecords)
                factions[f.mCommon.mId] = f;
            inventory::InventoryRecords items;
            ASSERT_TRUE(inventory::parseInventoryRecords(*file, order, inventory::InventoryLimits(), items));
            table.add(std::move(items));
        }
        std::size_t vendors = 0, exclusion = 0, stolen = 0, withValues = 0;
        for (const auto& [id, f] : factions)
        {
            if (!f.mVendor.isVendor())
                continue;
            ++vendors;
            exclusion += f.mVendor.mListIsExclusion ? 1 : 0;
            stolen += f.mVendor.mBuysStolen ? 1 : 0;
            withValues += f.mVendor.mHasValues ? 1 : 0;
        }
        std::cout << "SKYRIMSE-BARTER vendor factions " << vendors << " exclusion " << exclusion << " buys stolen "
                  << stolen << " with VENV " << withValues << " odd " << odd << "\n";
        EXPECT_EQ(odd, 0u);
        EXPECT_GE(vendors, 140u); // 145 in Skyrim.esm alone
        EXPECT_GE(exclusion, 20u); // the miscellaneous vendors
        EXPECT_EQ(withValues, vendors);

        ASSERT_EQ(factions.count(0x0009CAFC), 1u);
        const FactionRecord* mare = &factions.at(0x0009CAFC);
        EXPECT_TRUE(mare->mVendor.isVendor());
        EXPECT_EQ(mare->mVendor.mList, 0x000914F0u);
        EXPECT_EQ(mare->mVendor.mChest, 0x0009CAFBu);
        EXPECT_EQ(mare->mVendor.mStartHour, 0u);
        EXPECT_EQ(mare->mVendor.mEndHour, 24u);
        EXPECT_FALSE(mare->mVendor.mListIsExclusion);
        EXPECT_FALSE(mare->mVendor.mBuysStolen);

        const inventory::ItemStat* sword = table.item(0x00012EB7);
        ASSERT_NE(sword, nullptr);
        bool weaponKeyword = false;
        for (const FormId k : sword->mKeywords)
            weaponKeyword = weaponKeyword || k == 0x0008F958u;
        EXPECT_TRUE(weaponKeyword);
        const inventory::ItemStat* cabbage = table.item(0x00064B3F);
        ASSERT_NE(cabbage, nullptr);
        barter::VendorInfo innkeeper;
        innkeeper.mKeywords = { 0x000A0E56u, 0x0008CDEAu }; // VendorItemFoodRaw, VendorItemFood
        EXPECT_TRUE(barter::vendorBuys(innkeeper, 0x00064B3F, *cabbage));
        EXPECT_FALSE(barter::vendorBuys(innkeeper, 0x00012EB7, *sword));
        std::size_t withKeywords = 0;
        for (const FormId id : { 0x00012EB7u, 0x00064B3Fu, 0x0003EADEu })
            if (const inventory::ItemStat* s = table.item(id); s != nullptr && !s->mKeywords.empty())
                ++withKeywords;
        EXPECT_EQ(withKeywords, 3u);
    }
}
