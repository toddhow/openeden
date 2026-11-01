// Tests for MWPhysics::RagdollHost, stage b of task 0035. Written from the header in the task file ("Stage b as
// decided") before the implementation exists; the tests use only that interface.
//
// In plain words: RagdollHost builds one limp, humanoid debug ragdoll inside a Jolt world that somebody else owns
// (here: a small stand-in for the game's world made by GameLikeWorld, with a floor whose top is at z = 0). The test
// plays the game's part: each physics step it calls host.beginStep(), then Jolt's Update, then host.endStep().

#include "ragdolltestworld.hpp"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;
    using MWPhysics::RagdollHost;
    using MWPhysics::RagdollStatus;

    constexpr double kUnits = Constants::UnitsPerMeter;

    std::unique_ptr<RagdollHost> makeHost(GameLikeWorld& world)
    {
        std::string error;
        std::unique_ptr<RagdollHost> host
            = RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        return host;
    }

    // One game physics step, in the order the spec gives: host.beginStep, system Update, host.endStep.
    void stepOnce(GameLikeWorld& world, RagdollHost& host)
    {
        host.beginStep();
        world.update();
        host.endStep();
    }

    void stepMany(GameLikeWorld& world, RagdollHost& host, int steps)
    {
        for (int i = 0; i < steps; ++i)
            stepOnce(world, host);
    }

    // An object-layer filter that lets through the layers whose bits are in a mask (the layers are single bits).
    class MaskLayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        explicit MaskLayerFilter(unsigned mask)
            : mMask(mask)
        {
        }

        bool ShouldCollide(JPH::ObjectLayer layer) const override { return (mMask & unsigned(layer)) != 0; }

    private:
        unsigned mMask;
    };

    TEST(RagdollHost, SpawnAddsBodiesAndReportsThem)
    {
        GameLikeWorld world;
        const std::size_t baseBodies = world.numBodies();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        EXPECT_FALSE(host->hasRagdoll());
        EXPECT_FALSE(host->status().present);

        std::string error;
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f, &error)) << error;

        const RagdollStatus status = host->status();
        EXPECT_TRUE(host->hasRagdoll());
        EXPECT_TRUE(status.present);
        EXPECT_GT(status.bodies, 0u);
        // The host's bodies are exactly the ones the system gained.
        EXPECT_EQ(world.numBodies(), baseBodies + status.bodies);
    }

    TEST(RagdollHost, RagdollRestsOnTheFloorAfterSixSeconds)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));

        stepMany(world, *host, 360);

        const RagdollStatus status = host->status();
        ASSERT_TRUE(status.present);
        std::cout << "[ragdoll 6s] lowestZ=" << status.lowestZ / kUnits << " m highestZ=" << status.highestZ / kUnits
                  << " m maxSpeed=" << status.maxSpeedMetresPerSecond << " m/s allAsleep=" << status.allAsleep << "\n";
        EXPECT_TRUE(std::isfinite(status.lowestZ));
        EXPECT_TRUE(std::isfinite(status.highestZ));
        // It did not sink through the floor (floor top is z = 0; body centres may be a little below it by contact
        // softness, but not more than 0.15 m).
        EXPECT_GE(status.lowestZ, -0.15 * kUnits);
        // Something is above the floor, and the lowest is not above the highest.
        EXPECT_GT(status.highestZ, 0.0);
        EXPECT_LE(status.lowestZ, status.highestZ);
        // It has come (nearly) to rest.
        EXPECT_LT(status.maxSpeedMetresPerSecond, 0.3f);
    }

    TEST(RagdollHost, SecondSpawnReplacesTheFirst)
    {
        GameLikeWorld world;
        const std::size_t baseBodies = world.numBodies();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));
        const std::size_t firstBodies = host->status().bodies;
        stepMany(world, *host, 10);

        ASSERT_TRUE(host->spawn(JPH::RVec3(500.0, 0.0, 0.0), 1.0f));
        const RagdollStatus status = host->status();
        EXPECT_TRUE(status.present);
        EXPECT_EQ(status.bodies, firstBodies);
        // Not double: one ragdoll's bodies on top of the base.
        EXPECT_EQ(world.numBodies(), baseBodies + status.bodies);
    }

    TEST(RagdollHost, RemoveLeavesTheSystemAsFound)
    {
        GameLikeWorld world;
        const std::size_t baseBodies = world.numBodies();
        const std::size_t baseConstraints = world.numConstraints();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));
        stepMany(world, *host, 5);
        EXPECT_GT(world.numConstraints(), baseConstraints);

        host->remove();
        EXPECT_FALSE(host->hasRagdoll());
        EXPECT_FALSE(host->status().present);
        EXPECT_EQ(world.numBodies(), baseBodies);
        EXPECT_EQ(world.numConstraints(), baseConstraints);

        // remove() with no ragdoll is a no-op.
        host->remove();
        EXPECT_EQ(world.numBodies(), baseBodies);

        // And the system still steps.
        stepMany(world, *host, 3);
    }

    TEST(RagdollHost, DestroyingTheHostLeavesTheSystemAsFound)
    {
        GameLikeWorld world;
        const std::size_t baseBodies = world.numBodies();
        const std::size_t baseConstraints = world.numConstraints();
        {
            auto host = makeHost(world);
            ASSERT_NE(host, nullptr);
            ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));
            stepMany(world, *host, 5);
        }
        EXPECT_EQ(world.numBodies(), baseBodies);
        EXPECT_EQ(world.numConstraints(), baseConstraints);
        world.update(); // still steps
    }

    TEST(RagdollHost, RemovesItselfWhenItFallsFarBelowItsSpawnPoint)
    {
        // No floor: the ragdoll falls for ever, and endStep must remove it once its lowest body is
        // cRagdollKillDepthMetres below the spawn point.
        GameLikeWorld world(false);
        const std::size_t baseBodies = world.numBodies();
        const std::size_t baseConstraints = world.numConstraints();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));

        int stepsUntilGone = -1;
        for (int step = 1; step <= 400; ++step)
        {
            stepOnce(world, *host);
            if (!host->hasRagdoll())
            {
                stepsUntilGone = step;
                break;
            }
        }
        std::cout << "[ragdoll kill depth] removed after " << stepsUntilGone << " steps\n";
        ASSERT_GT(stepsUntilGone, 0) << "the ragdoll was never removed within 400 steps";
        // Falling 20 m takes about 2 s (120 steps); it must not vanish at once.
        EXPECT_GT(stepsUntilGone, 30);
        EXPECT_FALSE(host->status().present);
        EXPECT_EQ(world.numBodies(), baseBodies);
        EXPECT_EQ(world.numConstraints(), baseConstraints);
    }

    TEST(RagdollHost, RagdollIgnoresAnActorCapsuleAndDoesNotPushIt)
    {
        GameLikeWorld world;
        // A kinematic capsule on the ACTOR layer where the ragdoll will be spawned (a living actor standing there).
        const JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(40.0f, 25.0f);
        const JPH::BodyCreationSettings settings(capsule, JPH::RVec3(0.0, 0.0, 65.0), JPH::Quat::sIdentity(),
            JPH::EMotionType::Kinematic, MWPhysics::Layers::ACTOR);
        const JPH::BodyID actor = world.bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
        const JPH::RVec3 positionBefore = world.bodies().GetCenterOfMassPosition(actor);
        const JPH::Quat rotationBefore = world.bodies().GetRotation(actor);

        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));
        stepMany(world, *host, 120);

        // The capsule's pose is exactly as it was.
        EXPECT_EQ(world.bodies().GetCenterOfMassPosition(actor), positionBefore);
        EXPECT_EQ(world.bodies().GetRotation(actor), rotationBefore);
        // And the ragdoll was not held up by it: it still came down to the floor.
        stepMany(world, *host, 240);
        const RagdollStatus status = host->status();
        ASSERT_TRUE(status.present);
        EXPECT_GE(status.lowestZ, -0.15 * kUnits);
        EXPECT_LT(status.highestZ, 100.0 * kUnits);
        world.bodies().RemoveBody(actor);
        world.bodies().DestroyBody(actor);
    }

    TEST(RagdollHost, OnlyAQueryThatNamesTheRagdollLayerCanHitIt)
    {
        GameLikeWorld world;
        const std::vector<JPH::BodyID> before = world.allBodies();
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ASSERT_TRUE(host->spawn(JPH::RVec3(0.0, 0.0, 0.0), 0.0f));
        const std::vector<JPH::BodyID> ragdollBodies = world.bodiesAddedSince(before);
        ASSERT_FALSE(ragdollBodies.empty());

        // Straight down through the spawn column, before any step, so the ragdoll still stands there.
        const JPH::RRayCast ray(JPH::RVec3(0.0, 0.0, 400.0), JPH::Vec3(0.0f, 0.0f, -400.0f));
        const JPH::NarrowPhaseQuery& query = world.system().GetNarrowPhaseQuery();

        // The game's usual mask: the ray passes the ragdoll and hits the floor.
        JPH::RayCastResult withDefault;
        const MaskLayerFilter defaultFilter(static_cast<unsigned>(MWPhysics::CollisionMask_Default));
        ASSERT_TRUE(query.CastRay(ray, withDefault, {}, defaultFilter));
        EXPECT_EQ(withDefault.mBodyID, world.floorId());

        // A mask that includes RAGDOLL hits one of the ragdoll's bodies, above the floor.
        JPH::RayCastResult withRagdoll;
        const MaskLayerFilter ragdollFilter(static_cast<unsigned>(MWPhysics::CollisionMask_Default)
            | static_cast<unsigned>(MWPhysics::Layers::RAGDOLL));
        ASSERT_TRUE(query.CastRay(ray, withRagdoll, {}, ragdollFilter));
        bool isRagdollBody = false;
        for (const JPH::BodyID id : ragdollBodies)
            isRagdollBody = isRagdollBody || id == withRagdoll.mBodyID;
        EXPECT_TRUE(isRagdollBody);
        EXPECT_LT(withRagdoll.mFraction, withDefault.mFraction);
    }

    // Runs the same scenario on a fresh world and returns what the host reports at several moments.
    std::vector<RagdollStatus> runScenario()
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        std::vector<RagdollStatus> snapshots;
        if (host == nullptr || !host->spawn(JPH::RVec3(120.0, -40.0, 30.0), 0.7f))
            return snapshots;
        for (int step = 1; step <= 240; ++step)
        {
            stepOnce(world, *host);
            if (step % 60 == 0)
                snapshots.push_back(host->status());
        }
        return snapshots;
    }

    TEST(RagdollHost, TwoIdenticalRunsGiveIdenticalResults)
    {
        const std::vector<RagdollStatus> first = runScenario();
        const std::vector<RagdollStatus> second = runScenario();
        ASSERT_EQ(first.size(), 4u);
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            // Exact comparison on purpose: the same inputs must give the same bits.
            EXPECT_EQ(first[i].present, second[i].present) << i;
            EXPECT_EQ(first[i].bodies, second[i].bodies) << i;
            EXPECT_EQ(first[i].lowestZ, second[i].lowestZ) << i;
            EXPECT_EQ(first[i].highestZ, second[i].highestZ) << i;
            EXPECT_EQ(first[i].maxSpeedMetresPerSecond, second[i].maxSpeedMetresPerSecond) << i;
            EXPECT_EQ(first[i].allAsleep, second[i].allAsleep) << i;
        }
    }
}
