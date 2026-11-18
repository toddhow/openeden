// Task 0305 (milestone M-S7): the start of the Dawnguard questline. The pack's side, no game data: the story manager's
// conditions on the player's level (DLC1VQ00Node: GetLevel of the player >= DLC1VQMinLevel) are answered by the story
// world, which has no speaker and no conversation player.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <span>
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
    constexpr FormId kRefType = kSelf | 0x300;
    constexpr FormId kRefType2 = kSelf | 0x301;
    constexpr FormId kRefA = kSelf | 0x400;
    constexpr FormId kRefB = kSelf | 0x401;

    survey::Ctda levelAtLeast(float value, std::uint32_t runOn, std::uint32_t reference = 0)
    {
        survey::Ctda c;
        c.mFunction = 80; // GetLevel
        c.mRunOn = runOn;
        c.mReference = reference;
        c.mCompare = 3; // >=
        c.mComparison = value;
        return c;
    }

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimDawnguardStory, aStoryWorldWithNoPlayerStandInStillKnowsTheLevel)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(levelAtLeast(10.f, 1)));
        f.mWorld.setPlayerLevel(10);
        EXPECT_TRUE(f.mWorld.answers(levelAtLeast(10.f, 1)));
        EXPECT_TRUE(f.holds(levelAtLeast(10.f, 1)));
        EXPECT_FALSE(f.holds(levelAtLeast(11.f, 1)));
    }

    TEST(SkyrimDawnguardStory, theReferenceFormOfTheSameConditionAnswersToo)
    {
        // DLC1VQ00Node names the player as a run-on Reference (0x14), which resolves to the player
        Fixture f;
        f.mWorld.setPlayerLevel(9);
        EXPECT_TRUE(f.mWorld.answers(levelAtLeast(10.f, 2, 0x14)));
        EXPECT_FALSE(f.holds(levelAtLeast(10.f, 2, 0x14)));
        f.mWorld.setPlayerLevel(10);
        EXPECT_TRUE(f.holds(levelAtLeast(10.f, 2, 0x14)));
    }

    TEST(SkyrimDawnguardStory, aLevelIsNotGuessedForAnotherReference)
    {
        Fixture f;
        f.mWorld.setPlayerLevel(30);
        EXPECT_FALSE(f.mWorld.answers(levelAtLeast(10.f, 0)));
    }

    // ---- the location lists an override adds (DLC1VQ00ChangeLocation's LocationCenterMarker) ----------------------

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes special(FormId refType, FormId ref)
    {
        return Bytes().u32(refType).u32(ref).u32(kWorld).u16(1).u16(2);
    }

    LocationRecords parse(bool added)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("City")));
        b.append(added ? sub4("ACSR", special(kRefType, kRefA)) : sub4("LCSR", special(kRefType, kRefA)));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("LCTN"), 0, record4("LCTN", 0u, kCity, b)));
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        LocationRecords out;
        if (plugin != nullptr)
            EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), out));
        return out;
    }

    TEST(SkyrimDawnguardLocations, aRecordWithOnlyAddedListsSaysSo)
    {
        const LocationRecords added = parse(true);
        ASSERT_EQ(added.mLocations.size(), 1u);
        EXPECT_TRUE(added.mLocations[0].mAddsToLists);
        EXPECT_EQ(added.mLocations[0].mSpecialRefs.size(), 1u);
        const LocationRecords own = parse(false);
        ASSERT_EQ(own.mLocations.size(), 1u);
        EXPECT_FALSE(own.mLocations[0].mAddsToLists);
    }

    TEST(SkyrimDawnguardLocations, anOverrideThatOnlyAddsKeepsTheEarlierListsAndTakesTheNewFields)
    {
        LocationIndex index;
        {
            LocationRecords first;
            LocationRecord l;
            l.mCommon.mId = kCity;
            l.mSpecialRefs.push_back({ kRefType2, kRefB, kWorld, { 1, 2 } });
            l.mCells.push_back({ kWorld, { { 3, 4 } } });
            first.mLocations.push_back(l);
            index.add(std::move(first));
        }
        {
            LocationRecords second;
            LocationRecord l;
            l.mCommon.mId = kCity;
            l.mAddsToLists = true;
            l.mSpecialRefs.push_back({ kRefType, kRefA, kWorld, { 1, 2 } });
            second.mLocations.push_back(l);
            index.add(std::move(second));
        }
        EXPECT_EQ(index.specialRefsOfType(kCity, kRefType).size(), 1u); // Dawnguard's marker
        EXPECT_EQ(index.specialRefsOfType(kCity, kRefType2).size(), 1u); // Skyrim.esm's own entry stays
        FormId world = 0;
        std::int32_t x = 0, y = 0;
        EXPECT_TRUE(index.gridOf(kCity, world, x, y)); // the listed cells stay
    }

    TEST(SkyrimDawnguardLocations, anOverrideWithItsOwnListsStillReplaces)
    {
        LocationIndex index;
        LocationRecords first;
        LocationRecord l;
        l.mCommon.mId = kCity;
        l.mSpecialRefs.push_back({ kRefType2, kRefB, kWorld, { 1, 2 } });
        first.mLocations.push_back(l);
        index.add(std::move(first));
        LocationRecords second;
        LocationRecord m;
        m.mCommon.mId = kCity;
        m.mSpecialRefs.push_back({ kRefType, kRefA, kWorld, { 1, 2 } });
        second.mLocations.push_back(m);
        index.add(std::move(second));
        EXPECT_EQ(index.specialRefsOfType(kCity, kRefType2).size(), 0u);
        EXPECT_EQ(index.specialRefsOfType(kCity, kRefType).size(), 1u);
    }

    // ---- fn_360 GetInCurrentLocAlias of the player (DLC1VQ00Forcegreet) -------------------------------------------

    survey::Ctda inLocAlias(std::uint32_t alias)
    {
        survey::Ctda c;
        c.mFunction = 360;
        c.mRunOn = 2;
        c.mReference = 0x14;
        c.mParam1 = alias;
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    TEST(SkyrimDawnguardAlias, thePlayerIsInTheLocationAnAliasHoldsOrInsideIt)
    {
        LocationIndex locations;
        LocationRecords records;
        LocationRecord hold;
        hold.mCommon.mId = kSelf | 0x10;
        LocationRecord city;
        city.mCommon.mId = kCity;
        city.mParent = hold.mCommon.mId;
        LocationRecord elsewhere;
        elsewhere.mCommon.mId = kSelf | 0x11;
        records.mLocations = { hold, city, elsewhere };
        locations.add(std::move(records));

        Fixture f;
        f.mWorld.setLocations(&locations);
        f.mWorld.setAliasView([&](std::uint32_t alias) {
            return alias == 7 ? kCity : alias == 8 ? FormId(kSelf | 0x11) : FormId(0);
        });
        EXPECT_FALSE(f.mWorld.answers(inLocAlias(7))); // the player's location is not told yet
        f.mWorld.setPlayerLocation(kCity);
        EXPECT_TRUE(f.mWorld.answers(inLocAlias(7)));
        EXPECT_TRUE(f.holds(inLocAlias(7)));
        EXPECT_FALSE(f.holds(inLocAlias(8))); // another location
        EXPECT_FALSE(f.holds(inLocAlias(9))); // an alias nothing fills
        f.mWorld.setAliasView([&](std::uint32_t) { return FormId(kSelf | 0x10); });
        EXPECT_TRUE(f.holds(inLocAlias(7))); // the city is inside the hold
        f.mWorld.setPlayerLocation(kNullFormId);
        EXPECT_FALSE(f.holds(inLocAlias(7)));
    }

    // ---- a ForceGreet package leads its actor to the player (DLC1VQ00's messenger waits outside the town) ----------

    using namespace openeden::sdk::game;

    constexpr FormId kPackGreet = kSelf | 0x912;
    constexpr FormId kTopicIntro = kSelf | 0x300;

    class GreetSource final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId, std::vector<const PackageRecord*>& out) const override
        {
            out = mList;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord&) const override { return PackageBehavior::Stand; }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        bool forceGreetTopic(const PackageRecord&, FormId& out) const override
        {
            out = kTopicIntro;
            return true;
        }
        std::vector<const PackageRecord*> mList;
    };

    class GreetWriter final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal& goal) override
        {
            ++mGoals;
            mLastGoalX = goal.x;
            return true;
        }
        bool cancelGoal(ActorHandle) override
        {
            ++mCancels;
            return true;
        }
        bool setHeading(ActorHandle, float) override { return true; }
        ActorClock clock() const override { return ActorClock{ 12.f, 0, true }; }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = 0.0;
            y = 0.0;
            z = 0.0;
            return true;
        }
        bool actorPosition(ActorHandle, double& x, double& y, double& z) const override
        {
            x = mActorX;
            y = 0.0;
            z = 0.0;
            return true;
        }
        double mActorX = 5000.0;
        double mLastGoalX = 0.0;
        int mGoals = 0;
        int mCancels = 0;
    };

    struct GreetRig
    {
        explicit GreetRig(double actorX)
        {
            mPackage.mCommon.mId = kPackGreet;
            mPackage.mHasPkdt = true;
            mSource.mList = { &mPackage };
            mWriter.mActorX = actorX;
            mBrain = std::make_unique<PackageBrain>(mSource);
            mBrain->setForceGreetHook([this](WorldId, FormId, FormId) { ++mAsks; });
            ActorInfo info;
            info.handle = { 77, 1 };
            info.base = kSelf | 0x200;
            info.x = actorX;
            mBrain->onActorAdded(info);
        }
        void run(int updates)
        {
            for (int i = 0; i < updates; ++i)
                mBrain->update(1.1f, mWriter);
        }
        PackageRecord mPackage;
        GreetSource mSource;
        GreetWriter mWriter;
        std::unique_ptr<PackageBrain> mBrain;
        int mAsks = 0;
    };

    TEST(SkyrimDawnguardGreet, anActorFarFromThePlayerWalksUpBeforeItAsksToGreet)
    {
        GreetRig rig(kGreetApproachFrom + 800.0);
        rig.run(4);
        EXPECT_EQ(rig.mAsks, 0);
        EXPECT_GE(rig.mWriter.mGoals, 1);
        EXPECT_LT(rig.mWriter.mLastGoalX, rig.mWriter.mActorX); // towards the player at the origin
    }

    TEST(SkyrimDawnguardGreet, onceNearEnoughItStopsAndAsks)
    {
        GreetRig rig(kGreetApproachFrom + 800.0);
        rig.run(3);
        ASSERT_EQ(rig.mAsks, 0);
        rig.mWriter.mActorX = kGreetApproachStop - 100.0; // it walked up
        rig.run(4);
        EXPECT_GE(rig.mAsks, 1);
        EXPECT_GE(rig.mWriter.mCancels, 1); // the leg out was ended
    }

    TEST(SkyrimDawnguardGreet, anActorAlreadyNearAsksAtOnceAndNeverWalks)
    {
        GreetRig rig(kGreetApproachFrom - 600.0);
        rig.run(3);
        EXPECT_GE(rig.mAsks, 1);
        EXPECT_EQ(rig.mWriter.mGoals, 0);
    }
}
