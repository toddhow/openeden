#include <gtest/gtest.h>

#include <cmath>

#include <vsg/maths/transform.h>

#include <apps/opencs/view/render/camerapose.hpp>

// Written from the OSG editor's cameracontroller.cpp: `getViewMatrix() *= osg::Matrixd::rotate(value, axis)` applies
// the rotation after the view matrix, in eye space, and osg::Matrixd::getLookAt gives eye, centre and up. Eye space is
// the same on both: x right, y up, looking along -z. VSG's Y flip is in the projection, not here.
namespace
{
    namespace Pose = CSVRender::CameraPose;

    constexpr double sTolerance = 1e-9;

    const vsg::dvec3 sEye(100, 200, 300);
    const vsg::dvec3 sForward(0, 1, 0);
    const vsg::dvec3 sUp(0, 0, 1);
    const vsg::dvec3 sRight(1, 0, 0);

    vsg::dmat4 startView()
    {
        return Pose::viewFrom(sEye, sEye + sForward, sUp);
    }

    void expectNear(const vsg::dvec3& actual, const vsg::dvec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, sTolerance);
        EXPECT_NEAR(actual.y, expected.y, sTolerance);
        EXPECT_NEAR(actual.z, expected.z, sTolerance);
    }

    TEST(CameraPose, lookAtGivesBackWhatTheViewWasBuiltFrom)
    {
        const Pose::LookAt lookAt = Pose::lookAtFrom(startView());
        expectNear(lookAt.mEye, sEye);
        expectNear(lookAt.mCenter, sEye + sForward);
        expectNear(lookAt.mUp, sUp);
    }

    TEST(CameraPose, lookAtPutsTheCentreAtTheDistanceAsked)
    {
        expectNear(Pose::lookAtFrom(startView(), 50).mCenter, sEye + sForward * 50.0);
    }

    TEST(CameraPose, positiveRotationAboutEyeUpTurnsTheCameraRight)
    {
        const double angle = 0.25;
        const auto lookAt = Pose::lookAtFrom(Pose::rotatedInEye(startView(), angle, vsg::dvec3(0, 1, 0)));
        expectNear(lookAt.mEye, sEye);
        expectNear(lookAt.mCenter - lookAt.mEye, sForward * std::cos(angle) + sRight * std::sin(angle));
        expectNear(lookAt.mUp, sUp);
    }

    TEST(CameraPose, positiveRotationAboutEyeXLooksDown)
    {
        const double angle = 0.25;
        const auto lookAt = Pose::lookAtFrom(Pose::rotatedInEye(startView(), angle, vsg::dvec3(1, 0, 0)));
        expectNear(lookAt.mEye, sEye);
        expectNear(lookAt.mCenter - lookAt.mEye, sForward * std::cos(angle) - sUp * std::sin(angle));
    }

    TEST(CameraPose, positiveRotationAboutEyeZRollsTheCameraClockwiseSeenFromBehind)
    {
        // Eye +z points back at the viewer; a world up vector appearing to rotate counter-clockwise means the camera
        // rolled clockwise, towards its right.
        const double angle = 0.25;
        const auto lookAt = Pose::lookAtFrom(Pose::rotatedInEye(startView(), angle, vsg::dvec3(0, 0, 1)));
        expectNear(lookAt.mUp, sUp * std::cos(angle) + sRight * std::sin(angle));
    }

    TEST(CameraPose, eyeSpaceTranslationAlongZMovesTheCameraForward)
    {
        const auto lookAt = Pose::lookAtFrom(Pose::translatedInEye(startView(), vsg::dvec3(0, 0, 10)));
        expectNear(lookAt.mEye, sEye + sForward * 10.0);
    }

    TEST(CameraPose, eyeSpaceTranslationAlongXMovesTheCameraLeft)
    {
        const auto lookAt = Pose::lookAtFrom(Pose::translatedInEye(startView(), vsg::dvec3(10, 0, 0)));
        expectNear(lookAt.mEye, sEye - sRight * 10.0);
    }

    TEST(CameraPose, eyeDirectionsMapToTheWorld)
    {
        expectNear(Pose::eyeToWorldDirection(startView(), vsg::dvec3(1, 0, 0)), sRight);
        expectNear(Pose::eyeToWorldDirection(startView(), vsg::dvec3(0, 1, 0)), sUp);
        expectNear(Pose::eyeToWorldDirection(startView(), vsg::dvec3(0, 0, 1)), -sForward);
    }

    TEST(CameraPose, rotatingAVectorFollowsTheRightHandRule)
    {
        expectNear(Pose::rotated(vsg::dvec3(1, 0, 0), std::acos(-1.0) / 2, vsg::dvec3(0, 0, 1)), vsg::dvec3(0, 1, 0));
    }

    TEST(CameraPose, rotatingNormalizesTheAxis)
    {
        expectNear(Pose::rotated(vsg::dvec3(1, 0, 0), std::acos(-1.0) / 2, vsg::dvec3(0, 0, 7)), vsg::dvec3(0, 1, 0));
    }

    TEST(CameraPose, aZeroAxisLeavesTheVectorAlone)
    {
        expectNear(Pose::rotated(vsg::dvec3(1, 2, 3), 1.0, vsg::dvec3(0, 0, 0)), vsg::dvec3(1, 2, 3));
    }

    TEST(CameraPose, orthoNormalRemovesScaleAndKeepsThePose)
    {
        vsg::dmat4 scaled = vsg::scale(2.0, 2.0, 2.0) * startView();
        scaled[3] = startView()[3];
        const auto lookAt = Pose::lookAtFrom(Pose::orthoNormal(scaled));
        expectNear(lookAt.mEye, sEye);
        expectNear(lookAt.mUp, sUp);
    }

    TEST(CameraPose, orbitingBackByTheSameAngleReturnsToTheStart)
    {
        const vsg::dvec3 offset(10, -20, 30);
        const vsg::dvec3 axis = vsg::normalize(vsg::dvec3(1, 1, 1));
        expectNear(Pose::rotated(Pose::rotated(offset, 0.7, axis), -0.7, axis), offset);
    }
}
