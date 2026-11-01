// Tests for the foot IK maths (task 0011, found broken on screen in the fix that added this file: foot planting
// exploded an NPC's mesh and had only ever been checked in its off state). They need no GPU and no world: a leg of
// Anim::Transform bones with Morrowind-sized offsets is solved with MWRender::FootIk and written back through
// MWRender::BonePose exactly as ActorIkController::solveLeg does it.
//
// Conventions: VSG column-vector matrices, world = parent * local (Anim::Transform::t_transform composes `in * local`).

#include "apps/openmw/mwrender/bonepose.hpp"
#include "apps/openmw/mwrender/footikmath.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/maths/transform.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace
{
    using MWRender::BonePose;
    namespace FootIk = MWRender::FootIk;

    vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        vsgUtil::setName(*t, name);
        return t;
    }

    // Rotation of @a angle radians about a unit axis, as a bone's local rotation.
    void setAngle(Anim::Transform& bone, float angle, const vsg::vec3& axis)
    {
        const double half = 0.5 * angle;
        const double s = std::sin(half);
        bone.setAttitude(vsg::quat(static_cast<float>(axis.x * s), static_cast<float>(axis.y * s),
            static_cast<float>(axis.z * s), static_cast<float>(std::cos(half))));
    }

    // actor root -> "Bip01" -> "Bip01 Pelvis" -> thigh -> calf -> foot, with Morrowind-like bone axes (x runs along the
    // bone, so the pelvis is turned to make x point down) and lengths (thigh ~46, calf ~44 units).
    struct Leg
    {
        vsg::ref_ptr<vsg::Group> mScene{ new vsg::Group };
        vsg::ref_ptr<Anim::Transform> mActor = makeBone("actor");
        vsg::ref_ptr<Anim::Transform> mBip = makeBone("Bip01");
        vsg::ref_ptr<Anim::Transform> mPelvis = makeBone("Bip01 Pelvis");
        vsg::ref_ptr<Anim::Transform> mThigh = makeBone("Bip01 L Thigh");
        vsg::ref_ptr<Anim::Transform> mCalf = makeBone("Bip01 L Calf");
        vsg::ref_ptr<Anim::Transform> mFoot = makeBone("Bip01 L Foot");

        explicit Leg(const vsg::vec3& actorPosition = vsg::vec3(4096.f * 3, -2048.f, 150.f), float actorYaw = 0.5f)
        {
            mActor->translation = actorPosition;
            setAngle(*mActor, actorYaw, vsg::vec3(0.f, 0.f, 1.f));

            mBip->translation = vsg::vec3(0.f, 0.f, 0.f);

            mPelvis->translation = vsg::vec3(0.f, 0.f, 90.f);
            setAngle(*mPelvis, 1.5707963f, vsg::vec3(0.f, 1.f, 0.f)); // x -> -z

            mThigh->translation = vsg::vec3(0.f, -8.f, 0.f);
            setAngle(*mThigh, 0.15f, vsg::vec3(0.f, 0.f, 1.f));

            mCalf->translation = vsg::vec3(46.f, 0.f, 0.f);
            setAngle(*mCalf, -0.9f, vsg::vec3(0.f, 0.f, 1.f));

            mFoot->translation = vsg::vec3(44.f, 0.f, 0.f);
            setAngle(*mFoot, 0.7f, vsg::vec3(0.f, 0.f, 1.f));

            mCalf->children.push_back(mFoot);
            mThigh->children.push_back(mCalf);
            mPelvis->children.push_back(mThigh);
            mBip->children.push_back(mPelvis);
            mScene->children.push_back(mBip);
        }
    };

    struct Fixture
    {
        Leg mLeg;
        Anim::Bones mBones{ MWAnim::Bones(*mLeg.mScene) };
        BonePose mPose{ mBones, mLeg.mActor.get() };
        BonePose::Bone mThigh = mPose.find("Bip01 L Thigh");
        BonePose::Bone mCalf = mPose.find("Bip01 L Calf");
        BonePose::Bone mFoot = mPose.find("Bip01 L Foot");

        FootIk::LegWorld world() const
        {
            return { mPose.getWorld(mThigh), mPose.getWorld(mCalf), mPose.getWorld(mFoot) };
        }

        vsg::vec3 pos(const BonePose::Bone& bone) const
        {
            return vsg::vec3(Misc::VsgMath::getTrans(mPose.getWorld(bone)));
        }

        // Mirrors ActorIkController::solveLeg's write-back: rotations only.
        bool solveAndWrite(const vsg::vec3& target, const vsg::vec3& pole, const vsg::vec3& normal, float weight)
        {
            const FootIk::LegSolution s = FootIk::solveLegPose(world(), target, pole, normal, weight);
            if (!s.mValid)
                return false;
            const FootIk::LegLocalRotations r = FootIk::solveLocalRotations(s,
                { mPose.getParentWorldRotation(mThigh), mPose.getWorldRotation(mThigh),
                    mPose.getWorldRotation(mCalf), mPose.getWorldRotation(mFoot) });
            mPose.setLocalRotation(mThigh, Misc::VsgMath::rotationOf(r.mThigh));
            mPose.setLocalRotation(mCalf, Misc::VsgMath::rotationOf(r.mCalf));
            mPose.setLocalRotation(mFoot, Misc::VsgMath::rotationOf(r.mFoot));
            return true;
        }
    };

    vsg::vec3 poleOf(const Fixture& f)
    {
        const vsg::vec3 hip = f.pos(f.mThigh);
        const vsg::vec3 knee = f.pos(f.mCalf);
        const vsg::vec3 ankle = f.pos(f.mFoot);
        return knee - (hip + ankle) * 0.5f;
    }

    bool finite(const vsg::mat4& m)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!std::isfinite(m[i][j]))
                    return false;
        return true;
    }

    // The rotation angle between the rotation blocks of two matrices.
    float angleBetween(const vsg::mat4& a, const vsg::mat4& b)
    {
        const vsg::dquat qa = Misc::VsgMath::rotationOf(a);
        const vsg::dquat qb = Misc::VsgMath::rotationOf(b);
        const double dot = std::abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
            / std::sqrt((qa.x * qa.x + qa.y * qa.y + qa.z * qa.z + qa.w * qa.w)
                * (qb.x * qb.x + qb.y * qb.y + qb.z * qb.z + qb.w * qb.w));
        return static_cast<float>(2.0 * std::acos(std::min(1.0, dot)));
    }

    TEST(MWRenderFootIkTest, ChainIsAHangingLegWithRealisticLengths)
    {
        Fixture f;
        const vsg::vec3 hip = f.pos(f.mThigh);
        const vsg::vec3 knee = f.pos(f.mCalf);
        const vsg::vec3 ankle = f.pos(f.mFoot);
        EXPECT_NEAR(vsg::length(knee - hip), 46.f, 0.05f);
        EXPECT_NEAR(vsg::length(ankle - knee), 44.f, 0.05f);
        EXPECT_LT(ankle.z, hip.z - 60.f);
    }

    TEST(MWRenderFootIkTest, TargetAtCurrentAnklePreservesThePose)
    {
        Fixture f;
        const FootIk::LegWorld before = f.world();
        const vsg::vec3 ankle = f.pos(f.mFoot);
        ASSERT_TRUE(f.solveAndWrite(ankle, poleOf(f), vsg::vec3(0.f, 0.f, 1.f), 1.f));
        const FootIk::LegWorld after = f.world();
        EXPECT_LT(angleBetween(before.mThigh, after.mThigh), 0.01f);
        EXPECT_LT(angleBetween(before.mCalf, after.mCalf), 0.01f);
        EXPECT_LT(angleBetween(before.mFoot, after.mFoot), 0.01f);
        EXPECT_LT(vsg::length(f.pos(f.mFoot) - ankle), 0.1f);
    }

    TEST(MWRenderFootIkTest, TargetAtCurrentAnkleWithBiasedPoleKeepsRotationsClose)
    {
        Fixture f;
        const FootIk::LegWorld before = f.world();
        const vsg::vec3 ankle = f.pos(f.mFoot);
        const vsg::vec3 pole = poleOf(f) + vsg::vec3(0.f, 8.f, 0.f);
        ASSERT_TRUE(f.solveAndWrite(ankle, pole, vsg::vec3(0.f, 0.f, 1.f), 1.f));
        const FootIk::LegWorld after = f.world();
        EXPECT_LT(angleBetween(before.mThigh, after.mThigh), 0.4f);
        EXPECT_LT(angleBetween(before.mCalf, after.mCalf), 0.6f);
        EXPECT_LT(vsg::length(f.pos(f.mFoot) - ankle), 0.1f);
    }

    TEST(MWRenderFootIkTest, SolutionKeepsBoneLengthsAndStaysFinite)
    {
        Fixture f;
        const vsg::vec3 ankle = f.pos(f.mFoot);
        for (const float dz : { -12.f, -3.f, 0.f, 4.f, 15.f })
        {
            Fixture g;
            ASSERT_TRUE(g.solveAndWrite(ankle + vsg::vec3(0.f, 0.f, dz), poleOf(g), vsg::vec3(0.f, 0.f, 1.f), 1.f));
            for (const auto* bone : { &g.mThigh, &g.mCalf, &g.mFoot })
            {
                EXPECT_TRUE(finite(g.mPose.getLocal(*bone))) << dz;
                EXPECT_TRUE(finite(g.mPose.getWorld(*bone))) << dz;
                EXPECT_LT(std::abs(g.mPose.getLocal(*bone)[3][0]), 200.f) << dz;
            }
            const float thigh = vsg::length(g.pos(g.mCalf) - g.pos(g.mThigh));
            const float calf = vsg::length(g.pos(g.mFoot) - g.pos(g.mCalf));
            EXPECT_NEAR(thigh, 46.f, 0.1f) << dz;
            EXPECT_NEAR(calf, 44.f, 0.1f) << dz;
        }
    }

    TEST(MWRenderFootIkTest, AnkleFollowsTheGroundAndKneeStaysBetweenHipAndAnkle)
    {
        const vsg::vec3 ankle0 = Fixture().pos(Fixture().mFoot);
        for (const float dz : { -8.f, -3.f, 3.f, 6.f })
        {
            Fixture g;
            const vsg::vec3 target = ankle0 + vsg::vec3(0.f, 0.f, dz);
            ASSERT_TRUE(g.solveAndWrite(target, poleOf(g), vsg::vec3(0.f, 0.f, 1.f), 1.f));
            EXPECT_LT(vsg::length(g.pos(g.mFoot) - target), 0.1f) << dz;
            const vsg::vec3 hip = g.pos(g.mThigh);
            const vsg::vec3 knee = g.pos(g.mCalf);
            const vsg::vec3 ankle = g.pos(g.mFoot);
            EXPECT_LT(knee.z, hip.z) << dz;
            EXPECT_GT(knee.z, ankle.z) << dz;
        }
    }

    TEST(MWRenderFootIkTest, WrittenRotationsComposeToTheSolvedJointPositions)
    {
        Fixture f;
        const vsg::vec3 target = f.pos(f.mFoot) + vsg::vec3(0.f, 0.f, -5.f);
        const FootIk::LegSolution s
            = FootIk::solveLegPose(f.world(), target, poleOf(f), vsg::vec3(0.f, 0.2f, 0.98f), 1.f);
        ASSERT_TRUE(s.mValid);
        ASSERT_TRUE(f.solveAndWrite(target, poleOf(f), vsg::vec3(0.f, 0.2f, 0.98f), 1.f));
        EXPECT_LT(vsg::length(f.pos(f.mCalf) - s.mNewKnee), 0.1f);
        EXPECT_LT(vsg::length(f.pos(f.mFoot) - s.mNewAnkle), 0.1f);
    }

    TEST(MWRenderFootIkTest, FootKeepsItsAnimatedOrientationPlusTheTilt)
    {
        Fixture f;
        const vsg::mat4 before = f.mPose.getWorldRotation(f.mFoot);
        const vsg::vec3 normal = vsg::normalize(vsg::vec3(0.f, 0.3f, 0.95f));
        ASSERT_TRUE(f.solveAndWrite(f.pos(f.mFoot) + vsg::vec3(0.f, 0.f, -2.f), poleOf(f), normal, 1.f));
        const vsg::mat4 after = f.mPose.getWorldRotation(f.mFoot);
        // The sole's up axis was the foot's local z; check the tilt only: the angle between old and new is the
        // angle between world z and the normal.
        const float expected = std::acos(vsg::dot(vsg::vec3(0.f, 0.f, 1.f), normal));
        EXPECT_NEAR(angleBetween(before, after), expected, 0.01f);
    }

    TEST(MWRenderFootIkTest, RotationsOnlyAreWrittenAndScalesStayAsAnimated)
    {
        // Race weight scales an actor's x and y. Writing a world-space rotation back as a full local matrix turned the
        // resulting shear into wild local scales (millions on the foot) that the animation never rewrote, so they
        // compounded every frame and exploded the mesh. Here the actor root and a bone carry such a scale.
        Fixture f;
        f.mLeg.mActor->scale = vsg::vec3(1.08f, 1.08f, 1.0f);
        f.mLeg.mPelvis->scale = vsg::vec3(1.0f, 0.93f, 1.0f);
        f.mLeg.mCalf->scale = vsg::vec3(1.02f, 0.97f, 1.0f);

        const Anim::Transform* nodes[] = { f.mLeg.mThigh.get(), f.mLeg.mCalf.get(), f.mLeg.mFoot.get() };
        vsg::vec3 scales[3];
        vsg::vec3 translations[3];
        vsg::mat3 animated[3];
        for (int i = 0; i < 3; ++i)
        {
            scales[i] = nodes[i]->scale;
            translations[i] = nodes[i]->translation;
            animated[i] = nodes[i]->rotation;
        }

        const vsg::vec3 ankle0 = f.pos(f.mFoot);
        for (int frame = 0; frame < 60; ++frame)
        {
            // The animation writes the rotations again each frame and never the scale.
            for (int i = 0; i < 3; ++i)
                const_cast<Anim::Transform*>(nodes[i])->rotation = animated[i];

            const float dz = (frame % 2 == 0) ? -4.f : 5.f;
            ASSERT_TRUE(f.solveAndWrite(
                ankle0 + vsg::vec3(0.f, 0.f, dz), poleOf(f) + vsg::vec3(0.f, 8.f, 0.f), vsg::vec3(0.f, 0.2f, 0.98f), 1.f));

            for (int i = 0; i < 3; ++i)
            {
                for (int k = 0; k < 3; ++k)
                {
                    EXPECT_EQ(nodes[i]->scale[k], scales[i][k]) << frame;
                    EXPECT_EQ(nodes[i]->translation[k], translations[i][k]) << frame;
                }
                EXPECT_TRUE(finite(f.mPose.getWorld(i == 0 ? f.mThigh : i == 1 ? f.mCalf : f.mFoot))) << frame;
            }
            // Directions are only approximate in a scaled skeleton, but the ankle follows the ground closely.
            EXPECT_LT(std::abs(f.pos(f.mFoot).z - (ankle0.z + dz)), 2.0f) << frame;
        }
    }
}
