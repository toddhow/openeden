// Task 0157: which neutral doors neutral actors walk through (the navigator's off-mesh connection and the swing).
// Written from the header before the implementation; the first run was against a stub that answered false.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutraldoor.hpp"
#include "apps/openmw/mwworld/neutralnavigation.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;

    WorldObject plainDoor()
    {
        return WorldObject{};
    }
}

TEST(NeutralDoorNav, a_plain_door_opens_for_actors)
{
    EXPECT_TRUE(MWWorld::neutralDoorOpensForActors(plainDoor(), false));
}

TEST(NeutralDoorNav, a_teleport_door_never_does)
{
    WorldObject door = plainDoor();
    door.teleport = TeleportComponent{};
    EXPECT_FALSE(MWWorld::neutralDoorOpensForActors(door, false));
    EXPECT_FALSE(MWWorld::neutralDoorOpensForActors(door, true));
}

TEST(NeutralDoorNav, a_locked_door_stays_a_wall_unless_the_key_was_used)
{
    WorldObject door = plainDoor();
    LockComponent lock;
    lock.locked = true;
    lock.level = 25;
    lock.key = 1234;
    door.lock = lock;
    EXPECT_FALSE(MWWorld::neutralDoorOpensForActors(door, false));
    EXPECT_TRUE(MWWorld::neutralDoorOpensForActors(door, true));
}

TEST(NeutralDoorNav, a_lock_component_that_is_not_locked_does_not_matter)
{
    WorldObject door = plainDoor();
    door.lock = LockComponent{};
    EXPECT_TRUE(MWWorld::neutralDoorOpensForActors(door, false));
}

namespace
{
    std::optional<float> floorOf(float doorZ, std::initializer_list<std::optional<float>> hits)
    {
        const std::vector<std::optional<float>> v(hits);
        return MWWorld::chooseNeutralDoorFloor(doorZ, v);
    }
}

TEST(NeutralDoorFloor, the_hit_nearest_to_the_doors_height_wins)
{
    EXPECT_EQ(floorOf(320.f, { 319.f, 322.f, 340.f }), 319.f);
    EXPECT_EQ(floorOf(320.f, { 330.f, 321.f }), 321.f);
}

TEST(NeutralDoorFloor, a_ray_that_fell_through_a_crack_is_ignored)
{
    // the Mare's seam: the centre ray reaches the ground floor 100 units down, its neighbours the real floor
    EXPECT_EQ(floorOf(320.f, { 216.f, 319.f, 319.f, 319.f, 319.f }), 319.f);
    EXPECT_EQ(floorOf(320.f, { 216.f, std::nullopt, 319.f }), 319.f);
}

TEST(NeutralDoorFloor, nothing_inside_the_band_gives_nothing)
{
    EXPECT_FALSE(floorOf(320.f, { 216.f, 68.f, std::nullopt }).has_value());
    EXPECT_FALSE(floorOf(320.f, {}).has_value());
    EXPECT_FALSE(floorOf(320.f, { 320.f + MWWorld::kNeutralDoorFloorBand + 1.f }).has_value());
    EXPECT_TRUE(floorOf(320.f, { 320.f + MWWorld::kNeutralDoorFloorBand }).has_value());
}

TEST(NeutralDoorFloor, a_hit_that_is_not_a_number_is_ignored)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(floorOf(320.f, { nan, 318.f }), 318.f);
    EXPECT_FALSE(floorOf(nan, { 318.f }).has_value());
}
