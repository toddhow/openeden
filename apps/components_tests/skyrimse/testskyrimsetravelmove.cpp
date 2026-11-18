// Task 0286: a scene's Travel package whose target stands in another cell asks the world to MOVE the actor there (the
// travel move hook) instead of waiting to be carried through the player's doors (task 0277). Pure tests over a fake
// source and writer; written first and run red against a brain that has no hook.
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <set>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr FormId kBase = 0x00013BA7;
    constexpr WorldId kRef = 0x10;
    constexpr FormId kMarker = 0x500;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            const auto it = mLists.find(base);
            if (it == mLists.end())
                return false;
            out = it->second;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord& p) const override
        {
            const auto it = mBehavior.find(p.mCommon.mId);
            return it == mBehavior.end() ? PackageBehavior::Stand : it->second;
        }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPlaces.find(reference);
            if (it == mPlaces.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        bool targetIsElsewhere(FormId reference) const override { return mElsewhere.count(reference) != 0; }
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, std::array<double, 3>> mPlaces;
        std::set<FormId> mElsewhere;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle h, const ActorGoal&) override
        {
            ++mGoals;
            (void)h;
            return true;
        }
        bool cancelGoal(ActorHandle) override { return true; }
        bool placeActor(ActorHandle, double, double, double, float) override
        {
            ++mPlaced;
            return true;
        }
        ActorClock clock() const override { return { 12.f, 0, true }; }
        int mGoals = 0;
        int mPlaced = 0;
    };

    ActorInfo infoOf(WorldId ref)
    {
        ActorInfo i;
        i.handle = { ref, 1 };
        i.base = kBase;
        i.editorId = "Test";
        i.x = 100;
        i.y = 200;
        i.z = 66;
        return i;
    }

    struct Rig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;
        std::vector<std::pair<WorldId, FormId>> mAsked;

        Rig(bool carry, bool hook, bool accept, bool scene = true)
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 0 });
            src.mLists[kBase] = {};
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mElsewhere.insert(kMarker);
            brain = std::make_unique<PackageBrain>(src);
            brain->setCarryTravellers(carry);
            if (hook)
                brain->setTravelMoveHook([this, accept](WorldId actor, FormId target) {
                    mAsked.emplace_back(actor, target);
                    return accept;
                });
            brain->onActorAdded(infoOf(kRef));
            if (scene)
                brain->forcePackage(kRef, &pkg);
            else
                src.mLists[kBase] = { &pkg };
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    TEST(SkyrimTravelMove, aHookThatAcceptsMovesTheActorAndEndsThePackageWithNoCarry)
    {
        Rig r(true, true, true);
        r.step(6);
        ASSERT_EQ(r.mAsked.size(), 1u);
        EXPECT_EQ(r.mAsked[0].first, kRef);
        EXPECT_EQ(r.mAsked[0].second, kMarker);
        EXPECT_TRUE(r.brain->travellerRefs().empty());
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
        EXPECT_EQ(r.brain->stats().mTravelMoves, 1u);
        EXPECT_EQ(r.brain->stats().mTravelSnaps, 0u);
        EXPECT_EQ(r.w.mPlaced, 0);
    }

    TEST(SkyrimTravelMove, theHookWorksWithTheCarryOptionOffToo)
    {
        Rig r(false, true, true);
        r.step(6);
        EXPECT_EQ(r.mAsked.size(), 1u);
        EXPECT_EQ(r.brain->stats().mTravelMoves, 1u);
        EXPECT_EQ(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimTravelMove, aHookThatRefusesLeavesTheCarryOfTheEarlierTaskInPlace)
    {
        Rig r(true, true, false);
        r.step(6);
        EXPECT_GE(r.mAsked.size(), 1u);
        EXPECT_EQ(r.brain->stats().mTravelMoves, 0u);
        EXPECT_EQ(r.brain->travellerRefs().count(kRef), 1u);
        EXPECT_NE(r.brain->view(kRef)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimTravelMove, withNoHookTheCarryIsAsItWas)
    {
        Rig r(true, false, false);
        r.step(6);
        EXPECT_TRUE(r.mAsked.empty());
        EXPECT_EQ(r.brain->travellerRefs().count(kRef), 1u);
        EXPECT_EQ(r.brain->stats().mTravelMoves, 0u);
    }

    TEST(SkyrimTravelMove, anActorsOwnScheduleNeverAsksTheHook)
    {
        Rig r(true, true, true, false);
        r.step(6);
        EXPECT_TRUE(r.mAsked.empty());
        EXPECT_EQ(r.brain->stats().mTravelMoves, 0u);
    }

    TEST(SkyrimTravelMove, aTargetInTheSameCellIsWalkedToAndNeverAsksTheHook)
    {
        Rig r(true, true, true);
        r.src.mElsewhere.clear();
        r.src.mPlaces[kMarker] = { 5000, 6000, 10 };
        r.step(6);
        EXPECT_TRUE(r.mAsked.empty());
        EXPECT_GT(r.w.mGoals, 0);
    }
}
