// Task 0263: the corpse pose that a settled ragdoll leaves, kept by the bone driver (drivenPose) and drawn again by an
// "absolute" driver after a load. Written from the spec before the code: what the driver wrote on its last frame can be
// read back in the layout of the ragdoll's states, and a driver given such a pose draws exactly it, whatever pose the
// skeleton had before.

#include "apps/openmw/mwphysics/ragdolltypes.hpp"
#include "apps/openmw/mwrender/actorragdolldriver.hpp"
#include "apps/openmw/mwrender/bonepose.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <algorithm>
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
        for (const vsg::dvec3& axis : { vsg::dvec3(1, 0, 0), vsg::dvec3(0, 1, 0), vsg::dvec3(0, 0, 1) })
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

    // Another pose for the same bones: what a death clip's last frame might look like against the standing pose above.
    void bendAll(Skeleton& skeleton)
    {
        int i = 0;
        for (auto& entry : skeleton.mBones)
        {
            entry.second->setAttitude(vsg::quat(axisAngle(vsg::dvec3(0.3, 1.0, 0.2 + 0.1 * i), 1.1 + 0.07 * i)));
            ++i;
        }
    }

    // States that differ bone from bone (a limp pile), so that a wrongly indexed read cannot pass by luck.
    void scramble(FakeRagdoll& ragdoll)
    {
        for (std::size_t i = 0; i < ragdoll.mStates.size(); ++i)
        {
            const double n = static_cast<double>(i);
            const vsg::dquat q = axisAngle(vsg::dvec3(1.0 + n, 0.5, -0.3), 0.3 + 0.21 * n);
            RagdollBoneState& state = ragdoll.mStates[i];
            state.mRotation[0] = q.x;
            state.mRotation[1] = q.y;
            state.mRotation[2] = q.z;
            state.mRotation[3] = q.w;
            state.mPosition[0] = 700.0 + 3.0 * n;
            state.mPosition[1] = -250.0 + n;
            state.mPosition[2] = 90.0 - 2.0 * n;
        }
    }

    struct CorpseFixture
    {
        Skeleton mSkeleton;
        vsg::ref_ptr<Anim::Transform> mRoot = makeActorRoot();
        Anim::Bones mBones = MWAnim::Bones(*mSkeleton.mScene);
        FakeRagdoll mRagdoll;
        BonePose mPose{ mBones, mRoot.get() };

        std::unique_ptr<ActorRagdollDriver> driver(bool absolute)
        {
            return std::make_unique<ActorRagdollDriver>(mBones, mRoot.get(), std::make_unique<FakeSource>(mRagdoll),
                MWRender::morrowindRagdollBones(), absolute);
        }
    };

    vsg::dquat rotationOfState(const RagdollBoneState& s)
    {
        return vsg::dquat(s.mRotation[0], s.mRotation[1], s.mRotation[2], s.mRotation[3]);
    }

    TEST(CorpsePoseDriver, thereIsNoDrivenPoseBeforeTheFirstFrame)
    {
        CorpseFixture f;
        auto driver = f.driver(false);
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        std::vector<RagdollBoneState> out(f.mRagdoll.mStates.size());
        EXPECT_FALSE(driver->drivenPose(out));
    }

    TEST(CorpsePoseDriver, aTooSmallSpanGetsNoPose)
    {
        CorpseFixture f;
        auto driver = f.driver(false);
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        scramble(f.mRagdoll);
        ASSERT_TRUE(driver->apply());
        std::vector<RagdollBoneState> out(f.mRagdoll.mStates.size() - 1);
        EXPECT_FALSE(driver->drivenPose(out));
    }

    TEST(CorpsePoseDriver, theDrivenPoseIsTheWorldRotationsAndThePelvisPositionThatWereDrawn)
    {
        CorpseFixture f;
        auto driver = f.driver(false);
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        scramble(f.mRagdoll);
        ASSERT_TRUE(driver->apply());
        std::vector<RagdollBoneState> out(f.mRagdoll.mStates.size());
        ASSERT_TRUE(driver->drivenPose(out));

        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            const std::size_t state = f.mRagdoll.indexOf(mapping.mRigBone);
            if (state == f.mRagdoll.mNames.size())
                continue; // the neck has no body
            EXPECT_TRUE(sameRotation(rotationOfState(out[state]), worldRotationOf(f.mPose, mapping.mEngineBone)))
                << mapping.mRigBone;
        }
        const std::size_t pelvis = f.mRagdoll.indexOf("Pelvis");
        const vsg::dvec3 drawn = worldPositionOf(f.mPose, "bip01 pelvis");
        EXPECT_NEAR(out[pelvis].mPosition[0], drawn.x, 1e-3);
        EXPECT_NEAR(out[pelvis].mPosition[1], drawn.y, 1e-3);
        EXPECT_NEAR(out[pelvis].mPosition[2], drawn.z, 1e-3);
    }

    TEST(CorpsePoseDriver, aBodyTheDriverDoesNotUseKeepsTheSourcesOwnState)
    {
        CorpseFixture f;
        auto driver = f.driver(false);
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        scramble(f.mRagdoll);
        ASSERT_TRUE(driver->apply());
        std::vector<RagdollBoneState> out(f.mRagdoll.mStates.size());
        ASSERT_TRUE(driver->drivenPose(out));
        const std::size_t fingers = f.mRagdoll.indexOf("LeftFingers");
        for (int k = 0; k < 3; ++k)
            EXPECT_EQ(out[fingers].mPosition[k], f.mRagdoll.mStates[fingers].mPosition[k]);
        for (int k = 0; k < 4; ++k)
            EXPECT_EQ(out[fingers].mRotation[k], f.mRagdoll.mStates[fingers].mRotation[k]);
    }

    TEST(CorpsePoseDriver, anAbsoluteDriverDrawsTheSavedPoseOnAnotherSkeletonPose)
    {
        // The first actor: a ragdoll settled in some pose, the driver wrote it, the world kept it.
        CorpseFixture first;
        auto driver = first.driver(false);
        ASSERT_TRUE(driver->isValid()) << driver->reason();
        scramble(first.mRagdoll);
        ASSERT_TRUE(driver->apply());
        std::vector<RagdollBoneState> saved(first.mRagdoll.mStates.size());
        ASSERT_TRUE(driver->drivenPose(saved));

        // After a load: the same actor stands in another pose (the death clip's last frame); the saved pose is armed.
        CorpseFixture second;
        bendAll(second.mSkeleton);
        second.mRagdoll.mStates = saved;
        auto frozen = second.driver(true);
        ASSERT_TRUE(frozen->isValid()) << frozen->reason();
        ASSERT_TRUE(frozen->apply());

        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            if (first.mRagdoll.indexOf(mapping.mRigBone) == first.mRagdoll.mNames.size())
                continue;
            EXPECT_TRUE(sameRotation(worldRotationOf(second.mPose, mapping.mEngineBone),
                worldRotationOf(first.mPose, mapping.mEngineBone)))
                << mapping.mRigBone;
        }
        const vsg::dvec3 a = worldPositionOf(first.mPose, "bip01 pelvis");
        const vsg::dvec3 b = worldPositionOf(second.mPose, "bip01 pelvis");
        EXPECT_NEAR(a.x, b.x, 1e-2);
        EXPECT_NEAR(a.y, b.y, 1e-2);
        EXPECT_NEAR(a.z, b.z, 1e-2);
    }

    TEST(CorpsePoseDriver, anAbsoluteDriverKeepsDrawingTheSamePoseFrameAfterFrame)
    {
        CorpseFixture f;
        scramble(f.mRagdoll);
        auto frozen = f.driver(true);
        ASSERT_TRUE(frozen->isValid()) << frozen->reason();
        ASSERT_TRUE(frozen->apply());
        const vsg::dquat once = worldRotationOf(f.mPose, "bip01 l hand");
        const vsg::dvec3 pelvisOnce = worldPositionOf(f.mPose, "bip01 pelvis");
        for (int i = 0; i < 5; ++i)
            ASSERT_TRUE(frozen->apply());
        EXPECT_TRUE(sameRotation(once, worldRotationOf(f.mPose, "bip01 l hand")));
        const vsg::dvec3 pelvisLater = worldPositionOf(f.mPose, "bip01 pelvis");
        EXPECT_NEAR(pelvisOnce.z, pelvisLater.z, 1e-3);
    }

    TEST(CorpsePoseDriver, anAbsoluteDriverGivesTheSavedPoseBackWhenAskedAgain)
    {
        // A corpse restored from a save is saved again at the next save.
        CorpseFixture f;
        scramble(f.mRagdoll);
        const std::vector<RagdollBoneState> saved = f.mRagdoll.mStates;
        auto frozen = f.driver(true);
        ASSERT_TRUE(frozen->isValid()) << frozen->reason();
        ASSERT_TRUE(frozen->apply());
        std::vector<RagdollBoneState> out(saved.size());
        ASSERT_TRUE(frozen->drivenPose(out));
        for (const auto& mapping : MWRender::ragdollBoneMappings())
        {
            const std::size_t state = f.mRagdoll.indexOf(mapping.mRigBone);
            if (state == f.mRagdoll.mNames.size())
                continue;
            EXPECT_TRUE(sameRotation(rotationOfState(saved[state]), rotationOfState(out[state]))) << mapping.mRigBone;
        }
    }
}
