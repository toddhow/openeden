// Tests for the death ragdoll's bones in a Skyrim skeleton (task 0174, Skyrim combat S5-5):
// apps/openmw/mwrender/skyrimragdollmap.hpp (the baked skeleton map and the table of rig bones) and
// apps/openmw/mwrender/skyrimragdoll.hpp (the controller that draws a neutral actor's skeleton from a ragdoll).
//
// In plain words: the CreativeMotion rig has 17 bodies (pelvis, spine, chest, neck, head, two arms of three bones, two
// legs of three bones). A Skyrim skeleton names its bones its own way ("NPC L UpperArm [LUar]"), so the map from the
// skeleton says which bone each body drives; it is written to a file for a person to inspect and correct, and read
// back. The controller then turns those bones by what the ragdoll's bodies turn.
//
// Written from the spec before the implementation was checked; the red run against stubs is in the task's stage log.

// Jolt requires Jolt.h before any other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
// clang-format on

#include "apps/openmw/mwphysics/joltlayers.hpp"
#include "apps/openmw/mwphysics/ragdollhost.hpp"
#include "apps/openmw/mwphysics/ragdolltypes.hpp"
#include "apps/openmw/mwrender/actorragdolldriver.hpp"
#include "apps/openmw/mwrender/bonepose.hpp"
#include "apps/openmw/mwrender/skyrimragdoll.hpp"
#include "apps/openmw/mwrender/skyrimragdollmap.hpp"
#include "apps/openmw_tests/mwphysics/ragdolltestworld.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/body/skeletonmap.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using MWPhysics::RagdollBoneState;
    namespace SkyrimRagdoll = MWRender::SkyrimRagdoll;

    // Skyrim's human skeleton (names as the game's skeleton.nif writes them), parents first.
    struct BoneDef
    {
        const char* mName;
        const char* mParent; // "" for a root
    };
    const std::vector<BoneDef>& skyrimBones()
    {
        static const std::vector<BoneDef> bones = {
            { "NPC Root [Root]", "" },
            { "NPC COM [COM ]", "NPC Root [Root]" },
            { "NPC Pelvis [Pelv]", "NPC COM [COM ]" },
            { "NPC Spine [Spn0]", "NPC COM [COM ]" }, // a SIBLING of the pelvis, as in the real skeleton
            { "NPC Spine1 [Spn1]", "NPC Spine [Spn0]" },
            { "NPC Spine2 [Spn2]", "NPC Spine1 [Spn1]" },
            { "NPC Neck [Neck]", "NPC Spine2 [Spn2]" },
            { "NPC Head [Head]", "NPC Neck [Neck]" },
            { "NPC L Clavicle [LClv]", "NPC Spine2 [Spn2]" },
            { "NPC L UpperArm [LUar]", "NPC L Clavicle [LClv]" },
            { "NPC L Forearm [LLar]", "NPC L UpperArm [LUar]" },
            { "NPC L Hand [LHnd]", "NPC L Forearm [LLar]" },
            { "NPC R Clavicle [RClv]", "NPC Spine2 [Spn2]" },
            { "NPC R UpperArm [RUar]", "NPC R Clavicle [RClv]" },
            { "NPC R Forearm [RLar]", "NPC R UpperArm [RUar]" },
            { "NPC R Hand [RHnd]", "NPC R Forearm [RLar]" },
            { "NPC L Thigh [LThg]", "NPC Pelvis [Pelv]" },
            { "NPC L Calf [LClf]", "NPC L Thigh [LThg]" },
            { "NPC L Foot [Lft ]", "NPC L Calf [LClf]" },
            { "NPC R Thigh [RThg]", "NPC Pelvis [Pelv]" },
            { "NPC R Calf [RClf]", "NPC R Thigh [RThg]" },
            { "NPC R Foot [Rft ]", "NPC R Calf [RClf]" },
        };
        return bones;
    }

    NifScene::Skeleton::Plan makePlan(const std::vector<BoneDef>& defs)
    {
        NifScene::Skeleton::Plan plan;
        std::map<std::string, std::uint32_t> index;
        for (const BoneDef& def : defs)
        {
            NifScene::Skeleton::BoneNode node;
            node.mName = def.mName;
            node.mParent = def.mParent[0] == '\0' ? NifScene::sNone : index.at(def.mParent);
            node.mLocal.mTranslation = { 0.f, 0.f, 5.f + static_cast<float>(plan.mNodes.size()) };
            index[def.mName] = static_cast<std::uint32_t>(plan.mNodes.size());
            plan.mNodes.push_back(std::move(node));
        }
        return plan;
    }

    std::filesystem::path freshFolder(const std::string& name)
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("oe_ragdoll_" + name);
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        return folder;
    }

    // ----- the table -----

    TEST(SkyrimRagdollMap, aSkyrimSkeletonGivesABoneForEveryRigBody)
    {
        const NifScene::Skeleton::Plan plan = makePlan(skyrimBones());
        std::string message;
        const auto table = SkyrimRagdoll::makeBoneTable(plan, {}, message);
        ASSERT_TRUE(table) << message;
        const auto rig = MWRender::ragdollBoneMappings();
        ASSERT_EQ(table->set().mMappings.size(), rig.size());
        EXPECT_EQ(table->resolved(), 17u);
        EXPECT_TRUE(table->missingRoles().empty());
        const std::map<std::string, std::string> expected = {
            { "Pelvis", "npc pelvis [pelv]" },
            { "Spine", "npc spine [spn0]" },
            { "Chest", "npc spine1 [spn1]" },
            { "Neck", "npc neck [neck]" },
            { "Head", "npc head [head]" },
            { "LeftUpperArm", "npc l upperarm [luar]" },
            { "LeftForearm", "npc l forearm [llar]" },
            { "LeftHand", "npc l hand [lhnd]" },
            { "RightUpperArm", "npc r upperarm [ruar]" },
            { "RightForearm", "npc r forearm [rlar]" },
            { "RightHand", "npc r hand [rhnd]" },
            { "LeftThigh", "npc l thigh [lthg]" },
            { "LeftShin", "npc l calf [lclf]" },
            { "LeftFoot", "npc l foot [lft ]" },
            { "RightThigh", "npc r thigh [rthg]" },
            { "RightShin", "npc r calf [rclf]" },
            { "RightFoot", "npc r foot [rft ]" },
        };
        for (std::size_t i = 0; i < rig.size(); ++i)
        {
            EXPECT_STREQ(table->set().mMappings[i].mRigBone, rig[i].mRigBone);
            EXPECT_EQ(std::string(table->set().mMappings[i].mEngineBone), expected.at(rig[i].mRigBone))
                << rig[i].mRigBone;
        }
        // The pelvis' parent turns the ragdoll pelvis' world position into a local one.
        EXPECT_STREQ(table->set().mRootBone, "npc com [com ]");
    }

    TEST(SkyrimRagdollMap, aSkeletonWithNoPelvisGivesNoTable)
    {
        std::vector<BoneDef> bones = skyrimBones();
        bones.erase(bones.begin() + 2); // the pelvis; its children now hang from nothing the file names
        for (BoneDef& bone : bones)
            if (std::string(bone.mParent) == "NPC Pelvis [Pelv]")
                bone.mParent = "NPC COM [COM ]";
        std::string message;
        const auto table = SkyrimRagdoll::makeBoneTable(makePlan(bones), {}, message);
        EXPECT_FALSE(table);
        EXPECT_FALSE(message.empty());
    }

    TEST(SkyrimRagdollMap, aSkeletonThatIsNotAPersonGivesNoTable)
    {
        // A creature's bones: none of the 17 roles.
        const std::vector<BoneDef> bones = { { "NPC Root [Root]", "" }, { "Mud Body", "NPC Root [Root]" },
            { "Mud Claw L", "Mud Body" }, { "Mud Claw R", "Mud Body" }, { "Mud Eye", "Mud Body" } };
        std::string message;
        EXPECT_FALSE(SkyrimRagdoll::makeBoneTable(makePlan(bones), {}, message));
        EXPECT_FALSE(message.empty());
    }

    TEST(SkyrimRagdollMap, aSkeletonWithTooFewRigBonesGivesNoTable)
    {
        // The pelvis, the spine and the head only.
        const std::vector<BoneDef> bones = { { "NPC Root [Root]", "" }, { "NPC Pelvis [Pelv]", "NPC Root [Root]" },
            { "NPC Spine [Spn0]", "NPC Pelvis [Pelv]" }, { "NPC Head [Head]", "NPC Spine [Spn0]" } };
        std::string message;
        EXPECT_FALSE(SkyrimRagdoll::makeBoneTable(makePlan(bones), {}, message));
        EXPECT_NE(message.find("of 17"), std::string::npos) << message;
    }

    TEST(SkyrimRagdollMap, aMissingBoneIsListedAndTheRestStillWork)
    {
        std::vector<BoneDef> bones = skyrimBones();
        bones.erase(bones.begin() + 21); // the right foot
        std::string message;
        const auto table = SkyrimRagdoll::makeBoneTable(makePlan(bones), {}, message);
        ASSERT_TRUE(table) << message;
        EXPECT_EQ(table->resolved(), 16u);
        ASSERT_EQ(table->missingRoles().size(), 1u);
        EXPECT_EQ(table->missingRoles()[0], "RightFoot");
        EXPECT_STREQ(table->set().mMappings[16].mEngineBone, "");
    }

    TEST(SkyrimRagdollMap, theMapIsBakedToAFileNamedByTheSkeletonDigestAndReadBack)
    {
        const std::filesystem::path folder = freshFolder("bake") / "skeletonmaps";
        const NifScene::Skeleton::Plan plan = makePlan(skyrimBones());
        std::string message;
        const auto first = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(first) << message;
        EXPECT_EQ(first->origin(), "baked");
        ASSERT_EQ(first->digest().size(), 64u);
        const std::filesystem::path file = folder / (first->digest().substr(0, 16) + ".skeletonmap.json");
        ASSERT_TRUE(std::filesystem::exists(file)) << file.string();

        const Body::SkeletonMapResult read = Body::readSkeletonMapFile(file);
        ASSERT_TRUE(read.mValue) << read.mError.toString();
        EXPECT_EQ(read.mValue->mSource.mDigest, first->digest());
        EXPECT_EQ(read.mValue->mProfile, "humanoid-to-eden");

        const auto second = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(second) << message;
        EXPECT_EQ(second->origin(), "file");
        for (std::size_t i = 0; i < first->set().mMappings.size(); ++i)
            EXPECT_STREQ(first->set().mMappings[i].mEngineBone, second->set().mMappings[i].mEngineBone);
    }

    TEST(SkyrimRagdollMap, aPersonCanCorrectTheFileAndTheCorrectionWins)
    {
        const std::filesystem::path folder = freshFolder("correct") / "skeletonmaps";
        const NifScene::Skeleton::Plan plan = makePlan(skyrimBones());
        std::string message;
        const auto first = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(first) << message;
        const std::filesystem::path file = folder / (first->digest().substr(0, 16) + ".skeletonmap.json");

        Body::SkeletonMapResult read = Body::readSkeletonMapFile(file);
        ASSERT_TRUE(read.mValue) << read.mError.toString();
        Body::SkeletonMap map = *read.mValue;
        bool changed = false;
        for (Body::MapRole& role : map.mRoles)
            if (role.mRole == "Chest")
            {
                role.mBone = "NPC Spine2 [Spn2]";
                changed = true;
            }
        ASSERT_TRUE(changed);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << Body::writeSkeletonMap(map);
        }
        const auto second = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(second) << message;
        EXPECT_EQ(second->origin(), "file");
        EXPECT_EQ(second->engineBone(2), "npc spine2 [spn2]"); // rig bone 2 is "Chest"
    }

    TEST(SkyrimRagdollMap, aFileThatCannotBeReadIsBakedAgain)
    {
        const std::filesystem::path folder = freshFolder("damaged") / "skeletonmaps";
        const NifScene::Skeleton::Plan plan = makePlan(skyrimBones());
        std::string message;
        const auto first = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(first) << message;
        const std::filesystem::path file = folder / (first->digest().substr(0, 16) + ".skeletonmap.json");
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << "{ not a map";
        }
        const auto second = SkyrimRagdoll::makeBoneTable(plan, folder, message);
        ASSERT_TRUE(second) << message;
        EXPECT_EQ(second->origin(), "baked");
        EXPECT_TRUE(Body::readSkeletonMapFile(file).mValue) << "the damaged file was replaced";
    }

    TEST(SkyrimRagdollMap, aFolderThatCannotBeWrittenStillGivesATable)
    {
        const std::filesystem::path folder = freshFolder("nowrite");
        std::filesystem::create_directories(folder);
        {
            std::ofstream blocker(folder / "skeletonmaps", std::ios::binary); // a FILE where the folder should be
            blocker << "x";
        }
        std::string message;
        const auto table = SkyrimRagdoll::makeBoneTable(makePlan(skyrimBones()), folder / "skeletonmaps", message);
        EXPECT_TRUE(table) << message;
    }

    TEST(SkyrimRagdollMap, theSameSkeletonGivesTheSameDigestAndAChangedBoneAnotherOne)
    {
        const NifScene::Skeleton::Plan plan = makePlan(skyrimBones());
        std::string message;
        const auto a = SkyrimRagdoll::makeBoneTable(plan, {}, message);
        const auto b = SkyrimRagdoll::makeBoneTable(plan, {}, message);
        std::vector<BoneDef> changed = skyrimBones();
        changed.push_back({ "NPC Tail [Tail]", "NPC Pelvis [Pelv]" });
        const auto c = SkyrimRagdoll::makeBoneTable(makePlan(changed), {}, message);
        ASSERT_TRUE(a && b && c);
        EXPECT_EQ(a->digest(), b->digest());
        EXPECT_NE(a->digest(), c->digest());
    }

    TEST(SkyrimRagdollMap, restBonesCarryParentsTranslationsAndAHandDerivedRotation)
    {
        NifScene::Skeleton::Plan plan = makePlan({ { "A", "" }, { "B", "A" } });
        // A quarter turn about +Z, row-major: x axis -> y axis.
        plan.mNodes[1].mLocal.mRotation = { 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
        const std::vector<Body::RestBone> bones = SkyrimRagdoll::restBonesOf(plan);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(bones[0].mParent, -1);
        EXPECT_EQ(bones[1].mParent, 0);
        EXPECT_DOUBLE_EQ(bones[1].mTranslation[2], 6.0);
        const double h = std::sqrt(0.5);
        EXPECT_NEAR(bones[1].mRotation[0], 0.0, 1e-9);
        EXPECT_NEAR(bones[1].mRotation[1], 0.0, 1e-9);
        EXPECT_NEAR(bones[1].mRotation[2], h, 1e-6);
        EXPECT_NEAR(bones[1].mRotation[3], h, 1e-6);
        EXPECT_NEAR(bones[0].mRotation[3], 1.0, 1e-9); // identity stays identity
    }

    // ----- the snapshot -----

    TEST(SkyrimRagdollSnapshot, statesGoInAndComeOutUntilItEnds)
    {
        SkyrimRagdoll::Snapshot snapshot({ "Pelvis", "Spine" });
        std::vector<RagdollBoneState> in(2);
        in[1].mPosition[2] = 42.0;
        EXPECT_TRUE(snapshot.set(in));
        std::vector<RagdollBoneState> out(2);
        EXPECT_TRUE(snapshot.get(out));
        EXPECT_DOUBLE_EQ(out[1].mPosition[2], 42.0);
        EXPECT_FALSE(snapshot.set(std::span<const RagdollBoneState>(in).first(1))) << "too few states";
        std::vector<RagdollBoneState> small(1);
        EXPECT_FALSE(snapshot.get(small)) << "too small a buffer";
        snapshot.end();
        EXPECT_FALSE(snapshot.get(out));
    }

    // ----- the controller -----

    vsg::dquat axisAngle(const vsg::dvec3& axis, double angle)
    {
        const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        const double s = std::sin(0.5 * angle) / length;
        return vsg::dquat(axis.x * s, axis.y * s, axis.z * s, std::cos(0.5 * angle));
    }

    // Skyrim's bones as a vsg tree, each with its own non-trivial local rotation and offset (the engine's axes do not
    // agree with the ragdoll's identity ones).
    struct SkyrimActorBones
    {
        vsg::ref_ptr<vsg::Group> mScene{ new vsg::Group };
        std::map<std::string, vsg::ref_ptr<Anim::Transform>> mBones;

        SkyrimActorBones()
        {
            int index = 0;
            for (const BoneDef& def : skyrimBones())
            {
                auto bone = vsg::ref_ptr{ new Anim::Transform };
                vsgUtil::setName(*bone, def.mName);
                bone->translation = vsg::vec3(0.f, 0.f, 10.f + static_cast<float>(index));
                const vsg::dvec3 axis(1.0 + (index % 3), 0.5 - (index % 2), 0.25 * index - 1.0);
                bone->setAttitude(vsg::quat(axisAngle(axis, 0.2 + 0.13 * index)));
                ++index;
                mBones[def.mName] = bone;
                if (def.mParent[0] == '\0')
                    mScene->children.push_back(bone);
                else
                    mBones.at(def.mParent)->children.push_back(bone);
            }
        }
    };

    vsg::ref_ptr<Anim::Transform> makeActorRoot()
    {
        auto t = vsg::ref_ptr{ new Anim::Transform };
        t->translation = vsg::vec3(500.f, -300.f, 40.f);
        t->setAttitude(vsg::quat(axisAngle(vsg::dvec3(0, 0, 1), 0.9)));
        return t;
    }

    vsg::dquat worldRotationOf(const MWRender::BonePose& pose, const std::string& name)
    {
        return Misc::VsgMath::rotationOf(pose.getWorldRotation(pose.find(name)));
    }

    vsg::dvec3 worldPositionOf(const MWRender::BonePose& pose, const std::string& name)
    {
        return Misc::VsgMath::getTrans(pose.getWorld(pose.find(name)));
    }

    // The rotation that applies inner first and then outer (vsg::dquat's own operator* is the other way round).
    vsg::dquat compose(const vsg::dquat& outer, const vsg::dquat& inner)
    {
        return inner * outer;
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
                    << "rotations differ on axis (" << axis.x << ", " << axis.y << ", " << axis.z << ")";
        }
        return ::testing::AssertionSuccess();
    }

    // Rig bone names of the embedded humanoid rig (no neck body) and a start state for each.
    struct FakeRagdoll
    {
        std::vector<std::string> mNames;
        std::vector<RagdollBoneState> mStates;
        FakeRagdoll()
        {
            for (const auto& mapping : MWRender::ragdollBoneMappings())
                if (std::string(mapping.mRigBone) != "Neck")
                    mNames.push_back(mapping.mRigBone);
            mStates.assign(mNames.size(), RagdollBoneState{});
            for (std::size_t i = 0; i < mStates.size(); ++i)
            {
                mStates[i].mPosition[0] = 100.0 + static_cast<double>(i);
                mStates[i].mPosition[1] = 200.0;
                mStates[i].mPosition[2] = 300.0 - static_cast<double>(i);
            }
        }
        void turnAll(const vsg::dquat& turn)
        {
            for (RagdollBoneState& state : mStates)
            {
                const vsg::dquat start(state.mRotation[0], state.mRotation[1], state.mRotation[2], state.mRotation[3]);
                const vsg::dquat now = compose(turn, start);
                state.mRotation[0] = now.x;
                state.mRotation[1] = now.y;
                state.mRotation[2] = now.z;
                state.mRotation[3] = now.w;
            }
        }
    };

    struct ControllerFixture
    {
        SkyrimActorBones mSkeleton;
        vsg::ref_ptr<Anim::Transform> mRoot = makeActorRoot();
        Anim::Bones mBones = MWAnim::Bones(*mSkeleton.mScene);
        std::shared_ptr<const SkyrimRagdoll::BoneTable> mTable = [] {
            std::string message;
            return SkyrimRagdoll::makeBoneTable(makePlan(skyrimBones()), {}, message);
        }();
        FakeRagdoll mRagdoll;
        vsg::ref_ptr<vsg::Group> mTarget{ new vsg::Group };
        MWRender::BonePose mPose{ mBones, mRoot.get() };

        vsg::ref_ptr<SkyrimRagdoll::Controller> controller()
        {
            return vsg::ref_ptr<SkyrimRagdoll::Controller>(new SkyrimRagdoll::Controller(mTable, mBones, mRoot.get()));
        }
    };

    TEST(SkyrimRagdollController, theStartPoseOfAnActorInItsRestPoseIsTheRigsRestPose)
    {
        ControllerFixture f;
        ASSERT_TRUE(f.mTable);
        auto controller = f.controller();
        MWPhysics::ActorStartPose pose;
        ASSERT_TRUE(controller->startPose(pose));
        EXPECT_EQ(pose.mCount, 17u);
        for (std::size_t i = 0; i < pose.mCount; ++i)
        {
            const auto& r = pose.mBones[i].mRotation;
            EXPECT_NEAR(std::abs(r[3]), 1.0, 1e-6) << pose.mBones[i].mRigBone; // the identity, up to sign
        }
        // The pelvis' world position is the Skyrim pelvis bone's.
        const vsg::dvec3 pelvis = worldPositionOf(f.mPose, "NPC Pelvis [Pelv]");
        EXPECT_NEAR(pose.mPelvisPosition[0], pelvis.x, 1e-3);
        EXPECT_NEAR(pose.mPelvisPosition[1], pelvis.y, 1e-3);
        EXPECT_NEAR(pose.mPelvisPosition[2], pelvis.z, 1e-3);
    }

    TEST(SkyrimRagdollController, aBoneTurnedByTheAnimationShowsInTheStartPose)
    {
        ControllerFixture f;
        auto controller = f.controller(); // the rest is taken now
        // The animation turns the left upper arm a quarter turn (its local rotation).
        const MWRender::BonePose::Bone arm = f.mPose.find("npc l upperarm [luar]");
        ASSERT_TRUE(arm);
        const vsg::dquat before = Misc::VsgMath::rotationOf(f.mPose.getLocalRotation(arm));
        f.mPose.setLocalRotation(arm, compose(axisAngle(vsg::dvec3(0, 1, 0), 1.2), before));
        MWPhysics::ActorStartPose pose;
        ASSERT_TRUE(controller->startPose(pose));
        bool found = false;
        for (std::size_t i = 0; i < pose.mCount; ++i)
            if (pose.mBones[i].mRigBone == "LeftUpperArm")
            {
                found = true;
                EXPECT_LT(std::abs(pose.mBones[i].mRotation[3]), 0.99);
            }
        EXPECT_TRUE(found);
    }

    TEST(SkyrimRagdollController, itAsksToBeRunEveryFrameLikeTheOtherNeutralActorControllers)
    {
        // An object runs a controller that is not auto-play only while an animation plays, which nothing does for a
        // neutral actor: without this the ragdoll is never drawn (found in game, not by the other tests, which call
        // run).
        ControllerFixture f;
        EXPECT_TRUE(f.controller()->hints.autoPlay);
    }

    TEST(SkyrimRagdollController, anUnarmedControllerWritesNothing)
    {
        ControllerFixture f;
        auto controller = f.controller();
        const vsg::dquat before = worldRotationOf(f.mPose, "NPC L Hand [LHnd]");
        f.mRagdoll.turnAll(axisAngle(vsg::dvec3(0, 0, 1), 1.0));
        controller->setStates(f.mRagdoll.mStates); // nothing armed: no snapshot yet
        controller->run(*f.mTarget, 0.f);
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "NPC L Hand [LHnd]"), before));
        EXPECT_FALSE(controller->armed());
    }

    TEST(SkyrimRagdollController, anArmedControllerDrawsTheFirstFrameAsTheActorWasAndThenFollowsTheRagdoll)
    {
        ControllerFixture f;
        auto controller = f.controller();
        std::map<std::string, vsg::dquat> start;
        for (const BoneDef& def : skyrimBones())
            start[def.mName] = worldRotationOf(f.mPose, def.mName);

        std::string reason;
        ASSERT_TRUE(controller->arm(f.mRagdoll.mNames, f.mRagdoll.mStates, reason)) << reason;
        EXPECT_TRUE(controller->armed());
        controller->run(*f.mTarget, 0.f);
        for (const char* name : { "NPC Pelvis [Pelv]", "NPC Head [Head]", "NPC L Foot [Lft ]", "NPC R Hand [RHnd]" })
            EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, name), start.at(name))) << name << " on the spawn frame";

        // The ragdoll turns a quarter turn about +Z, world space: every mapped bone does the same.
        const vsg::dquat turn = axisAngle(vsg::dvec3(0, 0, 1), 1.5707963);
        f.mRagdoll.turnAll(turn);
        controller->setStates(f.mRagdoll.mStates);
        controller->run(*f.mTarget, 0.f);
        for (const char* name : { "NPC Pelvis [Pelv]", "NPC Head [Head]", "NPC L Foot [Lft ]", "NPC R Hand [RHnd]" })
            EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, name), compose(turn, start.at(name)))) << name;
        // A bone the table does not name (the clavicle) keeps what the animation gave it, under its turned parent.
        EXPECT_FALSE(sameRotation(worldRotationOf(f.mPose, "NPC L Clavicle [LClv]"), start.at("NPC L Clavicle [LClv]")))
            << "it follows its driven parent";
    }

    TEST(SkyrimRagdollController, thePelvisFollowsTheRagdollThroughItsRealParentBone)
    {
        ControllerFixture f;
        auto controller = f.controller();
        const vsg::dvec3 pelvisAtStart = worldPositionOf(f.mPose, "NPC Pelvis [Pelv]");
        // In Skyrim's skeleton the spine is the pelvis' SIBLING (both hang from "NPC COM"): the whole upper body goes
        // with the body only if the translation is put on the COM bone (found in game: the upper body stayed behind).
        const vsg::dvec3 spineAtStart = worldPositionOf(f.mPose, "NPC Spine [Spn0]");
        const vsg::dvec3 headAtStart = worldPositionOf(f.mPose, "NPC Head [Head]");
        std::string reason;
        ASSERT_TRUE(controller->arm(f.mRagdoll.mNames, f.mRagdoll.mStates, reason)) << reason;
        // The ragdoll's pelvis falls 30 units and slides 10 along x.
        f.mRagdoll.mStates[0].mPosition[0] += 10.0;
        f.mRagdoll.mStates[0].mPosition[2] -= 30.0;
        controller->setStates(f.mRagdoll.mStates);
        controller->run(*f.mTarget, 0.f);
        const vsg::dvec3 pelvis = worldPositionOf(f.mPose, "NPC Pelvis [Pelv]");
        EXPECT_NEAR(pelvis.x, pelvisAtStart.x + 10.0, 1e-2);
        EXPECT_NEAR(pelvis.y, pelvisAtStart.y, 1e-2);
        EXPECT_NEAR(pelvis.z, pelvisAtStart.z - 30.0, 1e-2);
        // The ragdoll did not turn, so everything below the COM bone moved by the same 10 and -30.
        const vsg::dvec3 spine = worldPositionOf(f.mPose, "NPC Spine [Spn0]");
        EXPECT_NEAR(spine.x, spineAtStart.x + 10.0, 1e-2);
        EXPECT_NEAR(spine.z, spineAtStart.z - 30.0, 1e-2);
        const vsg::dvec3 head = worldPositionOf(f.mPose, "NPC Head [Head]");
        EXPECT_NEAR(head.x, headAtStart.x + 10.0, 1e-2);
        EXPECT_NEAR(head.z, headAtStart.z - 30.0, 1e-2);
    }

    TEST(SkyrimRagdollController, aDisarmedControllerStopsWriting)
    {
        ControllerFixture f;
        auto controller = f.controller();
        std::string reason;
        ASSERT_TRUE(controller->arm(f.mRagdoll.mNames, f.mRagdoll.mStates, reason)) << reason;
        controller->run(*f.mTarget, 0.f);
        controller->disarm();
        EXPECT_FALSE(controller->armed());
        const vsg::dquat held = worldRotationOf(f.mPose, "NPC Head [Head]");
        f.mRagdoll.turnAll(axisAngle(vsg::dvec3(1, 0, 0), 1.0));
        controller->setStates(f.mRagdoll.mStates);
        controller->run(*f.mTarget, 0.f);
        EXPECT_TRUE(sameRotation(worldRotationOf(f.mPose, "NPC Head [Head]"), held));
    }

    TEST(SkyrimRagdollController, armingWithTooFewStatesIsRefusedAndLeavesItUnarmed)
    {
        ControllerFixture f;
        auto controller = f.controller();
        std::string reason;
        EXPECT_FALSE(
            controller->arm(f.mRagdoll.mNames, std::span<const RagdollBoneState>(f.mRagdoll.mStates).first(3), reason));
        EXPECT_FALSE(reason.empty());
        EXPECT_FALSE(controller->armed());
    }

    TEST(SkyrimRagdollController, aRagdollWithoutThePelvisBodyIsRefused)
    {
        ControllerFixture f;
        auto controller = f.controller();
        std::vector<std::string> names(f.mRagdoll.mNames.begin() + 1, f.mRagdoll.mNames.end());
        std::vector<RagdollBoneState> states(f.mRagdoll.mStates.begin() + 1, f.mRagdoll.mStates.end());
        std::string reason;
        EXPECT_FALSE(controller->arm(names, states, reason));
        EXPECT_FALSE(controller->armed());
    }

    // ----- the capsule layer: the corpse's own body must never meet its ragdoll -----

    // A neutral actor's capsule as the game makes it: a static body on Layers::WORLD, feet at the origin.
    JPH::BodyID addActorCapsule(MWPhysicsTest::GameLikeWorld& world, JPH::ObjectLayer layer)
    {
        const JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(40.0f, 22.0f);
        const JPH::BodyCreationSettings settings(
            shape, JPH::RVec3(0.0, 0.0, 62.0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, layer);
        return world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    double pelvisDistanceFromSpawn(MWPhysicsTest::GameLikeWorld& world)
    {
        std::string error;
        auto host = MWPhysics::RagdollHost::create(
            world.system(), Constants::UnitsPerMeter, MWPhysicsTest::kStepSeconds, &error);
        EXPECT_NE(host, nullptr) << error;
        if (host == nullptr)
            return -1.0;
        MWPhysics::ActorStartPose pose;
        pose.mPelvisPosition[0] = 0.0;
        pose.mPelvisPosition[1] = 0.0;
        pose.mPelvisPosition[2] = 62.0;
        const double noVelocity[3] = { 0.0, 0.0, 0.0 };
        const MWPhysics::ActorRagdollHandle handle
            = host->spawnActor(JPH::RVec3(0.0, 0.0, 0.0), 0.0f, noVelocity, &error, &pose);
        EXPECT_TRUE(handle.isValid()) << error;
        std::vector<RagdollBoneState> states(host->actorBodyBones().size());
        EXPECT_TRUE(host->readActor(handle, states));
        for (std::size_t i = 0; i < states.size(); ++i)
            if (host->actorBodyBones()[i] == "Pelvis")
                return std::hypot(states[i].mPosition[0], states[i].mPosition[1]);
        return -1.0;
    }

    TEST(SkyrimRagdollLayers, aRagdollNeverMeetsTheDebrisLayerButMeetsTheWorldLayer)
    {
        const MWPhysics::JoltObjectLayerPairFilter pairs;
        const MWPhysics::JoltObjectVsBroadPhaseLayerFilter broadPhase;
        EXPECT_TRUE(pairs.ShouldCollide(MWPhysics::Layers::RAGDOLL, MWPhysics::Layers::WORLD));
        EXPECT_FALSE(pairs.ShouldCollide(MWPhysics::Layers::RAGDOLL, MWPhysics::Layers::DEBRIS));
        EXPECT_FALSE(pairs.ShouldCollide(MWPhysics::Layers::DEBRIS, MWPhysics::Layers::RAGDOLL));
        EXPECT_FALSE(broadPhase.ShouldCollide(MWPhysics::Layers::RAGDOLL, MWPhysics::BroadPhaseLayers::DEBRIS));
    }

    TEST(SkyrimRagdollLayers, theCapsuleOfTheDeadMustLeaveTheWorldLayerOrItPushesItsOwnRagdollAway)
    {
        // The control: the capsule still on WORLD (as a living neutral actor's is) holds the standing ragdoll out.
        MWPhysicsTest::GameLikeWorld pillar(true);
        addActorCapsule(pillar, MWPhysics::Layers::WORLD);
        EXPECT_GT(pelvisDistanceFromSpawn(pillar), 5.0);

        // Moved to DEBRIS, as PhysicsSystem::startNeutralActorRagdoll does before it spawns: the ragdoll is built where
        // the actor stood.
        MWPhysicsTest::GameLikeWorld debris(true);
        const JPH::BodyID capsule = addActorCapsule(debris, MWPhysics::Layers::WORLD);
        debris.bodies().SetObjectLayer(capsule, MWPhysics::Layers::DEBRIS);
        EXPECT_NEAR(pelvisDistanceFromSpawn(debris), 0.0, 0.01);
    }
}
