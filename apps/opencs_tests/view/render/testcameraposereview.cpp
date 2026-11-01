#include <gtest/gtest.h>

#include <vsg/maths/transform.h>

#include <apps/opencs/view/render/camerapose.hpp>

namespace
{
    namespace Pose = CSVRender::CameraPose;

    // Review of C5: a controller normalizes every tick, which must not count as a change of view.
    TEST(CameraPoseReviewTest, normalizingAnUnmovedViewIsTheSameView)
    {
        const vsg::dmat4 view = Pose::viewFrom(
            vsg::dvec3(-12000.5, -68000.25, 300.125), vsg::dvec3(-11000, -67000, 100), vsg::dvec3(0, 0, 1));
        EXPECT_TRUE(Pose::sameView(view, Pose::orthoNormal(view)));
        EXPECT_FALSE(Pose::sameView(view, Pose::translatedInEye(view, vsg::dvec3(0.01, 0, 0))));
        EXPECT_FALSE(Pose::sameView(view, Pose::rotatedInEye(view, 1e-4, vsg::dvec3(0, 1, 0))));
    }

    // Review of C5: stabilize with the up parallel to the view would put NaN into the view for good.
    TEST(CameraPoseReviewTest, anUpAlongTheViewCannotStabilize)
    {
        const vsg::dvec3 eye(10, 20, 500);
        EXPECT_FALSE(Pose::canStabilize(eye, vsg::dvec3(10, 20, 0), vsg::dvec3(0, 0, 1)));
        EXPECT_FALSE(Pose::canStabilize(eye, vsg::dvec3(10, 20, 900), vsg::dvec3(0, 0, 1)));
        EXPECT_TRUE(Pose::canStabilize(eye, vsg::dvec3(10, 21, 0), vsg::dvec3(0, 0, 1)));
    }
}
