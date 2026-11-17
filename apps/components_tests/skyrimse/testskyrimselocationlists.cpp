// Task 0211: the location records' own lists (exterior cells, special references, persistent references, unique NPCs)
// and the index questions radiant quests and the exterior "change location" story event ask of them. Generated records;
// no game data. Written first and run red against stubs.
#include <game_skyrimse/location.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kWorld = 0x0000003C; // a Skyrim.esm world (Tamriel)
    constexpr FormId kWorld2 = 0x0000003D;
    constexpr FormId kCity = kSelf | 0x100;
    constexpr FormId kInner = kSelf | 0x101;
    constexpr FormId kOther = kSelf | 0x102;
    constexpr FormId kKeywordA = kSelf | 0x200;
    constexpr FormId kRefType = kSelf | 0x300;
    constexpr FormId kRefType2 = kSelf | 0x301;
    constexpr FormId kRefA = kSelf | 0x400;
    constexpr FormId kRefB = kSelf | 0x401;
    constexpr FormId kActor = kSelf | 0x500;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes cells(FormId world, const std::vector<std::pair<int, int>>& yx)
    {
        Bytes b;
        b.u32(world);
        for (const auto& [y, x] : yx)
            b.u16(static_cast<std::uint16_t>(y)).u16(static_cast<std::uint16_t>(x));
        return b;
    }

    Bytes lctn(FormId id, const std::string& name, FormId parent, const std::vector<Bytes>& extra)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring(name)));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        for (const Bytes& e : extra)
            b.append(e);
        return record4("LCTN", 0u, id, b);
    }

    struct Parsed
    {
        explicit Parsed(const std::vector<Bytes>& records)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : records)
                all.append(r);
            bytes.append(group4(Content::fourCC("LCTN"), 0, all));
            Content::Error error;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            if (plugin == nullptr)
                return;
            mOk = parseLocationRecords(*plugin, order(), LocationLimits(), mRecords);
            LocationRecords copy = mRecords;
            mIndex.add(std::move(copy));
        }
        bool mOk = false;
        LocationRecords mRecords;
        LocationIndex mIndex;
    };

    // ---- decoding ---------------------------------------------------------------------------------------------------

    TEST(LocationLists, exteriorCellsSpecialPersistentAndUniqueListsAreRead)
    {
        const Bytes special = Bytes()
                                  .u32(kRefType)
                                  .u32(kRefA)
                                  .u32(kWorld)
                                  .u16(static_cast<std::uint16_t>(-3))
                                  .u16(7)
                                  .u32(kRefType2)
                                  .u32(kRefB)
                                  .u32(kWorld)
                                  .u16(1)
                                  .u16(2);
        const Bytes persistent = Bytes().u32(kRefA).u32(kWorld).u16(4).u16(5);
        const Bytes unique = Bytes().u32(kActor).u32(kRefB).u32(kCity);
        const Parsed p({ lctn(kCity, "City", 0,
            { sub4("LCEC", cells(kWorld, { { -3, 7 }, { 0, 0 } })), sub4("LCSR", special), sub4("LCPR", persistent),
                sub4("LCUN", unique) }) });
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mLocations.size(), 1u);
        const LocationRecord& l = p.mRecords.mLocations[0];
        ASSERT_EQ(l.mCells.size(), 1u);
        EXPECT_EQ(l.mCells[0].mWorld, kWorld);
        ASSERT_EQ(l.mCells[0].mGrids.size(), 2u);
        EXPECT_EQ(l.mCells[0].mGrids[0].mY, -3);
        EXPECT_EQ(l.mCells[0].mGrids[0].mX, 7);
        ASSERT_EQ(l.mSpecialRefs.size(), 2u);
        EXPECT_EQ(l.mSpecialRefs[0].mRefType, kRefType);
        EXPECT_EQ(l.mSpecialRefs[0].mRef, kRefA);
        EXPECT_EQ(l.mSpecialRefs[0].mWorld, kWorld);
        EXPECT_EQ(l.mSpecialRefs[0].mGrid.mY, -3);
        EXPECT_EQ(l.mSpecialRefs[1].mRefType, kRefType2);
        ASSERT_EQ(l.mPersistentRefs.size(), 1u);
        EXPECT_EQ(l.mPersistentRefs[0].mRef, kRefA);
        EXPECT_EQ(l.mPersistentRefs[0].mGrid.mX, 5);
        ASSERT_EQ(l.mUniqueNpcs.size(), 1u);
        EXPECT_EQ(l.mUniqueNpcs[0].mActor, kActor);
        EXPECT_EQ(l.mUniqueNpcs[0].mLocation, kCity);
    }

    TEST(LocationLists, theThreeFamiliesOfOneListAreMergedInOrder)
    {
        const Parsed p({ lctn(kCity, "City", 0,
            { sub4("LCEC", cells(kWorld, { { 0, 0 } })), sub4("ACEC", cells(kWorld2, { { 1, 1 } })),
                sub4("RCEC", cells(kWorld, { { 2, 2 } })), sub4("LCPR", Bytes().u32(kRefA).u32(kWorld).u16(0).u16(0)),
                sub4("ACPR", Bytes().u32(kRefB).u32(kWorld).u16(0).u16(0)),
                sub4("RCPR", Bytes().u32(kRefA).u32(kWorld).u16(1).u16(1)) }) });
        ASSERT_TRUE(p.mOk);
        const LocationRecord& l = p.mRecords.mLocations[0];
        ASSERT_EQ(l.mCells.size(), 3u);
        EXPECT_EQ(l.mCells[1].mWorld, kWorld2);
        ASSERT_EQ(l.mPersistentRefs.size(), 3u);
        EXPECT_EQ(l.mPersistentRefs[1].mRef, kRefB);
    }

    TEST(LocationLists, aListWithAWrongSizeIsOddAndKeepsNothing)
    {
        const Parsed p({ lctn(kCity, "City", 0,
            { sub4("LCEC", Bytes().u32(kWorld).u16(1)), // 2 bytes after the world: not a whole pair
                sub4("LCSR", Bytes().u32(1).u32(2).u32(3)), // 12 of 16
                sub4("LCPR", Bytes().u32(1).u32(2)), // 8 of 12
                sub4("LCUN", Bytes().u32(1).u32(2)) }) });
        ASSERT_TRUE(p.mOk);
        const LocationRecord& l = p.mRecords.mLocations[0];
        EXPECT_TRUE(l.mCells.empty());
        EXPECT_TRUE(l.mSpecialRefs.empty());
        EXPECT_TRUE(l.mPersistentRefs.empty());
        EXPECT_TRUE(l.mUniqueNpcs.empty());
        EXPECT_EQ(p.mRecords.mOddSubrecords, 4u);
    }

    TEST(LocationLists, aListOverTheCapFailsTheRecordAndTheWalkGoesOn)
    {
        std::vector<std::pair<int, int>> many;
        for (int i = 0; i < 70; ++i)
            many.emplace_back(i, i);
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes all = lctn(kCity, "City", 0, { sub4("LCEC", cells(kWorld, many)) });
        all.append(lctn(kInner, "Inner", 0, {}));
        bytes.append(group4(Content::fourCC("LCTN"), 0, all));
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        ASSERT_NE(plugin, nullptr);
        LocationLimits limits;
        limits.mMaxEntries = 64;
        LocationRecords out;
        ASSERT_TRUE(parseLocationRecords(*plugin, order(), limits, out));
        EXPECT_EQ(out.mLocations.size(), 1u);
        EXPECT_EQ(out.mCount.mFailed, 1u);
    }

    // ---- the index --------------------------------------------------------------------------------------------------

    TEST(LocationIndexLists, aGridSquareBelongsToTheLocationThatListsFewestCells)
    {
        const Parsed p(
            { lctn(kCity, "City", 0, { sub4("LCEC", cells(kWorld, { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } })) }),
                lctn(kInner, "Inner", kCity, { sub4("LCEC", cells(kWorld, { { 1, 1 } })) }),
                lctn(kOther, "Other", 0, { sub4("LCEC", cells(kWorld2, { { 1, 1 } })) }) });
        ASSERT_TRUE(p.mOk);
        // the grid is stored Y then X: the pair (0, 1) is X 1, Y 0
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 1, 0), kCity);
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 1, 1), kInner); // both list it: the smaller list wins
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld2, 1, 1), kOther);
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 5, 5), kNullFormId);
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld2, 0, 0), kNullFormId);
    }

    TEST(LocationIndexLists, aTieOnTheListSizeGoesToTheLowerFormId)
    {
        const Parsed p({ lctn(kOther, "Other", 0, { sub4("LCEC", cells(kWorld, { { 0, 0 } })) }),
            lctn(kCity, "City", 0, { sub4("LCEC", cells(kWorld, { { 0, 0 } })) }) });
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 0, 0), kCity);
    }

    TEST(LocationIndexLists, aLaterRecordReplacesAnEarlierOneAndTheGridMapFollows)
    {
        Parsed p({ lctn(kCity, "City", 0, { sub4("LCEC", cells(kWorld, { { 0, 0 } })) }) });
        ASSERT_EQ(p.mIndex.locationOfGrid(kWorld, 0, 0), kCity);
        LocationRecords later;
        LocationRecord l;
        l.mCommon.mId = kCity;
        l.mCells.push_back({ kWorld, { { 3, 4 } } });
        later.mLocations.push_back(l);
        p.mIndex.add(std::move(later));
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 0, 0), kNullFormId);
        EXPECT_EQ(p.mIndex.locationOfGrid(kWorld, 4, 3), kCity);
    }

    TEST(LocationIndexLists, gridOfGivesAListedCellAndIdsAreSorted)
    {
        const Parsed p(
            { lctn(kOther, "Other", 0, { sub4("LCEC", cells(kWorld2, { { 6, 5 } })) }), lctn(kCity, "City", 0, {}) });
        FormId world = 0;
        std::int32_t x = 0, y = 0;
        EXPECT_TRUE(p.mIndex.gridOf(kOther, world, x, y));
        EXPECT_EQ(world, kWorld2);
        EXPECT_EQ(x, 5);
        EXPECT_EQ(y, 6);
        EXPECT_FALSE(p.mIndex.gridOf(kCity, world, x, y));
        EXPECT_FALSE(p.mIndex.gridOf(kInner, world, x, y));
        EXPECT_EQ(p.mIndex.ids(), (std::vector<FormId>{ kCity, kOther }));
    }

    TEST(LocationIndexLists, specialReferencesAreFoundByTheirType)
    {
        const Bytes special = Bytes()
                                  .u32(kRefType)
                                  .u32(kRefA)
                                  .u32(kWorld)
                                  .u16(0)
                                  .u16(0)
                                  .u32(kRefType2)
                                  .u32(kRefB)
                                  .u32(kWorld)
                                  .u16(0)
                                  .u16(0)
                                  .u32(kRefType)
                                  .u32(kRefB)
                                  .u32(kWorld)
                                  .u16(0)
                                  .u16(0);
        const Parsed p({ lctn(kCity, "City", 0, { sub4("LCSR", special) }) });
        EXPECT_TRUE(p.mIndex.hasRefType(kCity, kRefType));
        EXPECT_FALSE(p.mIndex.hasRefType(kCity, kKeywordA));
        EXPECT_FALSE(p.mIndex.hasRefType(kInner, kRefType));
        const std::vector<LocationSpecialRef> found = p.mIndex.specialRefsOfType(kCity, kRefType);
        ASSERT_EQ(found.size(), 2u);
        EXPECT_EQ(found[0].mRef, kRefA);
        EXPECT_EQ(found[1].mRef, kRefB);
        EXPECT_TRUE(p.mIndex.specialRefsOfType(kCity, kKeywordA).empty());
    }
}
