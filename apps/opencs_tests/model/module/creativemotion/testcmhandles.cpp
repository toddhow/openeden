// Task 0091 stage c: the rig inspector's handles (API 1.2 picking and dragging). Written from the spec alone.
//
// Contract pinned here (the limits gizmo, id "creativemotion.rig.limits", is gizmos()[1]):
//  - handles(): one handle "creativemotion.joint.<bone>" per joint, in rig order, at the bone's world rest origin (the
//    place a click selects the joint); with a CONE joint selected also "creativemotion.cone.plane" and
//    "creativemotion.cone.normal", at the rim point of the drawing at t = 0 (plane half angle, swing direction
//    swingDir(planeHalf, 0)) and t = 90 degrees (swingDir(0, normalHalf)), kLimitLengthM (0.1 m) from the origin.
//  - drag("creativemotion.cone.plane", p): the half angle of the plane axis becomes the angle of the direction from the
//    joint origin to p, measured in the joint frame (atan2(-dz, dx)), taken as an absolute value and clamped to
//    [0, pi]; min = -max stays; the command marks the document dirty, undoes exactly, and the document stays a valid
//    rig. The normal handle does the same for the normal axis (atan2(dy, dx)). Other handles and positions that give
//    no direction (the origin, non-finite) return null.
#include "editor_modules/creativemotion_editor/documents.hpp"

#include "cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace CMTest
{
    namespace
    {
        using edit::AssetDocument;
        using edit::Gizmo;
        using edit::GizmoHandle;

        constexpr double kLength = 0.1;
        constexpr double kTol = 1e-9;

        struct JointFrame
        {
            math::Vec3 origin;
            math::Quat rotation;
            math::Vec3 x, y, z; // joint axes in the bone frame
        };

        JointFrame frameOf(const rig::Rig& value, const std::string& bone)
        {
            std::vector<math::Transform> world;
            for (const rig::Bone& b : value.skeleton.bones)
                world.push_back(b.parent < 0 ? b.localRest : world[static_cast<std::size_t>(b.parent)] * b.localRest);
            std::size_t boneIndex = 0;
            for (std::size_t i = 0; i < value.skeleton.bones.size(); ++i)
                if (value.skeleton.bones[i].name == bone)
                    boneIndex = i;
            const rig::Joint* joint = nullptr;
            for (const rig::Joint& j : value.joints)
                if (j.bone == bone)
                    joint = &j;
            JointFrame frame;
            frame.origin = world[boneIndex].translation;
            frame.rotation = world[boneIndex].rotation;
            frame.x = joint->twistAxis;
            frame.z = joint->planeAxis;
            frame.y = math::cross(frame.z, frame.x);
            return frame;
        }

        /// The model-space direction of joint-frame coordinates (dx, dy, dz).
        math::Vec3 direction(const JointFrame& f, double dx, double dy, double dz)
        {
            const math::Vec3 local = f.x * dx + f.y * dy + f.z * dz;
            return math::rotate(f.rotation, local);
        }

        math::Vec3 swingPoint(const JointFrame& f, double p, double n, double length)
        {
            return f.origin + direction(f, std::cos(p) * std::cos(n), std::cos(p) * std::sin(n), -std::sin(p)) * length;
        }

        class CmHandlesTest : public TempDirTest
        {
        protected:
            void SetUp() override
            {
                TempDirTest::SetUp();
                mRig = makeLimitsRig();
                std::string error;
                mDocument = creativemotion_editor::openRig(writeRigFile("limits", mRig), error);
                ASSERT_NE(mDocument, nullptr) << error;
                mSelectable = dynamic_cast<creativemotion_editor::JointSelectable*>(mDocument.get());
                ASSERT_NE(mSelectable, nullptr);
                ASSERT_EQ(mDocument->gizmos().size(), 2u);
                mGizmo = mDocument->gizmos()[1];
            }

            double number(const std::string& id) { return std::get<double>(mDocument->properties().get(id)); }

            std::vector<std::string> handleIds()
            {
                std::vector<std::string> ids;
                for (const GizmoHandle& handle : mGizmo->handles())
                    ids.push_back(handle.id);
                return ids;
            }

            GizmoHandle handle(const std::string& id)
            {
                for (const GizmoHandle& h : mGizmo->handles())
                    if (h.id == id)
                        return h;
                ADD_FAILURE() << "no handle " << id;
                return {};
            }

            rig::Rig mRig;
            std::unique_ptr<AssetDocument> mDocument;
            creativemotion_editor::JointSelectable* mSelectable = nullptr;
            Gizmo* mGizmo = nullptr;
        };

        TEST_F(CmHandlesTest, everyJointHasAClickHandleAtItsOriginAndNoConeHandleWithoutASelection)
        {
            EXPECT_EQ(handleIds(),
                (std::vector<std::string>{
                    "creativemotion.joint.Spine", "creativemotion.joint.LeftThigh", "creativemotion.joint.LeftShin" }));
            for (const char* bone : { "Spine", "LeftThigh", "LeftShin" })
            {
                const JointFrame f = frameOf(mRig, bone);
                EXPECT_LT(
                    distanceBetween(handle(std::string("creativemotion.joint.") + bone).position, f.origin), kTol);
            }
        }

        TEST_F(CmHandlesTest, aSelectedConeJointAddsTheTwoRimHandlesAtTheDrawnRimPoints)
        {
            mSelectable->setSelectedJoint("Spine");
            EXPECT_EQ(handleIds(),
                (std::vector<std::string>{ "creativemotion.joint.Spine", "creativemotion.joint.LeftThigh",
                    "creativemotion.joint.LeftShin", "creativemotion.cone.plane", "creativemotion.cone.normal" }));
            const JointFrame f = frameOf(mRig, "Spine");
            EXPECT_LT(
                distanceBetween(handle("creativemotion.cone.plane").position, swingPoint(f, 0.4, 0.0, kLength)), kTol);
            EXPECT_LT(
                distanceBetween(handle("creativemotion.cone.normal").position, swingPoint(f, 0.0, 0.7, kLength)), kTol);
        }

        TEST_F(CmHandlesTest, aSelectedPyramidJointAddsNoConeHandles)
        {
            mSelectable->setSelectedJoint("LeftThigh");
            EXPECT_EQ(handleIds().size(), 3u);
        }

        TEST_F(CmHandlesTest, draggingThePlaneHandleSetsTheSymmetricHalfAngleAndUndoRestores)
        {
            mSelectable->setSelectedJoint("Spine");
            const JointFrame f = frameOf(mRig, "Spine");
            // A point 0.17 m from the origin in the direction of plane angle 0.9: the handle follows the angle only.
            const math::Vec3 target = swingPoint(f, 0.9, 0.0, 0.17);
            auto command = mGizmo->drag("creativemotion.cone.plane", toEdit(target));
            ASSERT_NE(command, nullptr);
            EXPECT_FALSE(mDocument->isDirty());
            command->redo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.9, 1e-9);
            EXPECT_NEAR(number("joint.Spine.planeSwingMinRad"), -0.9, 1e-9);
            EXPECT_NEAR(number("joint.Spine.normalSwingMaxRad"), 0.7, 1e-12); // untouched
            EXPECT_TRUE(mDocument->isDirty());
            // The handle now sits at the new rim point.
            EXPECT_LT(
                distanceBetween(handle("creativemotion.cone.plane").position, swingPoint(f, 0.9, 0.0, kLength)), kTol);
            command->undo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.4, 1e-12);
            EXPECT_NEAR(number("joint.Spine.planeSwingMinRad"), -0.4, 1e-12);
        }

        TEST_F(CmHandlesTest, draggingTheNormalHandleSetsTheNormalHalfAngle)
        {
            mSelectable->setSelectedJoint("Spine");
            const JointFrame f = frameOf(mRig, "Spine");
            auto command = mGizmo->drag("creativemotion.cone.normal", toEdit(swingPoint(f, 0.0, 1.1, 0.05)));
            ASSERT_NE(command, nullptr);
            command->redo();
            EXPECT_NEAR(number("joint.Spine.normalSwingMaxRad"), 1.1, 1e-9);
            EXPECT_NEAR(number("joint.Spine.normalSwingMinRad"), -1.1, 1e-9);
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.4, 1e-12);
        }

        TEST_F(CmHandlesTest, aDragThroughTheOppositeSideTakesTheAbsoluteAngleAndTheLargestIsPi)
        {
            mSelectable->setSelectedJoint("Spine");
            const JointFrame f = frameOf(mRig, "Spine");
            auto negative = mGizmo->drag("creativemotion.cone.plane", toEdit(swingPoint(f, -0.5, 0.0, 0.1)));
            ASSERT_NE(negative, nullptr);
            negative->redo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.5, 1e-9);
            // Straight back (the opposite of the twist axis side): the angle is pi, which is still valid.
            auto back = mGizmo->drag("creativemotion.cone.plane", toEdit(f.origin + direction(f, -1, 0, 0) * 0.1));
            ASSERT_NE(back, nullptr);
            back->redo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), std::numbers::pi, 1e-9);
        }

        TEST_F(CmHandlesTest, successiveDragCommandsAreIndependentUndoSteps)
        {
            mSelectable->setSelectedJoint("Spine");
            const JointFrame f = frameOf(mRig, "Spine");
            auto first = mGizmo->drag("creativemotion.cone.plane", toEdit(swingPoint(f, 0.6, 0.0, 0.1)));
            first->redo();
            auto second = mGizmo->drag("creativemotion.cone.plane", toEdit(swingPoint(f, 1.0, 0.0, 0.1)));
            second->redo();
            second->undo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.6, 1e-9);
            first->undo();
            EXPECT_NEAR(number("joint.Spine.planeSwingMaxRad"), 0.4, 1e-12);
        }

        TEST_F(CmHandlesTest, otherHandlesAndPositionsWithoutADirectionGiveNoCommand)
        {
            mSelectable->setSelectedJoint("Spine");
            const JointFrame f = frameOf(mRig, "Spine");
            EXPECT_EQ(mGizmo->drag("creativemotion.joint.Spine", toEdit(f.origin + math::Vec3{ 0.1, 0, 0 })), nullptr);
            EXPECT_EQ(mGizmo->drag("nonsense", toEdit(f.origin + math::Vec3{ 0.1, 0, 0 })), nullptr);
            EXPECT_EQ(mGizmo->drag("creativemotion.cone.plane", toEdit(f.origin)), nullptr);
            const double nan = std::numeric_limits<double>::quiet_NaN();
            EXPECT_EQ(mGizmo->drag("creativemotion.cone.plane", edit::Vec3{ nan, 0, 0 }), nullptr);
            EXPECT_EQ(mGizmo->drag("creativemotion.cone.plane", edit::Vec3{ 1e300, 1e300, 1e300 }), nullptr);
        }

        TEST_F(CmHandlesTest, draggingWithoutAConeSelectionGivesNoCommand)
        {
            const JointFrame f = frameOf(mRig, "Spine");
            EXPECT_EQ(mGizmo->drag("creativemotion.cone.plane", toEdit(swingPoint(f, 0.9, 0.0, 0.1))), nullptr);
            mSelectable->setSelectedJoint("LeftThigh");
            EXPECT_EQ(mGizmo->drag("creativemotion.cone.plane", toEdit(swingPoint(f, 0.9, 0.0, 0.1))), nullptr);
        }
    }
}
