// Task 0184 (loot, equip and keys; milestone M-S6): the player's equipped items in the inventory model, their save
// chunk
// "equipment", the ALCH effect decode, and the service's equip, unequip and use over a stub bindings service. Generated
// plugin written to a temporary file; no game data. Written from the task spec before the code and run red against
// stubs
// first (see the task's stage log).
#include <game_skyrimse/inventory_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    using namespace game_skyrimse::inventory;
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kSwordA = kSelf | 0x101;
    constexpr FormId kSwordB = kSelf | 0x102;
    constexpr FormId kHelm = kSelf | 0x110; // slot 0x1
    constexpr FormId kCuirass = kSelf | 0x111; // slot 0x4
    constexpr FormId kBoots = kSelf | 0x112; // slot 0x80
    constexpr FormId kHelmCuirass = kSelf | 0x113; // slots 0x5
    constexpr FormId kHealing = kSelf | 0x120; // an instant 25 point health potion
    constexpr FormId kSlowHeal = kSelf | 0x121; // 10 health over 30 seconds
    constexpr FormId kPoison = kSelf | 0x122; // a hostile health effect
    constexpr FormId kMisc = kSelf | 0x130;
    constexpr FormId kEffHeal = kSelf | 0x200;
    constexpr FormId kEffPoison = kSelf | 0x201;

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

    EquipInfo weapon()
    {
        return { EquipKind::Weapon, 0 };
    }
    EquipInfo armour(std::uint32_t mask)
    {
        return { EquipKind::Armor, mask };
    }

    // The player holds one of each item named.
    InventoryModel holding(std::vector<FormId> items)
    {
        InventoryModel m;
        std::vector<Stack> stacks;
        for (const FormId id : items)
            stacks.push_back({ id, 1 });
        m.setPlayer(std::move(stacks));
        return m;
    }

    std::vector<FormId> ids(const std::vector<EquippedItem>& items)
    {
        std::vector<FormId> out;
        for (const EquippedItem& e : items)
            out.push_back(e.mItem);
        return out;
    }

    // ---- the model ------------------------------------------------------------------------------------------------

    TEST(SkyrimEquipModel, anItemTheVisitorDoesNotHoldCannotBeEquipped)
    {
        InventoryModel m = holding({ kSwordA });
        EXPECT_EQ(m.equip(kSwordB, weapon()), EquipStatus::NotHeld);
        EXPECT_TRUE(m.equipped().empty());
        EXPECT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_TRUE(m.isEquipped(kSwordA));
    }

    TEST(SkyrimEquipModel, aPotionOrAnItemOfNoKindIsNotEquippable)
    {
        InventoryModel m = holding({ kHealing, kMisc });
        EXPECT_EQ(m.equip(kHealing, { EquipKind::Potion, 0 }), EquipStatus::NotEquippable);
        EXPECT_EQ(m.equip(kMisc, {}), EquipStatus::NotEquippable);
        EXPECT_TRUE(m.equipped().empty());
    }

    TEST(SkyrimEquipModel, aWeaponReplacesTheEquippedWeapon)
    {
        InventoryModel m = holding({ kSwordA, kSwordB });
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        ASSERT_EQ(m.equip(kSwordB, weapon()), EquipStatus::Ok);
        EXPECT_EQ(ids(m.equipped()), std::vector<FormId>{ kSwordB });
        EXPECT_FALSE(m.isEquipped(kSwordA));
    }

    TEST(SkyrimEquipModel, anArmourReplacesEveryPieceWhoseSlotsOverlap)
    {
        InventoryModel m = holding({ kHelm, kCuirass, kBoots, kHelmCuirass, kSwordA });
        ASSERT_EQ(m.equip(kHelm, armour(0x1)), EquipStatus::Ok);
        ASSERT_EQ(m.equip(kCuirass, armour(0x4)), EquipStatus::Ok);
        ASSERT_EQ(m.equip(kBoots, armour(0x80)), EquipStatus::Ok);
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_EQ(m.equipped().size(), 4u);
        ASSERT_EQ(m.equip(kHelmCuirass, armour(0x5)), EquipStatus::Ok); // head and body at once
        const std::vector<FormId> got = ids(m.equipped());
        EXPECT_EQ(got.size(), 3u);
        EXPECT_TRUE(m.isEquipped(kHelmCuirass));
        EXPECT_TRUE(m.isEquipped(kBoots));
        EXPECT_TRUE(m.isEquipped(kSwordA));
        EXPECT_FALSE(m.isEquipped(kHelm));
        EXPECT_FALSE(m.isEquipped(kCuirass));
    }

    TEST(SkyrimEquipModel, equippingTheSameItemAgainChangesNothingButIsNotAnError)
    {
        InventoryModel m = holding({ kSwordA });
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        const std::uint32_t revision = m.equipmentRevision();
        EXPECT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_EQ(m.equipped().size(), 1u);
        EXPECT_EQ(m.equipmentRevision(), revision);
    }

    TEST(SkyrimEquipModel, unequipTakesItOffAndRefusesWhatIsNotWorn)
    {
        InventoryModel m = holding({ kSwordA, kSwordB });
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_EQ(m.unequip(kSwordB), EquipStatus::NotEquipped);
        EXPECT_EQ(m.unequip(kSwordA), EquipStatus::Ok);
        EXPECT_TRUE(m.equipped().empty());
        EXPECT_EQ(m.unequip(kSwordA), EquipStatus::NotEquipped);
    }

    TEST(SkyrimEquipModel, theRevisionsMoveOnlyWhenSomethingChanges)
    {
        InventoryModel m = holding({ kSwordA });
        const std::uint32_t equipRev = m.equipmentRevision();
        const std::uint32_t rev = m.revision();
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_NE(m.equipmentRevision(), equipRev);
        EXPECT_NE(m.revision(), rev);
        const std::uint32_t rev2 = m.revision();
        EXPECT_EQ(m.equip(kHealing, weapon()), EquipStatus::NotHeld); // a refusal changes nothing
        EXPECT_EQ(m.revision(), rev2);
    }

    TEST(SkyrimEquipModel, movingTheLastOfAnEquippedItemOffThePlayerUnequipsItForGood)
    {
        InventoryModel m = holding({ kSwordA });
        ASSERT_TRUE(m.ensureOwner(0x10, {}));
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        const std::uint32_t equipRev = m.equipmentRevision();
        ASSERT_EQ(m.transfer(kPlayerOwner, 0x10, kSwordA, 1), TransferStatus::Ok);
        EXPECT_TRUE(m.equipped().empty());
        EXPECT_NE(m.equipmentRevision(), equipRev);
        // taking it back does not put it on again
        ASSERT_EQ(m.transfer(0x10, kPlayerOwner, kSwordA, 1), TransferStatus::Ok);
        EXPECT_TRUE(m.equipped().empty());
        EXPECT_EQ(m.equippedRawCount(), 0u);
    }

    TEST(SkyrimEquipModel, movingOneOfTwoLeavesTheOtherEquipped)
    {
        InventoryModel m;
        m.setPlayer({ { kSwordA, 2 } });
        ASSERT_TRUE(m.ensureOwner(0x10, {}));
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        const std::uint32_t equipRev = m.equipmentRevision();
        ASSERT_EQ(m.transfer(kPlayerOwner, 0x10, kSwordA, 1), TransferStatus::Ok);
        EXPECT_TRUE(m.isEquipped(kSwordA));
        EXPECT_EQ(m.equipmentRevision(), equipRev);
    }

    TEST(SkyrimEquipModel, consumeRemovesItemsAndUnequipsWhenTheLastGoes)
    {
        InventoryModel m;
        m.setPlayer({ { kHealing, 2 }, { kSwordA, 1 } });
        EXPECT_EQ(m.consume(kPlayerOwner, kHealing, 1), TransferStatus::Ok);
        EXPECT_EQ(m.count(kPlayerOwner, kHealing), 1);
        EXPECT_EQ(m.consume(kPlayerOwner, kHealing, 2), TransferStatus::NotEnough);
        EXPECT_EQ(m.consume(kPlayerOwner, kHealing, 0), TransferStatus::BadCount);
        EXPECT_EQ(m.consume(0x99, kHealing, 1), TransferStatus::UnknownOwner);
        EXPECT_EQ(m.count(kPlayerOwner, kHealing), 1);
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EXPECT_EQ(m.consume(kPlayerOwner, kSwordA, 1), TransferStatus::Ok);
        EXPECT_TRUE(m.equipped().empty());
    }

    TEST(SkyrimEquipModel, anEquippedItemTheInventoryNoLongerHoldsIsNotListed)
    {
        // the equipment chunk may be read before the inventory chunk: setEquipped does not check holdings
        InventoryModel m = holding({ kSwordA });
        m.setEquipped({ { kSwordB, EquipKind::Weapon, 0 } });
        EXPECT_TRUE(m.equipped().empty());
        EXPECT_FALSE(m.isEquipped(kSwordB));
        m.setEquipped({ { kSwordA, EquipKind::Weapon, 0 }, { kSwordA, EquipKind::Weapon, 0 } });
        EXPECT_EQ(m.equipped().size(), 1u); // a duplicate keeps the first
    }

    // ---- the chunk ------------------------------------------------------------------------------------------------

    TEST(SkyrimEquipChunk, theBytesAreCanonicalAndRoundTrip)
    {
        InventoryModel m = holding({ kSwordA, kHelm, kBoots });
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        ASSERT_EQ(m.equip(kHelm, armour(0x1)), EquipStatus::Ok);
        EquipmentChunk chunk(m);
        EXPECT_EQ(chunk.chunkName(), "equipment");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemoryWriter a, b;
        chunk.write(a);
        chunk.write(b);
        EXPECT_EQ(a.mBytes, b.mBytes);
        // u32 count, then 9 bytes an item
        EXPECT_EQ(a.mBytes.size(), 4u + 2u * 9u);

        InventoryModel back = holding({ kSwordA, kHelm, kBoots });
        EquipmentChunk readChunk(back);
        MemoryReader reader(a.mBytes);
        ASSERT_TRUE(readChunk.read(reader, 1));
        EXPECT_TRUE(back.isEquipped(kSwordA));
        EXPECT_TRUE(back.isEquipped(kHelm));
        EXPECT_FALSE(back.isEquipped(kBoots));
        MemoryWriter c;
        readChunk.write(c);
        EXPECT_EQ(a.mBytes, c.mBytes);
        // a restore moves the equipment revision so that the combat feed is rebuilt
        InventoryModel again = holding({ kSwordA, kHelm });
        const std::uint32_t before = again.equipmentRevision();
        EquipmentChunk third(again);
        MemoryReader reader2(a.mBytes);
        ASSERT_TRUE(third.read(reader2, 1));
        EXPECT_NE(again.equipmentRevision(), before);
    }

    TEST(SkyrimEquipChunk, aDamagedChunkIsRefusedAndChangesNothing)
    {
        InventoryModel m = holding({ kSwordA, kHelm });
        ASSERT_EQ(m.equip(kSwordA, weapon()), EquipStatus::Ok);
        EquipmentChunk source(m);
        MemoryWriter good;
        source.write(good);

        InventoryModel target = holding({ kSwordA, kHelm });
        ASSERT_EQ(target.equip(kHelm, armour(0x1)), EquipStatus::Ok);
        EquipmentChunk chunk(target);
        std::string reasons;
        chunk.setRefusalSink([&](const std::string& why) { reasons += why + ";"; });
        const auto refused = [&](std::vector<unsigned char> bytes, std::uint32_t version) {
            MemoryReader reader(bytes);
            return !chunk.read(reader, version);
        };
        EXPECT_TRUE(refused(good.mBytes, 2)); // a version it does not know
        EXPECT_TRUE(refused(std::vector<unsigned char>(good.mBytes.begin(), good.mBytes.end() - 2), 1));
        std::vector<unsigned char> trailing = good.mBytes;
        trailing.push_back(0);
        EXPECT_TRUE(refused(trailing, 1));
        std::vector<unsigned char> badKind = good.mBytes;
        badKind[4 + 4] = 9; // the kind byte of the first item
        EXPECT_TRUE(refused(badKind, 1));
        std::vector<unsigned char> huge = good.mBytes;
        const std::uint32_t big = 0x7FFFFFFF;
        std::memcpy(huge.data(), &big, 4);
        EXPECT_TRUE(refused(huge, 1));
        EXPECT_FALSE(reasons.empty());
        EXPECT_TRUE(target.isEquipped(kHelm)); // untouched
        EXPECT_FALSE(target.isEquipped(kSwordA));
        EXPECT_FALSE(chunk.refusal().empty());
    }

    // ---- the test aid ---------------------------------------------------------------------------------------------

    TEST(SkyrimStackList, readsHexIdsWithOptionalCounts)
    {
        std::vector<Stack> out;
        ASSERT_TRUE(parseStackList("0008715f, 00012EB7:2 ,1:300", out));
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0], (Stack{ 0x0008715F, 1 }));
        EXPECT_EQ(out[1], (Stack{ 0x00012EB7, 2 }));
        EXPECT_EQ(out[2], (Stack{ 0x1, 300 }));
        ASSERT_TRUE(parseStackList("", out));
        EXPECT_TRUE(out.empty());
        ASSERT_TRUE(parseStackList("  ", out));
        EXPECT_TRUE(out.empty());
    }

    TEST(SkyrimStackList, aMalformedEntryRefusesTheWholeListAndTouchesNothing)
    {
        std::vector<Stack> out{ { 0x5, 5 } };
        for (const char* bad : { "xyz", "0008715f:0", "0008715f:-1", "0008715f:", ":3", "0", "0008715f,,00012eb7",
                 "123456789", "0008715f:99999999", "0008715f:1:2", "0008715f," })
        {
            EXPECT_FALSE(parseStackList(bad, out)) << bad;
            ASSERT_EQ(out.size(), 1u) << bad;
            EXPECT_EQ(out[0], (Stack{ 0x5, 5 })) << bad;
        }
        std::string many;
        for (int i = 1; i <= 65; ++i)
            many += (many.empty() ? "" : ",") + std::to_string(i);
        EXPECT_FALSE(parseStackList(many, out));
    }

    // ---- the service ----------------------------------------------------------------------------------------------

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
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args)
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

    Bytes effects(Bytes alch, FormId effect, float magnitude, std::uint32_t duration)
    {
        alch.append(sub4("EFID", Bytes().u32(effect)));
        alch.append(sub4("EFIT", Bytes().f32(magnitude).u32(0).u32(duration)));
        return alch;
    }

    Bytes testPlugin()
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        const auto data
            = [](std::uint32_t value, float weight) { return sub4("DATA", Bytes().u32(value).f32(weight)); };
        all.append(group4(Content::fourCC("WEAP"), 0,
            record4("WEAP", 0, kSwordA, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)))
                .append(record4("WEAP", 0, kSwordB, sub4("DATA", Bytes().u32(30).f32(10.f).u16(8))))));
        all.append(group4(Content::fourCC("ARMO"), 0,
            record4("ARMO", 0, kHelm, data(60, 3.f))
                .append(record4("ARMO", 0, kCuirass, data(100, 20.f)))
                .append(record4("ARMO", 0, kBoots, data(40, 6.f)))
                .append(record4("ARMO", 0, kHelmCuirass, data(150, 22.f)))));
        const auto potion = [&](FormId id, FormId effect, float magnitude, std::uint32_t duration) {
            Bytes alch;
            alch.append(sub4("DATA", Bytes().f32(0.5f)));
            alch.append(sub4("ENIT", Bytes().u32(30).u32(0).u32(0).u32(0).u32(0)));
            return record4("ALCH", 0, id, effects(alch, effect, magnitude, duration));
        };
        all.append(group4(Content::fourCC("ALCH"), 0,
            potion(kHealing, kEffHeal, 25.f, 0)
                .append(potion(kSlowHeal, kEffHeal, 10.f, 30))
                .append(potion(kPoison, kEffPoison, 20.f, 0))));
        all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, kMisc, data(5, 1.f))));
        all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, 0x0000000F, data(1, 0.f))));
        return all;
    }

    struct EquipService : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::vector<float> mHealed;
        bool mHealWorks = true;
        std::unique_ptr<InventoryService> mService;
        StubBindings mBindings;

        void SetUp() override
        {
            const Bytes all = testPlugin();
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_equip_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            Hooks hooks;
            hooks.mInputs = [this](Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            hooks.mEquipInfo = [](FormId id, EquipInfo& out) {
                if (id == kSwordA || id == kSwordB)
                    out = { EquipKind::Weapon, 0 };
                else if (id == kHelm)
                    out = { EquipKind::Armor, 0x1 };
                else if (id == kCuirass)
                    out = { EquipKind::Armor, 0x4 };
                else if (id == kBoots)
                    out = { EquipKind::Armor, 0x80 };
                else if (id == kHelmCuirass)
                    out = { EquipKind::Armor, 0x5 };
                else
                    return false;
                return true;
            };
            hooks.mHealthOf = [](FormId effect, float magnitude, std::uint32_t duration) {
                return effect == kEffHeal && duration == 0 ? magnitude : 0.f; // the poison is hostile
            };
            hooks.mApplyHealth = [this](float amount) {
                if (!mHealWorks)
                    return false;
                mHealed.push_back(amount);
                return true;
            };
            mService = std::make_unique<InventoryService>(std::move(hooks), 100.f, 1);
            ASSERT_TRUE(mService->declare(mBindings).ok);
            waitReady();
        }

        void TearDown() override
        {
            mService.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        void waitReady()
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!mService->poll() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mService->ready());
        }

        void give(FormId item, std::int32_t count)
        {
            std::vector<Stack> stacks = mService->model().stacks(kPlayerOwner);
            stacks.push_back({ item, count });
            mService->model().setPlayer(std::move(stacks));
        }
    };

    TEST_F(EquipService, anAlchRecordsEffectsAreDecoded)
    {
        const ItemStat* heal = mService->table().item(kHealing);
        ASSERT_NE(heal, nullptr);
        ASSERT_EQ(heal->mEffects.size(), 1u);
        EXPECT_EQ(heal->mEffects[0].mEffect, kEffHeal);
        EXPECT_FLOAT_EQ(heal->mEffects[0].mMagnitude, 25.f);
        EXPECT_EQ(heal->mEffects[0].mDuration, 0u);
        const ItemStat* slow = mService->table().item(kSlowHeal);
        ASSERT_NE(slow, nullptr);
        EXPECT_EQ(slow->mEffects[0].mDuration, 30u);
        const ItemStat* sword = mService->table().item(kSwordA);
        ASSERT_NE(sword, nullptr);
        EXPECT_TRUE(sword->mEffects.empty());
    }

    TEST_F(EquipService, healthOfSumsTheInstantHealthEffectsOnly)
    {
        EXPECT_FLOAT_EQ(mService->healthOf(kHealing), 25.f);
        EXPECT_FLOAT_EQ(mService->healthOf(kSlowHeal), 0.f); // lasts: not instant
        EXPECT_FLOAT_EQ(mService->healthOf(kPoison), 0.f); // hostile
        EXPECT_FLOAT_EQ(mService->healthOf(kSwordA), 0.f);
    }

    TEST_F(EquipService, theEquipInfoComesFromTheHookThenTheCategory)
    {
        EXPECT_EQ(mService->equipInfoOf(kHelm), (EquipInfo{ EquipKind::Armor, 0x1 }));
        EXPECT_EQ(mService->equipInfoOf(kSwordB), (EquipInfo{ EquipKind::Weapon, 0 }));
        EXPECT_EQ(mService->equipInfoOf(kHealing).mKind, EquipKind::Potion); // by category
        EXPECT_EQ(mService->equipInfoOf(kMisc).mKind, EquipKind::None);
    }

    TEST(SkyrimEquipSlots, aFailedSlotLookupIsNotRememberedButASuccessIs)
    {
        // A service with one hook that fails once (the records are not read yet) and then answers.
        const Bytes all = testPlugin();
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "oe_equip_slots";
        std::filesystem::create_directories(dir);
        const std::filesystem::path path = dir / "test.esp";
        std::ofstream(path, std::ios::binary).write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
        int asked = 0;
        Hooks hooks;
        hooks.mInputs = [&](Inputs& out) {
            out.mPaths = { path };
            std::string err;
            EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
            EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
            return true;
        };
        hooks.mEquipInfo = [&](FormId, EquipInfo& out) {
            if (++asked == 1)
                return false;
            out = { EquipKind::Armor, 0x1 };
            return true;
        };
        InventoryService service(std::move(hooks), 0.f, 1);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!service.poll() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ASSERT_TRUE(service.ready());
        EXPECT_EQ(service.equipInfoOf(kHelm), (EquipInfo{ EquipKind::Armor, 0x4 })); // the body slot, not remembered
        EXPECT_EQ(service.equipInfoOf(kHelm), (EquipInfo{ EquipKind::Armor, 0x1 })); // asked again, now known
        EXPECT_EQ(service.equipInfoOf(kHelm), (EquipInfo{ EquipKind::Armor, 0x1 }));
        EXPECT_EQ(asked, 2); // and remembered
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    TEST_F(EquipService, equipAndUnequipThroughTheService)
    {
        give(kSwordA, 1);
        give(kHelm, 1);
        EXPECT_EQ(mService->equip(kSwordA), EquipStatus::Ok);
        EXPECT_EQ(mService->equip(kHelm), EquipStatus::Ok);
        EXPECT_EQ(mService->equip(kBoots), EquipStatus::NotHeld);
        give(kHealing, 1);
        EXPECT_EQ(mService->equip(kHealing), EquipStatus::NotEquippable);
        EXPECT_EQ(mService->model().equipped().size(), 2u);
        EXPECT_EQ(mService->unequip(kSwordA), EquipStatus::Ok);
        EXPECT_EQ(mService->unequip(kSwordA), EquipStatus::NotEquipped);
        EXPECT_FALSE(mLog.empty()); // refusals are logged
    }

    TEST_F(EquipService, theBindingsEquipUnequipAndReportTheState)
    {
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_GE(mBindings.mVersion.minor, 1);
        for (const char* read : { "equipped", "use_kind" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(read), 1u) << read;
            EXPECT_EQ(mBindings.mFunctions[read].access, BindingAccess::Read) << read;
        }
        for (const char* mutate : { "equip", "unequip", "use" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(mutate), 1u) << mutate;
            EXPECT_EQ(mBindings.mFunctions[mutate].access, BindingAccess::Mutate) << mutate;
            EXPECT_FALSE(mBindings.mFunctions[mutate].hasResult) << mutate;
        }
        give(kSwordA, 1);
        give(kHealing, 1);
        EXPECT_EQ(mBindings.call("use_kind", { str(formatId(kSwordA)) }).value.text, "weapon");
        EXPECT_EQ(mBindings.call("use_kind", { str(formatId(kHelm)) }).value.text, "armor");
        EXPECT_EQ(mBindings.call("use_kind", { str(formatId(kHealing)) }).value.text, "potion");
        EXPECT_EQ(mBindings.call("use_kind", { str(formatId(kMisc)) }).value.text, "");
        EXPECT_FALSE(mBindings.call("equipped", { str(formatId(kSwordA)) }).value.boolean);
        const std::uint32_t revision = mService->model().revision();
        (void)mBindings.call("equip", { str(formatId(kSwordA)) });
        EXPECT_TRUE(mBindings.call("equipped", { str(formatId(kSwordA)) }).value.boolean);
        EXPECT_NE(mService->model().revision(), revision); // the windows re-read
        (void)mBindings.call("unequip", { str(formatId(kSwordA)) });
        EXPECT_FALSE(mBindings.call("equipped", { str(formatId(kSwordA)) }).value.boolean);
        (void)mBindings.call("equip", { str("nonsense") }); // refused, logged, nothing thrown
        EXPECT_FALSE(mLog.empty());
    }

    TEST_F(EquipService, drinkingAPotionHealsAndConsumesOne)
    {
        give(kHealing, 2);
        EXPECT_EQ(mService->use(kHealing), InventoryService::UseStatus::Ok);
        ASSERT_EQ(mHealed.size(), 1u);
        EXPECT_FLOAT_EQ(mHealed[0], 25.f);
        EXPECT_EQ(mService->model().count(kPlayerOwner, kHealing), 1);
        // through the binding
        (void)mBindings.call("use", { str(formatId(kHealing)) });
        EXPECT_EQ(mService->model().count(kPlayerOwner, kHealing), 0);
        EXPECT_EQ(mHealed.size(), 2u);
        // none left
        EXPECT_EQ(mService->use(kHealing), InventoryService::UseStatus::NotHeld);
        EXPECT_EQ(mHealed.size(), 2u);
    }

    TEST_F(EquipService, aPotionThatCannotHealOrCannotBeAppliedIsNotConsumed)
    {
        give(kSlowHeal, 1);
        give(kPoison, 1);
        give(kSwordA, 1);
        give(kHealing, 1);
        EXPECT_EQ(mService->use(kSlowHeal), InventoryService::UseStatus::NoHealing);
        EXPECT_EQ(mService->use(kPoison), InventoryService::UseStatus::NoHealing);
        EXPECT_EQ(mService->use(kSwordA), InventoryService::UseStatus::NotUsable);
        EXPECT_EQ(mService->model().count(kPlayerOwner, kSlowHeal), 1);
        mHealWorks = false; // the combat rules are off
        EXPECT_EQ(mService->use(kHealing), InventoryService::UseStatus::Refused);
        EXPECT_EQ(mService->model().count(kPlayerOwner, kHealing), 1);
        EXPECT_TRUE(mHealed.empty());
    }
}
