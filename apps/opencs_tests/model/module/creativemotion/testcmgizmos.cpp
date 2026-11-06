// Task 0032: the creativemotion_editor module's rig gizmos, written from the spec alone.
//
// The expected lines are computed here, independently, from the formulas in the spec ("Rig document", Gizmos):
// body world transform = bone world rest transform * Body::local; the joint frame has X = twistAxis, Z = planeAxis,
// Y = Z cross X; a joint direction (dx, dy, dz) is R * (dx X + dy Y + dz Z) with R the bone's world rest rotation.
//
// Assumptions (see the report):
//  - The gizmos are returned by AssetDocument::gizmos() in the order shapes, limits, with the ids
//    "creativemotion.rig.shapes" and "creativemotion.rig.limits".
//  - The sphere's three great circles are drawn in the order XY, YZ, XZ (24 lines each), the capsule's lines in the
//    order given in the spec (circles at +h then -h, four side lines, then the end half circles).
//  - The joints are drawn in rig order, one after the other, in one gizmo.
#include "editor_modules/creativemotion_editor/documents.hpp"

#include "cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

namespace CMTest
{
    namespace
    {
        using edit::AssetDocument;
        using edit::Gizmo;
        using edit::PropertyValue;

        constexpr double kTol = 1e-9;
        constexpr double kLimitLengthM = 0.1; // kLimitLengthM of the spec
        constexpr int kRimSegments = 24;
        constexpr int kTwistSegments = 16;

        std::vector<math::Transform> worldRest(const rig::Rig& value)
        {
            std::vector<math::Transform> world;
            for (const rig::Bone& bone : value.skeleton.bones)
            {
                if (bone.parent < 0)
                    world.push_back(bone.localRest);
                else
                    world.push_back(world[static_cast<std::size_t>(bone.parent)] * bone.localRest);
            }
            return world;
        }

        std::size_t boneIndex(const rig::Rig& value, const std::string& name)
        {
            for (std::size_t i = 0; i < value.skeleton.bones.size(); ++i)
                if (value.skeleton.bones[i].name == name)
                    return i;
            ADD_FAILURE() << "no bone " << name;
            return 0;
        }

        ::testing::AssertionResult isNear(const edit::Vec3& actual, math::Vec3 expected, double tolerance = kTol)
        {
            if (std::abs(actual.x - expected.x) <= tolerance && std::abs(actual.y - expected.y) <= tolerance
                && std::abs(actual.z - expected.z) <= tolerance)
                return ::testing::AssertionSuccess();
            return ::testing::AssertionFailure()
                << "(" << actual.x << ", " << actual.y << ", " << actual.z << ") is not (" << expected.x << ", "
                << expected.y << ", " << expected.z << ")";
        }

        struct ExpectedLine
        {
            math::Vec3 a;
            math::Vec3 b;
            edit::Colour colour;
        };

        const edit::Colour kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
        const edit::Colour kOrange{ 1.0f, 0.6f, 0.1f, 1.0f };
        const edit::Colour kCyan{ 0.1f, 0.9f, 0.9f, 1.0f };
        const edit::Colour kGrey{ 0.8f, 0.8f, 0.8f, 1.0f };

        std::vector<ExpectedLine> expectedLimitLines(const rig::Rig& value)
        {
            const std::vector<math::Transform> world = worldRest(value);
            std::vector<ExpectedLine> lines;
            for (const rig::Joint& joint : value.joints)
            {
                const math::Transform& bone = world[boneIndex(value, joint.bone)];
                const math::Vec3 o = bone.translation;
                const math::Vec3 x = joint.twistAxis;
                const math::Vec3 z = joint.planeAxis;
                const math::Vec3 y = math::cross(z, x);
                const auto dirWorld = [&](double dx, double dy, double dz) {
                    return math::rotate(bone.rotation, dx * x + dy * y + dz * z);
                };
                const auto swingDir = [&](double p, double n) {
                    return dirWorld(std::cos(p) * std::cos(n), std::cos(p) * std::sin(n), -std::sin(p));
                };

                // 1. the twist axis
                lines.push_back({ o, o + kLimitLengthM * dirWorld(1.0, 0.0, 0.0), kWhite });

                if (joint.swing == rig::SwingShape::Cone)
                {
                    const double planeHalf = joint.planeSwingMaxRad;
                    const double normalHalf = joint.normalSwingMaxRad;
                    if (!(planeHalf == 0.0 && normalHalf == 0.0))
                    {
                        std::vector<math::Vec3> rim;
                        for (int k = 0; k < kRimSegments; ++k)
                        {
                            const double t = 2.0 * std::numbers::pi * k / kRimSegments;
                            rim.push_back(
                                o + kLimitLengthM * swingDir(planeHalf * std::cos(t), normalHalf * std::sin(t)));
                        }
                        for (int k = 0; k < kRimSegments; ++k)
                            lines.push_back({ rim[k], rim[(k + 1) % kRimSegments], kOrange });
                        for (int k : { 0, 6, 12, 18 })
                            lines.push_back({ o, rim[k], kOrange });
                    }
                }
                else
                {
                    const math::Vec3 corners[4] = {
                        o + kLimitLengthM * swingDir(joint.planeSwingMinRad, joint.normalSwingMinRad),
                        o + kLimitLengthM * swingDir(joint.planeSwingMaxRad, joint.normalSwingMinRad),
                        o + kLimitLengthM * swingDir(joint.planeSwingMaxRad, joint.normalSwingMaxRad),
                        o + kLimitLengthM * swingDir(joint.planeSwingMinRad, joint.normalSwingMaxRad),
                    };
                    for (int k = 0; k < 4; ++k)
                        lines.push_back({ corners[k], corners[(k + 1) % 4], kOrange });
                    for (int k = 0; k < 4; ++k)
                        lines.push_back({ o, corners[k], kOrange });
                }

                // 4. the twist arc
                std::vector<math::Vec3> arc;
                for (int i = 0; i <= kTwistSegments; ++i)
                {
                    const double a = joint.twistMinRad + (joint.twistMaxRad - joint.twistMinRad) * i / kTwistSegments;
                    arc.push_back(o + 0.25 * kLimitLengthM * dirWorld(0.0, std::cos(a), std::sin(a)));
                }
                for (int i = 0; i < kTwistSegments; ++i)
                    lines.push_back({ arc[i], arc[i + 1], kCyan });
            }
            return lines;
        }

        void expectLinesMatch(const std::vector<DrawnLine>& actual, const std::vector<ExpectedLine>& expected)
        {
            ASSERT_EQ(actual.size(), expected.size());
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                EXPECT_TRUE(isNear(actual[i].a, expected[i].a)) << "line " << i << " start";
                EXPECT_TRUE(isNear(actual[i].b, expected[i].b)) << "line " << i << " end";
                EXPECT_FLOAT_EQ(actual[i].colour.r, expected[i].colour.r) << "line " << i;
                EXPECT_FLOAT_EQ(actual[i].colour.g, expected[i].colour.g) << "line " << i;
                EXPECT_FLOAT_EQ(actual[i].colour.b, expected[i].colour.b) << "line " << i;
                EXPECT_FLOAT_EQ(actual[i].colour.a, expected[i].colour.a) << "line " << i;
            }
        }

        class CmGizmosTest : public TempDirTest
        {
        protected:
            std::unique_ptr<AssetDocument> open(const rig::Rig& value, const std::string& name = "gizmos")
            {
                std::string error;
                auto document = creativemotion_editor::openRig(writeRigFile(name, value), error);
                EXPECT_NE(document, nullptr) << error;
                return document;
            }

            static Gizmo* gizmoById(AssetDocument& document, const std::string& id)
            {
                for (Gizmo* gizmo : document.gizmos())
                    if (gizmo != nullptr && gizmo->id() == id)
                        return gizmo;
                return nullptr;
            }

            static std::vector<DrawnLine> draw(Gizmo& gizmo)
            {
                RecordingSink sink;
                gizmo.draw(sink);
                return sink.mLines;
            }
        };

        TEST_F(CmGizmosTest, aRigHasTwoGizmosWithNoHandles)
        {
            auto document = open(makeLimitsRig());
            ASSERT_NE(document, nullptr);
            const std::vector<Gizmo*> gizmos = document->gizmos();
            ASSERT_EQ(gizmos.size(), 2u);
            ASSERT_NE(gizmos[0], nullptr);
            ASSERT_NE(gizmos[1], nullptr);
            EXPECT_EQ(gizmos[0]->id(), "creativemotion.rig.shapes");
            EXPECT_EQ(gizmos[1]->id(), "creativemotion.rig.limits");
            for (Gizmo* gizmo : gizmos)
            {
                EXPECT_EQ(gizmo->handles().size(), gizmo == gizmos[1] ? 3u : 0u); // API 1.2: a click handle per joint, 3 joints
                EXPECT_EQ(gizmo->drag("anything", edit::Vec3{ 1.0, 2.0, 3.0 }), nullptr);
            }
        }

        // ---- shapes ---------------------------------------------------------------------------------------

        TEST_F(CmGizmosTest, theShapeGizmoDrawsBoxSphereAndCapsuleInBodyOrder)
        {
            const rig::Rig value = makeShapesRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.shapes");
            ASSERT_NE(gizmo, nullptr);

            RecordingSink sink;
            gizmo->draw(sink);
            ASSERT_EQ(sink.mLines.size(), 12u + 72u + 100u);
            EXPECT_EQ(sink.mPoints, 0);
            EXPECT_EQ(sink.mLabels, 0);
            for (const DrawnLine& line : sink.mLines)
            {
                EXPECT_FLOAT_EQ(line.colour.r, kGrey.r);
                EXPECT_FLOAT_EQ(line.colour.g, kGrey.g);
                EXPECT_FLOAT_EQ(line.colour.b, kGrey.b);
                EXPECT_FLOAT_EQ(line.colour.a, kGrey.a);
            }

            const std::vector<math::Transform> world = worldRest(value);
            const auto bodyWorld = [&](std::size_t body) {
                return world[boneIndex(value, value.bodies[body].bone)] * value.bodies[body].local;
            };
            const auto toLocal = [](const math::Transform& t, const edit::Vec3& p) {
                return math::rotate(math::conjugate(t.rotation), toMath(p) - t.translation);
            };

            // The box: 12 edges between corners of the box in the world.
            {
                const math::Transform t = bodyWorld(0);
                const math::Vec3 h = std::get<rig::BoxShape>(value.bodies[0].shape).halfExtentsM;
                std::vector<math::Vec3> corners;
                for (int sx : { -1, 1 })
                    for (int sy : { -1, 1 })
                        for (int sz : { -1, 1 })
                            corners.push_back(math::transformPoint(t, { sx * h.x, sy * h.y, sz * h.z }));
                const auto isCorner = [&](const edit::Vec3& p) {
                    return std::any_of(corners.begin(), corners.end(),
                        [&](const math::Vec3& c) { return distanceBetween(p, c) <= kTol; });
                };
                for (std::size_t i = 0; i < 12; ++i)
                {
                    EXPECT_TRUE(isCorner(sink.mLines[i].a)) << "box line " << i;
                    EXPECT_TRUE(isCorner(sink.mLines[i].b)) << "box line " << i;
                    // An edge is as long as one of the box's sides.
                    const double length = math::distance(toMath(sink.mLines[i].a), toMath(sink.mLines[i].b));
                    const bool isSide = std::abs(length - 2 * h.x) <= kTol || std::abs(length - 2 * h.y) <= kTol
                        || std::abs(length - 2 * h.z) <= kTol;
                    EXPECT_TRUE(isSide) << "box line " << i << " length " << length;
                }
            }

            // The sphere: 3 great circles of 24 segments (local XY, YZ, XZ), every point at the radius.
            {
                const math::Transform t = bodyWorld(1);
                const double radius = std::get<rig::SphereShape>(value.bodies[1].shape).radiusM;
                for (std::size_t i = 12; i < 84; ++i)
                {
                    EXPECT_NEAR(distanceBetween(sink.mLines[i].a, t.translation), radius, kTol) << "sphere line " << i;
                    EXPECT_NEAR(distanceBetween(sink.mLines[i].b, t.translation), radius, kTol) << "sphere line " << i;

                    // Circle k lies in the local plane with a zero coordinate: Z for the first, X, then Y.
                    const std::size_t circle = (i - 12) / 24;
                    const math::Vec3 a = toLocal(t, sink.mLines[i].a);
                    const math::Vec3 b = toLocal(t, sink.mLines[i].b);
                    const double zeroA = circle == 0 ? a.z : (circle == 1 ? a.x : a.y);
                    const double zeroB = circle == 0 ? b.z : (circle == 1 ? b.x : b.y);
                    EXPECT_NEAR(zeroA, 0.0, kTol) << "sphere line " << i;
                    EXPECT_NEAR(zeroB, 0.0, kTol) << "sphere line " << i;
                }
                // Consecutive segments of a circle share their end points.
                for (std::size_t circle = 0; circle < 3; ++circle)
                    for (std::size_t k = 0; k < 24; ++k)
                    {
                        const DrawnLine& l = sink.mLines[12 + circle * 24 + k];
                        const DrawnLine& next = sink.mLines[12 + circle * 24 + (k + 1) % 24];
                        EXPECT_TRUE(isNear(next.a, toMath(l.b))) << "circle " << circle << " segment " << k;
                    }
            }

            // The capsule: 100 lines, all within the capsule, reaching both ends.
            {
                const math::Transform t = bodyWorld(2);
                const auto shape = std::get<rig::CapsuleShape>(value.bodies[2].shape);
                const double h = shape.halfHeightM;
                const double r = shape.radiusM;
                double minZ = 1e9;
                double maxZ = -1e9;
                for (std::size_t i = 84; i < 184; ++i)
                {
                    for (const edit::Vec3& p : { sink.mLines[i].a, sink.mLines[i].b })
                    {
                        const math::Vec3 local = toLocal(t, p);
                        // Distance from the axis segment from (0, 0, -h) to (0, 0, h).
                        const double clampedZ = std::clamp(local.z, -h, h);
                        const double distance = std::sqrt(
                            local.x * local.x + local.y * local.y + (local.z - clampedZ) * (local.z - clampedZ));
                        EXPECT_LE(distance, r + kTol) << "capsule line " << i;
                        minZ = std::min(minZ, local.z);
                        maxZ = std::max(maxZ, local.z);
                    }
                }
                EXPECT_NEAR(maxZ, h + r, kTol);
                EXPECT_NEAR(minZ, -h - r, kTol);

                // The two end circles (first 48 lines) and the four side lines (next 4).
                for (std::size_t i = 84; i < 132; ++i)
                {
                    const math::Vec3 a = toLocal(t, sink.mLines[i].a);
                    const math::Vec3 b = toLocal(t, sink.mLines[i].b);
                    EXPECT_NEAR(std::abs(a.z), h, kTol) << "capsule circle line " << i;
                    EXPECT_NEAR(a.z, b.z, kTol) << "capsule circle line " << i;
                    EXPECT_NEAR(std::hypot(a.x, a.y), r, kTol) << "capsule circle line " << i;
                    EXPECT_NEAR(std::hypot(b.x, b.y), r, kTol) << "capsule circle line " << i;
                }
                for (std::size_t i = 132; i < 136; ++i)
                {
                    const math::Vec3 a = toLocal(t, sink.mLines[i].a);
                    const math::Vec3 b = toLocal(t, sink.mLines[i].b);
                    EXPECT_NEAR(a.x, b.x, kTol) << "capsule side line " << i;
                    EXPECT_NEAR(a.y, b.y, kTol) << "capsule side line " << i;
                    EXPECT_NEAR(std::hypot(a.x, a.y), r, kTol) << "capsule side line " << i;
                    EXPECT_NEAR(std::min(a.z, b.z), -h, kTol) << "capsule side line " << i;
                    EXPECT_NEAR(std::max(a.z, b.z), h, kTol) << "capsule side line " << i;
                }
            }
        }

        TEST_F(CmGizmosTest, drawingTheShapesTwiceGivesTheSameLines)
        {
            auto document = open(makeShapesRig());
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.shapes");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> first = draw(*gizmo);
            const std::vector<DrawnLine> second = draw(*gizmo);
            ASSERT_EQ(first.size(), second.size());
            for (std::size_t i = 0; i < first.size(); ++i)
                EXPECT_TRUE(sameLine(first[i], second[i])) << "line " << i;
        }

        TEST_F(CmGizmosTest, theShapeGizmoDrawsOneShapePerBody)
        {
            const rig::Rig value = makeLimitsRig(); // one box and three capsules
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.shapes");
            ASSERT_NE(gizmo, nullptr);
            EXPECT_EQ(draw(*gizmo).size(), 12u + 3u * 100u);
        }

        // ---- limits ---------------------------------------------------------------------------------------

        TEST_F(CmGizmosTest, theLimitsGizmoDrawsTheSpecifiedLinesForEveryJoint)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);

            RecordingSink sink;
            gizmo->draw(sink);
            EXPECT_EQ(sink.mPoints, 0);
            // cone 45 + pyramid 25 + hinge 17
            ASSERT_EQ(sink.mLines.size(), 45u + 25u + 17u);
            expectLinesMatch(sink.mLines, expectedLimitLines(value));
        }

        TEST_F(CmGizmosTest, theTwistAxisLineAndTheTwistArcEndsMatchTheJointFrame)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            ASSERT_EQ(lines.size(), 87u);

            const std::vector<math::Transform> world = worldRest(value);
            // The hinge (the last joint): 1 axis line and 16 arc lines.
            const rig::Joint& hinge = value.joints[2];
            const math::Transform& bone = world[boneIndex(value, hinge.bone)];
            const math::Vec3 x = hinge.twistAxis;
            const math::Vec3 z = hinge.planeAxis;
            const math::Vec3 y = math::cross(z, x);
            const std::size_t base = 45 + 25;

            EXPECT_TRUE(isNear(lines[base].a, bone.translation));
            EXPECT_TRUE(isNear(lines[base].b, bone.translation + kLimitLengthM * math::rotate(bone.rotation, x)));
            EXPECT_FLOAT_EQ(lines[base].colour.r, 1.0f);
            EXPECT_FLOAT_EQ(lines[base].colour.g, 1.0f);
            EXPECT_FLOAT_EQ(lines[base].colour.b, 1.0f);

            const auto arcPoint = [&](double angle) {
                return bone.translation
                    + 0.25 * kLimitLengthM * math::rotate(bone.rotation, std::cos(angle) * y + std::sin(angle) * z);
            };
            // First point at twistMin = 0, last at twistMax = 2.
            EXPECT_TRUE(isNear(lines[base + 1].a, arcPoint(hinge.twistMinRad)));
            EXPECT_TRUE(isNear(lines[base + 16].b, arcPoint(hinge.twistMaxRad)));
            for (std::size_t i = base + 1; i <= base + 16; ++i)
            {
                EXPECT_FLOAT_EQ(lines[i].colour.r, 0.1f);
                EXPECT_FLOAT_EQ(lines[i].colour.g, 0.9f);
                EXPECT_FLOAT_EQ(lines[i].colour.b, 0.9f);
            }
        }

        TEST_F(CmGizmosTest, aConesRimPointsAndSpokesMatchTheFormula)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            ASSERT_GE(lines.size(), 45u);

            // The Spine cone is the first joint: line 0 is its twist axis, lines 1..24 the rim, 25..28 the spokes.
            const rig::Joint& joint = value.joints[0];
            const math::Transform bone = worldRest(value)[boneIndex(value, joint.bone)];
            const math::Vec3 x = joint.twistAxis;
            const math::Vec3 z = joint.planeAxis;
            const math::Vec3 y = math::cross(z, x);
            const auto rimPoint = [&](int k) {
                const double t = 2.0 * std::numbers::pi * k / 24.0;
                const double p = joint.planeSwingMaxRad * std::cos(t);
                const double n = joint.normalSwingMaxRad * std::sin(t);
                const math::Vec3 direction
                    = std::cos(p) * std::cos(n) * x + std::cos(p) * std::sin(n) * y + (-std::sin(p)) * z;
                return bone.translation + kLimitLengthM * math::rotate(bone.rotation, direction);
            };
            for (int k = 0; k < 24; ++k)
            {
                EXPECT_TRUE(isNear(lines[1 + k].a, rimPoint(k))) << "rim " << k;
                EXPECT_TRUE(isNear(lines[1 + k].b, rimPoint((k + 1) % 24))) << "rim " << k;
            }
            const int spokes[4] = { 0, 6, 12, 18 };
            for (int s = 0; s < 4; ++s)
            {
                EXPECT_TRUE(isNear(lines[25 + s].a, bone.translation)) << "spoke " << s;
                EXPECT_TRUE(isNear(lines[25 + s].b, rimPoint(spokes[s]))) << "spoke " << s;
            }
        }

        TEST_F(CmGizmosTest, aPyramidDrawsItsFourCornersAndSpokes)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            ASSERT_GE(lines.size(), 70u);

            const rig::Joint& joint = value.joints[1];
            const math::Transform bone = worldRest(value)[boneIndex(value, joint.bone)];
            const math::Vec3 x = joint.twistAxis;
            const math::Vec3 z = joint.planeAxis;
            const math::Vec3 y = math::cross(z, x);
            const auto corner = [&](double p, double n) {
                const math::Vec3 direction
                    = std::cos(p) * std::cos(n) * x + std::cos(p) * std::sin(n) * y + (-std::sin(p)) * z;
                return bone.translation + kLimitLengthM * math::rotate(bone.rotation, direction);
            };
            const math::Vec3 corners[4] = {
                corner(joint.planeSwingMinRad, joint.normalSwingMinRad),
                corner(joint.planeSwingMaxRad, joint.normalSwingMinRad),
                corner(joint.planeSwingMaxRad, joint.normalSwingMaxRad),
                corner(joint.planeSwingMinRad, joint.normalSwingMaxRad),
            };
            // The thigh follows the cone's 45 lines: axis, then 4 loop lines, 4 spokes, 16 arc lines.
            const std::size_t base = 45;
            for (int k = 0; k < 4; ++k)
            {
                EXPECT_TRUE(isNear(lines[base + 1 + k].a, corners[k])) << "loop " << k;
                EXPECT_TRUE(isNear(lines[base + 1 + k].b, corners[(k + 1) % 4])) << "loop " << k;
                EXPECT_TRUE(isNear(lines[base + 5 + k].a, bone.translation)) << "spoke " << k;
                EXPECT_TRUE(isNear(lines[base + 5 + k].b, corners[k])) << "spoke " << k;
            }
        }

        TEST_F(CmGizmosTest, aHingeDrawsAnAxisAndATwistArcOnly)
        {
            rig::Rig value = makeLimitsRig();
            // Only the hinge; the bodies of Spine and LeftThigh go, so that the shin's body hangs off the pelvis body.
            value.joints = { value.joints[2] };
            value.bodies.erase(value.bodies.begin() + 1, value.bodies.begin() + 3);
            auto document = open(value, "hinge");
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            EXPECT_EQ(lines.size(), 17u);
        }

        TEST_F(CmGizmosTest, aZeroTwistRangeStillDrawsSixteenZeroLengthArcLines)
        {
            rig::Rig value = makeLimitsRig();
            value.joints[1].twistMinRad = 0.0;
            value.joints[1].twistMaxRad = 0.0;
            auto document = open(value, "zerotwist");
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            ASSERT_EQ(lines.size(), 87u);
            // The thigh's arc is its last 16 lines before the hinge's 17.
            for (std::size_t i = 45 + 9; i < 45 + 25; ++i)
                EXPECT_TRUE(isNear(lines[i].a, toMath(lines[i].b), 1e-12)) << "arc line " << i;
            expectLinesMatch(lines, expectedLimitLines(value));
        }

        TEST_F(CmGizmosTest, aConeWithOneZeroHalfAngleStillDrawsItsRim)
        {
            rig::Rig value = makeLimitsRig();
            value.joints[0].planeSwingMinRad = 0.0;
            value.joints[0].planeSwingMaxRad = 0.0;
            auto document = open(value, "flatcone");
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> lines = draw(*gizmo);
            ASSERT_EQ(lines.size(), 87u);
            expectLinesMatch(lines, expectedLimitLines(value));
        }

        TEST_F(CmGizmosTest, drawingTheLimitsTwiceGivesTheSameLines)
        {
            auto document = open(makeLimitsRig());
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> first = draw(*gizmo);
            const std::vector<DrawnLine> second = draw(*gizmo);
            ASSERT_EQ(first.size(), second.size());
            for (std::size_t i = 0; i < first.size(); ++i)
                EXPECT_TRUE(sameLine(first[i], second[i])) << "line " << i;
        }

        TEST_F(CmGizmosTest, anEditChangesWhatTheLimitsGizmoDraws)
        {
            rig::Rig value = makeLimitsRig();
            auto document = open(value);
            ASSERT_NE(document, nullptr);
            Gizmo* gizmo = gizmoById(*document, "creativemotion.rig.limits");
            ASSERT_NE(gizmo, nullptr);
            const std::vector<DrawnLine> before = draw(*gizmo);

            auto planeMax
                = document->properties().makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(0.9));
            auto twistMax = document->properties().makeSetCommand("joint.LeftShin.twistMaxRad", PropertyValue(1.5));
            auto coneTwist = document->properties().makeSetCommand("joint.Spine.twistMaxRad", PropertyValue(0.1));
            ASSERT_NE(planeMax, nullptr);
            ASSERT_NE(twistMax, nullptr);
            ASSERT_NE(coneTwist, nullptr);
            // Nothing changed before redo.
            const std::vector<DrawnLine> unchanged = draw(*gizmo);
            ASSERT_EQ(unchanged.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i)
                EXPECT_TRUE(sameLine(before[i], unchanged[i])) << "line " << i;

            planeMax->redo();
            twistMax->redo();
            coneTwist->redo();

            value.joints[1].planeSwingMaxRad = 0.9;
            value.joints[2].twistMaxRad = 1.5;
            value.joints[0].twistMaxRad = 0.1;
            const std::vector<DrawnLine> after = draw(*gizmo);
            ASSERT_EQ(after.size(), before.size());
            expectLinesMatch(after, expectedLimitLines(value));
            bool anyDifferent = false;
            for (std::size_t i = 0; i < before.size(); ++i)
                anyDifferent = anyDifferent || !sameLine(before[i], after[i]);
            EXPECT_TRUE(anyDifferent);

            // Undo draws the original again.
            coneTwist->undo();
            twistMax->undo();
            planeMax->undo();
            const std::vector<DrawnLine> undone = draw(*gizmo);
            ASSERT_EQ(undone.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i)
                EXPECT_TRUE(sameLine(before[i], undone[i])) << "line " << i;
        }
    }
}
