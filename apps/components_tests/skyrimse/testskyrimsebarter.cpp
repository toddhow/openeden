// Task 0189 (barter; milestone M-S6): the FACT vendor data, item keywords, the vendor rule, the prices, one trade over
// the inventory model, and the barter service with its "skyrimse.barter" bindings over a stub bindings service.
// Generated plugins written to a temporary file; no game data. Written from the task spec before the code and run red
// against stubs first (see the task's stage log).
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/barter_service.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/dialogue.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::barter;
    using inventory::InventoryModel;
    using inventory::ItemStat;
    using inventory::ItemTable;
    using inventory::kGoldItem;
    using inventory::kPlayerOwner;
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kBread = kSelf | 0x100; // value 5, keyword kKwFood
    constexpr FormId kSword = kSelf | 0x101; // value 25, keyword kKwWeapon
    constexpr FormId kPebble = kSelf | 0x102; // value 10, no keyword
    constexpr FormId kJunk = kSelf | 0x103; // value 0, keyword kKwFood
    constexpr FormId kKwFood = kSelf | 0x800;
    constexpr FormId kKwWeapon = kSelf | 0x801;
    constexpr FormId kKwNoSale = kSelf | 0x802;
    constexpr FormId kCont = kSelf | 0x200; // the vendor chest's base
    constexpr std::uint64_t kChest = kSelf | 0x700; // the vendor chest's REFR
    constexpr FormId kNpc = kSelf | 0x900;
    constexpr FormId kNpcPlain = kSelf | 0x901;
    constexpr FormId kNpcLate = kSelf | 0x902; // becomes ready after a few asks

    // ---- the FACT vendor data and the item keywords ----------------------------------------------------------------

    LoadOrder defaultOrder()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    Bytes header1()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    ActorRecords parseFaction(const Bytes& subs, bool* ok = nullptr)
    {
        Bytes bytes = header1();
        bytes.append(group4(Content::fourCC("FACT"), 0, record4("FACT", 0, kSelf | 0x400, subs)));
        ActorRecords records;
        Content::Error error;
        const bool parsed
            = parseActorRecordsBytes(bytes.str(), "test.esp", defaultOrder(), ActorLimits(), records, error);
        if (ok != nullptr)
            *ok = parsed;
        return records;
    }

    Bytes venv(
        std::uint16_t start, std::uint16_t end, std::uint32_t radius, std::uint8_t stolen, std::uint8_t exclusion)
    {
        return sub4("VENV", Bytes().u16(start).u16(end).u32(radius).u8(stolen).u8(exclusion).u16(0));
    }

    TEST(SkyrimBarterData, aFactionKeepsItsVendorListChestHoursAndFlags)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Services")));
        f.append(sub4("VEND", Bytes().u32(kSelf | 0x810)));
        f.append(sub4("VENC", Bytes().u32(static_cast<std::uint32_t>(kChest))));
        f.append(venv(8, 20, 256, 1, 1));
        bool ok = false;
        const ActorRecords records = parseFaction(f, &ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(records.mFactionRecords.size(), 1u);
        const VendorData& v = records.mFactionRecords[0].mVendor;
        EXPECT_EQ(v.mList, kSelf | 0x810);
        EXPECT_EQ(v.mChest, static_cast<FormId>(kChest));
        EXPECT_EQ(v.mStartHour, 8u);
        EXPECT_EQ(v.mEndHour, 20u);
        EXPECT_EQ(v.mRadius, 256u);
        EXPECT_TRUE(v.mBuysStolen);
        EXPECT_TRUE(v.mListIsExclusion);
        EXPECT_TRUE(v.mHasValues);
        EXPECT_TRUE(v.isVendor());
    }

    TEST(SkyrimBarterData, aFactionWithoutAChestOrListIsNoVendor)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Plain")));
        f.append(sub4("VEND", Bytes().u32(kSelf | 0x810)));
        const ActorRecords noChest = parseFaction(f);
        ASSERT_EQ(noChest.mFactionRecords.size(), 1u);
        EXPECT_FALSE(noChest.mFactionRecords[0].mVendor.isVendor());
        const ActorRecords none = parseFaction(sub4("EDID", Bytes().zstring("None")));
        ASSERT_EQ(none.mFactionRecords.size(), 1u);
        EXPECT_FALSE(none.mFactionRecords[0].mVendor.isVendor());
        EXPECT_FALSE(none.mFactionRecords[0].mVendor.mHasValues);
    }

    TEST(SkyrimBarterData, aVendorSubrecordOfTheWrongSizeIsOddAndDecodesToNothing)
    {
        Bytes f;
        f.append(sub4("EDID", Bytes().zstring("Odd")));
        f.append(sub4("VEND", Bytes().u32(1).u32(2)));
        f.append(sub4("VENC", Bytes().u16(1)));
        f.append(sub4("VENV", Bytes().u32(1)));
        const ActorRecords records = parseFaction(f);
        ASSERT_EQ(records.mFactionRecords.size(), 1u);
        EXPECT_FALSE(records.mFactionRecords[0].mVendor.isVendor());
        EXPECT_FALSE(records.mFactionRecords[0].mVendor.mHasValues);
        EXPECT_EQ(records.mOddBy.at("FACT.VEND"), 1u);
        EXPECT_EQ(records.mOddBy.at("FACT.VENC"), 1u);
        EXPECT_EQ(records.mOddBy.at("FACT.VENV"), 1u);
    }

    TEST(SkyrimBarterData, anItemKeepsItsKeywords)
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 2, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes misc;
        misc.append(sub4("DATA", Bytes().u32(25).f32(0.5f)));
        misc.append(sub4("KSIZ", Bytes().u32(2)));
        misc.append(sub4("KWDA", Bytes().u32(kKwFood).u32(kKwWeapon)));
        all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, kPebble, misc)));
        Bytes weap;
        weap.append(sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)));
        all.append(group4(Content::fourCC("WEAP"), 0, record4("WEAP", 0, kSword, weap)));
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(all.str(), std::ios::binary), "test.esp", error);
        ASSERT_NE(plugin, nullptr) << error.mMessage;
        inventory::InventoryRecords records;
        ASSERT_TRUE(inventory::parseInventoryRecords(*plugin, defaultOrder(), inventory::InventoryLimits(), records));
        ItemTable table;
        table.add(std::move(records));
        ASSERT_NE(table.item(kPebble), nullptr);
        EXPECT_EQ(table.item(kPebble)->mKeywords, (std::vector<FormId>{ kKwFood, kKwWeapon }));
        ASSERT_NE(table.item(kSword), nullptr);
        EXPECT_TRUE(table.item(kSword)->mKeywords.empty());
    }

    // ---- the barter line is offered to a merchant ------------------------------------------------------------------

    TEST(SkyrimBarterLine, aMerchantOffersItsServicesWhateverItsPackage)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        DialogueSpeaker speaker;
        speaker.mBase = kNpc;
        speaker.mReference = kSelf | 0x950;
        world.setSpeaker(speaker);
        survey::Ctda c;
        c.mFunction = 255; // GetOffersServicesNow
        c.mCompare = 0;
        c.mComparison = 1.f;
        conditions::Evaluator evaluator;
        const auto holds = [&] { return evaluator.evaluate(std::span<const survey::Ctda>(&c, 1), world); };
        EXPECT_FALSE(world.answers(c)); // no package lookup and no merchant lookup: unanswered
        EXPECT_FALSE(holds());
        world.setVendorServicesLookup([](FormId base) { return base == kNpc; });
        EXPECT_TRUE(world.answers(c));
        EXPECT_TRUE(holds());
        speaker.mBase = kNpcPlain;
        world.setSpeaker(speaker);
        EXPECT_FALSE(world.answers(c)); // not a merchant: as before
        EXPECT_FALSE(holds());
    }

    // ---- prices ----------------------------------------------------------------------------------------------------

    TEST(SkyrimBarterPrices, theRatioFollowsSpeechBetweenTheTwoGmsts)
    {
        const BarterSettings s;
        EXPECT_FLOAT_EQ(priceRatio(0.f, s), 3.3f);
        EXPECT_FLOAT_EQ(priceRatio(100.f, s), 2.0f);
        EXPECT_NEAR(priceRatio(15.f, s), 3.105f, 1e-5f);
        EXPECT_FLOAT_EQ(priceRatio(250.f, s), 2.0f); // clamped
        EXPECT_FLOAT_EQ(priceRatio(-5.f, s), 3.3f);
        EXPECT_FLOAT_EQ(priceRatio(std::numeric_limits<float>::quiet_NaN(), s), 3.3f);
    }

    TEST(SkyrimBarterPrices, thePlayerPaysAtLeastTheMinimumMultiplierAndIsPaidAFractionOfValue)
    {
        const BarterSettings s;
        EXPECT_NEAR(buyMultiplier(0.f, s), 1.65f, 1e-5f);
        EXPECT_NEAR(buyMultiplier(15.f, s), 1.5525f, 1e-5f);
        EXPECT_FLOAT_EQ(buyMultiplier(100.f, s), 1.05f); // ratio / 2 = 1.0, floored at fMinBuyMult
        EXPECT_NEAR(sellMultiplier(0.f, s), 1.f / 3.3f, 1e-5f);
        EXPECT_NEAR(sellMultiplier(15.f, s), 1.f / 3.105f, 1e-5f);
        EXPECT_FLOAT_EQ(sellMultiplier(100.f, s), 0.5f);
    }

    TEST(SkyrimBarterPrices, unitPricesAreWholeGoldAtLeastOneAndZeroForWorthlessItems)
    {
        const BarterSettings s;
        EXPECT_EQ(buyPrice(25, 15.f, s), 39); // 38.8125
        EXPECT_EQ(sellPrice(25, 15.f, s), 8); // 8.05
        EXPECT_EQ(buyPrice(100, 0.f, s), 165);
        EXPECT_EQ(sellPrice(100, 0.f, s), 30);
        EXPECT_EQ(buyPrice(100, 100.f, s), 105);
        EXPECT_EQ(sellPrice(100, 100.f, s), 50);
        EXPECT_EQ(sellPrice(1, 0.f, s), 1); // 0.30 rounds to 0, at least 1
        EXPECT_EQ(buyPrice(1, 100.f, s), 1);
        EXPECT_EQ(buyPrice(0, 15.f, s), 0);
        EXPECT_EQ(sellPrice(0, 15.f, s), 0);
        EXPECT_EQ(buyPrice(-4, 15.f, s), 0);
        EXPECT_EQ(sellPrice(-4, 15.f, s), 0);
    }

    TEST(SkyrimBarterPrices, theSettingsAreTunable)
    {
        BarterSettings s;
        s.mBarterMin = 1.0f;
        s.mBarterMax = 1.0f;
        s.mMinBuyMult = 1.0f;
        EXPECT_EQ(buyPrice(40, 0.f, s), 40);
        EXPECT_EQ(sellPrice(40, 0.f, s), 40);
    }

    // ---- what the vendor buys --------------------------------------------------------------------------------------

    ItemStat item(
        FormId id, std::int32_t value, std::vector<FormId> keywords, inventory::Category c = inventory::Category::Misc)
    {
        ItemStat s;
        s.mId = id;
        s.mCategory = c;
        s.mValue = value;
        s.mDecoded = true;
        s.mKeywords = std::move(keywords);
        return s;
    }

    VendorInfo vendorOf(std::vector<FormId> list, bool exclusion = false)
    {
        VendorInfo v;
        v.mFaction = kSelf | 0x400;
        v.mChestRef = static_cast<FormId>(kChest);
        v.mChestBase = kCont;
        v.mKeywords = std::move(list);
        v.mListIsExclusion = exclusion;
        return v;
    }

    TEST(SkyrimBarterRules, anItemIsBoughtWhenItsKeywordIsInTheList)
    {
        const VendorInfo v = vendorOf({ kKwFood });
        EXPECT_TRUE(vendorBuys(v, kBread, item(kBread, 5, { kKwFood })));
        EXPECT_TRUE(vendorBuys(v, kBread, item(kBread, 5, { kKwWeapon, kKwFood })));
        EXPECT_FALSE(vendorBuys(v, kSword, item(kSword, 25, { kKwWeapon })));
        EXPECT_FALSE(vendorBuys(v, kPebble, item(kPebble, 10, {}))); // no keyword, not in an inclusive list
    }

    TEST(SkyrimBarterRules, anExclusionListBuysEverythingElse)
    {
        const VendorInfo v = vendorOf({ kKwNoSale }, true);
        EXPECT_TRUE(vendorBuys(v, kPebble, item(kPebble, 10, {})));
        EXPECT_TRUE(vendorBuys(v, kSword, item(kSword, 25, { kKwWeapon })));
        EXPECT_FALSE(vendorBuys(v, kBread, item(kBread, 5, { kKwNoSale, kKwFood })));
    }

    TEST(SkyrimBarterRules, goldAndWorthlessItemsAreNeverBought)
    {
        const VendorInfo all = vendorOf({ kKwNoSale }, true);
        EXPECT_FALSE(vendorBuys(all, kGoldItem, item(kGoldItem, 1, {})));
        EXPECT_FALSE(vendorBuys(all, kJunk, item(kJunk, 0, {})));
        const VendorInfo food = vendorOf({ kKwFood });
        EXPECT_FALSE(vendorBuys(food, kJunk, item(kJunk, 0, { kKwFood })));
    }

    TEST(SkyrimBarterRules, everyTradeStatusHasAName)
    {
        std::map<std::string, int> names;
        for (int i = 0; i <= static_cast<int>(TradeStatus::NotEnoughVendorGold); ++i)
        {
            const std::string_view n = tradeStatusName(static_cast<TradeStatus>(i));
            EXPECT_FALSE(n.empty()) << i;
            ++names[std::string(n)];
        }
        for (const auto& [name, count] : names)
            EXPECT_EQ(count, 1) << name;
    }

    // ---- one trade --------------------------------------------------------------------------------------------------

    struct BarterTradeFixture : testing::Test
    {
        ItemTable mTable;
        InventoryModel mModel;
        VendorInfo mVendor = vendorOf({ kKwFood, kKwWeapon });
        const float mSpeech = 15.f;
        const BarterSettings mSettings;

        void SetUp() override
        {
            inventory::InventoryRecords records;
            records.mItems.push_back(item(kGoldItem, 1, {}));
            records.mItems.push_back(item(kBread, 5, { kKwFood }, inventory::Category::Potion));
            records.mItems.push_back(item(kSword, 25, { kKwWeapon }, inventory::Category::Weapon));
            records.mItems.push_back(item(kPebble, 10, {}));
            records.mItems.push_back(item(kJunk, 0, { kKwFood }));
            mTable.add(std::move(records));
            mModel.setPlayer({ { kGoldItem, 100 }, { kSword, 2 }, { kPebble, 3 }, { kJunk, 1 } });
            ASSERT_TRUE(mModel.ensureOwner(kChest, { { kGoldItem, 50 }, { kBread, 4 }, { kSword, 1 }, { kJunk, 2 } }));
        }

        std::int32_t gold(std::uint64_t owner) const { return mModel.count(owner, kGoldItem); }

        TradeResult buyItem(FormId id, std::int32_t n)
        {
            return buy(mModel, mTable, kChest, id, n, mSpeech, mSettings);
        }
        TradeResult sellItem(FormId id, std::int32_t n)
        {
            return sell(mModel, mTable, mVendor, kChest, id, n, mSpeech, mSettings);
        }

        // A refused trade must leave stock, gold and the revision exactly as they were.
        void expectRefused(const TradeResult& r, TradeStatus status)
        {
            EXPECT_EQ(r.mStatus, status) << tradeStatusName(r.mStatus);
            EXPECT_EQ(r.mTotal, 0);
        }
    };

    TEST_F(BarterTradeFixture, buyingMovesTheItemToThePlayerAndTheGoldToTheVendor)
    {
        const std::uint32_t before = mModel.revision();
        const TradeResult r = buyItem(kBread, 2);
        ASSERT_EQ(r.mStatus, TradeStatus::Ok) << tradeStatusName(r.mStatus);
        EXPECT_EQ(r.mUnitPrice, 8); // 5 * 1.5525 = 7.76
        EXPECT_EQ(r.mTotal, 16);
        EXPECT_EQ(mModel.count(kPlayerOwner, kBread), 2);
        EXPECT_EQ(mModel.count(kChest, kBread), 2);
        EXPECT_EQ(gold(kPlayerOwner), 84);
        EXPECT_EQ(gold(kChest), 66);
        EXPECT_GT(mModel.revision(), before);
    }

    TEST_F(BarterTradeFixture, sellingMovesTheItemToTheVendorAndTheGoldToThePlayer)
    {
        const TradeResult r = sellItem(kSword, 1);
        ASSERT_EQ(r.mStatus, TradeStatus::Ok) << tradeStatusName(r.mStatus);
        EXPECT_EQ(r.mUnitPrice, 8); // 25 / 3.105 = 8.05
        EXPECT_EQ(r.mTotal, 8);
        EXPECT_EQ(mModel.count(kPlayerOwner, kSword), 1);
        EXPECT_EQ(mModel.count(kChest, kSword), 2);
        EXPECT_EQ(gold(kPlayerOwner), 108);
        EXPECT_EQ(gold(kChest), 42);
    }

    TEST_F(BarterTradeFixture, aSoldItemCanBeBoughtBackAtTheBuyPrice)
    {
        ASSERT_EQ(sellItem(kSword, 2).mStatus, TradeStatus::Ok);
        EXPECT_EQ(mModel.count(kChest, kSword), 3);
        const TradeResult r = buyItem(kSword, 1);
        ASSERT_EQ(r.mStatus, TradeStatus::Ok);
        EXPECT_EQ(r.mUnitPrice, 39);
        EXPECT_EQ(mModel.count(kPlayerOwner, kSword), 1);
    }

    TEST_F(BarterTradeFixture, aBadCountIsRefusedAndChangesNothing)
    {
        const std::uint32_t before = mModel.revision();
        expectRefused(buyItem(kBread, 0), TradeStatus::BadCount);
        expectRefused(buyItem(kBread, -3), TradeStatus::BadCount);
        expectRefused(buyItem(kBread, inventory::kMaxStackCount + 1), TradeStatus::BadCount);
        expectRefused(sellItem(kSword, 0), TradeStatus::BadCount);
        EXPECT_EQ(mModel.revision(), before);
        EXPECT_EQ(gold(kPlayerOwner), 100);
    }

    TEST_F(BarterTradeFixture, anUnknownVendorOrItemIsRefused)
    {
        const std::uint32_t before = mModel.revision();
        expectRefused(buy(mModel, mTable, 0x999, kBread, 1, mSpeech, mSettings), TradeStatus::UnknownOwner);
        expectRefused(buyItem(kSelf | 0x555, 1), TradeStatus::UnknownItem);
        expectRefused(sellItem(kSelf | 0x555, 1), TradeStatus::UnknownItem);
        EXPECT_EQ(mModel.revision(), before);
    }

    TEST_F(BarterTradeFixture, goldAndWorthlessItemsCannotBeTraded)
    {
        expectRefused(buyItem(kGoldItem, 1), TradeStatus::NotTradable);
        expectRefused(sellItem(kGoldItem, 1), TradeStatus::NotTradable);
        expectRefused(buyItem(kJunk, 1), TradeStatus::NotTradable);
        expectRefused(sellItem(kJunk, 1), TradeStatus::NotTradable);
    }

    TEST_F(BarterTradeFixture, theVendorMustHaveTheStockAndThePlayerMustAffordIt)
    {
        const std::uint32_t before = mModel.revision();
        expectRefused(buyItem(kBread, 5), TradeStatus::NotEnoughItems); // the vendor has 4
        expectRefused(buyItem(kPebble, 1), TradeStatus::NotEnoughItems); // the vendor has none
        mModel.setPlayer({ { kGoldItem, 20 } });
        const std::uint32_t poor = mModel.revision();
        expectRefused(buyItem(kBread, 3), TradeStatus::NotEnoughGold); // 24 gold
        EXPECT_EQ(mModel.revision(), poor);
        EXPECT_GT(poor, before);
        EXPECT_EQ(mModel.count(kChest, kBread), 4);
        ASSERT_EQ(buyItem(kBread, 2).mStatus, TradeStatus::Ok); // 16 gold: affordable
    }

    TEST_F(BarterTradeFixture, theVendorOnlyBuysWhatItsListSays)
    {
        expectRefused(sellItem(kPebble, 1), TradeStatus::WontBuy);
        EXPECT_EQ(mModel.count(kPlayerOwner, kPebble), 3);
    }

    TEST_F(BarterTradeFixture, thePlayerMustHoldWhatItSells)
    {
        mVendor.mKeywords.push_back(kKwNoSale);
        mVendor.mListIsExclusion = false;
        expectRefused(sellItem(kBread, 1), TradeStatus::NotEnoughItems); // the player has none
        expectRefused(sellItem(kSword, 3), TradeStatus::NotEnoughItems);
    }

    TEST_F(BarterTradeFixture, theEquippedCopyIsNotForSale)
    {
        ASSERT_EQ(mModel.equip(kSword, { inventory::EquipKind::Weapon, 0 }), inventory::EquipStatus::Ok);
        EXPECT_EQ(sellableCount(mModel, kSword), 1);
        EXPECT_EQ(sellableCount(mModel, kPebble), 3);
        EXPECT_EQ(sellableCount(mModel, kBread), 0);
        ASSERT_EQ(sellItem(kSword, 1).mStatus, TradeStatus::Ok);
        expectRefused(sellItem(kSword, 1), TradeStatus::Equipped);
        EXPECT_TRUE(mModel.isEquipped(kSword));
        EXPECT_EQ(sellableCount(mModel, kSword), 0);
    }

    TEST_F(BarterTradeFixture, aVendorWithoutEnoughGoldCannotBuy)
    {
        ASSERT_TRUE(mModel.transfer(kChest, kPlayerOwner, kGoldItem, 45) == inventory::TransferStatus::Ok);
        EXPECT_EQ(gold(kChest), 5);
        expectRefused(sellItem(kSword, 1), TradeStatus::NotEnoughVendorGold); // 8 gold
        EXPECT_EQ(mModel.count(kPlayerOwner, kSword), 2);
        EXPECT_EQ(gold(kChest), 5);
    }

    TEST_F(BarterTradeFixture, aVendorWithNoGoldAtAllStillSells)
    {
        ASSERT_TRUE(mModel.transfer(kChest, kPlayerOwner, kGoldItem, 50) == inventory::TransferStatus::Ok);
        const TradeResult r = buyItem(kBread, 1);
        ASSERT_EQ(r.mStatus, TradeStatus::Ok);
        EXPECT_EQ(gold(kChest), 8);
    }

    TEST_F(BarterTradeFixture, aTradeIsOneStepForTheSaveChunk)
    {
        const std::size_t before = mModel.touchedCount();
        ASSERT_EQ(buyItem(kBread, 1).mStatus, TradeStatus::Ok);
        EXPECT_EQ(mModel.touchedCount(), before + 2); // the player and the vendor chest are saved
    }

    // ---- the service and its bindings
    // --------------------------------------------------------------------------------

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            EXPECT_EQ(args.size(), it->second.params.size()) << name;
            return it->second.handler->call(args);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }
    BindingValue num(std::int32_t n)
    {
        BindingValue v;
        v.type = BindingType::Int32;
        v.integer = n;
        return v;
    }

    struct BarterServiceFixture : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<BarterService> mBarter;
        StubBindings mBindings;
        int mLateAsks = 0;
        float mSpeechValue = 15.f;

        void SetUp() override
        {
            Bytes all = tes4Header(1.7f, 0, "A", "B", 6, { { "Skyrim.esm", 0 } }, 24, 44);
            const auto kw = [](std::vector<FormId> ids) {
                Bytes b;
                b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(ids.size()))));
                Bytes list;
                for (const FormId id : ids)
                    list.u32(id);
                b.append(sub4("KWDA", list));
                return b;
            };
            Bytes bread = sub4("DATA", Bytes().f32(0.2f));
            bread.append(sub4("ENIT", Bytes().u32(5).u32(0).u32(0).u32(0).u32(0)));
            bread.append(kw({ kKwFood }));
            all.append(group4(Content::fourCC("ALCH"), 0, record4("ALCH", 0, kBread, bread)));
            Bytes sword = sub4("DATA", Bytes().u32(25).f32(9.f).u16(7));
            sword.append(kw({ kKwWeapon }));
            all.append(group4(Content::fourCC("WEAP"), 0, record4("WEAP", 0, kSword, sword)));
            all.append(group4(
                Content::fourCC("MISC"), 0, record4("MISC", 0, kPebble, sub4("DATA", Bytes().u32(10).f32(0.5f)))));
            all.append(group4(
                Content::fourCC("MISC"), 0, record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f)))));
            Bytes cont;
            cont.append(sub4("CNTO", Bytes().u32(0x0000000F).u32(50)));
            cont.append(sub4("CNTO", Bytes().u32(kBread).u32(4)));
            cont.append(sub4("CNTO", Bytes().u32(kSword).u32(1)));
            all.append(group4(Content::fourCC("CONT"), 0, record4("CONT", 0, kCont, cont)));
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_barter_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));

            inventory::Hooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mName = [](FormId id) -> std::string {
                if (id == kBread)
                    return "Bread";
                if (id == kSword)
                    return "Sword";
                if (id == kPebble)
                    return "Pebble";
                if (id == kGoldItem)
                    return "Gold";
                return {};
            };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(hooks), 100.f, 1);
            ASSERT_TRUE(mInventory->declare(mBindings).ok);

            ServiceHooks sh;
            sh.mVendorOf = [this](FormId npc, VendorInfo& out) {
                if (npc == kNpc)
                {
                    out = vendorOf({ kKwFood, kKwWeapon });
                    return VendorLookup::Ready;
                }
                if (npc == kNpcLate)
                {
                    if (++mLateAsks < 4)
                        return VendorLookup::Pending;
                    out = vendorOf({ kKwFood });
                    return VendorLookup::Ready;
                }
                if (npc == kNpcPlain)
                    return VendorLookup::NotVendor;
                return VendorLookup::Pending; // never ready
            };
            sh.mSpeech = [this]() { return mSpeechValue; };
            sh.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mBarter = std::make_unique<BarterService>(*mInventory, std::move(sh));
            ASSERT_TRUE(mBarter->declare(mBindings).ok);
        }

        void TearDown() override
        {
            mBarter.reset();
            mInventory.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        void waitInventory()
        {
            for (int i = 0; i < 4000 && !mInventory->poll(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mInventory->ready());
        }

        std::string text(const std::string& fn, std::vector<BindingValue> args = {})
        {
            return std::string(mBindings.call(fn, std::move(args)).value.text);
        }
        std::int32_t integer(const std::string& fn, std::vector<BindingValue> args = {})
        {
            return mBindings.call(fn, std::move(args)).value.integer;
        }
        void open(FormId npc = kNpc)
        {
            mBindings.call("open", { str("1a66e"), str(inventory::formatId(npc)), str("Hulda") });
        }
        void buyCall(FormId item, int n) { mBindings.call("buy", { str(inventory::formatId(item)), num(n) }); }
        void sellCall(FormId item, int n) { mBindings.call("sell", { str(inventory::formatId(item)), num(n) }); }
        std::int32_t held(std::uint64_t owner, FormId item) { return mInventory->model().count(owner, item); }
    };

    TEST_F(BarterServiceFixture, theNamespaceIsDeclaredWithReadAndMutateFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.barter");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 0);
        for (const char* read : { "state", "reason", "owner", "vendor_gold", "buy_price", "sell_price", "sellable",
                 "last_result", "trades" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(read), 1u) << read;
            EXPECT_EQ(mBindings.mFunctions[read].access, BindingAccess::Read) << read;
            EXPECT_TRUE(mBindings.mFunctions[read].hasResult) << read;
        }
        for (const char* mutate : { "open", "buy", "sell", "close" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(mutate), 1u) << mutate;
            EXPECT_EQ(mBindings.mFunctions[mutate].access, BindingAccess::Mutate) << mutate;
            EXPECT_FALSE(mBindings.mFunctions[mutate].hasResult) << mutate;
        }
    }

    TEST_F(BarterServiceFixture, thereIsNoSessionAtFirst)
    {
        EXPECT_EQ(text("state"), "none");
        EXPECT_EQ(text("owner"), "");
        EXPECT_EQ(text("last_result"), "");
        EXPECT_EQ(integer("trades"), 0);
        EXPECT_EQ(integer("vendor_gold"), 0);
    }

    TEST_F(BarterServiceFixture, aSessionIsPendingUntilTheItemTableIsBuiltThenReady)
    {
        open();
        EXPECT_EQ(text("state"), "pending");
        EXPECT_EQ(text("owner"), "");
        waitInventory();
        mBarter->poll();
        EXPECT_EQ(text("state"), "ready");
        EXPECT_EQ(text("owner"), inventory::formatId(kChest));
        EXPECT_EQ(text("reason"), "");
        EXPECT_EQ(integer("vendor_gold"), 50);
        EXPECT_EQ(held(kChest, kBread), 4);
        EXPECT_EQ(held(kChest, kSword), 1);
    }

    TEST_F(BarterServiceFixture, aNonVendorIsRefusedWithAReason)
    {
        waitInventory();
        open(kNpcPlain);
        EXPECT_EQ(text("state"), "refused");
        EXPECT_NE(text("reason"), "");
        EXPECT_EQ(text("owner"), "");
        buyCall(kBread, 1);
        EXPECT_EQ(held(kPlayerOwner, kBread), 0);
    }

    TEST_F(BarterServiceFixture, aVendorWhoseRecordsAreNotReadYetIsRetriedEveryPoll)
    {
        waitInventory();
        open(kNpcLate);
        EXPECT_EQ(text("state"), "pending");
        for (int i = 0; i < 10 && mBarter->state() == BarterService::State::Pending; ++i)
            mBarter->poll();
        EXPECT_EQ(text("state"), "ready");
        EXPECT_EQ(mLateAsks, 4);
    }

    TEST_F(BarterServiceFixture, aSessionThatNeverGetsItsDataIsRefusedAfterTheLimit)
    {
        waitInventory();
        open(kSelf | 0x999);
        for (std::uint32_t i = 0; i < BarterService::kMaxPendingPolls + 2; ++i)
            mBarter->poll();
        EXPECT_EQ(text("state"), "refused");
        EXPECT_NE(text("reason"), "");
    }

    TEST_F(BarterServiceFixture, buyingThroughTheBindingsMovesItemAndGold)
    {
        waitInventory();
        open();
        mBarter->poll();
        ASSERT_EQ(text("state"), "ready");
        EXPECT_EQ(integer("buy_price", { str(inventory::formatId(kBread)) }), 8);
        buyCall(kBread, 2);
        EXPECT_EQ(held(kPlayerOwner, kBread), 2);
        EXPECT_EQ(held(kPlayerOwner, kGoldItem), 84);
        EXPECT_EQ(integer("vendor_gold"), 66);
        EXPECT_EQ(text("last_result"), "ok");
        EXPECT_EQ(integer("trades"), 1);
    }

    TEST_F(BarterServiceFixture, sellingThroughTheBindingsMovesItemAndGold)
    {
        waitInventory();
        mInventory->model().setPlayer({ { kGoldItem, 100 }, { kSword, 2 }, { kPebble, 3 } });
        open();
        mBarter->poll();
        ASSERT_EQ(text("state"), "ready");
        EXPECT_EQ(integer("sell_price", { str(inventory::formatId(kSword)) }), 8);
        EXPECT_EQ(integer("sell_price", { str(inventory::formatId(kPebble)) }), 0); // the list refuses it
        EXPECT_EQ(integer("sellable", { str(inventory::formatId(kSword)) }), 2);
        sellCall(kSword, 1);
        EXPECT_EQ(held(kPlayerOwner, kSword), 1);
        EXPECT_EQ(held(kPlayerOwner, kGoldItem), 108);
        EXPECT_EQ(integer("vendor_gold"), 42);
        sellCall(kPebble, 1);
        EXPECT_EQ(text("last_result"), "the merchant does not buy that");
        EXPECT_EQ(held(kPlayerOwner, kPebble), 3);
        EXPECT_EQ(integer("trades"), 2);
    }

    TEST_F(BarterServiceFixture, aRefusedTradeSaysWhy)
    {
        waitInventory();
        open();
        mBarter->poll();
        ASSERT_EQ(text("state"), "ready");
        mInventory->model().setPlayer({ { kGoldItem, 3 } });
        buyCall(kBread, 1);
        EXPECT_EQ(text("last_result"), "not enough gold");
        buyCall(kBread, 9);
        EXPECT_EQ(text("last_result"), "the merchant has not got that many");
        buyCall(kBread, 0);
        EXPECT_EQ(text("last_result"), "bad count");
        EXPECT_EQ(held(kChest, kBread), 4);
    }

    TEST_F(BarterServiceFixture, noTradeHappensWithoutAReadySession)
    {
        waitInventory();
        buyCall(kBread, 1);
        EXPECT_EQ(held(kPlayerOwner, kBread), 0);
        EXPECT_EQ(text("last_result"), "no merchant");
        open();
        mBarter->poll();
        mBindings.call("close");
        EXPECT_EQ(text("state"), "none");
        buyCall(kBread, 1);
        EXPECT_EQ(held(kPlayerOwner, kBread), 0);
    }

    TEST_F(BarterServiceFixture, reopeningTheSameMerchantKeepsWhatWasTraded)
    {
        waitInventory();
        open();
        mBarter->poll();
        buyCall(kBread, 3);
        mBindings.call("close");
        open();
        mBarter->poll();
        ASSERT_EQ(text("state"), "ready");
        EXPECT_EQ(held(kChest, kBread), 1);
        EXPECT_EQ(integer("vendor_gold"), 74);
    }

    TEST_F(BarterServiceFixture, thePriceFollowsTheSpeechSkill)
    {
        waitInventory();
        mSpeechValue = 100.f;
        open();
        mBarter->poll();
        ASSERT_EQ(text("state"), "ready");
        EXPECT_EQ(integer("buy_price", { str(inventory::formatId(kSword)) }), 26); // 25 * 1.05 = 26.25
        EXPECT_EQ(integer("sell_price", { str(inventory::formatId(kSword)) }), 13); // 12.5 rounds up
    }

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    TEST_F(BarterServiceFixture, theNewStockAndGoldGoThroughTheInventoryChunk)
    {
        waitInventory();
        open();
        mBarter->poll();
        buyCall(kBread, 3);
        MemoryWriter writer;
        mInventory->chunk().write(writer);
        InventoryModel fresh;
        inventory::InventoryChunk chunk(fresh);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(chunk.read(reader, inventory::InventoryChunk::kVersion)) << chunk.refusal();
        EXPECT_EQ(fresh.count(kChest, kBread), 1);
        EXPECT_EQ(fresh.count(kChest, kGoldItem), 74);
        EXPECT_EQ(fresh.count(kPlayerOwner, kBread), 3);
        EXPECT_EQ(fresh.count(kPlayerOwner, kGoldItem), 76);
    }
}
