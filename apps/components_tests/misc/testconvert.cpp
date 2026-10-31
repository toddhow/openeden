#include "../math/conventions.hpp"

#include <components/misc/convert.hpp>

#include <gtest/gtest.h>

// Misc::Convert moved from OSG's math types to VSG's in task 0013 (Track V stage V5), and physics results must not
// change. asks for the same bits. They go with extern/osg at the end of V5, when these become literal expected values.
namespace
{
    using namespace MathConventions;

#include "testconvert.golden.inc"
    template <class Expected>
    void expectSameMatrix(const Expected& expected, const vsg::dmat4& actual)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                // Jolt's float quaternion is under these, and its last bits differ between compilers.
                EXPECT_TRUE(sameAsRecorded<float>(expected(i, j), actual[i][j], 1.0)) << i << ", " << j;
            }
    }

    void expectSame(const GoldenRow& expected, const vsg::dmat4& actual)
    {
        expectSameMatrix(expected, actual);
    }

    void expectSame(const GoldenRow& expected, const vsg::dquat& actual)
    {
        EXPECT_TRUE(recordedEqual(expected[0], actual.x));
        EXPECT_TRUE(recordedEqual(expected[1], actual.y));
        EXPECT_TRUE(recordedEqual(expected[2], actual.z));
        EXPECT_TRUE(recordedEqual(expected[3], actual.w));
    }

    // The quaternion and the translation, as the original code's JPH::RMat44 gave them.
    template <class Expected>
    void expectSameJolt(const Expected& expected, const JPH::RMat44& actual)
    {
        const JPH::Quat actualRotation = actual.GetQuaternion();
        EXPECT_TRUE(recordedEqual(expected[0], actualRotation.GetX()));
        EXPECT_TRUE(recordedEqual(expected[1], actualRotation.GetY()));
        EXPECT_TRUE(recordedEqual(expected[2], actualRotation.GetZ()));
        EXPECT_TRUE(recordedEqual(expected[3], actualRotation.GetW()));
        EXPECT_TRUE(recordedEqual(expected[4], actual.GetTranslation().GetX()));
        EXPECT_TRUE(recordedEqual(expected[5], actual.GetTranslation().GetY()));
        EXPECT_TRUE(recordedEqual(expected[6], actual.GetTranslation().GetZ()));
    }

    void expectSame(const GoldenRow& expected, const JPH::RMat44& actual)
    {
        expectSameJolt(expected, actual);
    }

    void expectSame(const std::array<double, 7>& expected, const JPH::RMat44& actual)
    {
        expectSameJolt(expected, actual);
    }

    const float kRotations[][3] = {
        { 0.f, 0.f, 0.f },
        { 0.3f, -1.1f, 2.7f },
        { 3.14159274f, 0.f, 0.f },
        { -0.0f, 1.57079637f, -3.14159274f },
        { 100.25f, -77.5f, 0.001f },
    };

    ESM::Position makePosition(const float (&rotation)[3])
    {
        ESM::Position position;
        position.pos[0] = 8192.125f;
        position.pos[1] = -123456.7f;
        position.pos[2] = 0.1f;
        for (int i = 0; i < 3; ++i)
            position.rot[i] = rotation[i];
        return position;
    }

    TEST(MiscConvert, makeOsgVec3fKeepsElements)
    {
        const float values[] = { 1.5f, -2.25f, 1e9f };
        const vsg::vec3 fromArray = Misc::Convert::makeOsgVec3f(values);
        EXPECT_EQ(fromArray.x, 1.5f);
        EXPECT_EQ(fromArray.y, -2.25f);
        EXPECT_EQ(fromArray.z, 1e9f);

        ESM::Pathgrid::Point point;
        point.mX = 16777217; // Not exact in a float: the cast rounds it.
        point.mY = -7;
        point.mZ = 3;
        const vsg::vec3 fromPoint = Misc::Convert::makeOsgVec3f(point);
        EXPECT_EQ(fromPoint.x, static_cast<float>(point.mX));
        EXPECT_EQ(fromPoint.y, -7.f);
        EXPECT_EQ(fromPoint.z, 3.f);
    }

    TEST(MiscConvert, toJoltFromFloatVector)
    {
        const vsg::vec3 v(0.1f, -0.2f, 1e7f);
        const JPH::Vec3 asVec3 = Misc::Convert::toJolt<JPH::Vec3>(v);
        EXPECT_EQ(asVec3.GetX(), 0.1f);
        EXPECT_EQ(asVec3.GetY(), -0.2f);
        EXPECT_EQ(asVec3.GetZ(), 1e7f);

        const JPH::RVec3 asRVec3 = Misc::Convert::toJolt<JPH::RVec3>(v);
        EXPECT_EQ(asRVec3.GetX(), static_cast<double>(0.1f));
        EXPECT_EQ(asRVec3.GetY(), static_cast<double>(-0.2f));
        EXPECT_EQ(asRVec3.GetZ(), static_cast<double>(1e7f));
    }

    TEST(MiscConvert, toJoltFromDoubleVector)
    {
        const vsg::dvec3 v(0.1, -1000000000.5, 3.3);
        const JPH::RVec3 asRVec3 = Misc::Convert::toJolt<JPH::RVec3>(v);
        EXPECT_EQ(asRVec3.GetX(), 0.1);
        EXPECT_EQ(asRVec3.GetY(), -1000000000.5);
        EXPECT_EQ(asRVec3.GetZ(), 3.3);

        const JPH::Vec3 asVec3 = Misc::Convert::toJolt<JPH::Vec3>(v);
        EXPECT_EQ(asVec3.GetX(), 0.1f);
        EXPECT_EQ(asVec3.GetY(), -1000000000.5f);
        EXPECT_EQ(asVec3.GetZ(), 3.3f);

        const JPH::Float3 asFloat3 = Misc::Convert::toJolt<JPH::Float3>(v);
        EXPECT_EQ(asFloat3.x, 0.1f);
        EXPECT_EQ(asFloat3.y, -1000000000.5f);
        EXPECT_EQ(asFloat3.z, 3.3f);
    }

    TEST(MiscConvert, toJoltFromQuatNarrowsEachElement)
    {
        const JPH::Quat expected(0.1f, -0.2f, 0.3f, 0.9f);
        const JPH::Quat actual = Misc::Convert::toJolt(vsg::dquat(0.1, -0.2, 0.3, 0.9));
        EXPECT_EQ(expected.GetX(), actual.GetX());
        EXPECT_EQ(expected.GetY(), actual.GetY());
        EXPECT_EQ(expected.GetZ(), actual.GetZ());
        EXPECT_EQ(expected.GetW(), actual.GetW());
    }

    TEST(MiscConvert, toOsgNoScaleMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            const JPH::Quat q = JPH::Quat::sEulerAngles(JPH::Vec3(rotation[0], rotation[1], rotation[2]));
            const JPH::Mat44 m = JPH::Mat44::sRotationTranslation(q, JPH::Vec3(10.5f, -20.25f, 1e5f));
            expectSame(MathConventions::golden(kGolden5), Misc::Convert::toOsgNoScale(m));
        }
    }

    TEST(MiscConvert, toJoltNoScaleMatchesOsg)
    {
        for (std::size_t i = 0; i < std::size(kRotations); ++i)
        {
            expectSame(MathConventions::golden(kGolden6),
                Misc::Convert::toJoltNoScale(MathConventions::golden(kGolden7).mat()));
        }
    }

    // getRotate does not remove a scale, so a scaled matrix is where another rotation-from-matrix would show.
    TEST(MiscConvert, toJoltNoScaleOnAScaledMatrixMatchesOsg)
    {
        expectSame(
            arrayOf(0.60561603307724, 0.01265491358935833, 0.786035418510437, -0.1233585923910141, 1.0, 2.0, 3.0),
            Misc::Convert::toJoltNoScale(matOf<double>(-0.8201672142839085, -0.3403725002438664, 1.7920581189491351,
                0.0, 0.3877156548281971, -1.9525041262317018, -0.19340167540463776, 0.0, 1.7824147417520189,
                0.2680936368620443, 0.8666738084534262, 0.0, 1.0, 2.0, 3.0, 1.0)));
    }

    TEST(MiscConvert, toOsgNarrowsToFloat)
    {
        const vsg::vec3 fromR = Misc::Convert::toOsg(JPH::RVec3(1000000000.5, 0.1, -3.3));
        EXPECT_EQ(fromR.x, 1000000000.5f);
        EXPECT_EQ(fromR.y, 0.1f);
        EXPECT_EQ(fromR.z, -3.3f);
        const vsg::vec3 fromV = Misc::Convert::toOsg(JPH::Vec3(0.1f, 0.2f, 0.3f));
        EXPECT_EQ(fromV.y, 0.2f);
        const vsg::vec3 fromF = Misc::Convert::toOsg(JPH::Float3(0.1f, 0.2f, 0.3f));
        EXPECT_EQ(fromF.z, 0.3f);
    }

    TEST(MiscConvert, toOsgXYTakesXAndY)
    {
        const vsg::vec2 fromVsg = Misc::Convert::toOsgXY(vsg::vec3(0.1f, 0.2f, 0.3f));
        EXPECT_EQ(fromVsg.x, 0.1f);
        EXPECT_EQ(fromVsg.y, 0.2f);
        const vsg::vec2 fromR = Misc::Convert::toOsgXY(JPH::RVec3(1000000000.5, 0.1, 3.0));
        EXPECT_EQ(fromR.x, 1000000000.5f);
        EXPECT_EQ(fromR.y, 0.1f);
        const vsg::vec2 fromV = Misc::Convert::toOsgXY(JPH::Vec3(0.1f, 0.2f, 0.3f));
        EXPECT_EQ(fromV.x, 0.1f);
        EXPECT_EQ(fromV.y, 0.2f);
    }

    TEST(MiscConvert, makeOsgQuatMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            expectSame(MathConventions::golden(kGolden10), Misc::Convert::makeOsgQuat(rotation));
            expectSame(MathConventions::golden(kGolden11), Misc::Convert::makeOsgQuat(makePosition(rotation)));
        }
    }

    TEST(MiscConvert, makeQuaternionMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            expectSame(MathConventions::golden(kGolden12), Misc::Convert::makeQuaternion(rotation));
            expectSame(MathConventions::golden(kGolden13), Misc::Convert::makeQuaternion(makePosition(rotation)));
        }
    }

    TEST(MiscConvert, makeOSGTransformMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            const ESM::Position position = makePosition(rotation);
            expectSame(MathConventions::golden(kGolden14), Misc::Convert::makeOSGTransform(position));
        }
    }
}
