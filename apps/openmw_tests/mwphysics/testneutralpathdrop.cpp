// Task 0332: a neutral actor that climbed onto a raised rim (the hearth of the Windpeak Inn: its edge stood 42 units
// above the floor) is allowed to step down as far as its own path goes down, not only the mover's 24 units, or it
// stands there for good ("blocked edge"). Written before the change; the first run was against a stub that returned
// the base limit.
#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "apps/openmw/mwphysics/neutralmover.hpp"
#include "apps/openmw/mwphysics/neutralpathfollower.hpp"

namespace
{
    using namespace MWPhysics::NeutralPath;

    TEST(NeutralPathDrop, a_waypoint_far_below_allows_a_step_down_that_reaches_it)
    {
        const std::vector<vsg::vec3> path = { { 1415.f, -250.f, 44.7f }, { 1164.f, 14.f, 2.8f } };
        const float down = pathStepDown(41.7f, path, 0, MWPhysics::NeutralMover::kStepDown);
        EXPECT_GE(down, 41.7f - 2.8f);
        EXPECT_LE(down, kMaxPathDrop);
    }

    TEST(NeutralPathDrop, a_level_path_keeps_the_base_limit)
    {
        const std::vector<vsg::vec3> path = { { 0.f, 0.f, 0.f }, { 300.f, 0.f, 3.f } };
        EXPECT_FLOAT_EQ(
            pathStepDown(0.f, path, 0, MWPhysics::NeutralMover::kStepDown), MWPhysics::NeutralMover::kStepDown);
    }

    TEST(NeutralPathDrop, a_drop_within_the_base_limit_keeps_it)
    {
        const std::vector<vsg::vec3> path = { { 0.f, 0.f, 20.f }, { 100.f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(
            pathStepDown(20.f, path, 0, MWPhysics::NeutralMover::kStepDown), MWPhysics::NeutralMover::kStepDown);
    }

    TEST(NeutralPathDrop, a_cliff_is_capped)
    {
        const std::vector<vsg::vec3> path = { { 0.f, 0.f, 400.f }, { 100.f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(pathStepDown(400.f, path, 0, MWPhysics::NeutralMover::kStepDown), kMaxPathDrop);
    }

    TEST(NeutralPathDrop, only_the_next_two_waypoints_count)
    {
        // a flight of stairs far ahead must not make every ledge on the way a safe drop
        const std::vector<vsg::vec3> path
            = { { 0.f, 0.f, 40.f }, { 100.f, 0.f, 40.f }, { 200.f, 0.f, 40.f }, { 300.f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(
            pathStepDown(40.f, path, 0, MWPhysics::NeutralMover::kStepDown), MWPhysics::NeutralMover::kStepDown);
        EXPECT_GT(pathStepDown(40.f, path, 2, MWPhysics::NeutralMover::kStepDown), MWPhysics::NeutralMover::kStepDown);
    }

    TEST(NeutralPathDrop, no_path_or_a_finished_one_keeps_the_base_limit)
    {
        const std::vector<vsg::vec3> none;
        EXPECT_FLOAT_EQ(pathStepDown(40.f, none, 0, 24.f), 24.f);
        const std::vector<vsg::vec3> path = { { 0.f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(pathStepDown(40.f, path, 5, 24.f), 24.f);
    }

    TEST(NeutralPathDrop, a_value_that_is_not_a_number_keeps_the_base_limit)
    {
        const std::vector<vsg::vec3> path = { { 0.f, 0.f, std::numeric_limits<float>::quiet_NaN() } };
        EXPECT_FLOAT_EQ(pathStepDown(40.f, path, 0, 24.f), 24.f);
        const std::vector<vsg::vec3> ok = { { 0.f, 0.f, 0.f } };
        EXPECT_FLOAT_EQ(pathStepDown(std::numeric_limits<float>::infinity(), ok, 0, 24.f), 24.f);
    }
}
