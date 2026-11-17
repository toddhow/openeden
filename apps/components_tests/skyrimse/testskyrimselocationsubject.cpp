// Task 0211: the conditions of a location alias run on each candidate LOCATION
// (SkyrimConditionWorld::setSubjectLocation): LocationHasKeyword (562), LocationHasRefType (563) and
// GetIsEditorLocation (565). Generated records; no game data. Written first and run red against stubs.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/location_conditions.hpp>

#include <gtest/gtest.h>

#include <algorithm>

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
    constexpr FormId kTown = kSelf | 0x500;
    constexpr FormId kInn = kSelf | 0x501;
    constexpr FormId kOther = kSelf | 0x502;
    constexpr FormId kKeywordA = kSelf | 0x600;
    constexpr FormId kKeywordB = kSelf | 0x601;
    constexpr FormId kRefType = kSelf | 0x700;
    constexpr FormId kRefTypeOther = kSelf | 0x701;
    constexpr FormId kNpc = kSelf | 0x200;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes lctn(FormId id, FormId parent, const std::vector<FormId>& keywords, const Bytes& special = Bytes())
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!keywords.empty())
        {
            b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(keywords.size()))));
            Bytes kw;
            for (FormId k : keywords)
                kw.u32(k);
            b.append(sub4("KWDA", kw));
        }
        if (!special.str().empty())
            b.append(sub4("LCSR", special));
        return record4("LCTN", 0u, id, b);
    }

    LocationRecords parseLocations(const Bytes& records)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("LCTN"), 0, records));
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        LocationRecords out;
        EXPECT_NE(plugin, nullptr);
        if (plugin != nullptr)
        {
            EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), out));
        }
        return out;
    }

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t runOn = 0)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        LocationIndex mIndex;
        conditions::Evaluator mEvaluator;

        Fixture()
        {
            Bytes recs
                = lctn(kTown, 0, { kKeywordA }, Bytes().u32(kRefType).u32(kSelf | 0x800).u32(0x3C).u16(0).u16(0));
            recs.append(lctn(kInn, kTown, { kKeywordB }));
            recs.append(lctn(kOther, 0, {}));
            mIndex.add(parseLocations(recs));
            mWorld.setLocations(&mIndex);
            mEvaluator.setExtraFunctions(locationExtraFunctions());
        }

        bool holds(const survey::Ctda& c) { return mEvaluator.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld); }
    };

    TEST(LocationSubject, locationHasKeywordAsksTheCandidateLocationNotTheSpeaker)
    {
        Fixture f;
        DialogueSpeaker s;
        s.mBase = kNpc;
        s.mLocationKnown = true;
        s.mLocation = kTown;
        f.mWorld.setSpeaker(s);
        EXPECT_TRUE(f.holds(cond(562, kKeywordA))); // the speaker's own location, as before
        f.mWorld.setSubjectLocation(kInn);
        EXPECT_FALSE(f.holds(cond(562, kKeywordA))); // the inn itself has only B
        EXPECT_TRUE(f.holds(cond(562, kKeywordB)));
        f.mWorld.setSubjectLocation(kNullFormId);
        EXPECT_TRUE(f.holds(cond(562, kKeywordA)));
        EXPECT_FALSE(f.holds(cond(562, kKeywordB)));
    }

    TEST(LocationSubject, aLocationCandidateNeedsNoSpeaker)
    {
        Fixture f;
        f.mWorld.setSubjectLocation(kInn);
        EXPECT_EQ(f.mWorld.locationSubject(), kInn);
        EXPECT_TRUE(f.holds(cond(562, kKeywordB)));
        EXPECT_FALSE(f.holds(cond(562, kKeywordA)));
    }

    TEST(LocationSubject, locationHasRefTypeLooksInTheCandidatesSpecialReferences)
    {
        Fixture f;
        f.mWorld.setSubjectLocation(kTown);
        EXPECT_TRUE(f.holds(cond(563, kRefType)));
        EXPECT_FALSE(f.holds(cond(563, kRefTypeOther)));
        f.mWorld.setSubjectLocation(kInn);
        EXPECT_FALSE(f.holds(cond(563, kRefType)));
        f.mWorld.setSubjectLocation(kNullFormId);
        EXPECT_FALSE(f.holds(cond(563, kRefType)));
    }

    TEST(LocationSubject, getIsEditorLocationIsTrueForTheLocationAndTheOnesInsideIt)
    {
        Fixture f;
        f.mWorld.setSubjectLocation(kInn);
        EXPECT_TRUE(f.holds(cond(565, kInn)));
        EXPECT_TRUE(f.holds(cond(565, kTown))); // the inn is inside the town
        EXPECT_FALSE(f.holds(cond(565, kOther)));
        f.mWorld.setSubjectLocation(kTown);
        EXPECT_FALSE(f.holds(cond(565, kInn)));
    }

    // not run red first (written after the code)
    TEST(LocationSubject, inTheStoryManagerThePlayerIsWhereTheEventSaysHeIs)
    {
        Fixture f;
        StoryEvent e;
        e.mType = "CLOC";
        e.mLocation1 = kOther;
        e.mLocation2 = kInn;
        EXPECT_FALSE(f.holds(cond(359, kTown))); // no event: nobody is anywhere
        f.mWorld.setStoryEvent(&e);
        EXPECT_TRUE(f.holds(cond(359, kInn)));
        EXPECT_TRUE(f.holds(cond(359, kTown))); // the inn is inside the town
        EXPECT_FALSE(f.holds(cond(359, kOther)));
        e.mLocation2 = kNullFormId; // an event with only a location left
        EXPECT_TRUE(f.holds(cond(359, kOther)));
    }

    TEST(LocationSubject, aSpeakerStillWinsOverTheStoryEvent)
    {
        Fixture f;
        StoryEvent e;
        e.mLocation2 = kInn;
        f.mWorld.setStoryEvent(&e);
        DialogueSpeaker s;
        s.mBase = kNpc;
        s.mLocationKnown = true;
        s.mLocation = kOther;
        f.mWorld.setSpeaker(s);
        EXPECT_FALSE(f.holds(cond(359, kTown)));
        EXPECT_TRUE(f.holds(cond(359, kOther)));
    }

    TEST(LocationSubject, theTwoFunctionsAreUnknownToAnEvaluatorWithoutTheExtraTable)
    {
        Fixture f;
        conditions::Evaluator plain;
        f.mWorld.setSubjectLocation(kTown);
        const survey::Ctda c = cond(563, kRefType);
        EXPECT_FALSE(plain.evaluate(std::span<const survey::Ctda>(&c, 1), f.mWorld));
        EXPECT_EQ(plain.mListsFailedClosed, 1u);
    }

    TEST(LocationSubject, theCombinedTableHasTheStoryFunctionToo)
    {
        std::vector<std::uint16_t> indices;
        for (const conditions::ExtraFunction& e : storyAndLocationExtraFunctions())
            indices.push_back(e.mIndex);
        EXPECT_EQ(indices.size(), 3u);
        EXPECT_NE(std::find(indices.begin(), indices.end(), 576), indices.end());
        EXPECT_NE(std::find(indices.begin(), indices.end(), 563), indices.end());
        EXPECT_NE(std::find(indices.begin(), indices.end(), 565), indices.end());
        EXPECT_EQ(locationExtraFunctions().size(), 2u);
    }
}
