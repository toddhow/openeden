// Task 0182 (inventory and container UI, milestone M-S6): item stats, container contents, the seeded leveled pick, the
// inventory model and its save chunk. Generated plugins; no game data. Written from the task spec before the code and
// run red against a stub first (see the task's stage log).
#include <game_skyrimse/inventory.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::inventory;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kMisc = kSelf | 0x100;
    constexpr FormId kWeap = kSelf | 0x101;
    constexpr FormId kBook = kSelf | 0x102;
    constexpr FormId kAmmo = kSelf | 0x103;
    constexpr FormId kAlch = kSelf | 0x104;
    constexpr FormId kArmo = kSelf | 0x105;
    constexpr FormId kOddMisc = kSelf | 0x106;
    constexpr FormId kIngr = kSelf | 0x107;
    constexpr FormId kCont = kSelf | 0x200;
    constexpr FormId kLvli = kSelf | 0x300;
    constexpr FormId kLvliAll = kSelf | 0x301;
    constexpr FormId kLvliNone = kSelf | 0x302;
    constexpr FormId kLvliNest = kSelf | 0x303;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes valueWeight(std::uint32_t value, float weight)
    {
        return sub4("DATA", Bytes().u32(value).f32(weight));
    }

    Bytes testPlugin(bool withBadSizes = false)
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, kMisc, valueWeight(25, 0.5f))));
        Bytes weap = valueWeight(0, 0);
        weap = Bytes();
        weap.append(sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)));
        all.append(group4(Content::fourCC("WEAP"), 0, record4("WEAP", 0, kWeap, weap)));
        all.append(group4(Content::fourCC("BOOK"), 0,
            record4("BOOK", 0, kBook,
                sub4("DATA", Bytes().u8(0).u8(0).u16(0).u32(0).u32(40).f32(1.5f)))));
        all.append(group4(Content::fourCC("AMMO"), 0,
            record4("AMMO", 0, kAmmo, sub4("DATA", Bytes().u32(0x1234).u32(0).f32(8.f).u32(3)))));
        Bytes alch;
        alch.append(sub4("DATA", Bytes().f32(0.5f)));
        alch.append(sub4("ENIT", Bytes().u32(30).u32(0).u32(0).u32(0).u32(0)));
        all.append(group4(Content::fourCC("ALCH"), 0, record4("ALCH", 0, kAlch, alch)));
        all.append(group4(Content::fourCC("ARMO"), 0, record4("ARMO", 0, kArmo, valueWeight(125, 12.f))));
        all.append(group4(Content::fourCC("INGR"), 0, record4("INGR", 0, kIngr, valueWeight(2, 0.1f))));
        if (withBadSizes)
            all.append(group4(Content::fourCC("MISC"), 0,
                record4("MISC", 0, kOddMisc, sub4("DATA", Bytes().u32(5)))));
        Bytes cont;
        cont.append(sub4("EDID", Bytes().zstring("Chest")));
        cont.append(sub4("CNTO", Bytes().u32(kMisc).u32(5)));
        cont.append(sub4("CNTO", Bytes().u32(kWeap).u32(1)));
        if (withBadSizes)
            cont.append(sub4("CNTO", Bytes().u32(kMisc)));
        all.append(group4(Content::fourCC("CONT"), 0, record4("CONT", 0, kCont, cont)));
        return all;
    }

    InventoryRecords parse(const Bytes& bytes, bool* ok = nullptr)
    {
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        InventoryRecords out;
        EXPECT_NE(plugin, nullptr) << error.mMessage;
        const bool result = plugin != nullptr && parseInventoryRecords(*plugin, order(), InventoryLimits(), out);
        if (ok != nullptr)
            *ok = result;
        return out;
    }

    ItemTable table(bool bad = false)
    {
        ItemTable t;
        t.add(parse(testPlugin(bad)));
        t.finish();
        return t;
    }

    LeveledList list(FormId id, std::uint8_t flags, std::uint8_t chanceNone, std::vector<LeveledEntry> entries)
    {
        LeveledList l;
        l.mCommon.mId = id;
        l.mFlags = flags;
        l.mChanceNone = chanceNone;
        l.mEntries = std::move(entries);
        return l;
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

    // ---- item stats -----------------------------------------------------------------------------------------------

    TEST(SkyrimInventoryRecords, everyItemTypeDecodesItsValueAndWeight)
    {
        const ItemTable t = table();
        const ItemStat* misc = t.item(kMisc);
        ASSERT_NE(misc, nullptr);
        EXPECT_EQ(misc->mValue, 25);
        EXPECT_FLOAT_EQ(misc->mWeight, 0.5f);
        EXPECT_EQ(misc->mCategory, Category::Misc);
        EXPECT_TRUE(misc->mDecoded);

        const ItemStat* weap = t.item(kWeap);
        ASSERT_NE(weap, nullptr);
        EXPECT_EQ(weap->mValue, 25);
        EXPECT_FLOAT_EQ(weap->mWeight, 9.f);
        EXPECT_EQ(weap->mCategory, Category::Weapon);

        const ItemStat* book = t.item(kBook);
        ASSERT_NE(book, nullptr);
        EXPECT_EQ(book->mValue, 40);
        EXPECT_FLOAT_EQ(book->mWeight, 1.5f);

        const ItemStat* ammo = t.item(kAmmo);
        ASSERT_NE(ammo, nullptr);
        EXPECT_EQ(ammo->mValue, 3);
        EXPECT_FLOAT_EQ(ammo->mWeight, 0.f);
        EXPECT_EQ(ammo->mCategory, Category::Ammo);

        const ItemStat* alch = t.item(kAlch);
        ASSERT_NE(alch, nullptr);
        EXPECT_EQ(alch->mValue, 30);
        EXPECT_FLOAT_EQ(alch->mWeight, 0.5f);
        EXPECT_EQ(alch->mCategory, Category::Potion);

        const ItemStat* armo = t.item(kArmo);
        ASSERT_NE(armo, nullptr);
        EXPECT_EQ(armo->mValue, 125);
        EXPECT_FLOAT_EQ(armo->mWeight, 12.f);
        EXPECT_EQ(armo->mCategory, Category::Armor);

        EXPECT_EQ(t.item(kIngr)->mCategory, Category::Ingredient);
    }

    TEST(SkyrimInventoryRecords, aDataOfAnUnexpectedSizeIsOddAndUndecoded)
    {
        const ItemTable t = table(true);
        const ItemStat* odd = t.item(kOddMisc);
        ASSERT_NE(odd, nullptr);
        EXPECT_FALSE(odd->mDecoded);
        EXPECT_EQ(odd->mValue, 0);
        EXPECT_GE(t.oddSubrecords(), 2u); // the DATA and the short CNTO
    }

    TEST(SkyrimInventoryRecords, aContainerListsItsEntriesAndSkipsAnOddOne)
    {
        const ItemTable t = table(true);
        const ContainerStat* c = t.container(kCont);
        ASSERT_NE(c, nullptr);
        ASSERT_EQ(c->mItems.size(), 2u);
        EXPECT_EQ(c->mItems[0].mItem, kMisc);
        EXPECT_EQ(c->mItems[0].mCount, 5);
    }

    TEST(SkyrimInventoryRecords, aLaterPluginReplacesAndADeletedRecordRemoves)
    {
        ItemTable t = table();
        InventoryRecords later;
        ItemStat changed;
        changed.mId = kMisc;
        changed.mValue = 99;
        changed.mDecoded = true;
        later.mItems.push_back(changed);
        later.mRemoved.push_back({ kWeap });
        ContainerStat gone;
        gone.mId = kCont;
        gone.mDeleted = true;
        later.mContainers.push_back(gone);
        t.add(std::move(later));
        EXPECT_EQ(t.item(kMisc)->mValue, 99);
        EXPECT_EQ(t.item(kWeap), nullptr);
        EXPECT_EQ(t.container(kCont), nullptr);
    }

    TEST(SkyrimInventoryRecords, categoriesHaveNamesAndTheOrderOfTheWindow)
    {
        EXPECT_EQ(categoryName(Category::Weapon), "weapon");
        EXPECT_EQ(categoryName(Category::Misc), "misc");
        EXPECT_LT(static_cast<int>(Category::Weapon), static_cast<int>(Category::Armor));
        EXPECT_LT(static_cast<int>(Category::Potion), static_cast<int>(Category::Misc));
        EXPECT_EQ(categoryOfType(Content::fourCC("KEYM")), Category::Key);
        EXPECT_EQ(categoryOfType(Content::fourCC("SLGM")), Category::SoulGem);
        EXPECT_FALSE(categoryOfType(Content::fourCC("NPC_")).has_value());
    }

    // ---- contents -------------------------------------------------------------------------------------------------

    TEST(SkyrimInventoryResolve, plainEntriesAreMergedSortedAndZeroCountsDropped)
    {
        const ItemTable t = table();
        const std::vector<InventoryEntry> entries = { { kWeap, 1 }, { kMisc, 2 }, { kMisc, 3 }, { kAmmo, 0 },
            { kArmo, -4 } };
        const std::vector<Stack> out = resolveContents(entries, t, 1, 1);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0], (Stack{ kMisc, 5 }));
        EXPECT_EQ(out[1], (Stack{ kWeap, 1 }));
    }

    TEST(SkyrimInventoryResolve, aLeveledListPickIsTheSameForTheSameSeedAndVariesWithTheSeed)
    {
        ItemTable t = table();
        t.addLeveledLists({ list(kLvli, 0, 0, { { 1, kMisc, 1 }, { 1, kWeap, 1 }, { 1, kArmo, 1 }, { 1, kBook, 1 } }) });
        const std::vector<InventoryEntry> entries = { { kLvli, 1 } };
        std::set<FormId> seen;
        for (std::uint64_t seed = 0; seed < 64; ++seed)
        {
            const std::vector<Stack> a = resolveContents(entries, t, contentsSeed(seed), 1);
            const std::vector<Stack> b = resolveContents(entries, t, contentsSeed(seed), 1);
            EXPECT_EQ(a, b);
            ASSERT_EQ(a.size(), 1u);
            seen.insert(a[0].mItem);
        }
        EXPECT_GE(seen.size(), 3u); // not stuck on one entry
        EXPECT_NE(contentsSeed(1), contentsSeed(2));
        EXPECT_EQ(contentsSeed(7), contentsSeed(7));
    }

    TEST(SkyrimInventoryResolve, aUseAllListGivesEveryEntryAndTheLevelGateAppliesToIt)
    {
        ItemTable t = table();
        t.addLeveledLists({ list(kLvliAll, LeveledFlags::UseAll, 0, { { 1, kMisc, 2 }, { 1, kWeap, 1 }, { 20, kArmo, 1 } }) });
        const std::vector<Stack> out = resolveContents({ { kLvliAll, 3 } }, t, 5, 1);
        ASSERT_EQ(out.size(), 2u); // the level 20 entry is not eligible at level 1
        EXPECT_EQ(out[0], (Stack{ kMisc, 6 })); // the container count multiplies
        EXPECT_EQ(out[1], (Stack{ kWeap, 3 }));
    }

    TEST(SkyrimInventoryResolve, chanceNoneOf100GivesNothingAndAHigherLevelEntryWaits)
    {
        ItemTable t = table();
        t.addLeveledLists({ list(kLvliNone, 0, 100, { { 1, kMisc, 1 } }) });
        EXPECT_TRUE(resolveContents({ { kLvliNone, 1 } }, t, 3, 1).empty());
        t.addLeveledLists({ list(kLvli, 0, 0, { { 5, kMisc, 1 } }) });
        EXPECT_TRUE(resolveContents({ { kLvli, 1 } }, t, 3, 1).empty()); // nothing eligible at level 1
        EXPECT_EQ(resolveContents({ { kLvli, 1 } }, t, 3, 5).size(), 1u);
    }

    TEST(SkyrimInventoryResolve, nestedListsAreFollowedToACapAndUnknownFormsAreCounted)
    {
        ItemTable t = table();
        t.addLeveledLists({ list(kLvliNest, 0, 0, { { 1, kLvliNest, 1 } }), list(kLvli, 0, 0, { { 1, kLvliAll, 1 } }),
            list(kLvliAll, 0, 0, { { 1, kMisc, 1 } }) });
        ResolveReport nested;
        EXPECT_TRUE(resolveContents({ { kLvliNest, 1 } }, t, 1, 1, &nested).empty());
        EXPECT_EQ(nested.mTooDeep, 1u);
        EXPECT_EQ(resolveContents({ { kLvli, 1 } }, t, 1, 1).size(), 1u); // two levels deep still resolve
        ResolveReport unknown;
        EXPECT_TRUE(resolveContents({ { kSelf | 0x999, 1 } }, t, 1, 1, &unknown).empty());
        EXPECT_EQ(unknown.mUnknownForms, 1u);
    }

    // ---- the model ------------------------------------------------------------------------------------------------

    InventoryModel withChest()
    {
        InventoryModel m;
        m.setPlayer({ { kGoldItem, 100 } });
        EXPECT_TRUE(m.ensureOwner(0x10, { { kMisc, 5 }, { kWeap, 1 } }));
        return m;
    }

    TEST(SkyrimInventoryModel, aTransferMovesAWholeCountOrNothing)
    {
        InventoryModel m = withChest();
        const std::uint32_t before = m.revision();
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, 2), TransferStatus::Ok);
        EXPECT_EQ(m.count(0x10, kMisc), 3);
        EXPECT_EQ(m.count(kPlayerOwner, kMisc), 2);
        EXPECT_NE(m.revision(), before);

        const std::uint32_t mid = m.revision();
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, 4), TransferStatus::NotEnough);
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, 0), TransferStatus::BadCount);
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, -1), TransferStatus::BadCount);
        EXPECT_EQ(m.transfer(0x10, 0x11, kMisc, 1), TransferStatus::UnknownOwner);
        EXPECT_EQ(m.transfer(0x10, 0x10, kMisc, 1), TransferStatus::SameOwner);
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kArmo, 1), TransferStatus::NotEnough);
        EXPECT_EQ(m.count(0x10, kMisc), 3);
        EXPECT_EQ(m.revision(), mid); // a refusal changes nothing, not even the revision
    }

    TEST(SkyrimInventoryModel, aStackThatReachesZeroLeavesTheList)
    {
        InventoryModel m = withChest();
        EXPECT_EQ(m.transfer(0x10, kPlayerOwner, kWeap, 1), TransferStatus::Ok);
        const std::vector<Stack> chest = m.stacks(0x10);
        ASSERT_EQ(chest.size(), 1u);
        EXPECT_EQ(chest[0].mItem, kMisc);
        EXPECT_EQ(m.transfer(kPlayerOwner, 0x10, kWeap, 1), TransferStatus::Ok);
        EXPECT_EQ(m.count(0x10, kWeap), 1);
    }

    TEST(SkyrimInventoryModel, ensureOwnerKeepsTheFirstContentsAndTheTableIsBounded)
    {
        InventoryModel m = withChest();
        EXPECT_TRUE(m.ensureOwner(0x10, { { kArmo, 9 } }));
        EXPECT_EQ(m.count(0x10, kArmo), 0);
        EXPECT_EQ(m.count(0x10, kMisc), 5);
        InventoryModel full;
        full.setPlayer({});
        for (std::uint64_t i = 1; i <= InventoryModel::kMaxOwners; ++i)
            (void)full.ensureOwner(i, {});
        EXPECT_FALSE(full.ensureOwner(InventoryModel::kMaxOwners + 100, {}));
    }

    TEST(SkyrimInventoryModel, transferAllMovesEveryStackAndWeightAddsUp)
    {
        const ItemTable t = table();
        InventoryModel m = withChest();
        std::int64_t moved = 0;
        EXPECT_EQ(m.transferAll(0x10, kPlayerOwner, moved), TransferStatus::Ok);
        EXPECT_EQ(moved, 6);
        EXPECT_TRUE(m.stacks(0x10).empty());
        EXPECT_FLOAT_EQ(m.weightOf(kPlayerOwner, t), 5 * 0.5f + 9.f); // gold has no stat here: 0
        EXPECT_EQ(m.transferAll(0x10, kPlayerOwner, moved), TransferStatus::Ok);
        EXPECT_EQ(moved, 0);
        EXPECT_EQ(m.transferAll(0x12, kPlayerOwner, moved), TransferStatus::UnknownOwner);
    }

    // ---- the save chunk -------------------------------------------------------------------------------------------

    TEST(SkyrimInventoryChunk, theBytesAreCanonicalAndRoundTripThePlayerAndTouchedOwners)
    {
        InventoryModel m = withChest();
        ASSERT_TRUE(m.ensureOwner(0x20, { { kArmo, 1 } })); // never touched: not saved
        ASSERT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, 2), TransferStatus::Ok);
        MemoryWriter a, b;
        m.write(a);
        m.write(b);
        EXPECT_EQ(a.mBytes, b.mBytes);

        InventoryModel back;
        back.setPlayer({});
        InventoryChunk chunk(back);
        MemoryReader reader(a.mBytes);
        ASSERT_TRUE(chunk.read(reader, InventoryChunk::kVersion));
        EXPECT_EQ(back.count(kPlayerOwner, kMisc), 2);
        EXPECT_EQ(back.count(kPlayerOwner, kGoldItem), 100);
        EXPECT_EQ(back.count(0x10, kMisc), 3);
        EXPECT_EQ(back.count(0x10, kWeap), 1);
        EXPECT_FALSE(back.hasOwner(0x20));
        MemoryWriter c;
        back.write(c);
        EXPECT_EQ(a.mBytes, c.mBytes); // write(read(x)) is x
    }

    TEST(SkyrimInventoryChunk, aDamagedChunkIsRefusedAndChangesNothing)
    {
        InventoryModel m = withChest();
        ASSERT_EQ(m.transfer(0x10, kPlayerOwner, kMisc, 2), TransferStatus::Ok);
        MemoryWriter good;
        m.write(good);

        InventoryModel target = withChest();
        const std::uint32_t revision = target.revision();
        std::string reasons;
        InventoryChunk chunk(target);
        chunk.setRefusalSink([&](const std::string& why) { reasons += why + ";"; });

        const auto refused = [&](std::vector<unsigned char> bytes, std::uint32_t version) {
            MemoryReader reader(bytes);
            return !chunk.read(reader, version);
        };
        EXPECT_TRUE(refused(good.mBytes, 2)); // a version it does not know
        std::vector<unsigned char> truncated(good.mBytes.begin(), good.mBytes.end() - 3);
        EXPECT_TRUE(refused(truncated, 1));
        std::vector<unsigned char> trailing = good.mBytes;
        trailing.push_back(0);
        EXPECT_TRUE(refused(trailing, 1));
        std::vector<unsigned char> zeroCount = good.mBytes;
        // the first stack of the player: count field is bytes 8..11 (n u32, item u32, count i32)
        std::memset(zeroCount.data() + 8, 0, 4);
        EXPECT_TRUE(refused(zeroCount, 1));
        std::vector<unsigned char> huge = good.mBytes;
        const std::uint32_t big = 0x7FFFFFFF;
        std::memcpy(huge.data(), &big, 4);
        EXPECT_TRUE(refused(huge, 1));
        EXPECT_FALSE(reasons.empty());
        EXPECT_EQ(target.revision(), revision);
        EXPECT_EQ(target.count(0x10, kMisc), 5); // untouched
    }
}
