// Tests for the ragdoll at game scale: sleeping, resting depth, determinism and allocation. Written from the spec
// alone, before the implementation exists.
//
// In plain words: Jolt's tolerances (how slow counts as "at rest", how much sinking is forgiven) are written in metres,
// but the game measures in units, about 70 per metre. Before the fix a limp ragdoll never went to sleep and sank 2 to
// 11 cm into the floor. RagdollHost now scales the system's tolerances while it lives and puts the old ones back when
// it goes. These tests drop a ragdoll 5 cm above the floor and check that: the settings are scaled while the host
// lives and restored after; every body is asleep by 8 seconds; nothing rests more than 1 cm inside the floor; two runs
// give bit-identical results; and the per-step calls (beginStep, endStep, status) allocate nothing.

#include "ragdolltestworld.hpp"

#include "apps/openmw_tests/support/allocationcounter.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/PhysicsSettings.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;

    constexpr double kUnits = Constants::UnitsPerMeter;
    constexpr int kEightSecondsOfSteps = 480;
    constexpr int kTenSecondsOfSteps = 600;

    std::unique_ptr<MWPhysics::RagdollHost> makeHost(GameLikeWorld& world)
    {
        std::string error;
        std::unique_ptr<MWPhysics::RagdollHost> host
            = MWPhysics::RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        return host;
    }

    void stepMany(GameLikeWorld& world, MWPhysics::RagdollHost& host, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            host.beginStep();
            world.update();
            host.endStep();
        }
    }

    // Feet 0.05 m above the floor, as in testragdollscale.cpp. Returns the ragdoll's bodies (empty on failure).
    std::vector<JPH::BodyID> spawnLimpRagdoll(GameLikeWorld& world, MWPhysics::RagdollHost& host)
    {
        const std::vector<JPH::BodyID> before = world.allBodies();
        std::string error;
        EXPECT_TRUE(host.spawn(JPH::RVec3(0.0, 0.0, 0.05 * kUnits), 0.0f, &error)) << error;
        return world.bodiesAddedSince(before);
    }

    double lowestPointOfBody(GameLikeWorld& world, JPH::BodyID id)
    {
        JPH::BodyLockRead lock(world.system().GetBodyLockInterface(), id);
        EXPECT_TRUE(lock.Succeeded());
        if (!lock.Succeeded())
            return 1.0e30;
        return lock.GetBody().GetWorldSpaceBounds().mMin.GetZ();
    }

    // Everything the asserts at the end of a settle need, checked at the current moment.
    void expectSettled(GameLikeWorld& world, MWPhysics::RagdollHost& host, const std::vector<JPH::BodyID>& bodies)
    {
        const MWPhysics::RagdollStatus status = host.status();
        ASSERT_TRUE(status.present);
        EXPECT_TRUE(status.allAsleep);
        EXPECT_EQ(world.system().GetNumActiveBodies(JPH::EBodyType::RigidBody), 0u);

        for (const JPH::BodyID id : bodies)
        {
            // At rest within 1 cm of the floor: the lowest point of the body's box is no deeper than 1 cm inside it.
            EXPECT_GE(lowestPointOfBody(world, id), -0.01 * kUnits);
            // Nothing is flung into the air.
            EXPECT_LE(world.bodies().GetCenterOfMassPosition(id).GetZ(), 2.5 * kUnits);
        }
    }

    TEST(RagdollSleep, systemSettingsAreScaledWhileTheHostLivesAndRestoredAfter)
    {
        GameLikeWorld world;
        const JPH::PhysicsSettings defaults;
        // GameLikeWorld leaves Jolt's defaults in place.
        ASSERT_FLOAT_EQ(
            world.system().GetPhysicsSettings().mPointVelocitySleepThreshold, defaults.mPointVelocitySleepThreshold);

        {
            auto host = makeHost(world);
            ASSERT_NE(host, nullptr);
            const JPH::PhysicsSettings scaled = world.system().GetPhysicsSettings();
            EXPECT_NEAR(scaled.mPointVelocitySleepThreshold, 0.03 * kUnits, 1e-4);
            EXPECT_EQ(scaled.mPenetrationSlop, 0.02f); // not scaled: how deep bodies may rest in each other
        }

        const JPH::PhysicsSettings restored = world.system().GetPhysicsSettings();
        EXPECT_FLOAT_EQ(restored.mPointVelocitySleepThreshold, defaults.mPointVelocitySleepThreshold);
        EXPECT_FLOAT_EQ(restored.mPenetrationSlop, defaults.mPenetrationSlop);
        EXPECT_FLOAT_EQ(restored.mSpeculativeContactDistance, defaults.mSpeculativeContactDistance);
        EXPECT_FLOAT_EQ(restored.mManifoldTolerance, defaults.mManifoldTolerance);
        EXPECT_FLOAT_EQ(restored.mMaxPenetrationDistance, defaults.mMaxPenetrationDistance);
        EXPECT_FLOAT_EQ(restored.mMinVelocityForRestitution, defaults.mMinVelocityForRestitution);
        EXPECT_FLOAT_EQ(restored.mBodyPairCacheMaxDeltaPositionSq, defaults.mBodyPairCacheMaxDeltaPositionSq);
        EXPECT_FLOAT_EQ(restored.mContactPointPreserveLambdaMaxDistSq, defaults.mContactPointPreserveLambdaMaxDistSq);
        EXPECT_FLOAT_EQ(restored.mTimeBeforeSleep, defaults.mTimeBeforeSleep);
    }

    TEST(RagdollSleep, aLimpRagdollIsAsleepAndOnTheFloorAfterEightSeconds)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<JPH::BodyID> bodies = spawnLimpRagdoll(world, *host);
        ASSERT_FALSE(bodies.empty());

        stepMany(world, *host, kEightSecondsOfSteps);
        expectSettled(world, *host, bodies);
    }

    TEST(RagdollSleep, itStaysSettledAtTenSeconds)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<JPH::BodyID> bodies = spawnLimpRagdoll(world, *host);
        ASSERT_FALSE(bodies.empty());

        stepMany(world, *host, kTenSecondsOfSteps);
        expectSettled(world, *host, bodies);
    }

    struct BodyPose
    {
        double x, y, z;
        float qx, qy, qz, qw;
    };

    std::vector<BodyPose> runAndRecord()
    {
        std::vector<BodyPose> poses;
        GameLikeWorld world;
        auto host = makeHost(world);
        if (host == nullptr)
            return poses;
        const std::vector<JPH::BodyID> bodies = spawnLimpRagdoll(world, *host);
        stepMany(world, *host, kEightSecondsOfSteps);
        for (const JPH::BodyID id : bodies)
        {
            const JPH::RVec3 position = world.bodies().GetCenterOfMassPosition(id);
            const JPH::Quat rotation = world.bodies().GetRotation(id);
            poses.push_back(BodyPose{ double(position.GetX()), double(position.GetY()), double(position.GetZ()),
                rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() });
        }
        return poses;
    }

    TEST(RagdollSleep, twoSeparateRunsGiveBitIdenticalPositionsAndRotations)
    {
        const std::vector<BodyPose> first = runAndRecord();
        const std::vector<BodyPose> second = runAndRecord();
        ASSERT_FALSE(first.empty());
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            // Exact comparison on purpose: the same inputs must give the same bits.
            EXPECT_EQ(first[i].x, second[i].x) << i;
            EXPECT_EQ(first[i].y, second[i].y) << i;
            EXPECT_EQ(first[i].z, second[i].z) << i;
            EXPECT_EQ(first[i].qx, second[i].qx) << i;
            EXPECT_EQ(first[i].qy, second[i].qy) << i;
            EXPECT_EQ(first[i].qz, second[i].qz) << i;
            EXPECT_EQ(first[i].qw, second[i].qw) << i;
        }
    }

    TEST(RagdollSleep, perStepCallsDoNotAllocate)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        host->setProgressLogging(false);
        const std::vector<JPH::BodyID> bodies = spawnLimpRagdoll(world, *host);
        ASSERT_FALSE(bodies.empty());
        // Warm up: let anything that allocates on first use do so before counting.
        stepMany(world, *host, 30);

        constexpr int kCalls = 100;
        std::size_t beginAllocations = 0;
        std::size_t endAllocations = 0;
        std::size_t statusAllocations = 0;
        bool statusPresent = false;

        for (int i = 0; i < kCalls; ++i)
        {
            {
                TestSupport::AllocationCounter counter;
                host->beginStep();
                beginAllocations += counter.count();
            }
            world.update(); // not counted: Jolt's own Update is not under test
            {
                TestSupport::AllocationCounter counter;
                host->endStep();
                endAllocations += counter.count();
            }
        }
        for (int i = 0; i < kCalls; ++i)
        {
            TestSupport::AllocationCounter counter;
            const MWPhysics::RagdollStatus status = host->status();
            statusAllocations += counter.count();
            statusPresent = status.present;
        }

        EXPECT_EQ(beginAllocations, 0u);
        EXPECT_EQ(endAllocations, 0u);
        EXPECT_EQ(statusAllocations, 0u);
        EXPECT_TRUE(statusPresent);
    }

    TEST(RagdollSleep, spawnAndRemoveLeaveTheBodyAndConstraintCountsAsBefore)
    {
        GameLikeWorld world;
        const std::size_t baseBodies = world.numBodies();
        const std::size_t baseConstraints = world.numConstraints();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);

        const std::vector<JPH::BodyID> bodies = spawnLimpRagdoll(world, *host);
        ASSERT_FALSE(bodies.empty());
        EXPECT_GT(world.numBodies(), baseBodies);
        EXPECT_GT(world.numConstraints(), baseConstraints);
        stepMany(world, *host, 20);

        host->remove();
        EXPECT_EQ(world.numBodies(), baseBodies);
        EXPECT_EQ(world.numConstraints(), baseConstraints);

        // And once more, so a second cycle through the same slots is checked too.
        ASSERT_FALSE(spawnLimpRagdoll(world, *host).empty());
        host->remove();
        EXPECT_EQ(world.numBodies(), baseBodies);
        EXPECT_EQ(world.numConstraints(), baseConstraints);
    }
}
