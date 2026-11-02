#include <gtest/gtest.h>

#include <components/render/volumemath.hpp>

namespace
{
    using namespace Render::Volume;

    TEST(RenderVolumeCamera, aStillCameraHasNotJumped)
    {
        EXPECT_FALSE(cameraJumped(10, 20, 30, 10, 20, 30));
    }

    TEST(RenderVolumeCamera, aWalkAndARunAreNotJumps)
    {
        // About 5 game units a frame at walking speed, a few dozen at the fastest levitation.
        EXPECT_FALSE(cameraJumped(0, 0, 0, 5, 0, 0));
        EXPECT_FALSE(cameraJumped(0, 0, 0, 40, 40, 40));
    }

    TEST(RenderVolumeCamera, aTeleportIsAJump)
    {
        EXPECT_TRUE(cameraJumped(0, 0, 0, 1000, 0, 0));
        EXPECT_TRUE(cameraJumped(0, 0, 0, 0, 0, -500));
    }

    TEST(RenderVolumeCamera, theLimitIsTheDistanceNotTheComponents)
    {
        // 200 along each of two axes is 283 units: a jump, although no component alone reaches the limit.
        EXPECT_TRUE(cameraJumped(0, 0, 0, 200, 200, 0));
        EXPECT_FALSE(cameraJumped(0, 0, 0, cameraJumpDistance, 0, 0));
    }

    TEST(RenderVolumeCamera, theDirectionDoesNotMatter)
    {
        EXPECT_EQ(cameraJumped(0, 0, 0, 300, 0, 0), cameraJumped(300, 0, 0, 0, 0, 0));
    }
}
