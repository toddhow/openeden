// Tests for the actor ragdolls of task 0106 (stage a): RagdollHost hosts up to cMaxActorRagdolls humanoid ragdolls for
// dead NPCs in the game's one Jolt world, beside the debug ragdoll. Written from docs/tasks/0106-*.md before the code.
//
// In plain words: spawnActor makes a limp humanoid at an actor's feet, readActor gives its bones back in game units
// (world rotation and origin), removeActor takes it out again, and a handle that has been removed never works again.

#include "ragdolltestworld.hpp"

#include "apps/openmw_tests/support/allocationcounter.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;
    using MWPhysics::ActorRagdollHandle;
    using MWPhysics::RagdollBoneState;
    using MWPhysics::RagdollHost;

    constexpr double kUnits = Constants::UnitsPerMeter;
    constexpr double kNoVelocity[3] = { 0.0, 0.0, 0.0 };

    std::unique_ptr<RagdollHost> makeHost(GameLikeWorld& world)
    {
        std::string error;
        auto host = RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        return host;
    }

    void stepMany(GameLikeWorld& world, RagdollHost& host, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            host.beginStep();
            world.update();
            host.endStep();
        }
    }

    ActorRagdollHandle spawn(RagdollHost& host, double x = 0.0, double y = 0.0, double z = 0.0, float yaw = 0.0f,
        const double* velocity = kNoVelocity)
    {
        std::string error;
        const ActorRagdollHandle handle = host.spawnActor(JPH::RVec3(x, y, z), yaw, velocity, &error);
        EXPECT_TRUE(handle.isValid()) << error;
        return handle;
    }

    std::vector<RagdollBoneState> read(const RagdollHost& host, ActorRagdollHandle handle)
    {
        std::vector<RagdollBoneState> states(host.actorBodyBones().size());
        EXPECT_TRUE(host.readActor(handle, states));
        return states;
    }

    std::size_t indexOf(const RagdollHost& host, const std::string& bone)
    {
        const auto names = host.actorBodyBones();
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == bone)
                return i;
        ADD_FAILURE() << "no body for rig bone " << bone;
        return 0;
    }

    TEST(ActorRagdolls, beforeTheFirstSpawnThereAreNoBodiesAndNoBoneNames)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        EXPECT_EQ(host->actorRagdollCount(), 0u);
        EXPECT_TRUE(host->actorBodyBones().empty());
        EXPECT_FALSE(host->hasActor(ActorRagdollHandle{}));
        std::array<RagdollBoneState, 32> out;
        EXPECT_FALSE(host->readActor(ActorRagdollHandle{}, out));
        EXPECT_FALSE(host->removeActor(ActorRagdollHandle{}));
    }

    TEST(ActorRagdolls, aSpawnedRagdollHasSixteenNamedBodiesAndCountsAsOne)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::size_t before = world.allBodies().size();
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 10.0);
        ASSERT_TRUE(handle.isValid());
        EXPECT_TRUE(host->hasActor(handle));
        EXPECT_EQ(host->actorRagdollCount(), 1u);
        EXPECT_EQ(host->actorBodyBones().size(), world.allBodies().size() - before);
        EXPECT_GE(host->actorBodyBones().size(), 16u);
        // The names the driver maps: the pelvis is among them.
        indexOf(*host, "Pelvis");
        indexOf(*host, "LeftThigh");
        indexOf(*host, "RightForearm");
    }

    TEST(ActorRagdolls, atSpawnWithYawZeroEveryBoneRotationIsTheIdentity)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 100.0, -50.0, 20.0, 0.0f);
        const std::vector<RagdollBoneState> states = read(*host, handle);
        ASSERT_FALSE(states.empty());
        for (const RagdollBoneState& state : states)
        {
            const double length
                = std::sqrt(state.mRotation[0] * state.mRotation[0] + state.mRotation[1] * state.mRotation[1]
                    + state.mRotation[2] * state.mRotation[2] + state.mRotation[3] * state.mRotation[3]);
            EXPECT_NEAR(length, 1.0, 1e-5);
            // The rig has identity rest rotations; the body-in-bone offset is taken out by readActor.
            EXPECT_NEAR(std::abs(state.mRotation[3]), 1.0, 1e-4);
        }
    }

    TEST(ActorRagdolls, thePelvisBoneStartsAtTheFeetPlusTheRigsPelvisHeightInGameUnits)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 100.0, -50.0, 20.0, 0.0f);
        const std::vector<RagdollBoneState> states = read(*host, handle);
        const RagdollBoneState& pelvis = states[indexOf(*host, "Pelvis")];
        EXPECT_NEAR(pelvis.mPosition[0], 100.0, 0.5);
        EXPECT_NEAR(pelvis.mPosition[1], -50.0, 0.5);
        // The embedded humanoid rig's pelvis is 0.95 m above its root (ragdollrigdata.cpp).
        EXPECT_NEAR(pelvis.mPosition[2], 20.0 + 0.95 * kUnits, 0.5);
    }

    TEST(ActorRagdolls, theGameYawTurnsTheRagdollTheOtherWayAboutZ)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const float yaw = 1.0f; // radians, a game rotation turns clockwise
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 20.0, yaw);
        const std::vector<RagdollBoneState> states = read(*host, handle);
        const RagdollBoneState& pelvis = states[indexOf(*host, "Pelvis")];
        // A right-handed rotation of -yaw about +Z: z = sin(-yaw / 2), w = cos(yaw / 2), x = y = 0.
        EXPECT_NEAR(pelvis.mRotation[0], 0.0, 1e-4);
        EXPECT_NEAR(pelvis.mRotation[1], 0.0, 1e-4);
        EXPECT_NEAR(pelvis.mRotation[2], std::sin(-0.5 * yaw), 1e-4);
        EXPECT_NEAR(std::abs(pelvis.mRotation[3]), std::cos(0.5 * yaw), 1e-4);
    }

    TEST(ActorRagdolls, theNinthIsRefusedWithAMessageAndLeavesTheWorldAsItWas)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        std::vector<ActorRagdollHandle> handles;
        for (std::size_t i = 0; i < MWPhysics::cMaxActorRagdolls; ++i)
            handles.push_back(spawn(*host, 300.0 * static_cast<double>(i), 0.0, 20.0));
        EXPECT_EQ(host->actorRagdollCount(), MWPhysics::cMaxActorRagdolls);

        const std::size_t bodies = world.allBodies().size();
        std::string error;
        const ActorRagdollHandle ninth = host->spawnActor(JPH::RVec3(0, 0, 0), 0.0f, kNoVelocity, &error);
        EXPECT_FALSE(ninth.isValid());
        EXPECT_NE(error.find("slot"), std::string::npos) << error;
        EXPECT_EQ(world.allBodies().size(), bodies);
        EXPECT_EQ(host->actorRagdollCount(), MWPhysics::cMaxActorRagdolls);

        // Freeing one makes room again.
        EXPECT_TRUE(host->removeActor(handles[3]));
        EXPECT_TRUE(spawn(*host, 0.0, 500.0, 20.0).isValid());
    }

    TEST(ActorRagdolls, removalGivesBackEveryBodyAndAStaleHandleNeverWorksAgain)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::size_t before = world.allBodies().size();
        const ActorRagdollHandle first = spawn(*host, 0.0, 0.0, 20.0);
        EXPECT_TRUE(host->removeActor(first));
        EXPECT_EQ(world.allBodies().size(), before);
        EXPECT_FALSE(host->hasActor(first));
        EXPECT_FALSE(host->removeActor(first));
        std::vector<RagdollBoneState> out(host->actorBodyBones().size());
        EXPECT_FALSE(host->readActor(first, out));

        // The slot is reused, the old handle stays stale.
        const ActorRagdollHandle second = spawn(*host, 0.0, 0.0, 20.0);
        EXPECT_EQ(second.mIndex, first.mIndex);
        EXPECT_NE(second.mGeneration, first.mGeneration);
        EXPECT_FALSE(host->hasActor(first));
        EXPECT_TRUE(host->hasActor(second));
        EXPECT_FALSE(host->removeActor(first));
        EXPECT_TRUE(host->hasActor(second));
    }

    TEST(ActorRagdolls, aTooSmallOutputSpanIsRefusedAndWritesNothing)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 20.0);
        std::array<RagdollBoneState, 3> tooSmall;
        tooSmall[0].mPosition[0] = 123.0;
        EXPECT_FALSE(host->readActor(handle, tooSmall));
        EXPECT_EQ(tooSmall[0].mPosition[0], 123.0);
    }

    TEST(ActorRagdolls, theHostDestructorRemovesEveryActorRagdollAndTheDebugOne)
    {
        GameLikeWorld world;
        const std::size_t before = world.allBodies().size();
        {
            auto host = makeHost(world);
            ASSERT_NE(host, nullptr);
            spawn(*host, 0.0, 0.0, 20.0);
            spawn(*host, 400.0, 0.0, 20.0);
            std::string error;
            ASSERT_TRUE(host->spawn(JPH::RVec3(800, 0, 20), 0.0f, &error)) << error;
            EXPECT_GT(world.allBodies().size(), before);
        }
        EXPECT_EQ(world.allBodies().size(), before);
    }

    TEST(ActorRagdolls, theDebugRagdollAndAnActorRagdollLiveSideBySide)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        std::string error;
        ASSERT_TRUE(host->spawn(JPH::RVec3(0, 0, 20), 0.0f, &error)) << error;
        const std::size_t debugBodies = world.allBodies().size();
        const ActorRagdollHandle handle = spawn(*host, 500.0, 0.0, 20.0);
        EXPECT_TRUE(host->hasActor(handle));
        EXPECT_TRUE(host->removeActor(handle));
        EXPECT_TRUE(host->hasRagdoll());
        EXPECT_EQ(world.allBodies().size(), debugBodies);
    }

    TEST(ActorRagdolls, theActorsVelocityIsGivenToEveryBody)
    {
        GameLikeWorld world(false); // no floor: nothing to push against
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        // 10 m/s along +X in game units per second, and no gravity influence worth speaking of in one step.
        const double velocity[3] = { 10.0 * kUnits, 0.0, 0.0 };
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 5000.0, 0.0f, velocity);
        const double startX = read(*host, handle)[indexOf(*host, "Pelvis")].mPosition[0];
        stepMany(world, *host, 6); // 0.1 s
        const double movedX = read(*host, handle)[indexOf(*host, "Pelvis")].mPosition[0] - startX;
        // About 1 m, i.e. about 70 units; a body that ignored the velocity would have moved by almost nothing.
        EXPECT_GT(movedX, 0.8 * kUnits);
        EXPECT_LT(movedX, 1.2 * kUnits);
    }

    TEST(ActorRagdolls, aLimpRagdollFallsAndSettlesOnTheFloorWithoutSinkingOrFlying)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 0.05 * kUnits);
        const double standingPelvisZ = read(*host, handle)[indexOf(*host, "Pelvis")].mPosition[2];
        stepMany(world, *host, 480); // 8 s
        const std::vector<RagdollBoneState> states = read(*host, handle);
        for (const RagdollBoneState& state : states)
        {
            for (const double value : state.mPosition)
                EXPECT_TRUE(std::isfinite(value));
            // A bone origin is never under the floor by more than a body radius, nor above the standing height.
            EXPECT_GT(state.mPosition[2], -0.15 * kUnits);
            EXPECT_LT(state.mPosition[2], standingPelvisZ + 0.5 * kUnits);
        }
        // The pelvis has come down: the body no longer stands.
        EXPECT_LT(states[indexOf(*host, "Pelvis")].mPosition[2], 0.6 * standingPelvisZ);
    }

    TEST(ActorRagdolls, twoRunsGiveBitIdenticalBones)
    {
        const auto run = [] {
            GameLikeWorld world;
            auto host = makeHost(world);
            const double velocity[3] = { 100.0, -40.0, 0.0 };
            const ActorRagdollHandle handle = spawn(*host, 10.0, 20.0, 0.05 * kUnits, 0.7f, velocity);
            stepMany(world, *host, 240);
            return read(*host, handle);
        };
        const std::vector<RagdollBoneState> a = run();
        const std::vector<RagdollBoneState> b = run();
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            for (int k = 0; k < 3; ++k)
                EXPECT_EQ(a[i].mPosition[k], b[i].mPosition[k]);
            for (int k = 0; k < 4; ++k)
                EXPECT_EQ(a[i].mRotation[k], b[i].mRotation[k]);
        }
    }

    TEST(ActorRagdolls, aRagdollThatFallsTwentyMetresBelowItsSpawnIsRemoved)
    {
        GameLikeWorld world(false); // no floor
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 0.0);
        stepMany(world, *host, 400); // free fall of 20 m takes about 2.1 s
        EXPECT_FALSE(host->hasActor(handle));
        EXPECT_EQ(host->actorRagdollCount(), 0u);
    }

    TEST(ActorRagdolls, aRagdollOnTheFloorIsNotRemovedByTheKillDepth)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 0.05 * kUnits);
        stepMany(world, *host, 600);
        EXPECT_TRUE(host->hasActor(handle));
    }

    TEST(ActorRagdolls, readingAndSteppingAllocateNothing)
    {
        GameLikeWorld world;
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        host->setProgressLogging(false);
        const ActorRagdollHandle handle = spawn(*host, 0.0, 0.0, 0.05 * kUnits);
        std::vector<RagdollBoneState> out(host->actorBodyBones().size());
        stepMany(world, *host, 30); // warm up

        std::size_t readAllocations = 0;
        std::size_t stepAllocations = 0;
        for (int i = 0; i < 100; ++i)
        {
            {
                TestSupport::AllocationCounter counter;
                EXPECT_TRUE(host->readActor(handle, out));
                readAllocations += counter.count();
            }
            {
                TestSupport::AllocationCounter counter;
                host->beginStep();
                stepAllocations += counter.count();
            }
            world.update();
            {
                TestSupport::AllocationCounter counter;
                host->endStep();
                stepAllocations += counter.count();
            }
        }
        EXPECT_EQ(readAllocations, 0u);
        EXPECT_EQ(stepAllocations, 0u);
    }
}
