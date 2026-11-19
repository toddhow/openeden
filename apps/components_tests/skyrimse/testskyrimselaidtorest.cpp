// Task 0342: "Laid to Rest" (MS14, Morthal). GetIsEditorLocation (565) run on an ACTOR (the Innkeeper alias: this
// location, JobInnkeeperFaction, Jonna by id) is answered from the actor's editor location only when the
// `laid to rest` switch is on; JobJarlFaction is the constant the setting adds to the rank -1 members. Generated records;
// no game data. The behaviour tests were written first and run red against the code without the switch (see the task).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/laidtorest.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/location_conditions.hpp>
#include <game_skyrimse/placed_refs.hpp>

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
    constexpr FormId kTown = kSelf | 0x500;
    constexpr FormId kInn = kSelf | 0x501;
    constexpr FormId kOther = kSelf | 0x502;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kRef = kSelf | 0x300;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes lctn(FormId id, FormId parent)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
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

    survey::Ctda cond565(FormId location)
    {
        survey::Ctda c;
        c.mFunction = 565;
        c.mParam1 = location;
        c.mRunOn = 0;
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    struct Rig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        LocationIndex mIndex;
        PlacedRefIndex mRefs;
        conditions::Evaluator mEvaluator;

        Rig()
        {
            Bytes recs = lctn(kTown, 0);
            recs.append(lctn(kInn, kTown));
            recs.append(lctn(kOther, 0));
            mIndex.add(parseLocations(recs));
            mWorld.setLocations(&mIndex);
            PlacedEntry entry;
            entry.mId = kRef;
            entry.mBase = kNpc;
            entry.mLocation = kInn; // Jonna's editor location: the inn
            PlacedRefRecords records;
            records.mRefs.push_back(entry);
            mRefs.add(std::move(records));
            mWorld.setPlacedRefs(&mRefs);
            DialogueSpeaker speaker;
            speaker.mBase = kNpc;
            speaker.mReference = kRef;
            mWorld.setSpeaker(speaker);
            mEvaluator.setExtraFunctions(locationExtraFunctions());
        }
        bool holds(const survey::Ctda& c) { return mEvaluator.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld); }
    };

    TEST(LaidToRest, anActorsEditorLocationIsNotAnsweredWhileTheSwitchIsOff)
    {
        Rig rig;
        EXPECT_FALSE(rig.mWorld.actorEditorLocationOn());
        EXPECT_FALSE(rig.holds(cond565(kInn)));
        EXPECT_FALSE(rig.holds(cond565(kTown)));
    }

    TEST(LaidToRest, anActorIsInItsEditorLocationAndInTheOnesAboveItWhenTheSwitchIsOn)
    {
        Rig rig;
        rig.mWorld.setActorEditorLocation(true);
        EXPECT_TRUE(rig.holds(cond565(kInn)));
        EXPECT_TRUE(rig.holds(cond565(kTown))); // the inn is inside the town
        EXPECT_FALSE(rig.holds(cond565(kOther)));
    }

    TEST(LaidToRest, anActorWithNoKnownEditorLocationIsInNoLocation)
    {
        Rig rig;
        rig.mWorld.setActorEditorLocation(true);
        DialogueSpeaker stranger;
        stranger.mBase = kSelf | 0x201;
        rig.mWorld.setSpeaker(stranger);
        EXPECT_FALSE(rig.holds(cond565(kInn)));
    }

    TEST(LaidToRest, aCandidateLocationStillAsksTheCandidateWhateverTheSwitch)
    {
        Rig rig;
        rig.mWorld.setSubjectLocation(kOther);
        EXPECT_FALSE(rig.holds(cond565(kInn)));
        rig.mWorld.setActorEditorLocation(true);
        EXPECT_FALSE(rig.holds(cond565(kInn))); // the candidate wins over the speaker's own place
        rig.mWorld.setSubjectLocation(kInn);
        EXPECT_TRUE(rig.holds(cond565(kTown)));
    }

    TEST(LaidToRest, theKeyAndTheJarlFactionAreTheDataValues)
    {
        EXPECT_EQ(std::string(laidtorest::kKey), "laid to rest");
        EXPECT_EQ(laidtorest::kJobJarlFaction, 330016u); // the fn_71 parameter the Jarl lines carry (0x50920)
        EXPECT_EQ(laidtorest::kMs14Quest, 155454u); // the GetStage parameter of MS14's lines
    }
}
