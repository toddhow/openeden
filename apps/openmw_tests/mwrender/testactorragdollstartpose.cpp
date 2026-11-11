// Tests for task 0106, second slice (1): reading the actor's pose at death for its ragdoll
// (apps/openmw/mwrender/actorragdolldriver.hpp: ActorRagdollRest, makeActorStartPose) and the rest-relative rotation
// of apps/openmw/mwrender/ragdollbonemath.hpp. Written from the spec before the code.
//
// In plain words: when an actor's pose controllers are made its bones are in their rest pose; that is remembered. At
// death each mapped bone's rotation is compared with its rest, and the ragdoll's bone is turned by the difference, so
// the ragdoll starts in the pose the actor died in. The first frame then draws exactly that pose.

#include "apps/openmw/mwphysics/ragdollhost.hpp"
#include "apps/openmw/mwrender/actorragdolldriver.hpp"
#include "apps/openmw/mwrender/bonepose.hpp"
#include "apps/openmw/mwrender/ragdollbonemath.hpp"
#include "apps/openmw_tests/mwphysics/ragdolltestworld.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
    using MWRender::ActorRagdollDriver;
    using MWRender::ActorRagdollRest;
    using MWRender::BonePose;
    namespace RB = MWRender::RagdollBones;

    vsg::dquat axisAngle(const vsg::dvec3& axis, double angle)
    {
        const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        const double s = std::sin(0.5 * angle) / length;
        return vsg::dquat(axis.x * s, axis.y * s, axis.z * s, std::cos(0.5 * angle));
    }

    vsg::dvec3 rotate(const vsg::dquat& q, const vsg::dvec3& v)
    {
        const vsg::dvec3 u(q.x, q.y, q.z);
        const vsg::dvec3 t = 2.0 * vsg::cross(u, v);
        return v + q.w * t + vsg::cross(u, t);
    }

    ::testing::AssertionResult sameRotation(const vsg::dquat& a, const vsg::dquat& b, double tolerance = 1e-4)
    {
        for (const vsg::dvec3& axis : { vsg::dvec3(1, 0, 0), vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, 1) })
        {
            const vsg::dvec3 ra = rotate(a, axis);
            const vsg::dvec3 rb = rotate(b, axis);
            if (std::abs(ra.x - rb.x) > tolerance || std::abs(ra.y - rb.y) > tolerance
                || std::abs(ra.z - rb.z) > tolerance)
                return ::testing::AssertionFailure() << "rotations differ on axis (" << axis.x << ", " << axis.y << ", "
                                                     << axis.z << ")";
        }
        return ::testing::AssertionSuccess();
    }

    vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        vsgUtil::setName(*t, name);
        return t;
    }

    // As the driver test's Morrowind human: every bone has its own rotation and offset.
    struct Skeleton
    {
        vsg::ref_ptr<vsg::Group> mScene{ new vsg::Group };
        std::map<std::string, vsg::ref_ptr<Anim::Transform>> mBones;
        std::map<std::string, vsg::dquat> mLocal;

        void add(const std::string& name, const std::string& parent, int index)
        {
            auto bone = makeBone(name);
            bone->translation = vsg::vec3(0.f, 0.f, 10.f + static_cast<float>(index));
            const double angle = 0.2 + 0.13 * index;
            const vsg::dvec3 axis(1.0 + (index % 3), 0.5 - (index % 2), 0.25 * index - 1.0);
            mLocal[name] = axisAngle(axis, angle);
            bone->setAttitude(vsg::quat(mLocal[name]));
            mBones[name] = bone;
            if (parent.empty())
                mScene->children.push_back(bone);
            else
                mBones.at(parent)->children.push_back(bone);
        }

        explicit Skeleton(bool withPelvis = true)
        {
            int i = 0;
            add("Bip01", "", i++);
            if (withPelvis)
                add("Bip01 Pelvis", "Bip01", i++);
            const std::string pelvisParent = withPelvis ? "Bip01 Pelvis" : "Bip01";
            add("Bip01 Spine", pelvisParent, i++);
            add("Bip01 Spine1", "Bip01 Spine", i++);
            add("Bip01 Neck", "Bip01 Spine1", i++);
            add("Bip01 Head", "Bip01 Neck", i++);
            add("Bip01 L Clavicle", "Bip01 Spine1", i++);
            add("Bip01 L UpperArm", "Bip01 L Clavicle", i++);
            add("Bip01 L Forearm", "Bip01 L UpperArm", i++);
            add("Bip01 L Hand", "Bip01 L Forearm", i++);
            add("Bip01 R Clavicle", "Bip01 Spine1", i++);
            add("Bip01 R UpperArm", "Bip01 R Clavicle", i++);
            add("Bip01 R Forearm", "Bip01 R UpperArm", i++);
            add("Bip01 R Hand", "Bip01 R Forearm", i++);
            add("Bip01 L Thigh", pelvisParent, i++);
            add("Bip01 L Calf", "Bip01 L Thigh", i++);
            add("Bip01 L Foot", "Bip01 L Calf", i++);
            add("Bip01 R Thigh", pelvisParent, i++);
            add("Bip01 R Calf", "Bip01 R Thigh", i++);
            add("Bip01 R Foot", "Bip01 R Calf", i++);
        }

        // Turns a bone's local rotation by `angle` about `axis` of its own frame (applied after the old rotation).
        void turnLocal(const std::string& name, const vsg::dvec3& axis, double angle)
        {
            mLocal[name] = RB::compose(mLocal[name], axisAngle(axis, angle));
            mBones.at(name)->setAttitude(vsg::quat(mLocal[name]));
        }
    };

    vsg::ref_ptr<Anim::Transform> makeActorRoot(double yaw = 0.9)
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        t->translation = vsg::vec3(500.f, -300.f, 40.f);
        t->setAttitude(vsg::quat(axisAngle(vsg::dvec3(0, 0, 1), yaw)));
        return t;
    }

    vsg::dquat modelRotationOf(Anim::Bones& bones, const std::string& name)
    {
        BonePose pose(bones, nullptr);
        return Misc::VsgMath::rotationOf(pose.getWorldRotation(pose.find(name)));
    }

    std::size_t mappingIndex(const std::string& rigBone)
    {
        const auto mappings = MWRender::ragdollBoneMappings();
        for (std::size_t i = 0; i < mappings.size(); ++i)
            if (rigBone == mappings[i].mRigBone)
                return i;
        ADD_FAILURE() << "no mapping for " << rigBone;
        return 0;
    }

    vsg::dquat startRotation(const MWPhysics::ActorStartPose& pose, const std::string& rigBone)
    {
        for (std::size_t i = 0; i < pose.mCount; ++i)
            if (pose.mBones[i].mRigBone == rigBone)
            {
                const double* r = pose.mBones[i].mRotation;
                return vsg::dquat(r[0], r[1], r[2], r[3]);
            }
        ADD_FAILURE() << "the start pose names no " << rigBone;
        return vsg::dquat(0, 0, 0, 1);
    }

    TEST(RagdollRestRelative, theRestRelativeRotationAfterTheRestGivesTheActorBone)
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> unit(-1.0, 1.0);
        for (int i = 0; i < 200; ++i)
        {
            const vsg::dquat actor = axisAngle(vsg::dvec3(unit(rng), unit(rng), unit(rng) + 2.0), 3.0 * unit(rng));
            const vsg::dquat rest = axisAngle(vsg::dvec3(unit(rng), unit(rng) + 2.0, unit(rng)), 3.0 * unit(rng));
            const vsg::dquat q = RB::restRelative(actor, rest);
            // The driver's own relation: actor = ragdoll bone "after" the offset, with the offset being the rest.
            EXPECT_TRUE(sameRotation(RB::drivenWorldRotation(q, rest), actor, 1e-9)) << i;
        }
    }

    TEST(RagdollRestRelative, anActorBoneAtRestIsTheIdentityRelativeToItsRest)
    {
        const vsg::dquat rest = axisAngle(vsg::dvec3(1, 2, 3), 1.1);
        EXPECT_TRUE(sameRotation(RB::restRelative(rest, rest), vsg::dquat(0, 0, 0, 1), 1e-9));
    }

    TEST(ActorRagdollRest, readsTheMappedBonesModelSpaceRotationsAndIsValid)
    {
        Skeleton skeleton;
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        const ActorRagdollRest rest(bones);
        ASSERT_TRUE(rest.isValid());
        const auto mappings = MWRender::ragdollBoneMappings();
        for (std::size_t i = 0; i < mappings.size(); ++i)
        {
            ASSERT_TRUE(rest.has(i)) << mappings[i].mEngineBone;
            EXPECT_TRUE(sameRotation(rest.rotation(i), modelRotationOf(bones, mappings[i].mEngineBone)))
                << mappings[i].mEngineBone;
        }
    }

    TEST(ActorRagdollRest, aSkeletonWithoutAPelvisIsNotValid)
    {
        Skeleton skeleton(false);
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        EXPECT_FALSE(ActorRagdollRest(bones).isValid());
    }

    TEST(ActorStartPose, aSkeletonStillInItsRestPoseGivesIdentityRotationsAtThePelvis)
    {
        Skeleton skeleton;
        auto root = makeActorRoot();
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        const ActorRagdollRest rest(bones);
        MWPhysics::ActorStartPose pose;
        ASSERT_TRUE(MWRender::makeActorStartPose(bones, root.get(), rest, pose));
        EXPECT_EQ(pose.mCount, 17u);
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            EXPECT_TRUE(sameRotation(startRotation(pose, mapping.mRigBone), vsg::dquat(0, 0, 0, 1)))
                << mapping.mRigBone;
        BonePose worldPose(bones, root.get());
        const vsg::dvec3 pelvis = Misc::VsgMath::getTrans(worldPose.getWorld(worldPose.find("bip01 pelvis")));
        EXPECT_NEAR(pose.mPelvisPosition[0], pelvis.x, 1e-3);
        EXPECT_NEAR(pose.mPelvisPosition[1], pelvis.y, 1e-3);
        EXPECT_NEAR(pose.mPelvisPosition[2], pelvis.z, 1e-3);
    }

    TEST(ActorStartPose, aTurnedArmGivesANonIdentityRotationForItselfAndItsChildrenOnly)
    {
        Skeleton skeleton;
        auto root = makeActorRoot();
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        const ActorRagdollRest rest(bones);
        skeleton.turnLocal("Bip01 L UpperArm", vsg::dvec3(0, 1, 0), 1.0);
        MWPhysics::ActorStartPose pose;
        ASSERT_TRUE(MWRender::makeActorStartPose(bones, root.get(), rest, pose));
        const vsg::dquat upper = startRotation(pose, "LeftUpperArm");
        EXPECT_FALSE(sameRotation(upper, vsg::dquat(0, 0, 0, 1)));
        // Below it, the whole arm turned by the same world rotation.
        EXPECT_TRUE(sameRotation(startRotation(pose, "LeftForearm"), upper));
        EXPECT_TRUE(sameRotation(startRotation(pose, "LeftHand"), upper));
        // Elsewhere nothing changed.
        EXPECT_TRUE(sameRotation(startRotation(pose, "RightUpperArm"), vsg::dquat(0, 0, 0, 1)));
        EXPECT_TRUE(sameRotation(startRotation(pose, "Pelvis"), vsg::dquat(0, 0, 0, 1)));
        EXPECT_TRUE(sameRotation(startRotation(pose, "LeftThigh"), vsg::dquat(0, 0, 0, 1)));
    }

    TEST(ActorStartPose, theRotationsDoNotDependOnWhereTheActorStandsOrWhichWayItFaces)
    {
        Skeleton skeleton;
        auto rootA = makeActorRoot(0.9);
        auto rootB = makeActorRoot(-2.0);
        rootB->translation = vsg::vec3(-4000.f, 77.f, 5.f);
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        const ActorRagdollRest rest(bones);
        skeleton.turnLocal("Bip01 R Thigh", vsg::dvec3(1, 0, 0), -0.8);
        MWPhysics::ActorStartPose a;
        MWPhysics::ActorStartPose b;
        ASSERT_TRUE(MWRender::makeActorStartPose(bones, rootA.get(), rest, a));
        ASSERT_TRUE(MWRender::makeActorStartPose(bones, rootB.get(), rest, b));
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            EXPECT_TRUE(sameRotation(startRotation(a, mapping.mRigBone), startRotation(b, mapping.mRigBone)))
                << mapping.mRigBone;
        // The pelvis position is a world position: it differs.
        EXPECT_GT(std::abs(a.mPelvisPosition[0] - b.mPelvisPosition[0]), 100.0);
    }

    TEST(ActorStartPose, anInvalidRestOrAMissingPelvisGivesNoPose)
    {
        Skeleton noPelvis(false);
        Anim::Bones bonesNo = MWAnim::Bones(*noPelvis.mScene);
        const ActorRagdollRest restNo(bonesNo);
        Skeleton skeleton;
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        MWPhysics::ActorStartPose pose;
        pose.mCount = 3;
        EXPECT_FALSE(MWRender::makeActorStartPose(bonesNo, nullptr, restNo, pose));
        EXPECT_EQ(pose.mCount, 3u); // untouched
        const ActorRagdollRest rest(bones);
        EXPECT_FALSE(MWRender::makeActorStartPose(bonesNo, nullptr, rest, pose)); // the actor lacks its pelvis
        EXPECT_EQ(pose.mCount, 3u);
    }

    class HostSource final : public MWRender::RagdollSource
    {
    public:
        HostSource(const MWPhysics::RagdollHost& host, MWPhysics::ActorRagdollHandle handle)
            : mHost(host)
            , mHandle(handle)
        {
        }
        std::span<const std::string> boneNames() const override { return mHost.actorBodyBones(); }
        bool read(std::span<MWPhysics::RagdollBoneState> out) const override { return mHost.readActor(mHandle, out); }

    private:
        const MWPhysics::RagdollHost& mHost;
        MWPhysics::ActorRagdollHandle mHandle;
    };

    // The whole chain on the real rig: rest, a posed actor, the start pose, the host, the driver.
    TEST(ActorStartPose, aRagdollStartedFromTheActorsPoseHasEachBoneWhereTheActorsRestAndPoseSay)
    {
        MWPhysicsTest::GameLikeWorld world(false);
        std::string error;
        auto host = MWPhysics::RagdollHost::create(world.system(), 70.0f, 1.0f / 60.0f, &error);
        ASSERT_NE(host, nullptr) << error;

        Skeleton skeleton;
        const double yaw = 0.9;
        auto root = makeActorRoot(yaw);
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        const ActorRagdollRest rest(bones);
        skeleton.turnLocal("Bip01 L UpperArm", vsg::dvec3(0, 1, 0), 1.0);
        skeleton.turnLocal("Bip01 R Thigh", vsg::dvec3(1, 0, 0), -0.6);
        skeleton.turnLocal("Bip01 Spine", vsg::dvec3(0, 0, 1), 0.4);

        MWPhysics::ActorStartPose pose;
        ASSERT_TRUE(MWRender::makeActorStartPose(bones, root.get(), rest, pose));
        const double velocity[3] = { 0, 0, 0 };
        // The actor's root turns by +yaw about Z; the game's rot[2] is the clockwise turn, hence -yaw.
        const MWPhysics::ActorRagdollHandle handle
            = host->spawnActor(JPH::RVec3(500.0, -300.0, 40.0), static_cast<float>(-yaw), velocity, &error, &pose);
        ASSERT_TRUE(handle.isValid()) << error;

        std::vector<MWPhysics::RagdollBoneState> states(host->actorBodyBones().size());
        ASSERT_TRUE(host->readActor(handle, states));
        BonePose worldPose(bones, root.get());
        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            std::size_t state = states.size();
            for (std::size_t i = 0; i < states.size(); ++i)
                if (host->actorBodyBones()[i] == mapping.mRigBone)
                    state = i;
            if (state == states.size())
                continue; // the neck has no body
            const vsg::dquat ragdollBone(states[state].mRotation[0], states[state].mRotation[1],
                states[state].mRotation[2], states[state].mRotation[3]);
            const vsg::dquat actorWorld
                = Misc::VsgMath::rotationOf(worldPose.getWorldRotation(worldPose.find(mapping.mEngineBone)));
            // The actor's world rotation is the ragdoll bone's with the bone's rest rotation applied first.
            const vsg::dquat predicted = RB::compose(ragdollBone, rest.rotation(mappingIndex(mapping.mRigBone)));
            EXPECT_TRUE(sameRotation(predicted, actorWorld, 2e-3)) << mapping.mRigBone;
        }

        // And the driver's first frame is the actor's pose.
        ActorRagdollDriver driver(bones, root.get(), std::make_unique<HostSource>(*host, handle));
        ASSERT_TRUE(driver.isValid()) << driver.reason();
        std::map<std::string, vsg::dquat> before;
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            before[mapping.mEngineBone]
                = Misc::VsgMath::rotationOf(worldPose.getWorldRotation(worldPose.find(mapping.mEngineBone)));
        ASSERT_TRUE(driver.apply());
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            EXPECT_TRUE(sameRotation(
                Misc::VsgMath::rotationOf(worldPose.getWorldRotation(worldPose.find(mapping.mEngineBone))),
                before.at(mapping.mEngineBone), 2e-3))
                << mapping.mEngineBone;
    }
}
