// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
// clang-format on

#include "apps/opencs/model/physics/joltruntime.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace CSMPhysics
{
    namespace
    {
        TEST(CSMPhysicsJoltRuntimeTest, firstObjectStartsJoltAndLastShutsItDown)
        {
            ASSERT_EQ(JPH::Factory::sInstance, nullptr);
            {
                JoltRuntime runtime;
                EXPECT_NE(JPH::Factory::sInstance, nullptr);
            }
            EXPECT_EQ(JPH::Factory::sInstance, nullptr);
        }

        TEST(CSMPhysicsJoltRuntimeTest, twoObjectsMayExistAndJoltSurvivesTheFirstOnesDestruction)
        {
            auto first = std::make_unique<JoltRuntime>();
            auto second = std::make_unique<JoltRuntime>();
            EXPECT_NE(JPH::Factory::sInstance, nullptr);

            first.reset();
            EXPECT_NE(JPH::Factory::sInstance, nullptr);
            {
                StudioWorld world; // steps with the second runtime alone keeping Jolt alive
                world.addDynamicSphere(JPH::RVec3(0, 0, 100), 10.0f);
                EXPECT_NO_THROW(world.stepOnce());
            }

            second.reset();
            EXPECT_EQ(JPH::Factory::sInstance, nullptr);
        }

        TEST(CSMPhysicsJoltRuntimeTest, aNewRuntimeStartsJoltAgainAfterBothAreGone)
        {
            {
                JoltRuntime a;
                JoltRuntime b;
            }
            ASSERT_EQ(JPH::Factory::sInstance, nullptr);

            StudioWorld world;
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 500), 20.0f);
            const double before = world.position(ball).GetZ();
            for (int i = 0; i < 10; ++i)
                world.stepOnce();
            EXPECT_LT(world.position(ball).GetZ(), before);
        }
    }
}
