// Task 0135 stage S4: the go-to follower of a neutral actor, tested with a fake path finder and a point that moves as
// the follower asks. Written from the header before the implementation (the first run was against a stub).
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralPath;

    class FakeFinder final : public PathFinder
    {
    public:
        FindStatus find(const vsg::vec3& from, const vsg::vec3& to, std::vector<vsg::vec3>& out) const override
        {
            ++mCalls;
            mLastFrom = from;
            mLastTo = to;
            if (!mScript.empty())
            {
                const FindStatus status = mScript[std::min<std::size_t>(mCalls - 1, mScript.size() - 1)];
                if (status == FindStatus::NotReady || status == FindStatus::NoPath)
                    return status;
                if (status == FindStatus::Partial)
                {
                    out = mPartialPath;
                    return status;
                }
            }
            out = mPath;
            return FindStatus::Success;
        }
        std::vector<vsg::vec3> mPath;
        std::vector<vsg::vec3> mPartialPath;
        std::vector<FindStatus> mScript; ///< status of call n (the last repeats); empty means Success
        mutable std::size_t mCalls = 0;
        mutable vsg::vec3 mLastFrom;
        mutable vsg::vec3 mLastTo;
    };

    float length2d(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }

    // The follower moving a point at exactly the velocity it asks for (a mover that never blocks).
    struct Sim
    {
        vsg::vec3 mFeet{ 0.f, 0.f, 0.f };
        Follower mFollower;
        float mVelX = 0.f;
        float mVelY = 0.f;
        Outcome mOutcome = Outcome::None;
        int mFrames = 0;

        void frame(const FakeFinder& finder, float dt, bool canMove = true)
        {
            const StepResult result = mFollower.step(mFeet, dt, finder);
            ++mFrames;
            if (result.mSetVelocity)
            {
                mVelX = result.mVelX;
                mVelY = result.mVelY;
            }
            if (result.mOutcome != Outcome::None)
                mOutcome = result.mOutcome;
            if (canMove)
            {
                mFeet.x += mVelX * dt;
                mFeet.y += mVelY * dt;
            }
        }
    };

    FakeFinder lPath()
    {
        FakeFinder finder;
        finder.mPath = { { 100.f, 0.f, 0.f }, { 100.f, 100.f, 0.f } };
        return finder;
    }

    TEST(NeutralPathFollower, withNoGoalItSetsNothing)
    {
        Follower follower;
        FakeFinder finder;
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_FALSE(r.mSetVelocity);
        EXPECT_EQ(r.mOutcome, Outcome::None);
        EXPECT_EQ(finder.mCalls, 0u);
        EXPECT_FALSE(follower.active());
    }

    TEST(NeutralPathFollower, theFirstStepAsksForAPathAndHeadsForItsFirstWaypointAtTheSpeed)
    {
        FakeFinder finder = lPath();
        Follower follower;
        ASSERT_TRUE(follower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed));
        EXPECT_TRUE(follower.active());
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(finder.mCalls, 1u);
        EXPECT_EQ(finder.mLastTo.x, 100.f);
        EXPECT_EQ(finder.mLastTo.y, 100.f);
        ASSERT_TRUE(r.mSetVelocity);
        EXPECT_NEAR(r.mVelX, kWalkSpeed, 1e-3f);
        EXPECT_NEAR(r.mVelY, 0.f, 1e-3f);
        EXPECT_EQ(r.mOutcome, Outcome::None);
        EXPECT_EQ(follower.path().size(), 2u);
    }

    TEST(NeutralPathFollower, itTurnsAtTheCornerAndArrives)
    {
        FakeFinder finder = lPath();
        Sim sim;
        ASSERT_TRUE(sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed));
        bool wentNorth = false;
        for (int i = 0; i < 600 && sim.mOutcome == Outcome::None; ++i)
        {
            sim.frame(finder, 1.f / 60.f);
            if (sim.mVelY > 0.9f * kWalkSpeed && std::fabs(sim.mVelX) < 0.5f * kWalkSpeed)
                wentNorth = true;
        }
        EXPECT_TRUE(wentNorth); // the second leg runs along +Y
        EXPECT_EQ(sim.mOutcome, Outcome::Arrived);
        EXPECT_LE(length2d(sim.mFeet.x - 100.f, sim.mFeet.y - 100.f), kArrivalRadius);
        EXPECT_FALSE(sim.mFollower.active());
        // 200 units at 120 a second: about 100 frames; arriving early by the arrival radius is allowed
        EXPECT_GE(sim.mFrames, 80);
        EXPECT_LE(sim.mFrames, 130);
        EXPECT_EQ(finder.mCalls, 1u);
    }

    TEST(NeutralPathFollower, arrivalStopsTheActorAndEndsTheGoalOnce)
    {
        FakeFinder finder;
        finder.mPath = { { 10.f, 0.f, 0.f } };
        Follower follower;
        ASSERT_TRUE(follower.setGoal({ 10.f, 0.f, 0.f }, kWalkSpeed));
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder); // 10 units: inside the arrival radius
        EXPECT_EQ(r.mOutcome, Outcome::Arrived);
        ASSERT_TRUE(r.mSetVelocity);
        EXPECT_EQ(r.mVelX, 0.f);
        EXPECT_EQ(r.mVelY, 0.f);
        const StepResult again = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_FALSE(again.mSetVelocity);
        EXPECT_EQ(again.mOutcome, Outcome::None);
    }

    TEST(NeutralPathFollower, theArrivalRadiusIsOnTheGroundPlaneAndTheHeightIsChecked)
    {
        {
            FakeFinder finder;
            finder.mPath = { { 300.f, 0.f, 0.f } };
            Follower follower;
            follower.setGoal({ 300.f, 0.f, 0.f }, kWalkSpeed);
            // 25 units short on the plane: not arrived (kArrivalRadius is 24)
            const StepResult r = follower.step({ 300.f - kArrivalRadius - 1.f, 0.f, 0.f }, 0.016f, finder);
            EXPECT_EQ(r.mOutcome, Outcome::None);
        }
        {
            FakeFinder finder;
            finder.mPath = { { 100.f, 0.f, 0.f }, { 300.f, 0.f, 0.f } };
            Follower follower;
            follower.setGoal({ 300.f, 0.f, 0.f }, kWalkSpeed);
            // beside the goal on the plane but a floor lower than the arrival height: another storey, not there yet
            const StepResult r = follower.step({ 290.f, 0.f, -(kArrivalHeight + 8.f) }, 0.016f, finder);
            EXPECT_EQ(r.mOutcome, Outcome::None);
        }
        {
            FakeFinder finder;
            finder.mPath = { { 300.f, 0.f, 0.f } };
            Follower follower;
            follower.setGoal({ 300.f, 0.f, 0.f }, kWalkSpeed);
            const StepResult r = follower.step({ 300.f - kArrivalRadius + 1.f, 0.f, 10.f }, 0.016f, finder);
            EXPECT_EQ(r.mOutcome, Outcome::Arrived);
        }
    }

    TEST(NeutralPathFollower, theRunSpeedIsUsedWhenAsked)
    {
        FakeFinder finder = lPath();
        Follower follower;
        follower.setGoal({ 100.f, 100.f, 0.f }, kRunSpeed);
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_NEAR(length2d(r.mVelX, r.mVelY), kRunSpeed, 1e-2f);
    }

    TEST(NeutralPathFollower, noPathEndsTheGoalAtOnce)
    {
        FakeFinder finder;
        finder.mScript = { FindStatus::NoPath };
        Follower follower;
        follower.setGoal({ 500.f, 0.f, 0.f }, kWalkSpeed);
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(r.mOutcome, Outcome::NoPath);
        EXPECT_TRUE(r.mSetVelocity);
        EXPECT_EQ(r.mVelX, 0.f);
        EXPECT_FALSE(follower.active());
        EXPECT_EQ(finder.mCalls, 1u);
    }

    TEST(NeutralPathFollower, aPartialPathThatStopsShortIsNoPath)
    {
        FakeFinder finder;
        finder.mScript = { FindStatus::Partial };
        finder.mPartialPath = { { 100.f, 0.f, 0.f } };
        Follower follower;
        follower.setGoal({ 500.f, 0.f, 0.f }, kWalkSpeed);
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(r.mOutcome, Outcome::NoPath);
    }

    TEST(NeutralPathFollower, aPartialPathThatEndsAtTheGoalIsFollowed)
    {
        FakeFinder finder;
        finder.mScript = { FindStatus::Partial };
        finder.mPartialPath = { { 490.f, 0.f, 0.f } }; // within the arrival radius of the goal
        Follower follower;
        follower.setGoal({ 500.f, 0.f, 0.f }, kWalkSpeed);
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(r.mOutcome, Outcome::None);
        EXPECT_GT(r.mVelX, 0.f);
    }

    TEST(NeutralPathFollower, anEmptyPathIsNoPathUnlessAlreadyThere)
    {
        FakeFinder finder; // Success with no waypoints
        Follower far;
        far.setGoal({ 500.f, 0.f, 0.f }, kWalkSpeed);
        EXPECT_EQ(far.step({ 0.f, 0.f, 0.f }, 0.016f, finder).mOutcome, Outcome::NoPath);
        Follower near;
        near.setGoal({ 5.f, 0.f, 0.f }, kWalkSpeed);
        EXPECT_EQ(near.step({ 0.f, 0.f, 0.f }, 0.016f, finder).mOutcome, Outcome::Arrived);
    }

    TEST(NeutralPathFollower, aNavigatorThatIsNotReadyIsAskedAgainLaterNotEveryFrame)
    {
        FakeFinder finder = lPath();
        finder.mScript = { FindStatus::NotReady, FindStatus::NotReady, FindStatus::Success };
        Sim sim;
        sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        sim.frame(finder, 0.1f);
        EXPECT_EQ(finder.mCalls, 1u);
        EXPECT_EQ(sim.mOutcome, Outcome::None);
        sim.frame(finder, 0.1f); // 0.1 s: still waiting for the retry interval
        sim.frame(finder, 0.1f);
        EXPECT_EQ(finder.mCalls, 1u);
        for (int i = 0; i < 20 && finder.mCalls < 3; ++i)
            sim.frame(finder, 0.1f);
        EXPECT_EQ(finder.mCalls, 3u);
        EXPECT_EQ(sim.mFollower.path().size(), 2u);
        EXPECT_EQ(sim.mOutcome, Outcome::None);
    }

    TEST(NeutralPathFollower, aNavigatorThatNeverGetsReadyEndsAsNoPathAfterABoundedNumberOfAsks)
    {
        FakeFinder finder;
        finder.mScript = { FindStatus::NotReady };
        Sim sim;
        sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        for (int i = 0; i < 400 && sim.mOutcome == Outcome::None; ++i)
            sim.frame(finder, 0.1f);
        EXPECT_EQ(sim.mOutcome, Outcome::NoPath);
        EXPECT_EQ(finder.mCalls, static_cast<std::size_t>(kMaxNotReadyAttempts));
    }

    TEST(NeutralPathFollower, anActorThatCannotMoveAsksForNewPathsAFewTimesThenIsBlocked)
    {
        FakeFinder finder = lPath();
        Sim sim;
        sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        for (int i = 0; i < 2000 && sim.mOutcome == Outcome::None; ++i)
            sim.frame(finder, 1.f / 60.f, false); // a wall in front of it
        EXPECT_EQ(sim.mOutcome, Outcome::Blocked);
        EXPECT_EQ(finder.mCalls, static_cast<std::size_t>(1 + kMaxRepaths)); // the path and each repath
        EXPECT_EQ(sim.mVelX, 0.f);
        EXPECT_FALSE(sim.mFollower.active());
        // each try takes one stall window, plus a frame to ask: bounded in time
        EXPECT_LE(sim.mFrames, static_cast<int>((kMaxRepaths + 1) * (kStallWindow * 60.f + 3.f)));
    }

    TEST(NeutralPathFollower, anActorThatGetsFreeAfterOneStallArrivesWithoutBeingBlocked)
    {
        FakeFinder finder = lPath();
        Sim sim;
        sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        for (int i = 0; i < 70; ++i)
            sim.frame(finder, 1.f / 60.f, false); // stuck for more than a window
        EXPECT_EQ(sim.mOutcome, Outcome::None);
        EXPECT_EQ(finder.mCalls, 2u);
        for (int i = 0; i < 800 && sim.mOutcome == Outcome::None; ++i)
            sim.frame(finder, 1.f / 60.f);
        EXPECT_EQ(sim.mOutcome, Outcome::Arrived);
    }

    TEST(NeutralPathFollower, aSlowButRealProgressIsNotAStall)
    {
        FakeFinder finder;
        finder.mPath = { { 1000.f, 0.f, 0.f } };
        Sim sim;
        sim.mFollower.setGoal({ 1000.f, 0.f, 0.f }, kWalkSpeed);
        // the mover only gets 40 percent of the way the request asks (a slope, a crowd): above kStallProgress
        for (int i = 0; i < 300; ++i)
        {
            const StepResult r = sim.mFollower.step(sim.mFeet, 1.f / 60.f, finder);
            sim.mFeet.x += (r.mSetVelocity ? r.mVelX : kWalkSpeed) * 0.4f / 60.f;
            ASSERT_EQ(r.mOutcome, Outcome::None);
        }
        EXPECT_EQ(finder.mCalls, 1u);
        EXPECT_EQ(sim.mFollower.repaths(), 0);
    }

    TEST(NeutralPathFollower, cancelEndsTheGoalWithoutAResultAndANewGoalReplacesTheOld)
    {
        FakeFinder finder = lPath();
        Follower follower;
        follower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        follower.cancel();
        EXPECT_FALSE(follower.active());
        const StepResult r = follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_FALSE(r.mSetVelocity);
        EXPECT_EQ(r.mOutcome, Outcome::None);

        follower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
        follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(finder.mCalls, 2u);
        follower.setGoal({ -50.f, 0.f, 0.f }, kWalkSpeed); // replaced: no result for the first
        follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
        EXPECT_EQ(finder.mCalls, 3u);
        EXPECT_EQ(finder.mLastTo.x, -50.f);
    }

    TEST(NeutralPathFollower, badInputsAreRefusedOrIgnored)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Follower follower;
        EXPECT_FALSE(follower.setGoal({ nan, 0.f, 0.f }, kWalkSpeed));
        EXPECT_FALSE(follower.setGoal({ 0.f, inf, 0.f }, kWalkSpeed));
        EXPECT_FALSE(follower.setGoal({ 0.f, 0.f, 0.f }, nan));
        EXPECT_FALSE(follower.setGoal({ 0.f, 0.f, 0.f }, 0.f));
        EXPECT_FALSE(follower.setGoal({ 0.f, 0.f, 0.f }, -5.f));
        EXPECT_FALSE(follower.active());

        FakeFinder finder = lPath();
        ASSERT_TRUE(follower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed));
        for (const float dt : { 0.f, -1.f, nan, inf })
        {
            const StepResult r = follower.step({ 0.f, 0.f, 0.f }, dt, finder);
            EXPECT_FALSE(r.mSetVelocity);
            EXPECT_EQ(r.mOutcome, Outcome::None);
        }
        EXPECT_EQ(finder.mCalls, 0u);
        EXPECT_TRUE(follower.active());
        // a position that is not finite does not start anything either
        EXPECT_FALSE(follower.step({ nan, 0.f, 0.f }, 0.016f, finder).mSetVelocity);
        EXPECT_EQ(finder.mCalls, 0u);
    }

    TEST(NeutralPathFollower, theVelocityNeverOvershootsTheWaypointInOneFrame)
    {
        FakeFinder finder;
        finder.mPath = { { 20.f, 0.f, 0.f }, { 400.f, 0.f, 0.f } };
        Follower follower;
        follower.setGoal({ 400.f, 0.f, 0.f }, kRunSpeed);
        // at 15 units from waypoint 0 (outside kWaypointRadius 12) with a huge frame time
        const StepResult r = follower.step({ 5.f, 0.f, 0.f }, 1.f, finder);
        ASSERT_TRUE(r.mSetVelocity);
        EXPECT_LE(r.mVelX * 1.f, 15.f + 1e-3f);
    }

    TEST(NeutralPathFollower, theSameFramesGiveTheSameResult)
    {
        auto run = [] {
            FakeFinder finder = lPath();
            Sim sim;
            sim.mFollower.setGoal({ 100.f, 100.f, 0.f }, kWalkSpeed);
            std::vector<float> trace;
            for (int i = 0; i < 150; ++i)
            {
                sim.frame(finder, 1.f / 60.f);
                trace.push_back(sim.mFeet.x);
                trace.push_back(sim.mFeet.y);
            }
            return trace;
        };
        EXPECT_EQ(run(), run());
    }

    TEST(NeutralPathFollower, followingAStoredPathAllocatesNothing)
    {
        FakeFinder finder;
        finder.mPath = { { 300.f, 0.f, 0.f }, { 300.f, 300.f, 0.f } };
        Sim sim;
        sim.mFollower.setGoal({ 300.f, 300.f, 0.f }, kWalkSpeed);
        sim.frame(finder, 1.f / 60.f); // the path request may allocate
        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 90; ++i)
            sim.frame(finder, 1.f / 60.f);
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_EQ(sim.mOutcome, Outcome::None);
    }

    TEST(NeutralPathFollower, theNavmeshAgentIsTheSessionsUnlessTheActorIsWider)
    {
        const vsg::vec3 session(18.f, 18.f, 66.5f);
        const vsg::vec3 same = agentHalfExtents(22.f, session);
        EXPECT_EQ(same.x, 22.f); // wider than the session's agent: the path keeps the capsule's clearance
        EXPECT_EQ(same.y, 22.f);
        EXPECT_EQ(same.z, 66.5f);
        const vsg::vec3 narrow = agentHalfExtents(10.f, session);
        EXPECT_EQ(narrow.x, 18.f); // narrower: the session's navmesh is reused
        EXPECT_EQ(narrow.y, 18.f);
        EXPECT_EQ(narrow.z, 66.5f);
        const vsg::vec3 bad = agentHalfExtents(std::numeric_limits<float>::quiet_NaN(), session);
        EXPECT_EQ(bad.x, 18.f);
        const vsg::vec3 zero = agentHalfExtents(0.f, session);
        EXPECT_EQ(zero.x, 18.f);
    }
}
