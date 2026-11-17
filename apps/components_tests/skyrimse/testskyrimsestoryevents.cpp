// Task 0211: the story events the world raises: the KILL event of a death, the location a place belongs to, and the
// rule for "the player changed location". Pure; generated location records. Written first and run red against stubs.
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/story_events.hpp>

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
    constexpr FormId kWorld = 0x0000003C;
    constexpr FormId kCity = kSelf | 0x100;
    constexpr FormId kCell = kSelf | 0x101;
    constexpr FormId kLair = kSelf | 0x102;
    constexpr FormId kVictim = kSelf | 0x400;
    constexpr FormId kKiller = kSelf | 0x401;
    constexpr FormId kPlayerRef = 0x14;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes lctn(FormId id, const Bytes& cells)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (!cells.str().empty())
            b.append(sub4("LCEC", cells));
        return record4("LCTN", 0u, id, b);
    }

    LocationIndex makeIndex()
    {
        Bytes recs = lctn(kCity, Bytes().u32(kWorld).u16(0).u16(0).u16(0).u16(1));
        recs.append(lctn(kLair, Bytes()));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("LCTN"), 0, recs));
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        LocationRecords out;
        EXPECT_NE(plugin, nullptr);
        if (plugin != nullptr)
        {
            EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), out));
        }
        LocationIndex index;
        index.add(std::move(out));
        return index;
    }

    TEST(StoryKill, theVictimIsReference1TheKillerReference2AndTheDeathPlaceLocation1)
    {
        const StoryEvent e = makeKillEvent(kVictim, kKiller, kCity);
        EXPECT_EQ(e.mType, "KILL");
        EXPECT_EQ(e.mRef1, kVictim);
        EXPECT_EQ(e.mRef2, kKiller);
        EXPECT_EQ(e.mLocation1, kCity);
        EXPECT_EQ(e.mLocation2, kNullFormId);
        EXPECT_EQ(e.mKeyword, kNullFormId);
    }

    TEST(StoryKill, thePlayerIsTheReferenceFourteenAndNobodyLeavesTheKillerEmpty)
    {
        EXPECT_EQ(makeKillEvent(kVictim, kPlayerRef, kNullFormId).mRef2, kPlayerRef);
        EXPECT_EQ(makeKillEvent(kVictim, kNullFormId, kNullFormId).mRef2, kNullFormId);
    }

    TEST(StoryPlace, anInteriorCellsOwnLocationIsUsedAndNothingElse)
    {
        const LocationIndex index = makeIndex();
        PlaceQuery place;
        place.mInterior = true;
        place.mCellLocation = kLair;
        EXPECT_EQ(locationOfPlace(place, index), kLair);
        place.mCellLocation = kNullFormId;
        place.mWorld = kWorld; // an interior has no grid square: the lists are not asked
        EXPECT_EQ(locationOfPlace(place, index), kNullFormId);
    }

    TEST(StoryPlace, anExteriorCellsXlcnWinsOverTheLocationsCellList)
    {
        const LocationIndex index = makeIndex();
        PlaceQuery place;
        place.mWorld = kWorld;
        place.mX = 1;
        place.mY = 0; // the city lists the pair Y 0, X 1
        EXPECT_EQ(locationOfPlace(place, index), kCity);
        place.mCellLocation = kLair;
        EXPECT_EQ(locationOfPlace(place, index), kLair);
        place.mCellLocation = kNullFormId;
        place.mX = 9;
        EXPECT_EQ(locationOfPlace(place, index), kNullFormId);
    }

    TEST(StoryPlace, anUnresolvedXlcnFallsBackToTheLists)
    {
        const LocationIndex index = makeIndex();
        PlaceQuery place;
        place.mWorld = kWorld;
        place.mCellLocation = kUnresolvedFormId;
        EXPECT_EQ(locationOfPlace(place, index), kCity);
    }

    TEST(StoryLocationChange, theFirstLocationRaisesFromNothingAndTheSameOneDoesNot)
    {
        LocationChange c;
        const auto first = c.arrive(kCity);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->mFrom, kNullFormId);
        EXPECT_EQ(first->mTo, kCity);
        EXPECT_FALSE(c.arrive(kCity).has_value());
        EXPECT_EQ(c.current(), kCity);
    }

    TEST(StoryLocationChange, aMoveToNowhereIsRememberedAndRaisesNothingSoComingBackRaises)
    {
        LocationChange c;
        ASSERT_TRUE(c.arrive(kCity).has_value());
        EXPECT_FALSE(c.arrive(kNullFormId).has_value());
        EXPECT_EQ(c.current(), kNullFormId);
        const auto back = c.arrive(kCity);
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(back->mFrom, kNullFormId);
        EXPECT_EQ(back->mTo, kCity);
    }

    TEST(StoryLocationChange, aDirectMoveBetweenTwoLocationsNamesBoth)
    {
        LocationChange c;
        ASSERT_TRUE(c.arrive(kCity).has_value());
        const auto next = c.arrive(kLair);
        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(next->mFrom, kCity);
        EXPECT_EQ(next->mTo, kLair);
    }

    TEST(StoryLocationChange, anUnresolvedIdIsNowhere)
    {
        LocationChange c;
        EXPECT_FALSE(c.arrive(kUnresolvedFormId).has_value());
        EXPECT_EQ(c.current(), kNullFormId);
    }

    TEST(StoryLocationChange, resetForgetsTheLocation)
    {
        LocationChange c;
        ASSERT_TRUE(c.arrive(kCity).has_value());
        c.reset();
        EXPECT_TRUE(c.arrive(kCity).has_value());
    }
}
