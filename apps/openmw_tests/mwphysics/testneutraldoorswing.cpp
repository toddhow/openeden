// Task 0157: the opening of a neutral door for a walking actor (an open door sinks into the floor, see the header).
// Written before the implementation; the first run was against a stub.
#include "apps/openmw/mwphysics/neutraldoorswing.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace MWPhysics::NeutralDoor;
}

TEST(NeutralDoorSwing, a_walker_is_near_inside_the_radius_and_height)
{
    EXPECT_TRUE(isApproached(0, 0, 100, 100, 0, 68));
    EXPECT_TRUE(isApproached(0, 0, 100, 0, 150, 100));
    EXPECT_FALSE(isApproached(0, 0, 100, 161, 0, 100));
    EXPECT_FALSE(isApproached(0, 0, 100, 0, 0, 100 + kTriggerHeight + 1.f));
    EXPECT_FALSE(isApproached(0, 0, 100, 0, 0, 100 - kTriggerHeight - 1.f)); // another floor
    EXPECT_FALSE(isApproached(0, 0, 100, std::nanf(""), 0, 100));
}

TEST(NeutralDoorSwing, a_walker_opens_the_door_in_one_second)
{
    State s;
    s = step(s, true, 0.5f);
    EXPECT_NEAR(s.mProgress, 0.5f, 1e-5f);
    s = step(s, true, 0.5f);
    EXPECT_NEAR(s.mProgress, 1.f, 1e-5f);
    s = step(s, true, 5.f); // and it does not go past open
    EXPECT_NEAR(s.mProgress, 1.f, 1e-5f);
}

TEST(NeutralDoorSwing, the_door_is_held_then_closes)
{
    State s;
    s = step(s, true, 2.f);
    ASSERT_NEAR(s.mProgress, 1.f, 1e-5f);
    s = step(s, false, 1.5f); // within the hold
    EXPECT_NEAR(s.mProgress, 1.f, 1e-5f);
    s = step(s, false, 1.0f); // hold used up (2.0), 0.5 s of closing
    EXPECT_NEAR(s.mProgress, 0.5f, 1e-5f);
    s = step(s, false, 1.0f);
    EXPECT_NEAR(s.mProgress, 0.f, 1e-5f);
}

TEST(NeutralDoorSwing, a_walker_returning_during_the_close_reopens_it)
{
    State s;
    s = step(s, true, 1.f);
    s = step(s, false, 2.5f); // 0.5 closing
    ASSERT_NEAR(s.mProgress, 0.5f, 1e-5f);
    s = step(s, true, 0.25f);
    EXPECT_NEAR(s.mProgress, 0.75f, 1e-5f);
}

TEST(NeutralDoorSwing, a_closed_door_with_nobody_near_stays_closed)
{
    const State s = step(State{}, false, 1.f);
    EXPECT_EQ(s.mProgress, 0.f);
    EXPECT_EQ(s.mHold, 0.f);
}

TEST(NeutralDoorSwing, the_offset_follows_the_progress)
{
    EXPECT_EQ(sinkOffset(State{}), 0.f);
    EXPECT_NEAR(sinkOffset(State{ 1.f, 0.f }), kSinkDepth, 1e-4f);
    EXPECT_NEAR(sinkOffset(State{ 0.5f, 0.f }), kSinkDepth * 0.5f, 1e-4f);
}

TEST(NeutralDoorSwing, a_bad_frame_time_changes_nothing)
{
    const State s{ 0.5f, 1.f };
    EXPECT_EQ(step(s, true, -1.f).mProgress, 0.5f);
    EXPECT_EQ(step(s, true, 0.f).mProgress, 0.5f);
    EXPECT_EQ(step(s, true, std::nanf("")).mProgress, 0.5f);
}
