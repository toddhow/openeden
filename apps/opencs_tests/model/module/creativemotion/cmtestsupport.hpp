#ifndef OPENCS_TESTS_MODEL_MODULE_CREATIVEMOTION_CMTESTSUPPORT_H
#define OPENCS_TESTS_MODEL_MODULE_CREATIVEMOTION_CMTESTSUPPORT_H

// Task 0032: shared helpers of the creativemotion_editor tests, written from the spec alone. Test rigs and profiles are
// built in code with opas::rig types and written with opas::rig::writeRig / writeProfile into temp folders.

#include <openeden/editor_module/types.hpp>

#include <opas/math/Quat.hpp>
#include <opas/math/Transform.hpp>
#include <opas/math/Vec3.hpp>
#include <opas/rig/Profile.hpp>
#include <opas/rig/ProfileFile.hpp>
#include <opas/rig/Rig.hpp>
#include <opas/rig/RigFile.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace CMTest
{
    namespace edit = openeden::editor;
    namespace rig = opas::rig;
    namespace math = opas::math;

    /// A normalised rotation of `angle` radians about `axis` (the axis need not be a unit vector).
    inline math::Quat rotation(math::Vec3 axis, double angle)
    {
        return math::normalized(math::fromAxisAngle(math::normalized(axis), angle));
    }

    inline rig::Bone makeBone(std::string name, int parent, math::Vec3 translation, math::Quat rotationQ)
    {
        rig::Bone bone;
        bone.name = std::move(name);
        bone.parent = parent;
        bone.localRest = math::Transform{ translation, rotationQ };
        return bone;
    }

    inline rig::Body makeBody(std::string bone, rig::Shape shape, math::Transform local, double massKg)
    {
        rig::Body body;
        body.bone = std::move(bone);
        body.shape = shape;
        body.local = local;
        body.massKg = massKg;
        return body;
    }

    inline rig::Joint makeCone(std::string bone, math::Vec3 twistAxis, math::Vec3 planeAxis, double planeHalf,
        double normalHalf, double twistMin, double twistMax)
    {
        rig::Joint joint;
        joint.bone = std::move(bone);
        joint.twistAxis = twistAxis;
        joint.planeAxis = planeAxis;
        joint.swing = rig::SwingShape::Cone;
        joint.planeSwingMinRad = -planeHalf;
        joint.planeSwingMaxRad = planeHalf;
        joint.normalSwingMinRad = -normalHalf;
        joint.normalSwingMaxRad = normalHalf;
        joint.twistMinRad = twistMin;
        joint.twistMaxRad = twistMax;
        return joint;
    }

    inline rig::Joint makePyramid(std::string bone, math::Vec3 twistAxis, math::Vec3 planeAxis, double planeMin,
        double planeMax, double normalMin, double normalMax, double twistMin, double twistMax)
    {
        rig::Joint joint;
        joint.bone = std::move(bone);
        joint.twistAxis = twistAxis;
        joint.planeAxis = planeAxis;
        joint.swing = rig::SwingShape::Pyramid;
        joint.planeSwingMinRad = planeMin;
        joint.planeSwingMaxRad = planeMax;
        joint.normalSwingMinRad = normalMin;
        joint.normalSwingMaxRad = normalMax;
        joint.twistMinRad = twistMin;
        joint.twistMaxRad = twistMax;
        return joint;
    }

    /// Four bones with non-identity rest rotations (so every bone's world rest rotation is not the identity), four
    /// bodies and three joints in this order: Spine (cone), LeftThigh (pyramid), LeftShin (hinge).
    inline rig::Rig makeLimitsRig()
    {
        rig::Rig result;
        result.name = "cm-test-rig";
        result.skeleton.bones = {
            makeBone("Pelvis", -1, { 0.1, -0.2, 1.0 }, rotation({ 0.0, 0.6, 0.8 }, 0.9)),
            makeBone("Spine", 0, { 0.0, 0.05, 0.2 }, rotation({ 1.0, 2.0, 2.0 }, 0.7)),
            makeBone("LeftThigh", 0, { 0.1, 0.0, -0.05 }, rotation({ 0.0, 1.0, 0.0 }, 0.4)),
            makeBone("LeftShin", 2, { 0.0, 0.0, -0.4 }, rotation({ 1.0, 0.0, 0.0 }, -0.3)),
        };
        result.bodies = {
            makeBody("Pelvis", rig::BoxShape{ { 0.1, 0.12, 0.08 } }, math::Transform{}, 5.0),
            makeBody("Spine", rig::CapsuleShape{ 0.1, 0.06 }, math::Transform{ { 0.0, 0.0, 0.1 }, math::Quat{} }, 4.0),
            makeBody(
                "LeftThigh", rig::CapsuleShape{ 0.15, 0.05 }, math::Transform{ { 0.0, 0.0, -0.2 }, math::Quat{} }, 3.0),
            makeBody(
                "LeftShin", rig::CapsuleShape{ 0.15, 0.04 }, math::Transform{ { 0.0, 0.0, -0.2 }, math::Quat{} }, 2.0),
        };
        result.joints = {
            makeCone("Spine", { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, 0.4, 0.7, -0.2, 0.3),
            makePyramid("LeftThigh", { 0.0, 0.0, -1.0 }, { 1.0, 0.0, 0.0 }, -0.3, 1.2, -0.5, 0.6, -0.4, 0.4),
            makeCone("LeftShin", { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, 0.0, 0.0, 0.0, 2.0),
        };
        return result;
    }

    /// Three bodies in this order: a box on Pelvis, a sphere on Spine and a capsule on LeftThigh, each with a
    /// non-trivial local transform on a bone with a non-trivial world rest transform.
    inline rig::Rig makeShapesRig()
    {
        rig::Rig result;
        result.name = "cm-shapes-rig";
        result.skeleton.bones = {
            makeBone("Pelvis", -1, { 0.3, -0.1, 0.9 }, rotation({ 0.0, 0.0, 1.0 }, 0.8)),
            makeBone("Spine", 0, { 0.0, 0.05, 0.2 }, rotation({ 1.0, 1.0, 0.0 }, 0.5)),
            makeBone("LeftThigh", 0, { 0.1, 0.0, -0.05 }, rotation({ 0.0, 1.0, 0.0 }, 0.4)),
        };
        result.bodies = {
            makeBody("Pelvis", rig::BoxShape{ { 0.1, 0.15, 0.2 } },
                math::Transform{ { 0.01, 0.02, 0.03 }, rotation({ 0.0, 0.0, 1.0 }, 0.5) }, 5.0),
            makeBody("Spine", rig::SphereShape{ 0.12 },
                math::Transform{ { 0.0, 0.03, 0.1 }, rotation({ 1.0, 0.0, 0.0 }, 0.3) }, 4.0),
            makeBody("LeftThigh", rig::CapsuleShape{ 0.15, 0.05 },
                math::Transform{ { 0.0, 0.02, -0.15 }, rotation({ 0.0, 1.0, 0.0 }, 1.1) }, 3.0),
        };
        result.joints = {
            makeCone("Spine", { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, 0.0, 0.0, -0.2, 0.3),
            makeCone("LeftThigh", { 0.0, 0.0, -1.0 }, { 1.0, 0.0, 0.0 }, 0.0, 0.0, 0.0, 1.0),
        };
        return result;
    }

    /// Default motor 300/30/500, overrides for Spine (600/60/900) and LeftShin (50/5/40).
    inline rig::Profile makeProfile()
    {
        rig::Profile result;
        result.rig = "cm-test-rig";
        result.defaultMotor = rig::MotorGains{ 300.0, 30.0, 500.0 };
        result.defaultMaterial = rig::Material{ 0.8, 0.0, 0.05, 0.05 };
        result.joints = {
            rig::JointOverride{ "Spine", rig::MotorGains{ 600.0, 60.0, 900.0 } },
            rig::JointOverride{ "LeftShin", rig::MotorGains{ 50.0, 5.0, 40.0 } },
        };
        return result;
    }

    struct DrawnLine
    {
        edit::Vec3 a;
        edit::Vec3 b;
        edit::Colour colour;
    };

    class RecordingSink : public edit::OverlaySink
    {
    public:
        void line(const edit::Vec3& a, const edit::Vec3& b, const edit::Colour& colour) override
        {
            mLines.push_back(DrawnLine{ a, b, colour });
        }
        void point(const edit::Vec3&, const edit::Colour&, float) override { ++mPoints; }
        void label(const edit::Vec3&, std::string_view, const edit::Colour&) override { ++mLabels; }

        std::vector<DrawnLine> mLines;
        int mPoints = 0;
        int mLabels = 0;
    };

    inline edit::Vec3 toEdit(math::Vec3 v)
    {
        return edit::Vec3{ v.x, v.y, v.z };
    }

    inline math::Vec3 toMath(const edit::Vec3& v)
    {
        return math::Vec3{ v.x, v.y, v.z };
    }

    inline double distanceBetween(const edit::Vec3& a, math::Vec3 b)
    {
        return math::distance(toMath(a), b);
    }

    inline bool sameLine(const DrawnLine& l, const DrawnLine& r)
    {
        return l.a.x == r.a.x && l.a.y == r.a.y && l.a.z == r.a.z && l.b.x == r.b.x && l.b.y == r.b.y && l.b.z == r.b.z
            && l.colour.r == r.colour.r && l.colour.g == r.colour.g && l.colour.b == r.colour.b
            && l.colour.a == r.colour.a;
    }

    inline std::vector<std::string> splitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
            lines.push_back(line);
        return lines;
    }

    inline std::string readAll(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    inline void writeText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }

    /// Each test gets an empty temp folder of its own, removed afterwards.
    class TempDirTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            mDir = std::filesystem::temp_directory_path()
                / (std::string("eden-studio-cm-") + info->test_suite_name() + "-" + info->name());
            std::filesystem::remove_all(mDir);
            std::filesystem::create_directories(mDir);
        }

        void TearDown() override { std::filesystem::remove_all(mDir); }

        std::filesystem::path writeRigFile(const std::string& name, const rig::Rig& value)
        {
            const std::filesystem::path path = mDir / (name + ".rig.json");
            writeText(path, rig::writeRig(value));
            return path;
        }

        std::filesystem::path writeProfileFile(const std::string& name, const rig::Profile& value)
        {
            const std::filesystem::path path = mDir / (name + ".profile.json");
            writeText(path, rig::writeProfile(value));
            return path;
        }

        std::filesystem::path mDir;
    };
}

#endif
