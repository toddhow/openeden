// Task 0159: a waypoint counts as reached only within the mover's step height of it (a raised lip next to the waypoint
// on the ground plane is not the waypoint). Written before the change; the first run was against the ground-plane-only
// rule.
#include <gtest/gtest.h>

#include <vector>

#include "apps/openmw/mwphysics/neutralmover.hpp"
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"

namespace
{
    using namespace MWPhysics::NeutralPath;

    class OnePath final : public PathFinder
    {
    public:
        explicit OnePath(std::vector<vsg::vec3> path)
            : mPath(std::move(path))
        {
        }
        FindStatus find(const vsg::vec3&, const vsg::vec3&, std::vector<vsg::vec3>& out) const override
        {
            out = mPath;
            return FindStatus::Success;
        }

    private:
        std::vector<vsg::vec3> mPath;
    };

    TEST(NeutralPathWaypointZ, aWaypointOverheadOnTheGroundPlaneButMuchHigherIsNotReached)
    {
        const OnePath finder({ { 100.f, 0.f, 60.f }, { 400.f, 0.f, 60.f } });
        Follower follower;
        ASSERT_TRUE(follower.setGoal({ 400.f, 0.f, 60.f }, kWalkSpeed));
        // The actor stands right under the first waypoint, 60 lower than it (a different floor, or the foot of a lip).
        const StepResult r = follower.step({ 100.f, 0.f, 0.f }, 1.f / 60.f, finder);
        EXPECT_EQ(r.mOutcome, Outcome::None);
        EXPECT_EQ(follower.reached(), 0u);
    }

    TEST(NeutralPathWaypointZ, aWaypointWithinTheStepHeightIsReachedAsBefore)
    {
        const OnePath finder({ { 100.f, 0.f, 10.f }, { 400.f, 0.f, 10.f } });
        Follower follower;
        ASSERT_TRUE(follower.setGoal({ 400.f, 0.f, 10.f }, kWalkSpeed));
        const StepResult r = follower.step({ 100.f, 0.f, 0.f }, 1.f / 60.f, finder);
        EXPECT_EQ(r.mOutcome, Outcome::None);
        EXPECT_EQ(follower.reached(), 1u);
    }
}
