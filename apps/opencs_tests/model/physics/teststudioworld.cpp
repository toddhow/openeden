// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "apps/opencs/model/physics/joltruntime.hpp"
#include "apps/opencs/model/physics/studioworld.hpp"

#include <components/extension/physics_service.hpp>
#include <components/misc/constants.hpp>
#include <openeden/physics.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CSMPhysics
{
    namespace
    {
        constexpr float kUnits = StudioWorld::sUnitsPerMeter;
        constexpr float kBallRadius = 0.3f * kUnits;
        constexpr float kSlabHalfXY = 20.0f * kUnits;
        constexpr float kSlabHalfZ = 0.5f * kUnits;
        constexpr float kGravityMps2 = 8.96f; // Constants::GravityConst, Morrowind's value

        // The slab's top is at z = 0.
        JPH::BodyID addSlab(StudioWorld& world)
        {
            return world.addStaticBox(JPH::RVec3(0, 0, -kSlabHalfZ), JPH::Vec3(kSlabHalfXY, kSlabHalfXY, kSlabHalfZ));
        }

        bool bitIdentical(double a, double b)
        {
            return std::memcmp(&a, &b, sizeof(double)) == 0;
        }

        TEST(CSMPhysicsStudioWorldTest, startsPausedAndEmpty)
        {
            StudioWorld world;
            EXPECT_EQ(world.transport(), StudioWorld::Transport::Paused);
            EXPECT_EQ(world.bodyCount(), 0u);
            EXPECT_EQ(world.stepIndex(), 0u);
        }

        TEST(CSMPhysicsStudioWorldTest, unitsAreMorrowindUnits)
        {
            EXPECT_EQ(StudioWorld::sUnitsPerMeter, Constants::UnitsPerMeter);
            EXPECT_EQ(StudioWorld::sStepSeconds, 1.0f / 60.0f);
            EXPECT_EQ(StudioWorld::sLayerWorld, 1u << 0);
            EXPECT_EQ(StudioWorld::sLayerDynamicWorld, 1u << 8);
        }

        TEST(CSMPhysicsStudioWorldTest, addingAndRemovingBodiesChangesTheCount)
        {
            StudioWorld world;
            const JPH::BodyID box = addSlab(world);
            EXPECT_EQ(world.bodyCount(), 1u);
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 100), 10.0f);
            EXPECT_EQ(world.bodyCount(), 2u);

            world.removeBody(ball);
            EXPECT_EQ(world.bodyCount(), 1u);
            EXPECT_NO_THROW(world.removeBody(ball)); // twice is harmless
            EXPECT_EQ(world.bodyCount(), 1u);
            world.removeBody(box);
            EXPECT_EQ(world.bodyCount(), 0u);
            EXPECT_NO_THROW(world.removeBody(JPH::BodyID())); // unknown ID
            EXPECT_EQ(world.bodyCount(), 0u);
        }

        TEST(CSMPhysicsStudioWorldTest, unknownBodyHasZeroPosition)
        {
            StudioWorld world;
            const JPH::RVec3 p = world.position(JPH::BodyID());
            EXPECT_EQ(p.GetX(), 0.0);
            EXPECT_EQ(p.GetY(), 0.0);
            EXPECT_EQ(p.GetZ(), 0.0);
        }

        TEST(CSMPhysicsStudioWorldTest, invalidArgumentsThrow)
        {
            StudioWorld world;
            EXPECT_THROW(world.addStaticBox(JPH::RVec3(0, 0, 0), JPH::Vec3(0.04f, 1.0f, 1.0f)), std::invalid_argument);
            EXPECT_THROW(world.addStaticBox(JPH::RVec3(0, 0, 0), JPH::Vec3(1.0f, 0.0f, 1.0f)), std::invalid_argument);
            EXPECT_THROW(world.addStaticBox(JPH::RVec3(0, 0, 0), JPH::Vec3(1.0f, 1.0f, -2.0f)), std::invalid_argument);
            EXPECT_THROW(world.addDynamicSphere(JPH::RVec3(0, 0, 0), 0.0f), std::invalid_argument);
            EXPECT_THROW(world.addDynamicSphere(JPH::RVec3(0, 0, 0), -1.0f), std::invalid_argument);
            EXPECT_EQ(world.bodyCount(), 0u);
        }

        // The spec's drop is from 3 m onto a slab, which is reached before 1 s (2.7 m of fall takes about 0.78 s), so
        // the 1 s fall is measured from higher up with nothing below.
        TEST(CSMPhysicsStudioWorldTest, freeFallFollowsMorrowindGravity)
        {
            StudioWorld world;
            const float startZ = 20.0f * kUnits;
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, startZ), kBallRadius);
            for (int i = 0; i < 60; ++i)
                world.stepOnce();

            const double fallen = startZ - world.position(ball).GetZ();
            const double expected = 0.5 * kGravityMps2 * 1.0 * 1.0 * kUnits;
            EXPECT_NEAR(fallen, expected, 0.15 * expected);
        }

        TEST(CSMPhysicsStudioWorldTest, sphereDroppedFromThreeMetersComesToRestOnTheSlab)
        {
            StudioWorld world;
            addSlab(world);
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 3.0 * kUnits), kBallRadius);
            for (int i = 0; i < 240; ++i)
                world.stepOnce();

            EXPECT_NEAR(world.position(ball).GetZ(), kBallRadius, 0.02 * kBallRadius);
        }

        TEST(CSMPhysicsStudioWorldTest, twoWorldsGiveBitIdenticalResults)
        {
            auto run = [](double& x, double& y, double& z)
            {
                StudioWorld world;
                addSlab(world);
                const JPH::BodyID a = world.addDynamicSphere(JPH::RVec3(3, 5, 2.0 * kUnits), kBallRadius);
                world.addDynamicSphere(JPH::RVec3(10, 5, 4.0 * kUnits), kBallRadius * 1.5f);
                for (int i = 0; i < 120; ++i)
                    world.stepOnce();
                const JPH::RVec3 p = world.position(a);
                x = p.GetX();
                y = p.GetY();
                z = p.GetZ();
            };
            double ax = 0, ay = 0, az = 0, bx = 1, by = 1, bz = 1;
            run(ax, ay, az);
            run(bx, by, bz);
            EXPECT_TRUE(bitIdentical(ax, bx));
            EXPECT_TRUE(bitIdentical(ay, by));
            EXPECT_TRUE(bitIdentical(az, bz));
        }

        TEST(CSMPhysicsStudioWorldTest, pausedTickDoesNothing)
        {
            StudioWorld world;
            world.addDynamicSphere(JPH::RVec3(0, 0, 500), 10.0f);
            const std::uint64_t revision = world.revision();
            EXPECT_FALSE(world.tick());
            EXPECT_EQ(world.stepIndex(), 0u);
            EXPECT_EQ(world.revision(), revision);
        }

        TEST(CSMPhysicsStudioWorldTest, playingTickStepsOnceAndPauseStopsIt)
        {
            StudioWorld world;
            world.play();
            EXPECT_EQ(world.transport(), StudioWorld::Transport::Playing);
            EXPECT_TRUE(world.tick());
            EXPECT_EQ(world.stepIndex(), 1u);
            EXPECT_TRUE(world.tick());
            EXPECT_EQ(world.stepIndex(), 2u);

            world.pause();
            EXPECT_EQ(world.transport(), StudioWorld::Transport::Paused);
            EXPECT_FALSE(world.tick());
            EXPECT_EQ(world.stepIndex(), 2u);
        }

        TEST(CSMPhysicsStudioWorldTest, stepOnceStepsWhilePausedAndKeepsTheState)
        {
            StudioWorld world;
            world.stepOnce();
            EXPECT_EQ(world.stepIndex(), 1u);
            EXPECT_EQ(world.transport(), StudioWorld::Transport::Paused);

            world.play();
            world.stepOnce();
            EXPECT_EQ(world.stepIndex(), 2u);
            EXPECT_EQ(world.transport(), StudioWorld::Transport::Playing);
        }

        TEST(CSMPhysicsStudioWorldTest, clearRemovesBodiesAndResetsTheStepIndex)
        {
            StudioWorld world;
            addSlab(world);
            world.addDynamicSphere(JPH::RVec3(0, 0, 500), 10.0f);
            world.stepOnce();
            world.stepOnce();
            ASSERT_EQ(world.stepIndex(), 2u);

            world.clear();
            EXPECT_EQ(world.bodyCount(), 0u);
            EXPECT_EQ(world.stepIndex(), 0u);
        }

        TEST(CSMPhysicsStudioWorldTest, revisionChangesWithAddRemoveAndStep)
        {
            StudioWorld world;
            std::uint64_t last = world.revision();

            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 500), 10.0f);
            EXPECT_GT(world.revision(), last);
            last = world.revision();

            world.stepOnce();
            EXPECT_GT(world.revision(), last);
            last = world.revision();

            world.removeBody(ball);
            EXPECT_GT(world.revision(), last);
        }

        class RecordingObserver final : public StudioWorld::StepObserver
        {
        public:
            explicit RecordingObserver(StudioWorld& world)
                : mWorld(world)
            {
            }

            void beforeStep(float dt) override
            {
                mLog.push_back("before");
                mBeforeDt.push_back(dt);
                mIndexAtBefore.push_back(mWorld.stepIndex());
                if (mThrow)
                    throw std::runtime_error("observer failure");
            }

            void afterStep(float dt) override
            {
                mLog.push_back("after");
                mAfterDt.push_back(dt);
                mIndexAtAfter.push_back(mWorld.stepIndex());
                if (mThrow)
                    throw std::runtime_error("observer failure");
            }

            StudioWorld& mWorld;
            bool mThrow = false;
            std::vector<std::string> mLog;
            std::vector<float> mBeforeDt, mAfterDt;
            std::vector<std::uint64_t> mIndexAtBefore, mIndexAtAfter;
        };

        TEST(CSMPhysicsStudioWorldTest, observerRunsBeforeAndAfterEachStepOnce)
        {
            StudioWorld world;
            RecordingObserver observer(world);
            world.setStepObserver(&observer);

            world.stepOnce();
            world.stepOnce();

            ASSERT_EQ(observer.mLog.size(), 4u);
            EXPECT_EQ(observer.mLog, (std::vector<std::string>{ "before", "after", "before", "after" }));
            for (float dt : observer.mBeforeDt)
                EXPECT_EQ(dt, StudioWorld::sStepSeconds);
            for (float dt : observer.mAfterDt)
                EXPECT_EQ(dt, StudioWorld::sStepSeconds);
            // The step is done between the two calls.
            EXPECT_EQ(observer.mIndexAtBefore[0], 0u);
            EXPECT_EQ(observer.mIndexAtAfter[0], 1u);

            world.setStepObserver(nullptr);
            world.stepOnce();
            EXPECT_EQ(observer.mLog.size(), 4u);
        }

        TEST(CSMPhysicsStudioWorldTest, aThrowingObserverDoesNotStopTheStepOrEscape)
        {
            StudioWorld world;
            RecordingObserver observer(world);
            observer.mThrow = true;
            world.setStepObserver(&observer);
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 500), 10.0f);

            EXPECT_NO_THROW(world.stepOnce());
            EXPECT_EQ(world.stepIndex(), 1u);
            EXPECT_EQ(observer.mLog.size(), 2u); // afterStep still ran
            EXPECT_LT(world.position(ball).GetZ(), 500.0);
            world.setStepObserver(nullptr);
        }

        TEST(CSMPhysicsStudioWorldTest, layersCollideAndRaysSeeThemThroughTheQueryService)
        {
            StudioWorld world;
            addSlab(world);
            const JPH::BodyID ball = world.addDynamicSphere(JPH::RVec3(0, 0, 3.0 * kUnits), kBallRadius);
            for (int i = 0; i < 240; ++i)
                world.stepOnce();
            // Collided with the static layer: it rests on the slab instead of falling through.
            ASSERT_NEAR(world.position(ball).GetZ(), kBallRadius, 0.02 * kBallRadius);

            openeden::extension::PhysicsServiceImpl service(world.system(), StudioWorld::sUnitsPerMeter);
            service.publishQueryMask("world", StudioWorld::sLayerWorld);
            service.publishQueryMask("dynamic_world", StudioWorld::sLayerDynamicWorld);
            EXPECT_EQ(service.queryMask("world"), StudioWorld::sLayerWorld);
            EXPECT_EQ(service.queryMask("dynamic_world"), StudioWorld::sLayerDynamicWorld);

            // Metres: from 10 m above, straight down through the ball's axis.
            const openeden::sdk::PhysicsVec3 origin{ 0.0f, 0.0f, 10.0f };
            const openeden::sdk::PhysicsVec3 direction{ 0.0f, 0.0f, -20.0f };

            const auto slabHit = service.castRay(origin, direction, {}, service.queryMask("world"));
            ASSERT_TRUE(slabHit.has_value());
            EXPECT_NEAR(slabHit->pointM.z, 0.0f, 0.05f);

            const auto ballHit = service.castRay(origin, direction, {}, service.queryMask("dynamic_world"));
            ASSERT_TRUE(ballHit.has_value());
            EXPECT_NEAR(ballHit->pointM.z, 0.6f, 0.05f); // top of a 0.3 m sphere resting on z = 0
        }
    }
}
