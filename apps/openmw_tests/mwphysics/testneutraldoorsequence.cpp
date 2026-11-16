// Task 0232: the rules around a door that swings by its model's own sequence (apps/openmw/mwphysics/neutraldoorswing.hpp).
#include "apps/openmw/mwphysics/neutraldoorswing.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace MWPhysics::NeutralDoor;
}

TEST(NeutralDoorSequence, the_collision_opens_at_half_way_and_not_before)
{
    EXPECT_FALSE(collisionOpen(0.f));
    EXPECT_FALSE(collisionOpen(0.49f));
    EXPECT_TRUE(collisionOpen(0.5f));
    EXPECT_TRUE(collisionOpen(1.f));
}

TEST(NeutralDoorSequence, a_door_opened_by_walkers_passes_the_collision_threshold_once_each_way)
{
    State state;
    int flips = 0;
    bool open = false;
    for (int i = 0; i < 200; ++i)
    {
        state = step(state, i < 40, 0.05f);
        const bool now = collisionOpen(state.mProgress);
        flips += now != open ? 1 : 0;
        open = now;
    }
    EXPECT_EQ(flips, 2);
    EXPECT_FALSE(open);
}

TEST(NeutralDoorSequence, a_poke_counts_down_to_zero_and_stays_there)
{
    float left = kPokeSeconds;
    left = stepPoke(left, 2.f);
    EXPECT_NEAR(left, kPokeSeconds - 2.f, 1e-6);
    left = stepPoke(left, 100.f);
    EXPECT_FLOAT_EQ(left, 0.f);
    EXPECT_FLOAT_EQ(stepPoke(0.f, 1.f), 0.f);
}

TEST(NeutralDoorSequence, a_bad_dt_leaves_a_poke_alone)
{
    EXPECT_FLOAT_EQ(stepPoke(3.f, -1.f), 3.f);
    EXPECT_FLOAT_EQ(stepPoke(3.f, std::nanf("")), 3.f);
    EXPECT_FLOAT_EQ(stepPoke(-2.f, 1.f), 0.f);
}

TEST(NeutralDoorSequence, a_lid_closes_when_the_window_it_was_seen_in_goes)
{
    EXPECT_FALSE(lidShouldClose(true, true, 30.f));
    EXPECT_TRUE(lidShouldClose(true, false, 0.1f));
}

TEST(NeutralDoorSequence, a_lid_whose_window_never_showed_closes_after_the_grace_time)
{
    EXPECT_FALSE(lidShouldClose(false, false, 0.f));
    EXPECT_FALSE(lidShouldClose(false, false, kLidGraceSeconds - 0.1f));
    EXPECT_TRUE(lidShouldClose(false, false, kLidGraceSeconds));
    EXPECT_FALSE(lidShouldClose(false, true, 0.1f));
}
