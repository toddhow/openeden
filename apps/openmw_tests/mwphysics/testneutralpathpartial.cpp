// Task 0158: a partial path that ends under (or over) its goal on another floor is not a way to the goal. Lucia's bed
// is on the upper floor; the navmesh has no connection, so its partial path ended on the cellar floor straight below
// the bed, and the follower (which only compared the ground plane) walked her there for ever.
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    using namespace MWPhysics::NeutralPath;

    class PartialFinder final : public PathFinder
    {
    public:
        FindStatus find(const vsg::vec3&, const vsg::vec3&, std::vector<vsg::vec3>& out) const override
        {
            out = mPath;
            return FindStatus::Partial;
        }
        std::vector<vsg::vec3> mPath;
    };

    StepResult stepOnce(const vsg::vec3& end, const vsg::vec3& goal)
    {
        PartialFinder finder;
        finder.mPath = { end };
        Follower follower;
        follower.setGoal(goal, kWalkSpeed);
        return follower.step({ 0.f, 0.f, 0.f }, 0.016f, finder);
    }
}

TEST(NeutralPathPartial, aPathEndingBelowTheGoalOnAnotherFloorIsNoPath)
{
    EXPECT_EQ(stepOnce({ 500.f, 0.f, 35.f }, { 500.f, 0.f, 318.f }).mOutcome, Outcome::NoPath);
}

TEST(NeutralPathPartial, aPathEndingAboveTheGoalOnAnotherFloorIsNoPath)
{
    EXPECT_EQ(stepOnce({ 500.f, 0.f, 320.f }, { 500.f, 0.f, 35.f }).mOutcome, Outcome::NoPath);
}

TEST(NeutralPathPartial, aPathEndingOnTheGoalsFloorIsStillFollowed)
{
    const StepResult r = stepOnce({ 495.f, 0.f, 330.f }, { 500.f, 0.f, 318.f });
    EXPECT_EQ(r.mOutcome, Outcome::None);
    EXPECT_GT(r.mVelX, 0.f);
}
