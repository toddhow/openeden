// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/physics/debuglinedrawer.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

namespace CSMPhysics
{
    namespace
    {
        constexpr float kUnits = StudioWorld::sUnitsPerMeter;
        constexpr float kSlack = 1.0f; // one unit, per the spec

        TEST(CSMPhysicsDebugLineDrawerTest, anEmptyWorldHasNoLines)
        {
            StudioWorld world;
            DebugLineDrawer drawer;
            drawer.rebuild(world.system());
            EXPECT_TRUE(drawer.lines().empty());
        }

        TEST(CSMPhysicsDebugLineDrawerTest, aBoxAndASphereProduceLinesInsideTheirBounds)
        {
            StudioWorld world;
            const JPH::RVec3 boxCentre(1000, 200, -50);
            const JPH::Vec3 half(300.0f, 200.0f, 50.0f);
            const JPH::RVec3 ballCentre(-500, 100, 700);
            const float radius = 0.3f * kUnits;
            world.addStaticBox(boxCentre, half);
            world.addDynamicSphere(ballCentre, radius);

            DebugLineDrawer drawer;
            drawer.rebuild(world.system());
            ASSERT_FALSE(drawer.lines().empty());

            auto inBox = [](const float p[3], const JPH::RVec3& c, const JPH::Vec3& h)
            {
                return std::abs(p[0] - c.GetX()) <= h.GetX() + kSlack && std::abs(p[1] - c.GetY()) <= h.GetY() + kSlack
                    && std::abs(p[2] - c.GetZ()) <= h.GetZ() + kSlack;
            };
            const JPH::Vec3 ballHalf(radius, radius, radius);
            for (const DebugLine& line : drawer.lines())
            {
                EXPECT_TRUE(inBox(line.mFrom, boxCentre, half) || inBox(line.mFrom, ballCentre, ballHalf));
                EXPECT_TRUE(inBox(line.mTo, boxCentre, half) || inBox(line.mTo, ballCentre, ballHalf));
            }
        }

        TEST(CSMPhysicsDebugLineDrawerTest, rebuildReplacesTheListInsteadOfAppending)
        {
            StudioWorld world;
            world.addStaticBox(JPH::RVec3(0, 0, 0), JPH::Vec3(100.0f, 100.0f, 100.0f));
            DebugLineDrawer drawer;
            drawer.rebuild(world.system());
            const std::size_t first = drawer.lines().size();
            ASSERT_GT(first, 0u);
            drawer.rebuild(world.system());
            EXPECT_EQ(drawer.lines().size(), first);

            world.clear();
            drawer.rebuild(world.system());
            EXPECT_TRUE(drawer.lines().empty());
        }
    }
}
