// Tests for task 0106, second slice (2): a ragdoll that is built inside static geometry is moved out of it before it is
// built. Written from the spec before the code: RagdollHost::spawnActor.
//
// In plain words: the start pose can put a hand in a wall or a foot in the floor (a death against a wall, a pose whose
// legs reach lower than the rig's). Before the bodies are made, the whole ragdoll is moved, in at most a few small
// steps, out of the world's walls, floor and doors, so Jolt does not have to shove deeply buried bodies out at speed.
// If it cannot be freed (a solid block), it is built where it was, and the call still returns.

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace MWPhysicsTest;
    using MWPhysics::ActorRagdollHandle;
    using MWPhysics::ActorStartPose;
    using MWPhysics::RagdollBoneState;
    using MWPhysics::RagdollHost;

    constexpr double kNoVelocity[3] = { 0.0, 0.0, 0.0 };

    // The deepest penetration of any body made since `before` into the static world layer, in game units.
    double deepestPenetration(GameLikeWorld& world, const std::vector<JPH::BodyID>& before)
    {
        double deepest = 0.0;
        for (const JPH::BodyID id : world.bodiesAddedSince(before))
        {
            JPH::RefConst<JPH::Shape> shape;
            JPH::RMat44 transform;
            {
                JPH::BodyLockRead lock(world.system().GetBodyLockInterface(), id);
                if (!lock.Succeeded())
                    continue;
                shape = lock.GetBody().GetShape();
                transform = lock.GetBody().GetCenterOfMassTransform();
            }
            JPH::CollideShapeSettings settings;
            JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
            struct OnlyWorld final : public JPH::ObjectLayerFilter
            {
                bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == MWPhysics::Layers::WORLD; }
            } layerFilter;
            world.system().GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), transform, settings,
                JPH::RVec3::sZero(), collector,
                world.system().GetDefaultBroadPhaseLayerFilter(MWPhysics::Layers::RAGDOLL), layerFilter);
            if (collector.HadHit())
                deepest = std::max(deepest, static_cast<double>(collector.mHit.mPenetrationDepth));
        }
        return deepest;
    }

    void addBox(GameLikeWorld& world, JPH::Vec3 half, JPH::RVec3 centre)
    {
        const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);
        const JPH::BodyCreationSettings settings(
            shape, centre, JPH::Quat::sIdentity(), JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
        world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    std::unique_ptr<RagdollHost> makeHost(GameLikeWorld& world)
    {
        std::string error;
        auto host = RagdollHost::create(world.system(), Constants::UnitsPerMeter, kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        return host;
    }

    ActorStartPose poseAt(double x, double y, double z)
    {
        ActorStartPose pose;
        pose.mPelvisPosition[0] = x;
        pose.mPelvisPosition[1] = y;
        pose.mPelvisPosition[2] = z;
        return pose;
    }

    ActorRagdollHandle spawn(RagdollHost& host, const ActorStartPose& pose, float yaw = 0.0f)
    {
        std::string error;
        const ActorRagdollHandle handle = host.spawnActor(
            JPH::RVec3(pose.mPelvisPosition[0], pose.mPelvisPosition[1], 0.0), yaw, kNoVelocity, &error, &pose);
        EXPECT_TRUE(handle.isValid()) << error;
        return handle;
    }

    RagdollBoneState pelvisOf(const RagdollHost& host, ActorRagdollHandle handle)
    {
        std::vector<RagdollBoneState> states(host.actorBodyBones().size());
        EXPECT_TRUE(host.readActor(handle, states));
        for (std::size_t i = 0; i < states.size(); ++i)
            if (host.actorBodyBones()[i] == "Pelvis")
                return states[i];
        ADD_FAILURE() << "no pelvis";
        return {};
    }

    TEST(ActorRagdollPlacement, aRagdollThatTouchesNothingIsBuiltWhereTheActorStood)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, poseAt(10.0, 20.0, 900.0));
        const RagdollBoneState pelvis = pelvisOf(*host, handle);
        EXPECT_NEAR(pelvis.mPosition[0], 10.0, 0.01);
        EXPECT_NEAR(pelvis.mPosition[1], 20.0, 0.01);
        EXPECT_NEAR(pelvis.mPosition[2], 900.0, 0.01);
    }

    TEST(ActorRagdollPlacement, aRagdollWithAHandInAWallIsMovedAwayFromTheWallBeforeItIsBuilt)
    {
        GameLikeWorld world(false);
        // A wall with its face at x = +6: the left arm hangs at x = -0.21 m = about -15 units and the right at +15.
        addBox(world, JPH::Vec3(100.0f, 400.0f, 400.0f), JPH::RVec3(106.0, 0.0, 400.0));
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<JPH::BodyID> before = world.allBodies();
        const ActorRagdollHandle handle = spawn(*host, poseAt(0.0, 0.0, 400.0));
        EXPECT_LT(deepestPenetration(world, before), 0.5);
        // It moved away from the wall (to -x), and not far.
        const RagdollBoneState pelvis = pelvisOf(*host, handle);
        EXPECT_LT(pelvis.mPosition[0], -5.0);
        EXPECT_GT(pelvis.mPosition[0], -60.0);
        EXPECT_NEAR(pelvis.mPosition[1], 0.0, 1.0);
        EXPECT_NEAR(pelvis.mPosition[2], 400.0, 1.0);
    }

    TEST(ActorRagdollPlacement, aRagdollWhoseFeetAreBelowTheFloorIsLiftedOutOfIt)
    {
        GameLikeWorld world(true); // floor top at z = 0
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const std::vector<JPH::BodyID> before = world.allBodies();
        // The rig's feet are 0.87 m below the pelvis, about 61 units: a pelvis at 40 puts them 21 units under.
        const ActorRagdollHandle handle = spawn(*host, poseAt(0.0, 0.0, 40.0));
        EXPECT_LT(deepestPenetration(world, before), 0.5);
        EXPECT_GT(pelvisOf(*host, handle).mPosition[2], 40.0 + 10.0);
        EXPECT_LT(pelvisOf(*host, handle).mPosition[2], 40.0 + 60.0);
    }

    TEST(ActorRagdollPlacement, aRagdollEmbeddedInASolidBlockIsStillBuiltAndTheCallReturns)
    {
        GameLikeWorld world(false);
        // A solid 40 m block: there is no way out within the allowed shift.
        addBox(world, JPH::Vec3(1400.0f, 1400.0f, 1400.0f), JPH::RVec3(0.0, 0.0, 1000.0));
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorRagdollHandle handle = spawn(*host, poseAt(0.0, 0.0, 1000.0));
        ASSERT_TRUE(handle.isValid());
        EXPECT_EQ(host->actorRagdollCount(), 1u);
        // Not thrown across the map: the start shift is bounded (at most 2 m, 140 units).
        const RagdollBoneState pelvis = pelvisOf(*host, handle);
        const double shift
            = std::sqrt(pelvis.mPosition[0] * pelvis.mPosition[0] + pelvis.mPosition[1] * pelvis.mPosition[1]
                + (pelvis.mPosition[2] - 1000.0) * (pelvis.mPosition[2] - 1000.0));
        EXPECT_LE(shift, 140.5);
    }

    TEST(ActorRagdollPlacement, aMovedStartIsTheSameInTwoRuns)
    {
        double first[3] = {};
        double second[3] = {};
        for (double* out : { first, second })
        {
            GameLikeWorld world(false);
            addBox(world, JPH::Vec3(100.0f, 400.0f, 400.0f), JPH::RVec3(106.0, 0.0, 400.0));
            auto host = makeHost(world);
            ASSERT_NE(host, nullptr);
            const RagdollBoneState pelvis = pelvisOf(*host, spawn(*host, poseAt(0.0, 0.0, 400.0)));
            for (int k = 0; k < 3; ++k)
                out[k] = pelvis.mPosition[k];
        }
        for (int k = 0; k < 3; ++k)
            EXPECT_EQ(first[k], second[k]);
    }

    TEST(ActorRagdollPlacement, aPoseOnTheFloorIsNotMovedByTheFloorTouchingItsFeet)
    {
        GameLikeWorld world(true);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        // Feet exactly resting: pelvis at 0.87 m above them, plus the foot box's own half height and ankle (the rig's
        // lowest point is its foot box, 0.0 m above the root, root = pelvis - 0.95 m).
        const double pelvisZ = 0.95 * Constants::UnitsPerMeter + 0.5;
        const ActorRagdollHandle handle = spawn(*host, poseAt(0.0, 0.0, pelvisZ));
        EXPECT_NEAR(pelvisOf(*host, handle).mPosition[2], pelvisZ, 0.6);
    }
}
