// Tests for task 0106, second slice (1): a dead actor's ragdoll starts in the actor's pose at death, not in the rig's
// standing rest pose. Written from the spec before the code: apps/openmw/mwphysics/ragdolltypes.hpp (ActorStartPose)
// and RagdollHost::spawnActor's pose argument.
//
// In plain words: the host is given the pelvis position and, per rig bone, how far the actor's bone has turned from
// its rest pose. The ragdoll is built with its bones turned that way (a bone that is not named turns with its parent),
// so the first frame of the ragdoll is the pose the actor died in.

#include "ragdolltestworld.hpp"

#include "apps/openmw/mwphysics/ragdollhost.hpp"

#include <gtest/gtest.h>

#include <opas/math/Quat.hpp>

#include <array>
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

    constexpr double kUnits = Constants::UnitsPerMeter;
    constexpr double kNoVelocity[3] = { 0.0, 0.0, 0.0 };
    constexpr double kPi = 3.14159265358979323846;

    opas::math::Quat axisAngle(double x, double y, double z, double angle)
    {
        return opas::math::fromAxisAngle(opas::math::Vec3{ x, y, z }, angle);
    }

    void addBone(ActorStartPose& pose, std::string_view bone, const opas::math::Quat& q)
    {
        const double rotation[4] = { q.x, q.y, q.z, q.w };
        ASSERT_TRUE(pose.add(bone, rotation));
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

    ActorRagdollHandle spawnPosed(RagdollHost& host, const ActorStartPose& pose, float yaw = 0.0f, double feetZ = 0.0)
    {
        std::string error;
        const ActorRagdollHandle handle = host.spawnActor(
            JPH::RVec3(pose.mPelvisPosition[0], pose.mPelvisPosition[1], feetZ), yaw, kNoVelocity, &error, &pose);
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

    opas::math::Quat rotationOf(const RagdollBoneState& s)
    {
        return opas::math::Quat{ s.mRotation[0], s.mRotation[1], s.mRotation[2], s.mRotation[3] };
    }

    ::testing::AssertionResult sameRotation(const opas::math::Quat& a, const opas::math::Quat& b, double tolerance)
    {
        const double angle = opas::math::angleBetween(a, b);
        if (angle > tolerance)
            return ::testing::AssertionFailure() << "rotations differ by " << angle << " rad";
        return ::testing::AssertionSuccess();
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

    TEST(ActorRagdollPose, thePelvisBoneStartsAtTheGivenPelvisPositionNotAtTheFeet)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorStartPose pose = poseAt(123.0, -45.0, 910.0);
        const ActorRagdollHandle handle = spawnPosed(*host, pose, 0.0f, 0.0);
        const RagdollBoneState pelvis = read(*host, handle)[indexOf(*host, "Pelvis")];
        EXPECT_NEAR(pelvis.mPosition[0], 123.0, 0.5);
        EXPECT_NEAR(pelvis.mPosition[1], -45.0, 0.5);
        EXPECT_NEAR(pelvis.mPosition[2], 910.0, 0.5);
    }

    TEST(ActorRagdollPose, theGivenPelvisPositionIsKeptWhateverTheYaw)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorStartPose pose = poseAt(10.0, 20.0, 800.0);
        const ActorRagdollHandle handle = spawnPosed(*host, pose, 1.3f, 0.0);
        const RagdollBoneState pelvis = read(*host, handle)[indexOf(*host, "Pelvis")];
        EXPECT_NEAR(pelvis.mPosition[0], 10.0, 0.5);
        EXPECT_NEAR(pelvis.mPosition[1], 20.0, 0.5);
        EXPECT_NEAR(pelvis.mPosition[2], 800.0, 0.5);
    }

    TEST(ActorRagdollPose, aNamedBoneStartsTurnedByTheGivenRotation)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ActorStartPose pose = poseAt(0.0, 0.0, 800.0);
        const opas::math::Quat q = axisAngle(1.0, 0.0, 0.0, 0.5 * kPi);
        addBone(pose, "LeftUpperArm", q);
        const ActorRagdollHandle handle = spawnPosed(*host, pose);
        const auto states = read(*host, handle);
        EXPECT_TRUE(sameRotation(rotationOf(states[indexOf(*host, "LeftUpperArm")]), q, 1e-3));
        // A bone that is not named and not below it keeps the rig's rest rotation.
        EXPECT_TRUE(sameRotation(rotationOf(states[indexOf(*host, "RightUpperArm")]), opas::math::Quat{}, 1e-3));
        EXPECT_TRUE(sameRotation(rotationOf(states[indexOf(*host, "LeftThigh")]), opas::math::Quat{}, 1e-3));
    }

    TEST(ActorRagdollPose, aBoneThatIsNotNamedTurnsWithItsParentAndHangsFromIt)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ActorStartPose pose = poseAt(0.0, 0.0, 800.0);
        addBone(pose, "LeftUpperArm", axisAngle(1.0, 0.0, 0.0, 0.5 * kPi));
        const ActorRagdollHandle handle = spawnPosed(*host, pose);
        const auto states = read(*host, handle);
        const RagdollBoneState& upper = states[indexOf(*host, "LeftUpperArm")];
        const RagdollBoneState& fore = states[indexOf(*host, "LeftForearm")];
        EXPECT_TRUE(sameRotation(rotationOf(fore), rotationOf(upper), 1e-3));
        // The rig's forearm hangs 0.29 m down the upper arm's -Z; a quarter turn about X sends -Z to +Y.
        EXPECT_NEAR(fore.mPosition[0] - upper.mPosition[0], 0.0, 0.5);
        EXPECT_NEAR(fore.mPosition[1] - upper.mPosition[1], 0.29 * kUnits, 0.5);
        EXPECT_NEAR(fore.mPosition[2] - upper.mPosition[2], 0.0, 0.5);
    }

    TEST(ActorRagdollPose, theActorsYawTurnsThePosedRagdollAsAWhole)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ActorStartPose pose = poseAt(0.0, 0.0, 800.0);
        const opas::math::Quat q = axisAngle(1.0, 0.0, 0.0, 0.5 * kPi);
        addBone(pose, "LeftUpperArm", q);
        const float yaw = 0.9f;
        const ActorRagdollHandle handle = spawnPosed(*host, pose, yaw);
        const RagdollBoneState upper = read(*host, handle)[indexOf(*host, "LeftUpperArm")];
        // The game's yaw is a clockwise turn about Z; the rig's world rotation is that turn after the pose rotation.
        const opas::math::Quat yawQ = axisAngle(0.0, 0.0, 1.0, -static_cast<double>(yaw));
        EXPECT_TRUE(sameRotation(rotationOf(upper), yawQ * q, 1e-3));
    }

    TEST(ActorRagdollPose, aPosedRagdollHangingInTheAirStaysCalmForAFewSteps)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        ActorStartPose pose = poseAt(0.0, 0.0, 2000.0);
        addBone(pose, "LeftUpperArm", axisAngle(1.0, 0.0, 0.0, 1.2));
        addBone(pose, "RightThigh", axisAngle(1.0, 0.0, 0.0, -0.8));
        addBone(pose, "Spine", axisAngle(0.0, 1.0, 0.0, 0.3));
        const ActorRagdollHandle handle = spawnPosed(*host, pose);
        stepMany(world, *host, 12);
        const MWPhysics::RagdollStatus status = host->actorStatus(handle);
        ASSERT_TRUE(status.present);
        // Free fall for 0.2 s is about 2 m/s; a joint that started out of its limits would throw a limb faster.
        EXPECT_LT(status.maxSpeedMetresPerSecond, 4.0f);
    }

    TEST(ActorRagdollPose, twoRunsOfTheSamePosedSpawnGiveIdenticalBodies)
    {
        std::vector<RagdollBoneState> first;
        std::vector<RagdollBoneState> second;
        for (std::vector<RagdollBoneState>* out : { &first, &second })
        {
            GameLikeWorld world;
            auto host = makeHost(world);
            ASSERT_NE(host, nullptr);
            ActorStartPose pose = poseAt(0.0, 0.0, 120.0);
            addBone(pose, "LeftUpperArm", axisAngle(1.0, 0.0, 0.0, 1.0));
            addBone(pose, "Chest", axisAngle(0.0, 0.0, 1.0, 0.4));
            const ActorRagdollHandle handle = spawnPosed(*host, pose);
            stepMany(world, *host, 90);
            *out = read(*host, handle);
        }
        ASSERT_EQ(first.size(), second.size());
        for (std::size_t i = 0; i < first.size(); ++i)
            for (int k = 0; k < 3; ++k)
                EXPECT_EQ(first[i].mPosition[k], second[i].mPosition[k]);
    }

    TEST(ActorRagdollPose, aPoseWithNoBonesIsTheRestPoseHungFromThePelvis)
    {
        GameLikeWorld world(false);
        auto host = makeHost(world);
        ASSERT_NE(host, nullptr);
        const ActorStartPose pose = poseAt(0.0, 0.0, 500.0);
        const ActorRagdollHandle handle = spawnPosed(*host, pose);
        const auto states = read(*host, handle);
        for (const RagdollBoneState& state : states)
            EXPECT_TRUE(sameRotation(rotationOf(state), opas::math::Quat{}, 1e-3));
        // The rig's feet are 0.95 - 0.01 - 0.43 - 0.43 = 0.08 m above the root, the pelvis 0.95 m.
        const double footZ = states[indexOf(*host, "LeftFoot")].mPosition[2];
        EXPECT_NEAR(footZ, 500.0 - (0.95 - 0.08) * kUnits, 1.0);
    }

    TEST(ActorRagdollPose, thePoseHoldsAtMostCMaxStartPoseBonesAndRefusesMore)
    {
        ActorStartPose pose;
        const double identity[4] = { 0.0, 0.0, 0.0, 1.0 };
        for (std::size_t i = 0; i < MWPhysics::cMaxStartPoseBones; ++i)
            EXPECT_TRUE(pose.add("Pelvis", identity));
        EXPECT_FALSE(pose.add("Spine", identity));
        EXPECT_EQ(pose.mCount, MWPhysics::cMaxStartPoseBones);
    }
}
