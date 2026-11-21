#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest_priority.hpp>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

// Task 0349 (main quest MQ106 "A Blade in the Dark"): Delphine's alias package MQ106DelphineToRoom walks her to the
// rented room behind locked doors, which the mover cannot path through; the quest's named Travel packages end with her
// placed at the target (a stand-in for the game's own door handling, like task 0341's MUST COMPLETE rule).

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
        std::uint32_t packagesRevision() const override { return mRevision; }
        std::uint32_t mRevision = 0;
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, std::array<double, 3>> mPlaces;
        std::set<FormId> mElsewhere;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle h, const ActorGoal& g) override
        {
            mGoals.push_back({ h.reference, g.x, g.y, g.z });
            return true;
        }
        bool cancelGoal(ActorHandle) override { return true; }
        bool placeActor(ActorHandle h, double x, double y, double z, float) override
        {
            mPlaced.push_back({ h.reference, x, y, z });
            return true;
        }
        bool actorPosition(ActorHandle h, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(h.reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        ActorClock clock() const override { return { 12.f, 0, true }; }
        struct Goal
        {
            WorldId mRef;
            double mX, mY, mZ;
        };
        std::vector<Goal> mGoals;
        std::vector<Goal> mPlaced;
        std::map<WorldId, std::array<double, 3>> mPositions;
    };

    ActorInfo infoOf(WorldId ref, std::uint32_t session)
    {
        ActorInfo i;
        i.handle = { ref, session };
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

        // a quest alias's package (not a scene's forced one) in the actor's list, flags = PKDT flags
        explicit Rig(bool useSet, std::uint32_t flags)
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mFlags = flags;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, kMarker, 0 });
            src.mLists[kBase] = { &pkg };
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mPlaces[kMarker] = { -1376, -490, 318 };
            brain = std::make_unique<PackageBrain>(src);
            if (useSet)
                brain->setSnapPackages({ 1 }); // task 0349: the quest's named Travel packages
            w.mPositions[kRef] = { 100, 200, 66 };
            brain->onActorAdded(infoOf(kRef, 1));
        }
        void step(int n)
        {
            for (int i = 0; i < n; ++i)
                brain->update(0.5f, w);
        }
    };

    TEST(SkyrimBlade349Snap, aNamedTravelPackageThatCannotBeWalkedEndsWithTheActorPlacedAtTheTarget)
    {
        Rig r(true, 0);
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        r.step(1);
        ASSERT_EQ(r.w.mPlaced.size(), 1u);
        EXPECT_NEAR(r.w.mPlaced[0].mX, -1376.0, 1e-6);
        EXPECT_EQ(r.brain->stats().mTravelSnaps, 1u);
    }

    TEST(SkyrimBlade349Snap, aPackageNotInTheSetIsNotPlaced)
    {
        Rig r(false, 0);
        r.step(3);
        ASSERT_FALSE(r.w.mGoals.empty());
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::NoPath);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
    }

    TEST(SkyrimBlade349Snap, aNamedPackageThatArrivesIsNotPlaced)
    {
        Rig r(true, 0);
        r.step(3);
        r.brain->onGoalFinished({ kRef, 1 }, GoalResult::Arrived);
        r.step(3);
        EXPECT_TRUE(r.w.mPlaced.empty());
    }
}
