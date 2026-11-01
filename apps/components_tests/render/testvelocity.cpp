// Tests for Render::Velocity's pure parts (task 0026, stage b): the matrix the velocity shader is given, and the
// rule that decides when the previous frame's camera is thrown away.
#include <components/render/gbuffermath.hpp>
#include <components/render/velocity.hpp>

#include <gtest/gtest.h>

#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>

namespace
{
    using namespace Render;

    // A plain perspective projection in the shape the engine's has: x_clip = a x, y_clip = b y, w = -z.
    vsg::dmat4 projection()
    {
        vsg::dmat4 p(0.0);
        p(0, 0) = 1.5;
        p(1, 1) = -2.0;
        p(2, 2) = -1.0;
        p(3, 2) = -0.5;
        p(2, 3) = -1.0;
        return p;
    }

    TEST(RenderVelocityTest, toPreviousClip_with_an_unmoved_camera_keeps_the_screen_position)
    {
        const vsg::dmat4 view = vsg::translate(10.0, -5.0, 3.0);
        const vsg::mat4 m = Velocity::toPreviousClip(projection(), view, vsg::inverse(view));
        // A view-space point maps to the clip position the projection gives it.
        const vsg::vec4 clip = m * vsg::vec4(2.f, 1.f, -10.f, 1.f);
        EXPECT_NEAR(clip.x / clip.w, 1.5 * 2.0 / 10.0, 1e-5);
        EXPECT_NEAR(clip.y / clip.w, -2.0 * 1.0 / 10.0, 1e-5);
    }

    TEST(RenderVelocityTest, toPreviousClip_agrees_with_reprojectVelocity)
    {
        const vsg::dmat4 proj = projection();
        const vsg::dmat4 view = vsg::translate(0.0, 0.0, 0.0);
        const vsg::dmat4 prevView = vsg::translate(-2.0, 0.0, 0.0); // the camera moved 2 units in x
        const vsg::mat4 m = Velocity::toPreviousClip(proj, prevView, vsg::inverse(view));

        const float depth = 10.f;
        const vsg::vec2 uv(0.75f, 0.5f);
        // The shader's way: view position from the uv and depth, then the matrix.
        const vsg::vec4 viewPos((uv.x * 2.f - 1.f) * depth / 1.5f, (uv.y * 2.f - 1.f) * depth / -2.f, -depth, 1.f);
        const vsg::vec4 prevClip = m * viewPos;
        const vsg::vec2 prevUv(prevClip.x / prevClip.w * 0.5f + 0.5f, prevClip.y / prevClip.w * 0.5f + 0.5f);
        const vsg::vec2 shaderVelocity(uv.x - prevUv.x, uv.y - prevUv.y);

        const vsg::vec2 twin = GBuffer::reprojectVelocity(uv, depth, vsg::mat4(proj), vsg::mat4(vsg::inverse(view)),
            vsg::mat4(prevView), vsg::mat4(proj));
        EXPECT_NEAR(shaderVelocity.x, twin.x, 1e-5);
        EXPECT_NEAR(shaderVelocity.y, twin.y, 1e-5);
    }

    TEST(RenderVelocityTest, cameraJumped_is_false_for_small_moves_and_true_for_a_teleport)
    {
        EXPECT_FALSE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, 0)));
        EXPECT_FALSE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(100, 0, 0)));
        EXPECT_TRUE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(100000, 0, 0)));
        EXPECT_TRUE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(0, 0, -100000)));
    }

    TEST(RenderVelocityTest, cameraJumped_threshold_is_the_named_constant)
    {
        const double just = Velocity::cameraJumpDistance;
        EXPECT_FALSE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(just * 0.9, 0, 0)));
        EXPECT_TRUE(Velocity::cameraJumped(vsg::dvec3(0, 0, 0), vsg::dvec3(just * 1.1, 0, 0)));
    }
}

namespace
{
    TEST(RenderVelocityTest, reprojectVelocity_is_zero_for_a_point_behind_the_previous_camera)
    {
        using namespace Render;
        vsg::dmat4 p(0.0);
        p(0, 0) = 1.5;
        p(1, 1) = -2.0;
        p(2, 2) = -1.0;
        p(3, 2) = -0.5;
        p(2, 3) = -1.0;
        // The previous view put the point 10 units behind the camera (z = -10 + 20 = +10).
        const vsg::dmat4 prevView = vsg::translate(0.0, 0.0, 20.0);
        const vsg::dmat4 identity = vsg::translate(0.0, 0.0, 0.0);
        const vsg::vec2 v = GBuffer::reprojectVelocity(
            vsg::vec2(0.7f, 0.4f), 10.f, vsg::mat4(p), vsg::mat4(identity), vsg::mat4(prevView), vsg::mat4(p));
        EXPECT_FLOAT_EQ(v.x, 0.f);
        EXPECT_FLOAT_EQ(v.y, 0.f);
    }
}
