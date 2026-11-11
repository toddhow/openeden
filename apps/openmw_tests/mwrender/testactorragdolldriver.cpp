// Tests for the bone driver of task 0106 (stage b): apps/openmw/mwrender/actorragdolldriver.hpp. Written from the spec
// before the code existed.
//
// In plain words: given a ragdoll (a fake that returns whatever states a test sets) and a skeleton with the bones of a
// Morrowind human, the driver captures the offsets on its first look, and every apply() turns the mapped bones by what
// the ragdoll's bones have turned since, through BonePose.

#include "apps/openmw/mwphysics/ragdollhost.hpp"
#include "apps/openmw/mwrender/actorragdolldriver.hpp"
#include "apps/openmw/mwrender/bonepose.hpp"
#include "apps/openmw_tests/mwphysics/ragdolltestworld.hpp"
#include "apps/openmw_tests/support/allocationcounter.hpp"

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
#include <string>
#include <vector>

namespace
{
    using MWPhysics::RagdollBoneState;
    using MWRender::ActorRagdollDriver;
    using MWRender::BonePose;
    using MWRender::RagdollSource;

    // The rotation that applies inner first and then outer (vsg::dquat's own operator* is the other way round).
    vsg::dquat compose(const vsg::dquat& outer, const vsg::dquat& inner)
    {
        return inner * outer;
    }

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
        for (const vsg::dvec3 axis : { vsg::dvec3(1, 0, 0), vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, 1) })
        {
            const vsg::dvec3 ra = rotate(a, axis);
            const vsg::dvec3 rb = rotate(b, axis);
            if (std::abs(ra.x - rb.x) > tolerance || std::abs(ra.y - rb.y) > tolerance
                || std::abs(ra.z - rb.z) > tolerance)
                return ::testing::AssertionFailure()
                    << "rotations differ on axis (" << axis.x << ", " << axis.y << ", " << axis.z << "): (" << ra.x
                    << ", " << ra.y << ", " << ra.z << ") against (" << rb.x << ", " << rb.y << ", " << rb.z << ")";
        }
        return ::testing::AssertionSuccess();
    }

    vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        vsgUtil::setName(*t, name);
        return t;
    }

    // A Morrowind human's bones in their tree. Every bone has its own non-trivial local rotation and offset, so that
    // the engine's axes do not agree with the ragdoll's identity ones.
    struct Skeleton
    {
        vsg::ref_ptr<vsg::Group> mScene{ new vsg::Group };
        std::map<std::string, vsg::ref_ptr<Anim::Transform>> mBones;

        vsg::ref_ptr<Anim::Transform> add(const std::string& name, const std::string& parent, int index)
        {
            auto bone = makeBone(name);
            bone->translation = vsg::vec3(0.f, 0.f, 10.f + static_cast<float>(index));
            const double angle = 0.2 + 0.13 * index;
            const vsg::dvec3 axis(1.0 + (index % 3), 0.5 - (index % 2), 0.25 * index - 1.0);
            bone->setAttitude(vsg::quat(axisAngle(axis, angle)));
            mBones[name] = bone;
            if (parent.empty())
                mScene->children.push_back(bone);
            else
                mBones.at(parent)->children.push_back(bone);
            return bone;
        }

        explicit Skeleton(bool withRightFoot = true)
        {
            int i = 0;
            add("Bip01", "", i++);
            add("Bip01 Pelvis", "Bip01", i++);
            add("Bip01 Spine", "Bip01 Pelvis", i++);
            add("Bip01 Spine1", "Bip01 Spine", i++);
            add("Bip01 Neck", "Bip01 Spine1", i++);
            add("Bip01 Head", "Bip01 Neck", i++);
            add("Bip01 L Clavicle", "Bip01 Spine1", i++);
            add("Bip01 L UpperArm", "Bip01 L Clavicle", i++);
            add("Bip01 L Forearm", "Bip01 L UpperArm", i++);
            add("Bip01 L Hand", "Bip01 L Forearm", i++);
            add("Bip01 L Finger1", "Bip01 L Hand", i++);
            add("Bip01 R Clavicle", "Bip01 Spine1", i++);
            add("Bip01 R UpperArm", "Bip01 R Clavicle", i++);
            add("Bip01 R Forearm", "Bip01 R UpperArm", i++);
            add("Bip01 R Hand", "Bip01 R Forearm", i++);
            add("Bip01 L Thigh", "Bip01 Pelvis", i++);
            add("Bip01 L Calf", "Bip01 L Thigh", i++);
            add("Bip01 L Foot", "Bip01 L Calf", i++);
            add("Bip01 R Thigh", "Bip01 Pelvis", i++);
            add("Bip01 R Calf", "Bip01 R Thigh", i++);
            if (withRightFoot)
                add("Bip01 R Foot", "Bip01 R Calf", i++);
        }
    };

    // The actor's root transform: moved and turned, so that world and model space differ.
    vsg::ref_ptr<Anim::Transform> makeActorRoot()
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        t->translation = vsg::vec3(500.f, -300.f, 40.f);
        t->setAttitude(vsg::quat(axisAngle(vsg::dvec3(0, 0, 1), 0.9)));
        return t;
    }

    // A ragdoll whose states a test sets by hand.
    struct FakeRagdoll
    {
        std::vector<std::string> mNames;
        std::vector<RagdollBoneState> mStates;
        bool mAlive = true;
        mutable int mReads = 0;

        FakeRagdoll()
        {
            // As the embedded humanoid rig: it has no body for the neck.
            for (const auto& mapping : MWRender::ragdollBoneMappings())
                if (std::string(mapping.mRigBone) != "Neck")
                    mNames.push_back(mapping.mRigBone);
            mNames.push_back("LeftFingers"); // a body the driver does not use
            mStates.assign(mNames.size(), RagdollBoneState{});
            for (std::size_t i = 0; i < mStates.size(); ++i)
            {
                mStates[i].mPosition[0] = 100.0 + static_cast<double>(i);
                mStates[i].mPosition[1] = 200.0;
                mStates[i].mPosition[2] = 300.0 - static_cast<double>(i);
            }
        }

        std::size_t indexOf(const std::string& name) const
        {
            for (std::size_t i = 0; i < mNames.size(); ++i)
                if (mNames[i] == name)
                    return i;
            return mNames.size();
        }
    };

    class FakeSource final : public RagdollSource
    {
    public:
        explicit FakeSource(FakeRagdoll& ragdoll)
            : mRagdoll(ragdoll)
        {
        }
        std::span<const std::string> boneNames() const override { return mRagdoll.mNames; }
        bool read(std::span<RagdollBoneState> out) const override
        {
            ++mRagdoll.mReads;
            if (!mRagdoll.mAlive || out.size() < mRagdoll.mStates.size())
                return false;
            std::copy(mRagdoll.mStates.begin(), mRagdoll.mStates.end(), out.begin());
            return true;
        }

    private:
        FakeRagdoll& mRagdoll;
    };

    vsg::dquat worldRotationOf(const BonePose& pose, const std::string& name)
    {
        return Misc::VsgMath::rotationOf(pose.getWorldRotation(pose.find(name)));
    }

    vsg::dvec3 worldPositionOf(const BonePose& pose, const std::string& name)
    {
        return Misc::VsgMath::getTrans(pose.getWorld(pose.find(name)));
    }

    void turnAll(FakeRagdoll& ragdoll, const vsg::dquat& turn)
    {
        for (RagdollBoneState& state : ragdoll.mStates)
        {
            const vsg::dquat start(state.mRotation[0], state.mRotation[1], state.mRotation[2], state.mRotation[3]);
            const vsg::dquat now = compose(turn, start);
            state.mRotation[0] = now.x;
            state.mRotation[1] = now.y;
            state.mRotation[2] = now.z;
            state.mRotation[3] = now.w;
        }
    }

    struct Fixture
    {
        Skeleton mSkeleton;
        vsg::ref_ptr<Anim::Transform> mRoot = makeActorRoot();
        Anim::Bones mBones = MWAnim::Bones(*mSkeleton.mScene);
        FakeRagdoll mRagdoll;
        BonePose mPose{ mBones, mRoot.get() };

        std::unique_ptr<ActorRagdollDriver> driver()
        {
            return std::make_unique<ActorRagdollDriver>(mBones, mRoot.get(), std::make_unique<FakeSource>(mRagdoll));
        }
    };

    TEST(ActorRagdollDriver, theMappingNamesSeventeenBonesParentFirst)
    {
        const auto mappings = MWRender::ragdollBoneMappings();
        ASSERT_EQ(mappings.size(), 17u);
        EXPECT_STREQ(mappings[0].mRigBone, "Pelvis");
        EXPECT_STREQ(mappings[0].mEngineBone, "bip01 pelvis");
        // Every bone's engine parent (spine -> pelvis, ...) comes earlier in the table than the bone itself.
        std::map<std::string, std::size_t> position;
        for (std::size_t i = 0; i < mappings.size(); ++i)
            position[mappings[i].mEngineBone] = i;
        EXPECT_LT(position.at("bip01 pelvis"), position.at("bip01 spine"));
        EXPECT_LT(position.at("bip01 spine"), position.at("bip01 spine1"));
        EXPECT_LT(position.at("bip01 spine1"), position.at("bip01 l upperarm"));
        EXPECT_LT(position.at("bip01 l upperarm"), position.at("bip01 l forearm"));
        EXPECT_LT(position.at("bip01 l forearm"), position.at("bip01 l hand"));
        EXPECT_LT(position.at("bip01 pelvis"), position.at("bip01 l thigh"));
        EXPECT_LT(position.at("bip01 l thigh"), position.at("bip01 l calf"));
        EXPECT_LT(position.at("bip01 l calf"), position.at("bip01 l foot"));
    }

    TEST(ActorRagdollDriver, aCompleteSkeletonAndARagdollWithoutANeckBodyDriveSixteenBones)
    {
        Fixture f;
        auto driver = f.driver();
        EXPECT_TRUE(driver->isValid()) << driver->reason();
        EXPECT_EQ(driver->drivenBoneCount(), 16u);
    }

    TEST(ActorRagdollDriver, anActorWithoutAnOptionalMappedBoneIsDrivenWithoutIt)
    {
        Skeleton skeleton(false);
        auto root = makeActorRoot();
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        FakeRagdoll ragdoll;
        ActorRagdollDriver driver(bones, root.get(), std::make_unique<FakeSource>(ragdoll));
        EXPECT_TRUE(driver.isValid()) << driver.reason();
        EXPECT_EQ(driver.drivenBoneCount(), 15u);
        EXPECT_TRUE(driver.apply());
    }

    TEST(ActorRagdollDriver, anActorWithoutAPelvisGetsNoDriverAndAReason)
    {
        Skeleton skeleton;
        auto root = makeActorRoot();
        skeleton.mBones.at("Bip01")->children.clear(); // the pelvis and all below it leave the tree
        Anim::Bones without = MWAnim::Bones(*skeleton.mScene);
        FakeRagdoll ragdoll;
        ActorRagdollDriver driver(without, root.get(), std::make_unique<FakeSource>(ragdoll));
        EXPECT_FALSE(driver.isValid());
        EXPECT_NE(driver.reason().find("bip01 pelvis"), std::string::npos) << driver.reason();
        EXPECT_FALSE(driver.apply());
    }

    TEST(ActorRagdollDriver, aRagdollWithoutAPelvisBodyGetsNoDriverAndAReason)
    {
        Fixture f;
        f.mRagdoll.mNames[f.mRagdoll.indexOf("Pelvis")] = "SomethingElse";
        auto driver = f.driver();
        EXPECT_FALSE(driver->isValid());
        EXPECT_NE(driver->reason().find("Pelvis"), std::string::npos) << driver->reason();
    }

    TEST(ActorRagdollDriver, aRagdollWithTooFewMappedBodiesGetsNoDriver)
    {
        Fixture f;
        f.mRagdoll.mNames = { "Pelvis", "Spine", "Chest" };
        f.mRagdoll.mStates.resize(3);
        auto driver = f.driver();
        EXPECT_FALSE(driver->isValid());
        EXPECT_NE(driver->reason().find("only 3"), std::string::npos) << driver->reason();
    }

    TEST(ActorRagdollDriver, aBoneWithNoBodyKeepsItsAnimatedLocalRotationUnderItsDrivenParent)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        const Anim::Transform& neck = *f.mSkeleton.mBones.at("Bip01 Neck");
        const vsg::mat3 neckLocal = neck.rotation;
        const vsg::dquat neckStart = worldRotationOf(f.mPose, "bip01 neck");

        const vsg::dquat turn = axisAngle(vsg::dvec3(0, 0, 1), 0.8);
        turnAll(f.mRagdoll, turn);
        ASSERT_TRUE(driver->apply());
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                EXPECT_EQ(neck.rotation[i][j], neckLocal[i][j]);
        // Its parent (spine1) turned by the whole turn, so the neck turned with it.
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "bip01 neck"), compose(turn, neckStart)));
    }

    TEST(ActorRagdollDriver, aRagdollAlreadyGoneGivesNoDriver)
    {
        Fixture f;
        f.mRagdoll.mAlive = false;
        auto driver = f.driver();
        EXPECT_FALSE(driver->isValid());
    }

    TEST(ActorRagdollDriver, theSpawnFrameDrawsTheSamePoseTheAnimationHad)
    {
        Fixture f;
        std::map<std::string, vsg::dquat> rotation;
        std::map<std::string, vsg::dvec3> position;
        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            rotation[mapping.mEngineBone] = worldRotationOf(f.mPose, mapping.mEngineBone);
            position[mapping.mEngineBone] = worldPositionOf(f.mPose, mapping.mEngineBone);
        }
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        ASSERT_TRUE(driver->apply());
        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, mapping.mEngineBone), rotation.at(mapping.mEngineBone)))
                << mapping.mEngineBone;
            const vsg::dvec3 now = worldPositionOf(f.mPose, mapping.mEngineBone);
            const vsg::dvec3 before = position.at(mapping.mEngineBone);
            EXPECT_NEAR(now.x, before.x, 1e-2) << mapping.mEngineBone;
            EXPECT_NEAR(now.y, before.y, 1e-2) << mapping.mEngineBone;
            EXPECT_NEAR(now.z, before.z, 1e-2) << mapping.mEngineBone;
        }
    }

    TEST(ActorRagdollDriver, aRagdollTurnedAQuarterAboutWorldZTurnsEveryMappedBoneTheSameWay)
    {
        Fixture f;
        std::map<std::string, vsg::dquat> start;
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            start[mapping.mEngineBone] = worldRotationOf(f.mPose, mapping.mEngineBone);
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();

        const vsg::dquat turn = axisAngle(vsg::dvec3(0, 0, 1), 0.5 * 3.14159265358979);
        turnAll(f.mRagdoll, turn);
        ASSERT_TRUE(driver->apply());
        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            EXPECT_TRUE(
                sameRotation(worldRotationOf(f.mPose, mapping.mEngineBone), compose(turn, start.at(mapping.mEngineBone))))
                << mapping.mEngineBone;
        }
    }

    TEST(ActorRagdollDriver, oneRagdollBoneTurnedOnItsOwnMovesItsActorBoneAndItsChildrenOnly)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        const vsg::dquat headStart = worldRotationOf(f.mPose, "bip01 head");
        const vsg::dquat footStart = worldRotationOf(f.mPose, "bip01 l foot");
        const vsg::dquat spineStart = worldRotationOf(f.mPose, "bip01 spine1");

        const std::size_t head = f.mRagdoll.indexOf("Head");
        const vsg::dquat turn = axisAngle(vsg::dvec3(1, 0, 0), 0.6);
        RagdollBoneState& state = f.mRagdoll.mStates[head];
        const vsg::dquat now
            = compose(turn, vsg::dquat(state.mRotation[0], state.mRotation[1], state.mRotation[2], state.mRotation[3]));
        state.mRotation[0] = now.x;
        state.mRotation[1] = now.y;
        state.mRotation[2] = now.z;
        state.mRotation[3] = now.w;

        ASSERT_TRUE(driver->apply());
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "bip01 head"), compose(turn, headStart)));
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "bip01 l foot"), footStart));
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "bip01 spine1"), spineStart));
    }

    TEST(ActorRagdollDriver, bonesTheMappingDoesNotNameAreNotWritten)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        const Anim::Transform& clavicle = *f.mSkeleton.mBones.at("Bip01 L Clavicle");
        const Anim::Transform& finger = *f.mSkeleton.mBones.at("Bip01 L Finger1");
        const Anim::Transform& root = *f.mSkeleton.mBones.at("Bip01");
        const vsg::vec3 clavicleT = clavicle.translation;
        const vsg::mat3 clavicleR = clavicle.rotation;
        const vsg::mat3 fingerR = finger.rotation;
        const vsg::mat3 rootR = root.rotation;
        const vsg::vec3 rootT = root.translation;

        turnAll(f.mRagdoll, axisAngle(vsg::dvec3(0.3, 0.8, 0.1), 1.2));
        ASSERT_TRUE(driver->apply());
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_EQ(clavicle.translation[i], clavicleT[i]);
            EXPECT_EQ(root.translation[i], rootT[i]);
            for (int j = 0; j < 3; ++j)
            {
                EXPECT_EQ(clavicle.rotation[i][j], clavicleR[i][j]);
                EXPECT_EQ(finger.rotation[i][j], fingerR[i][j]);
                EXPECT_EQ(root.rotation[i][j], rootR[i][j]);
            }
        }
    }

    TEST(ActorRagdollDriver, thePelvisBoneFollowsTheRagdollPelvisDisplacementInWorldSpace)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        const vsg::dvec3 start = worldPositionOf(f.mPose, "bip01 pelvis");

        const std::size_t pelvis = f.mRagdoll.indexOf("Pelvis");
        f.mRagdoll.mStates[pelvis].mPosition[0] += 40.0;
        f.mRagdoll.mStates[pelvis].mPosition[1] -= 25.0;
        f.mRagdoll.mStates[pelvis].mPosition[2] -= 60.0;
        ASSERT_TRUE(driver->apply());
        const vsg::dvec3 now = worldPositionOf(f.mPose, "bip01 pelvis");
        EXPECT_NEAR(now.x - start.x, 40.0, 5e-2);
        EXPECT_NEAR(now.y - start.y, -25.0, 5e-2);
        EXPECT_NEAR(now.z - start.z, -60.0, 5e-2);
    }

    TEST(ActorRagdollDriver, whenTheRagdollIsGoneApplyReportsItAndTheBonesKeepTheLastPose)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        turnAll(f.mRagdoll, axisAngle(vsg::dvec3(0, 0, 1), 0.4));
        ASSERT_TRUE(driver->apply());
        const vsg::dquat written = worldRotationOf(f.mPose, "bip01 l hand");

        f.mRagdoll.mAlive = false;
        EXPECT_FALSE(driver->apply());
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "bip01 l hand"), written));
    }

    TEST(ActorRagdollDriver, applyAllocatesNothing)
    {
        Fixture f;
        auto driver = f.driver();
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        ASSERT_TRUE(driver->apply()); // warm up
        std::size_t allocations = 0;
        for (int i = 0; i < 100; ++i)
        {
            turnAll(f.mRagdoll, axisAngle(vsg::dvec3(0, 1, 0), 0.01));
            TestSupport::AllocationCounter counter;
            EXPECT_TRUE(driver->apply());
            allocations += counter.count();
        }
        EXPECT_EQ(allocations, 0u);
    }

    // The REAL rig: a driver reading a RagdollHost's actor ragdoll in a Jolt world, over the fake Morrowind skeleton.
    class HostSource final : public RagdollSource
    {
    public:
        HostSource(const MWPhysics::RagdollHost& host, MWPhysics::ActorRagdollHandle handle)
            : mHost(host)
            , mHandle(handle)
        {
        }
        std::span<const std::string> boneNames() const override { return mHost.actorBodyBones(); }
        bool read(std::span<RagdollBoneState> out) const override { return mHost.readActor(mHandle, out); }

    private:
        const MWPhysics::RagdollHost& mHost;
        MWPhysics::ActorRagdollHandle mHandle;
    };

    TEST(ActorRagdollDriver, theRealEmbeddedRigGivesAValidDriverAndTheSpawnFramePoseIsKept)
    {
        MWPhysicsTest::GameLikeWorld world;
        std::string error;
        auto host = MWPhysics::RagdollHost::create(world.system(), 70.0f, 1.0f / 60.0f, &error);
        ASSERT_NE(host, nullptr) << error;
        const double velocity[3] = { 0.0, 0.0, 0.0 };
        const MWPhysics::ActorRagdollHandle handle
            = host->spawnActor(JPH::RVec3(500.0, -300.0, 40.0), 0.9f, velocity, &error);
        ASSERT_TRUE(handle.isValid()) << error;

        Skeleton skeleton;
        auto root = makeActorRoot();
        Anim::Bones bones = MWAnim::Bones(*skeleton.mScene);
        BonePose pose(bones, root.get());
        std::map<std::string, vsg::dquat> start;
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            start[mapping.mEngineBone] = worldRotationOf(pose, mapping.mEngineBone);

        ActorRagdollDriver driver(bones, root.get(), std::make_unique<HostSource>(*host, handle));
        ASSERT_TRUE(driver.isValid()) << driver.reason();
        // 16 bodies: every mapped bone but the neck.
        EXPECT_EQ(driver.drivenBoneCount(), 16u);
        ASSERT_TRUE(driver.apply());
        for (const auto& mapping : MWRender::ragdollBoneMappings())
            EXPECT_TRUE(sameRotation(worldRotationOf(pose, mapping.mEngineBone), start.at(mapping.mEngineBone)))
                << mapping.mEngineBone;

        // The ragdoll falls and slumps: the pelvis bone moves down with it.
        const double startZ = worldPositionOf(pose, "bip01 pelvis").z;
        for (int i = 0; i < 120; ++i)
        {
            host->beginStep();
            world.update();
            host->endStep();
        }
        ASSERT_TRUE(driver.apply());
        EXPECT_LT(worldPositionOf(pose, "bip01 pelvis").z, startZ - 10.0);

        // When the ragdoll is removed the driver says so.
        ASSERT_TRUE(host->removeActor(handle));
        EXPECT_FALSE(driver.apply());
    }
}
