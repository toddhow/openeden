// Task 0280 (dragons, second slice): a path asked while the navigator is still building the navmesh for the actor's
// agent is not an answer. The first path of an agent size the session did not build for (the dragon's cylinder of
// radius 150) came back PARTIAL (two waypoints, 42 units: only the start's tile existed yet) and the follower took a
// partial path that stops short of the goal for "no way to the target"; the actor then gave up after a few asks. Found
// by running a dragon on the Whiterun plains (docs/tasks/0280-skyrim-dragons-2.md), where the same walk with the
// navmesh built answered. `refineNeutralPathStatus` turns a Partial answer into NotReady while the navigator has update
// jobs pending, so the follower asks again (up to its 40 attempts, 20 s). Written before the code (the first run was
// against a stub that returned its input).
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <apps/openmw/mwphysics/neutralmover.hpp>
#include <apps/openmw/mwworld/neutralnavigation.hpp>

namespace
{
    using MWPhysics::NeutralPath::FindStatus;
    using MWWorld::refineNeutralPathStatus;

    TEST(NeutralPathBusy, aPartialPathWhileTheNavmeshIsBeingBuiltIsNotReady)
    {
        EXPECT_EQ(refineNeutralPathStatus(FindStatus::Partial, true), FindStatus::NotReady);
    }

    TEST(NeutralPathBusy, aPartialPathWithNothingPendingIsAPartialPath)
    {
        EXPECT_EQ(refineNeutralPathStatus(FindStatus::Partial, false), FindStatus::Partial);
    }

    TEST(NeutralPathBusy, everyOtherAnswerIsKeptWhetherOrNotTheNavigatorIsBusy)
    {
        for (const bool busy : { false, true })
        {
            EXPECT_EQ(refineNeutralPathStatus(FindStatus::Success, busy), FindStatus::Success) << busy;
            EXPECT_EQ(refineNeutralPathStatus(FindStatus::NotReady, busy), FindStatus::NotReady) << busy;
            EXPECT_EQ(refineNeutralPathStatus(FindStatus::NoPath, busy), FindStatus::NoPath) << busy;
        }
    }

    // The agent size: one navmesh per size, so a dragon (radius 150) goes by the widest one the creatures already use.
    TEST(NeutralNavigationRadius, aCapsuleNarrowerThanALargeCreatureKeepsItsOwnRadius)
    {
        using MWPhysics::NeutralPath::navigationRadius;
        for (const float radius : { 10.f, 22.f, 32.f, 68.f, 100.f, 119.9f })
            EXPECT_FLOAT_EQ(navigationRadius(radius), radius) << radius;
    }

    TEST(NeutralNavigationRadius, aLargeCreatureGoesByTheLimit)
    {
        using MWPhysics::NeutralPath::kMaxNavigationRadius;
        using MWPhysics::NeutralPath::navigationRadius;
        EXPECT_FLOAT_EQ(kMaxNavigationRadius, 32.f);
        for (const float radius : { MWPhysics::NeutralMover::kLargeCreatureRadius, 150.f, 500.f })
            EXPECT_FLOAT_EQ(navigationRadius(radius), kMaxNavigationRadius) << radius;
    }

    TEST(NeutralNavigationRadius, aRadiusThatIsNotUsableIsReturnedForTheAgentRuleToIgnore)
    {
        using MWPhysics::NeutralPath::navigationRadius;
        EXPECT_FLOAT_EQ(navigationRadius(0.f), 0.f);
        EXPECT_FLOAT_EQ(navigationRadius(-5.f), -5.f);
        EXPECT_TRUE(std::isnan(navigationRadius(std::numeric_limits<float>::quiet_NaN())));
    }
}
