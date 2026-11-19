// Task 0336: a goal beyond the navmesh built round the player. The navigator answers with a path that stops short (or
// not at all, until the tiles are there); the follower walks the partial path to its end and asks again, as long as
// each path gets it nearer. Written before the change; the first run was against a follower that ignored the option.
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace MWPhysics::NeutralPath;

    // The navmesh covers 3,000 units of the line to the goal round the asker: a path to the goal ends there.
    class ReachFinder final : public PathFinder
    {
    public:
        FindStatus find(const vsg::vec3& from, const vsg::vec3& to, std::vector<vsg::vec3>& out) const override
        {
            ++mCalls;
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= mReach)
            {
                out = { to };
                return FindStatus::Success;
            }
            if (mReach <= 0.f)
                return FindStatus::Partial;
            out = { vsg::vec3(from.x + dx / distance * mReach, from.y + dy / distance * mReach, 0.f) };
            return FindStatus::Partial;
        }
        float mReach = 3000.f;
        mutable int mCalls = 0;
    };

    struct Walk
    {
        vsg::vec3 mFeet{ 0.f, 0.f, 0.f };
        Follower mFollower;
        Outcome mOutcome = Outcome::None;
        float mVelX = 0.f;
        float mVelY = 0.f;
        int mFrames = 0;

        void run(const PathFinder& finder, int maxFrames)
        {
            for (int i = 0; i < maxFrames && mOutcome == Outcome::None; ++i)
            {
                const StepResult r = mFollower.step(mFeet, 1.f / 30.f, finder);
                ++mFrames;
                if (r.mSetVelocity)
                {
                    mVelX = r.mVelX;
                    mVelY = r.mVelY;
                }
                if (r.mOutcome != Outcome::None)
                    mOutcome = r.mOutcome;
                mFeet.x += mVelX / 30.f;
                mFeet.y += mVelY / 30.f;
            }
        }
    };
}

TEST(NeutralPathLong, aPartialPathIsNoWayToTheGoalWithoutTheOption)
{
    ReachFinder finder;
    Walk walk;
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(9000.f, 0.f, 0.f), kWalkSpeed));
    walk.run(finder, 10);
    EXPECT_EQ(walk.mOutcome, Outcome::NoPath);
}

TEST(NeutralPathLong, withTheOptionTheWalkGoesOnInPartsAndArrives)
{
    ReachFinder finder;
    Walk walk;
    walk.mFollower.setLongPaths(true);
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(9000.f, 0.f, 0.f), kWalkSpeed));
    walk.run(finder, 30 * 200);
    EXPECT_EQ(walk.mOutcome, Outcome::Arrived);
    EXPECT_NEAR(walk.mFeet.x, 9000.f, 60.f);
    EXPECT_EQ(finder.mCalls, 4); // 3000, 3000, 3000 and the last short one: one ask for each part
}

TEST(NeutralPathLong, aPartialPathThatGetsNoNearerEndsTheGoal)
{
    ReachFinder finder;
    finder.mReach = 50.f; // less than kLongPathProgress
    Walk walk;
    walk.mFollower.setLongPaths(true);
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(9000.f, 0.f, 0.f), kWalkSpeed));
    walk.run(finder, 200);
    EXPECT_EQ(walk.mOutcome, Outcome::NoPath);
}

TEST(NeutralPathLong, aGoalThatStaysBehindACliffGivesUpAfterTheFirstPart)
{
    // the navmesh ends at 3,000 whatever the asker's place: the second path gets no nearer
    class Cliff final : public PathFinder
    {
    public:
        FindStatus find(const vsg::vec3& from, const vsg::vec3&, std::vector<vsg::vec3>& out) const override
        {
            out = { vsg::vec3(std::min(from.x + 1000.f, 3000.f), 0.f, 0.f) };
            return FindStatus::Partial;
        }
    } finder;
    Walk walk;
    walk.mFollower.setLongPaths(true);
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(9000.f, 0.f, 0.f), kWalkSpeed));
    walk.run(finder, 30 * 300);
    EXPECT_EQ(walk.mOutcome, Outcome::NoPath);
    EXPECT_NEAR(walk.mFeet.x, 3000.f, 60.f);
}

TEST(NeutralPathLong, aSuccessfulPathIsFollowedAsBefore)
{
    ReachFinder finder;
    finder.mReach = 100000.f;
    Walk walk;
    walk.mFollower.setLongPaths(true);
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(1000.f, 0.f, 0.f), kWalkSpeed));
    walk.run(finder, 30 * 40);
    EXPECT_EQ(walk.mOutcome, Outcome::Arrived);
    EXPECT_EQ(finder.mCalls, 1);
}

TEST(NeutralPathLong, theNumberOfPartsIsBounded)
{
    // a navmesh that grows by 250 units a part: 64 parts at most, then the goal ends
    class Slow final : public PathFinder
    {
    public:
        FindStatus find(const vsg::vec3& from, const vsg::vec3&, std::vector<vsg::vec3>& out) const override
        {
            out = { vsg::vec3(from.x + 250.f, 0.f, 0.f) };
            return FindStatus::Partial;
        }
    } finder;
    Walk walk;
    walk.mFollower.setLongPaths(true);
    ASSERT_TRUE(walk.mFollower.setGoal(vsg::vec3(900000.f, 0.f, 0.f), kRunSpeed));
    walk.run(finder, 30 * 600);
    EXPECT_EQ(walk.mOutcome, Outcome::NoPath);
    EXPECT_LE(walk.mFeet.x, 250.f * kMaxPartialRounds + 60.f);
}
