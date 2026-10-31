#include <components/misc/convert.hpp>

#include <osg/Matrixd>
#include <osg/Quat>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <gtest/gtest.h>

// Misc::Convert moved from OSG's math types to VSG's in task 0013 (Track V stage V5), and physics results must not change.
// The functions in namespace Original are convert.hpp's bodies as they were, on OSG's types (commit 4ec68b683b), and each test
// asks for the same bits. They go with extern/osg at the end of V5, when these become literal expected values.
namespace
{
    namespace Original
    {
        osg::Matrixd toOsgNoScale(const JPH::Mat44& joltMatrix)
        {
            osg::Matrixd mat;
            auto inPosition = joltMatrix.GetTranslation();
            auto subRot = joltMatrix.GetRotation().GetQuaternion();
            mat.makeRotate(osg::Quat(subRot.GetX(), subRot.GetY(), subRot.GetZ(), subRot.GetW()));
            mat.setTrans(osg::Vec3f(inPosition.GetX(), inPosition.GetY(), inPosition.GetZ()));
            return mat;
        }

        JPH::Quat toJolt(const osg::Quat& quat)
        {
            return JPH::Quat(static_cast<float>(quat.x()), static_cast<float>(quat.y()), static_cast<float>(quat.z()),
                static_cast<float>(quat.w()));
        }

        JPH::RMat44 toJoltNoScale(const osg::Matrixd& mat)
        {
            JPH::Quat quat = toJolt(mat.getRotate()).Normalized();
            osg::Vec3d trans = mat.getTrans();
            return JPH::RMat44::sRotationTranslation(quat, JPH::RVec3(trans.x(), trans.y(), trans.z()));
        }

        osg::Quat makeOsgQuat(const float (&rotation)[3])
        {
            return osg::Quat(rotation[2], osg::Vec3f(0, 0, -1)) * osg::Quat(rotation[1], osg::Vec3f(0, -1, 0))
                * osg::Quat(rotation[0], osg::Vec3f(-1, 0, 0));
        }

        osg::Quat makeQuaternion(const float (&rotation)[3])
        {
            return osg::Quat(0, 0, -1, rotation[2]) * osg::Quat(0, -1, 0, rotation[1])
                * osg::Quat(-1, 0, 0, rotation[0]);
        }

        osg::Matrixd makeOSGTransform(const ESM::Position& position)
        {
            osg::Matrixd mat;
            mat.setRotate(makeQuaternion(position.rot));
            mat.setTrans(position.asVec3());
            return mat;
        }
    }

    vsg::dmat4 sameNumbers(const osg::Matrixd& source)
    {
        vsg::dmat4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result[i][j] = source(i, j);
        return result;
    }

    void expectSame(const osg::Matrixd& expected, const vsg::dmat4& actual)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                EXPECT_EQ(expected(i, j), actual[i][j]) << i << ", " << j;
    }

    void expectSame(const osg::Quat& expected, const vsg::dquat& actual)
    {
        EXPECT_EQ(expected.x(), actual.x);
        EXPECT_EQ(expected.y(), actual.y);
        EXPECT_EQ(expected.z(), actual.z);
        EXPECT_EQ(expected.w(), actual.w);
    }

    void expectSame(const JPH::RMat44& expected, const JPH::RMat44& actual)
    {
        const JPH::Quat expectedRotation = expected.GetQuaternion();
        const JPH::Quat actualRotation = actual.GetQuaternion();
        EXPECT_EQ(expectedRotation.GetX(), actualRotation.GetX());
        EXPECT_EQ(expectedRotation.GetY(), actualRotation.GetY());
        EXPECT_EQ(expectedRotation.GetZ(), actualRotation.GetZ());
        EXPECT_EQ(expectedRotation.GetW(), actualRotation.GetW());
        EXPECT_EQ(expected.GetTranslation().GetX(), actual.GetTranslation().GetX());
        EXPECT_EQ(expected.GetTranslation().GetY(), actual.GetTranslation().GetY());
        EXPECT_EQ(expected.GetTranslation().GetZ(), actual.GetTranslation().GetZ());
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

    // Callers on the other side of the bridge still pass OSG's types.
    TEST(MiscConvert, toJoltAcceptsOsgTypesThroughTheBridge)
    {
        const JPH::RVec3 fromF = Misc::Convert::toJolt<JPH::RVec3>(osg::Vec3f(0.1f, 0.2f, 0.3f));
        EXPECT_EQ(fromF.GetX(), static_cast<double>(0.1f));
        const JPH::RVec3 fromD = Misc::Convert::toJolt<JPH::RVec3>(osg::Vec3d(0.1, 0.2, 0.3));
        EXPECT_EQ(fromD.GetX(), 0.1);
        const JPH::Quat fromQ = Misc::Convert::toJolt(osg::Quat(0.1, 0.2, 0.3, 0.4));
        EXPECT_EQ(fromQ.GetW(), 0.4f);
    }

    TEST(MiscConvert, toJoltFromQuatNarrowsEachElement)
    {
        const JPH::Quat expected = Original::toJolt(osg::Quat(0.1, -0.2, 0.3, 0.9));
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
            expectSame(Original::toOsgNoScale(m), Misc::Convert::toOsgNoScale(m));
        }
    }

    TEST(MiscConvert, toJoltNoScaleMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            osg::Matrixd original = osg::Matrixd::rotate(Original::makeOsgQuat(rotation));
            original.setTrans(osg::Vec3d(1000000000.5, -0.25, 7.125));
            expectSame(Original::toJoltNoScale(original), Misc::Convert::toJoltNoScale(sameNumbers(original)));
        }
    }

    // getRotate does not remove a scale, so a scaled matrix is where another rotation-from-matrix would show.
    TEST(MiscConvert, toJoltNoScaleOnAScaledMatrixMatchesOsg)
    {
        osg::Matrixd original
            = osg::Matrixd::scale(2.0, 2.0, 2.0) * osg::Matrixd::rotate(Original::makeOsgQuat(kRotations[1]));
        original.setTrans(osg::Vec3d(1.0, 2.0, 3.0));
        expectSame(Original::toJoltNoScale(original), Misc::Convert::toJoltNoScale(sameNumbers(original)));
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
            expectSame(Original::makeOsgQuat(rotation), Misc::Convert::makeOsgQuat(rotation));
            expectSame(Original::makeOsgQuat(rotation), Misc::Convert::makeOsgQuat(makePosition(rotation)));
        }
    }

    TEST(MiscConvert, makeQuaternionMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            expectSame(Original::makeQuaternion(rotation), Misc::Convert::makeQuaternion(rotation));
            expectSame(Original::makeQuaternion(rotation), Misc::Convert::makeQuaternion(makePosition(rotation)));
        }
    }

    TEST(MiscConvert, makeOSGTransformMatchesOsg)
    {
        for (const auto& rotation : kRotations)
        {
            const ESM::Position position = makePosition(rotation);
            expectSame(Original::makeOSGTransform(position), Misc::Convert::makeOSGTransform(position));
        }
    }
}
