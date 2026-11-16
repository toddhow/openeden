// Task 0204: the two hooks the crime rules put into the inventory (a take from a container) and the barter (a merchant
// that is no fence refuses stolen goods). Generated plugin written to a temporary file; no game data. Written after the
// hooks; each test was checked by removing the hook call (see the stage log of the task).
#include <game_skyrimse/barter_service.hpp>
#include <game_skyrimse/inventory_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using inventory::kGoldItem;
    using inventory::kPlayerOwner;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kSword = kSelf | 0x101;
    constexpr FormId kPebble = kSelf | 0x102;
    constexpr FormId kCont = kSelf | 0x200;
constexpr FormId kVendorCont = kSelf | 0x201; // the merchant's stock: gold
    constexpr std::uint64_t kChestRef = kSelf | 0x700; // an owned container in the world
    constexpr std::uint64_t kVendorRef = kSelf | 0x701; // the merchant's chest
    constexpr FormId kNpcBase = kSelf | 0x900;
    constexpr FormId kCorpseBase = kSelf | 0x901;

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            for (const auto& f : d.functions)
                mFunctions[std::string(d.name) + "." + std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            return it->second.handler->call(args);
        }
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

    struct Taken
    {
        std::uint64_t mOwner;
        FormId mItem;
        std::int32_t mCount;
    };

    struct CrimeHooksTest : ::testing::Test
    {
        std::filesystem::path mDir, mPath;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<barter::BarterService> mBarter;
        StubBindings mBindings;
        std::vector<Taken> mTaken;
        std::map<FormId, std::int32_t> mStolen;
        std::vector<std::pair<FormId, std::int32_t>> mSoldStolen;
        bool mFence = false;

        void SetUp() override
        {
            Bytes all = tes4Header(1.7f, 0, "A", "B", 6, { { "Skyrim.esm", 0 } }, 24, 44);
            all.append(group4(Content::fourCC("WEAP"), 0,
                record4("WEAP", 0, kSword, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)))));
            all.append(group4(Content::fourCC("MISC"), 0,
                record4("MISC", 0, kPebble, sub4("DATA", Bytes().u32(10).f32(0.5f)))));
            all.append(group4(Content::fourCC("MISC"), 0,
                record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f)))));
            Bytes cont;
            cont.append(sub4("CNTO", Bytes().u32(kPebble).u32(3)));
            cont.append(sub4("CNTO", Bytes().u32(kSword).u32(1)));
            all.append(group4(Content::fourCC("CONT"), 0, record4("CONT", 0, kCont, cont)));
            all.append(group4(Content::fourCC("CONT"), 0,
                record4("CONT", 0, kVendorCont, sub4("CNTO", Bytes().u32(0x0000000F).u32(500)))));
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_crimehooks_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const std::string bytes = all.str();
            std::ofstream(mPath, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

            inventory::Hooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mName = [](FormId) { return std::string("thing"); };
            hooks.mNpcContents = [](FormId base, std::vector<InventoryEntry>& out) {
                if (base != kCorpseBase)
                    return false;
                out.push_back({ kPebble, 2 });
                return true;
            };
            hooks.mTaken = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                mTaken.push_back({ owner, item, count });
            };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(hooks), 100.f, 1);
            ASSERT_TRUE(mInventory->declare(mBindings).ok);

            barter::ServiceHooks sh;
            sh.mVendorOf = [this](FormId npc, barter::VendorInfo& out) {
                if (npc != kNpcBase)
                    return barter::VendorLookup::NotVendor;
                out = {};
                out.mChestRef = static_cast<FormId>(kVendorRef);
                out.mChestBase = kVendorCont;
                out.mListIsExclusion = true; // an empty exclusion list: buys everything
                out.mBuysStolen = mFence;
                return barter::VendorLookup::Ready;
            };
            sh.mSpeech = []() { return 15.f; };
            sh.mStolenHeld = [this](FormId item) {
                const auto it = mStolen.find(item);
                return it == mStolen.end() ? 0 : it->second;
            };
            sh.mStolenSold = [this](FormId item, std::int32_t count) { mSoldStolen.emplace_back(item, count); };
            mBarter = std::make_unique<barter::BarterService>(*mInventory, std::move(sh));
            ASSERT_TRUE(mBarter->declare(mBindings).ok);
            for (int i = 0; i < 4000 && !mInventory->poll(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mInventory->ready());
        }

        void TearDown() override
        {
            mBarter.reset();
            mInventory.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        std::string id(std::uint64_t v) { return inventory::formatId(v); }
        void openContainer()
        {
            mBindings.call("skyrimse.inventory.open", { str(id(kChestRef)), str(id(kCont)), str("container") });
        }
        void transfer(const std::string& from, const std::string& to, FormId item, int n)
        {
            mBindings.call("skyrimse.inventory.transfer", { str(from), str(to), str(id(item)), num(n) });
        }
    };

    TEST_F(CrimeHooksTest, takingFromAContainerTellsTheHook)
    {
        openContainer();
        transfer(id(kChestRef), "player", kPebble, 2);
        ASSERT_EQ(mTaken.size(), 1u);
        EXPECT_EQ(mTaken[0].mOwner, kChestRef);
        EXPECT_EQ(mTaken[0].mItem, kPebble);
        EXPECT_EQ(mTaken[0].mCount, 2);
    }

    TEST_F(CrimeHooksTest, aTransferThatFailsOrGoesTheOtherWayIsNotATake)
    {
        openContainer();
        transfer(id(kChestRef), "player", kPebble, 9); // the container holds 3
        EXPECT_TRUE(mTaken.empty());
        transfer(id(kChestRef), "player", kPebble, 1);
        mTaken.clear();
        transfer("player", id(kChestRef), kPebble, 1); // putting back
        EXPECT_TRUE(mTaken.empty());
    }

    TEST_F(CrimeHooksTest, lootingACorpseIsNoTake)
    {
        mBindings.call("skyrimse.inventory.open", { str(id(kSelf | 0x800)), str(id(kCorpseBase)), str("actor") });
        transfer(id(kSelf | 0x800), "player", kPebble, 1);
        EXPECT_TRUE(mTaken.empty());
    }

    TEST_F(CrimeHooksTest, takeAllTellsTheHookOncePerStack)
    {
        openContainer();
        mBindings.call("skyrimse.inventory.take_all", { str(id(kChestRef)), str("player") });
        ASSERT_EQ(mTaken.size(), 2u);
        std::int32_t total = 0;
        for (const Taken& t : mTaken)
            total += t.mCount;
        EXPECT_EQ(total, 4);
    }

    // ---- barter ------------------------------------------------------------------------------------------------

    void openBarter(CrimeHooksTest& t)
    {
        t.mBindings.call("skyrimse.barter.open", { str(t.id(kVendorRef)), str(t.id(kNpcBase)), str("Hulda") });
        for (int i = 0; i < 5; ++i)
            t.mBarter->poll();
        ASSERT_EQ(t.mBarter->state(), barter::BarterService::State::Ready);
    }

    TEST_F(CrimeHooksTest, aMerchantThatIsNoFenceRefusesStolenGoods)
    {
        mInventory->model().setPlayer({ { kSword, 2 }, { kGoldItem, 5 } });
        mStolen[kSword] = 1; // one of the two is stolen
        openBarter(*this);
        mBindings.call("skyrimse.barter.sell", { str(id(kSword)), num(2) });
        EXPECT_EQ(mInventory->model().count(kPlayerOwner, kSword), 2) << "selling the stolen one is refused";
        EXPECT_NE(mBarter->lastResult().find("stolen"), std::string::npos);
        mBindings.call("skyrimse.barter.sell", { str(id(kSword)), num(1) });
        EXPECT_EQ(mInventory->model().count(kPlayerOwner, kSword), 1) << "the clean one sells";
        EXPECT_TRUE(mSoldStolen.empty());
    }

    TEST_F(CrimeHooksTest, aFenceBuysStolenGoodsAndTheLedgerIsTold)
    {
        mFence = true;
        mInventory->model().setPlayer({ { kSword, 2 }, { kGoldItem, 5 } });
        mStolen[kSword] = 2;
        openBarter(*this);
        mBindings.call("skyrimse.barter.sell", { str(id(kSword)), num(2) });
        EXPECT_EQ(mInventory->model().count(kPlayerOwner, kSword), 0);
        ASSERT_EQ(mSoldStolen.size(), 1u);
        EXPECT_EQ(mSoldStolen[0].second, 2);
    }

    TEST_F(CrimeHooksTest, goodsThatWereNeverStolenSellAsBefore)
    {
        mInventory->model().setPlayer({ { kSword, 1 }, { kGoldItem, 5 } });
        openBarter(*this);
        mBindings.call("skyrimse.barter.sell", { str(id(kSword)), num(1) });
        EXPECT_EQ(mInventory->model().count(kPlayerOwner, kSword), 0);
    }
}
