// Task 0138 (milestone M-S2, "leave and enter an interior from the street"): doors whose destination is an exterior.
// The pure parts: the grid square an arrival stands in, and the door decision chained with a lock and an exterior
// destination, both ways. Written from the task spec before the implementation; the first run was against a stub.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <components/misc/convert.hpp>

#include "apps/openmw/mwworld/neutraldoor.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralDestination;
    using MWWorld::NeutralDoorOutcome;

    constexpr double kSkyrimCell = 4096.0;
    constexpr double kMorrowindCell = 8192.0;

    WorldObject door(WorldId id, WorldId destinationCell, Vec3d at, bool locked = false)
    {
        WorldObject o;
        o.id = id;
        o.base = 500;
        o.kind = ObjectKind::Door;
        TeleportComponent t;
        t.destinationCell = destinationCell;
        t.destinationDoor = id + 1;
        t.destination.position = at;
        t.destination.rotation = Quatd{ 0.0, 0.0, std::sin(0.4), std::cos(0.4) };
        o.teleport = t;
        if (locked)
        {
            LockComponent l;
            l.locked = true;
            o.lock = l;
        }
        return o;
    }

    // ---- the grid square of an arrival ----------------------------------------------------------------------------

    TEST(NeutralExteriorSquare, a_position_inside_a_square_gives_that_square)
    {
        const auto s = MWWorld::neutralExteriorSquare(100.0, 200.0, kSkyrimCell);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->first, 0);
        EXPECT_EQ(s->second, 0);
        const auto t = MWWorld::neutralExteriorSquare(6 * kSkyrimCell + 17.0, -2 * kSkyrimCell + 4000.0, kSkyrimCell);
        ASSERT_TRUE(t.has_value());
        EXPECT_EQ(t->first, 6);
        EXPECT_EQ(t->second, -2);
    }

    TEST(NeutralExteriorSquare, negative_coordinates_round_down_not_towards_zero)
    {
        const auto s = MWWorld::neutralExteriorSquare(-0.5, -4095.0, kSkyrimCell);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->first, -1);
        EXPECT_EQ(s->second, -1);
        const auto t = MWWorld::neutralExteriorSquare(-4097.0, -8192.5, kSkyrimCell);
        ASSERT_TRUE(t.has_value());
        EXPECT_EQ(t->first, -2);
        EXPECT_EQ(t->second, -3);
    }

    TEST(NeutralExteriorSquare, a_boundary_belongs_to_the_square_above_it)
    {
        const auto s = MWWorld::neutralExteriorSquare(kSkyrimCell, -kSkyrimCell, kSkyrimCell);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->first, 1);
        EXPECT_EQ(s->second, -1);
    }

    TEST(NeutralExteriorSquare, the_cell_size_is_the_worldspaces_own)
    {
        const auto s = MWWorld::neutralExteriorSquare(5000.0, 5000.0, kMorrowindCell);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->first, 0);
        EXPECT_EQ(s->second, 0);
        const auto t = MWWorld::neutralExteriorSquare(5000.0, 5000.0, kSkyrimCell);
        ASSERT_TRUE(t.has_value());
        EXPECT_EQ(t->first, 1);
        EXPECT_EQ(t->second, 1);
    }

    TEST(NeutralExteriorSquare, a_value_that_is_not_a_place_gives_no_square)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(nan, 0.0, kSkyrimCell).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, inf, kSkyrimCell).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, 0.0, 0.0).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, 0.0, -4096.0).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, 0.0, nan).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(1e15, 0.0, kSkyrimCell).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, -1e15, kSkyrimCell).has_value());
    }

    TEST(NeutralExteriorSquare, the_largest_square_that_fits_is_still_given)
    {
        const auto s = MWWorld::neutralExteriorSquare(999999.0 * kSkyrimCell, -999999.0 * kSkyrimCell, kSkyrimCell);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->first, 999999);
        EXPECT_EQ(s->second, -999999);
    }

    TEST(NeutralExteriorSquare, one_square_past_the_limit_gives_no_square)
    {
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(1000001.0 * kSkyrimCell, 0.0, kSkyrimCell).has_value());
        EXPECT_FALSE(MWWorld::neutralExteriorSquare(0.0, -1000001.0 * kSkyrimCell, kSkyrimCell).has_value());
    }

    // ---- an exterior door, both ways, with a lock -----------------------------------------------------------------

    TEST(NeutralExteriorDoorBothWays, leaving_an_interior_arrives_at_the_xtel_position_and_rotation_in_the_street)
    {
        const WorldObject front = door(7, 30, { 24000.5, -7000.25, 66.0 });
        int asked = 0;
        const auto d = MWWorld::decideNeutralDoor(front, true, false, [&](WorldId c) {
            ++asked;
            EXPECT_EQ(c, 30u);
            return NeutralDestination::ExteriorWorld;
        });
        EXPECT_EQ(asked, 1);
        ASSERT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 30u);
        EXPECT_FLOAT_EQ(d.mArrival.pos[0], 24000.5f);
        EXPECT_FLOAT_EQ(d.mArrival.pos[1], -7000.25f);
        EXPECT_FLOAT_EQ(d.mArrival.pos[2], 66.0f);
        const vsg::dquat q = Misc::Convert::makeOsgQuat(d.mArrival);
        EXPECT_NEAR(q.z, std::sin(0.4), 1e-6);
        EXPECT_NEAR(q.w, std::cos(0.4), 1e-6);
        const auto square = MWWorld::neutralExteriorSquare(d.mArrival.pos[0], d.mArrival.pos[1], kSkyrimCell);
        ASSERT_TRUE(square.has_value());
        EXPECT_EQ(square->first, 5);
        EXPECT_EQ(square->second, -2);
    }

    TEST(NeutralExteriorDoorBothWays, entering_from_the_street_is_the_ordinary_interior_teleport)
    {
        const WorldObject back = door(8, 10, { 50.0, 60.0, 70.0 });
        const auto d
            = MWWorld::decideNeutralDoor(back, true, false, [](WorldId) { return NeutralDestination::Interior; });
        ASSERT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 10u);
    }

    TEST(NeutralExteriorDoorBothWays, a_locked_door_to_the_street_is_refused_before_the_exterior_is_looked_up)
    {
        const WorldObject front = door(7, 30, { 1.0, 2.0, 3.0 }, true);
        int asked = 0;
        const auto d = MWWorld::decideNeutralDoor(front, true, false, [&](WorldId) {
            ++asked;
            return NeutralDestination::ExteriorWorld;
        });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Locked);
        EXPECT_EQ(asked, 0);
    }

    TEST(NeutralExteriorDoorBothWays, an_opened_lock_lets_a_door_to_the_street_through)
    {
        const WorldObject front = door(7, 30, { 1.0, 2.0, 3.0 }, true);
        const auto d = MWWorld::decideNeutralDoor(
            front, true, true, [](WorldId) { return NeutralDestination::ExteriorWorld; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
    }

    TEST(NeutralExteriorDoorBothWays, a_gate_to_another_world_is_a_teleport_like_any_exterior)
    {
        const WorldObject gate = door(9, 77, { -3000.0, 40000.0, 1200.0 });
        const auto d = MWWorld::decideNeutralDoor(
            gate, true, false, [](WorldId) { return NeutralDestination::ExteriorWorld; });
        ASSERT_EQ(d.mOutcome, NeutralDoorOutcome::Teleport);
        EXPECT_EQ(d.mDestinationCell, 77u);
    }

    TEST(NeutralExteriorDoorBothWays, an_exterior_of_a_world_nobody_describes_still_says_outside)
    {
        const WorldObject front = door(7, 30, { 1.0, 2.0, 3.0 });
        const auto d
            = MWWorld::decideNeutralDoor(front, true, false, [](WorldId) { return NeutralDestination::Exterior; });
        EXPECT_EQ(d.mOutcome, NeutralDoorOutcome::Exterior);
        EXPECT_EQ(MWWorld::neutralDoorMessageKey(d.mOutcome), "NeutralDoorOutside");
    }
}
