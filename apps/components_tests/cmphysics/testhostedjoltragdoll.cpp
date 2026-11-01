// CreativeMotion's ragdoll builder over HostedJoltWorld, stage a of task 0035: opas::motor::buildRagdoll must work
// through the adapter exactly as it does through CreativeMotion's own world, in the host's single Jolt world.
//
// The rig is built in code (the vendored CreativeMotion copy has no rig file): a pelvis, a spine, a head and two legs
// of two bones each, in meters with Z up, standing with its feet about half a meter above the floor.

#include "hostedjoltfixture.hpp"

#include <opas/math/Transform.hpp>
#include <opas/motor/Ragdoll.hpp>
#include <opas/rig/Profile.hpp>
#include <opas/rig/Rig.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace
{
    using namespace CMPhysicsTest;

    // The rig has seven bodies (pelvis, spine, head, two thighs, two shins) and six joints (every body but the pelvis).
    constexpr std::size_t kRigBodies = 7;
    constexpr std::size_t kRigJoints = 6;

    // Where the model root starts in the world: its feet end up about half a meter above the floor's top (z = 0).
    constexpr double kStartHeightM = 0.5;

    // The floor is a static box whose top face is z = 0.
    constexpr float kFloorTopZ = 0.0f;
    constexpr float kFloorHalfThickness = 0.5f;

    // How long the ragdoll has to settle: 4 s at the host's 1/60 s step.
    constexpr int kSettleSteps = 240;

    // A body centre may sink this far below the floor's top before the test calls it fallen through (meters). The
    // lowest part of a lying ragdoll is a capsule or sphere of radius 0.07 m or more, so a centre should stay above 0.
    constexpr float kSinkTolerance = 0.05f;

    // After settling, every body is slower than this (meters per second): resting, not tumbling or bouncing.
    constexpr float kRestSpeed = 0.5f;

    opas::math::Transform atRest(double x, double y, double z)
    {
        opas::math::Transform transform;
        transform.translation = opas::math::Vec3{ x, y, z };
        return transform;
    }

    opas::rig::Bone bone(std::string name, int parent, double x, double y, double z)
    {
        opas::rig::Bone result;
        result.name = std::move(name);
        result.parent = parent;
        result.localRest = atRest(x, y, z);
        return result;
    }

    opas::rig::Body capsuleBody(std::string boneName, double halfHeight, double radius, double centreZ, double massKg)
    {
        opas::rig::Body body;
        body.bone = std::move(boneName);
        body.shape = opas::rig::CapsuleShape{ halfHeight, radius };
        body.local = atRest(0.0, 0.0, centreZ);
        body.massKg = massKg;
        return body;
    }

    // A cone joint on `boneName` with its twist axis along +Z or -Z of the bone's frame.
    opas::rig::Joint coneJoint(std::string boneName, double twistAxisZ)
    {
        constexpr double kConeHalfAngleRad = 0.5;
        constexpr double kTwistLimitRad = 0.3;
        opas::rig::Joint joint;
        joint.bone = std::move(boneName);
        joint.twistAxis = opas::math::Vec3{ 0.0, 0.0, twistAxisZ };
        joint.planeAxis = opas::math::Vec3{ 1.0, 0.0, 0.0 };
        joint.swing = opas::rig::SwingShape::Cone;
        joint.planeSwingMinRad = -kConeHalfAngleRad;
        joint.planeSwingMaxRad = kConeHalfAngleRad;
        joint.normalSwingMinRad = -kConeHalfAngleRad;
        joint.normalSwingMaxRad = kConeHalfAngleRad;
        joint.twistMinRad = -kTwistLimitRad;
        joint.twistMaxRad = kTwistLimitRad;
        return joint;
    }

    opas::rig::Rig makeRig()
    {
        opas::rig::Rig rig;
        rig.name = "test_humanoid";
        rig.skeleton.bones = {
            bone("pelvis", -1, 0.0, 0.0, 1.0),
            bone("spine", 0, 0.0, 0.0, 0.2),
            bone("head", 1, 0.0, 0.0, 0.4),
            bone("thigh_l", 0, 0.1, 0.0, -0.05),
            bone("shin_l", 3, 0.0, 0.0, -0.45),
            bone("thigh_r", 0, -0.1, 0.0, -0.05),
            bone("shin_r", 5, 0.0, 0.0, -0.45),
        };

        opas::rig::Body pelvis;
        pelvis.bone = "pelvis";
        pelvis.shape = opas::rig::BoxShape{ opas::math::Vec3{ 0.15, 0.1, 0.1 } };
        pelvis.massKg = 10.0;
        opas::rig::Body head;
        head.bone = "head";
        head.shape = opas::rig::SphereShape{ 0.12 };
        head.local = atRest(0.0, 0.0, 0.12);
        head.massKg = 4.0;
        rig.bodies = {
            pelvis,
            capsuleBody("spine", 0.1, 0.1, 0.2, 12.0),
            head,
            capsuleBody("thigh_l", 0.15, 0.07, -0.225, 8.0),
            capsuleBody("shin_l", 0.15, 0.07, -0.225, 4.0),
            capsuleBody("thigh_r", 0.15, 0.07, -0.225, 8.0),
            capsuleBody("shin_r", 0.15, 0.07, -0.225, 4.0),
        };

        rig.joints = {
            coneJoint("spine", 1.0),
            coneJoint("head", 1.0),
            coneJoint("thigh_l", -1.0),
            coneJoint("shin_l", -1.0),
            coneJoint("thigh_r", -1.0),
            coneJoint("shin_r", -1.0),
        };
        return rig;
    }

    opas::rig::Profile makeProfile(const opas::rig::Rig& rig)
    {
        opas::rig::Profile profile;
        profile.rig = rig.name;
        // Default motor gains for every joint: a soft spring with damping, a finite torque cap.
        profile.defaultMotor = opas::rig::MotorGains{ 20.0, 2.0, 200.0 };
        profile.defaultMaterial = opas::rig::Material{ 0.6, 0.0, 0.05, 0.05 };
        return profile;
    }

    // The host with a floor and the adapter in the game's style: meters, no static layer (the static world is the
    // host's), a stepper that calls the host's Update.
    class RagdollOverAdapterTest : public ::testing::Test
    {
    protected:
        RagdollOverAdapterTest()
            : mStepper(mHost)
            , mRig(makeRig())
            , mProfile(makeProfile(mRig))
        {
        }

        void SetUp() override
        {
            ASSERT_EQ(opas::rig::validateRig(mRig), "");
            ASSERT_EQ(opas::rig::validateProfile(mProfile, mRig), "");

            mFloor = mHost.addHostBox(
                JPH::Vec3(10.0f, 10.0f, kFloorHalfThickness), JPH::RVec3(0.0, 0.0, kFloorTopZ - kFloorHalfThickness));
            mAdapter = makeAdapter(mHost, makeConfig(1.0f, &mStepper, /*allowStatic=*/false));
            ASSERT_NE(mAdapter, nullptr);
        }

        opas::math::Transform rootWorld() const { return atRest(0.0, 0.0, kStartHeightM); }

        HostWorld mHost;
        UpdateStepper mStepper;
        opas::rig::Rig mRig;
        opas::rig::Profile mProfile;
        JPH::BodyID mFloor;
        std::unique_ptr<CMPhysics::HostedJoltWorld> mAdapter;
    };

    TEST_F(RagdollOverAdapterTest, buildRagdollSucceedsAndPutsOneBodyPerRigBodyInTheHostSystem)
    {
        const std::size_t bodiesBefore = mHost.numBodies();
        const std::size_t constraintsBefore = mHost.numConstraints();

        opas::motor::BuildResult result = opas::motor::buildRagdoll(*mAdapter, mRig, mProfile, rootWorld());
        ASSERT_TRUE(result.ok()) << result.message;

        EXPECT_EQ(result.ragdoll->bodies.size(), kRigBodies);
        EXPECT_EQ(result.ragdoll->joints.size(), kRigJoints);
        EXPECT_EQ(mHost.numBodies(), bodiesBefore + kRigBodies);
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore + kRigJoints);

        // Every body of the ragdoll can be read back, in meters, near where the rig puts it.
        for (const opas::motor::RagdollBody& body : result.ragdoll->bodies)
        {
            opas::physics::BodyState state;
            ASSERT_TRUE(mAdapter->readBody(body.id, state));
            EXPECT_TRUE(isFiniteState(state));
            EXPECT_GT(state.positionM.z, static_cast<float>(kStartHeightM));
        }

        EXPECT_TRUE(opas::motor::destroyRagdoll(*mAdapter, *result.ragdoll));
    }

    TEST_F(RagdollOverAdapterTest, theRagdollFallsOntoTheFloorAndComesToRest)
    {
        opas::motor::BuildResult result = opas::motor::buildRagdoll(*mAdapter, mRig, mProfile, rootWorld());
        ASSERT_TRUE(result.ok()) << result.message;

        for (int i = 0; i < kSettleSteps; ++i)
            mAdapter->step();

        float lowestCentreZ = 1000.0f;
        float fastest = 0.0f;
        for (const opas::motor::RagdollBody& body : result.ragdoll->bodies)
        {
            opas::physics::BodyState state;
            ASSERT_TRUE(mAdapter->readBody(body.id, state));
            ASSERT_TRUE(isFiniteState(state));
            lowestCentreZ = std::min(lowestCentreZ, state.positionM.z);
            const opas::physics::Vec3& v = state.linearVelocityMps;
            fastest = std::max(fastest, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
        }
        // Nothing fell through the floor, and it fell from where it started (so the test is not vacuous).
        EXPECT_GT(lowestCentreZ, kFloorTopZ - kSinkTolerance);
        EXPECT_LT(lowestCentreZ, static_cast<float>(kStartHeightM));
        // And it is resting.
        EXPECT_LT(fastest, kRestSpeed);

        EXPECT_TRUE(opas::motor::destroyRagdoll(*mAdapter, *result.ragdoll));
    }

    TEST_F(RagdollOverAdapterTest, destroyRagdollLeavesTheSystemAsFound)
    {
        const std::size_t bodiesBefore = mHost.numBodies();
        const std::size_t constraintsBefore = mHost.numConstraints();

        opas::motor::BuildResult result = opas::motor::buildRagdoll(*mAdapter, mRig, mProfile, rootWorld());
        ASSERT_TRUE(result.ok()) << result.message;
        for (int i = 0; i < kSettleSteps / 4; ++i)
            mAdapter->step();

        ASSERT_TRUE(opas::motor::destroyRagdoll(*mAdapter, *result.ragdoll));
        EXPECT_EQ(mHost.numBodies(), bodiesBefore);
        EXPECT_EQ(mHost.numConstraints(), constraintsBefore);

        // The collision group was destroyed too, and the world still steps and can build another ragdoll.
        mAdapter->step();
        opas::motor::BuildResult again = opas::motor::buildRagdoll(*mAdapter, mRig, mProfile, rootWorld());
        EXPECT_TRUE(again.ok()) << again.message;
        if (again.ok())
            EXPECT_TRUE(opas::motor::destroyRagdoll(*mAdapter, *again.ragdoll));
        EXPECT_EQ(mHost.numBodies(), bodiesBefore);
    }

    TEST(RagdollOverAdapterCapacity, aBuildThatRunsOutOfBodiesFailsAndLeavesNothingBehind)
    {
        HostWorld host;
        CMPhysics::HostedWorldConfig config = makeConfig(1.0f, nullptr, /*allowStatic=*/false);
        config.maxBodies = 5; // the rig needs seven bodies (and a collision group)
        const std::unique_ptr<CMPhysics::HostedJoltWorld> adapter = makeAdapter(host, config);
        ASSERT_NE(adapter, nullptr);
        const opas::rig::Rig rig = makeRig();
        const opas::rig::Profile profile = makeProfile(rig);
        const std::size_t bodiesBefore = host.numBodies();
        const std::size_t constraintsBefore = host.numConstraints();

        const opas::motor::BuildResult result
            = opas::motor::buildRagdoll(*adapter, rig, profile, atRest(0.0, 0.0, kStartHeightM));

        // Which call fails first (the collision group or the sixth body) is the builder's order, not the adapter's, so
        // only the failure itself is checked, not its error code.
        EXPECT_FALSE(result.ok());
        EXPECT_NE(result.error, opas::physics::ErrorCode::None);
        // "On any failure everything already created is destroyed."
        EXPECT_EQ(host.numBodies(), bodiesBefore);
        EXPECT_EQ(host.numConstraints(), constraintsBefore);
    }
}
