// Tests for Render::GBuffer (task 0026, "Interfaces (pure parts)") and Tonemap::debugPushConstants.
//
// Written from the spec alone, without reading the implementation.

#include <components/render/gbuffermath.hpp>
#include <components/render/tonemap.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>

namespace
{
    using namespace Render;
    using namespace Render::GBuffer;

    // ---- parseDebugView
    TEST(RenderGBufferTest, parseDebugView_known_names)
    {
        ASSERT_TRUE(parseDebugView("none").has_value());
        EXPECT_EQ(*parseDebugView("none"), DebugView::None);
        EXPECT_EQ(*parseDebugView("normals"), DebugView::Normals);
        EXPECT_EQ(*parseDebugView("depth"), DebugView::Depth);
        EXPECT_EQ(*parseDebugView("velocity"), DebugView::Velocity);
    }

    TEST(RenderGBufferTest, parseDebugView_unknown_and_empty_are_nullopt)
    {
        EXPECT_FALSE(parseDebugView("").has_value());
        EXPECT_FALSE(parseDebugView("albedo").has_value());
        EXPECT_FALSE(parseDebugView("normal").has_value());
        EXPECT_FALSE(parseDebugView(" depth").has_value());
    }

    TEST(RenderGBufferTest, parseDebugView_is_case_sensitive)
    {
        // The spec lists the lower-case names only and says "else nullopt".
        EXPECT_FALSE(parseDebugView("Normals").has_value());
        EXPECT_FALSE(parseDebugView("DEPTH").has_value());
        EXPECT_FALSE(parseDebugView("None").has_value());
    }

    // ---- useGBuffer
    TEST(RenderGBufferTest, useGBuffer_truth_table)
    {
        EXPECT_FALSE(useGBuffer(false, false));
        EXPECT_FALSE(useGBuffer(false, true));
        EXPECT_FALSE(useGBuffer(true, false));
        EXPECT_TRUE(useGBuffer(true, true));
    }

    // ---- formats
    TEST(RenderGBufferTest, format_constants)
    {
        EXPECT_EQ(normalFormat, VK_FORMAT_R16G16B16A16_SFLOAT);
        EXPECT_EQ(linearDepthFormat, VK_FORMAT_R32_SFLOAT);
        EXPECT_EQ(objectMotionFormat, VK_FORMAT_R16G16_SFLOAT);
        EXPECT_EQ(velocityFormat, VK_FORMAT_R16G16_SFLOAT);
    }

    TEST(RenderGBufferTest, extraColorFormats_without_object_motion)
    {
        const std::vector<VkFormat> formats = extraColorFormats(false);
        ASSERT_EQ(formats.size(), 2u);
        EXPECT_EQ(formats[0], normalFormat);
        EXPECT_EQ(formats[1], linearDepthFormat);
    }

    TEST(RenderGBufferTest, extraColorFormats_with_object_motion)
    {
        const std::vector<VkFormat> formats = extraColorFormats(true);
        ASSERT_EQ(formats.size(), 3u);
        EXPECT_EQ(formats[0], normalFormat);
        EXPECT_EQ(formats[1], linearDepthFormat);
        EXPECT_EQ(formats[2], objectMotionFormat);
    }

    // ---- debugNormalColor
    TEST(RenderGBufferTest, debugNormalColor_zero_vector_is_black)
    {
        const vsg::vec3 c = debugNormalColor(vsg::vec3(0.f, 0.f, 0.f));
        EXPECT_FLOAT_EQ(c.x, 0.f);
        EXPECT_FLOAT_EQ(c.y, 0.f);
        EXPECT_FLOAT_EQ(c.z, 0.f);
    }

    TEST(RenderGBufferTest, debugNormalColor_facing_the_camera)
    {
        const vsg::vec3 c = debugNormalColor(vsg::vec3(0.f, 0.f, 1.f));
        EXPECT_FLOAT_EQ(c.x, 0.5f);
        EXPECT_FLOAT_EQ(c.y, 0.5f);
        EXPECT_FLOAT_EQ(c.z, 1.f);
    }

    TEST(RenderGBufferTest, debugNormalColor_maps_minus_one_to_one_onto_zero_to_one)
    {
        const vsg::vec3 c = debugNormalColor(vsg::vec3(-1.f, 1.f, -1.f));
        EXPECT_FLOAT_EQ(c.x, 0.f);
        EXPECT_FLOAT_EQ(c.y, 1.f);
        EXPECT_FLOAT_EQ(c.z, 0.f);
    }

    TEST(RenderGBufferTest, debugNormalColor_is_n_times_half_plus_half)
    {
        const vsg::vec3 c = debugNormalColor(vsg::vec3(0.6f, -0.8f, 0.f));
        EXPECT_NEAR(c.x, 0.8f, 1e-6f);
        EXPECT_NEAR(c.y, 0.1f, 1e-6f);
        EXPECT_NEAR(c.z, 0.5f, 1e-6f);
    }

    // ---- debugDepthGray
    TEST(RenderGBufferTest, debugDepthGray_no_surface_is_zero)
    {
        EXPECT_FLOAT_EQ(debugDepthGray(0.f, 100.f), 0.f);
        EXPECT_FLOAT_EQ(debugDepthGray(-5.f, 100.f), 0.f);
    }

    TEST(RenderGBufferTest, debugDepthGray_non_positive_range_is_zero)
    {
        EXPECT_FLOAT_EQ(debugDepthGray(50.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(debugDepthGray(50.f, -10.f), 0.f);
    }

    TEST(RenderGBufferTest, debugDepthGray_matches_formula)
    {
        // 1 - exp(-depth / range): depth == range gives 1 - 1/e.
        EXPECT_NEAR(debugDepthGray(100.f, 100.f), 1.f - std::exp(-1.f), 1e-6f);
        EXPECT_NEAR(debugDepthGray(50.f, 100.f), 1.f - std::exp(-0.5f), 1e-6f);
    }

    TEST(RenderGBufferTest, debugDepthGray_increases_with_depth_and_stays_below_one)
    {
        float previous = 0.f;
        for (float depth = 1.f; depth <= 400.f; depth *= 2.f)
        {
            const float g = debugDepthGray(depth, 100.f);
            EXPECT_GT(g, previous) << "depth " << depth;
            EXPECT_LT(g, 1.f) << "depth " << depth;
            previous = g;
        }
    }

    // ---- debugVelocityColor
    TEST(RenderGBufferTest, debugVelocityColor_zero_is_mid_grey)
    {
        const vsg::vec3 c = debugVelocityColor(vsg::vec2(0.f, 0.f), 40.f);
        EXPECT_FLOAT_EQ(c.x, 0.5f);
        EXPECT_FLOAT_EQ(c.y, 0.5f);
        EXPECT_FLOAT_EQ(c.z, 0.5f);
    }

    TEST(RenderGBufferTest, debugVelocityColor_scales_and_offsets)
    {
        // 0.5 + 0.005 * 40 = 0.7; 0.5 - 0.0025 * 40 = 0.4.
        const vsg::vec3 c = debugVelocityColor(vsg::vec2(0.005f, -0.0025f), 40.f);
        EXPECT_NEAR(c.x, 0.7f, 1e-6f);
        EXPECT_NEAR(c.y, 0.4f, 1e-6f);
        EXPECT_FLOAT_EQ(c.z, 0.5f);
    }

    TEST(RenderGBufferTest, debugVelocityColor_clamps_to_unit_range)
    {
        const vsg::vec3 high = debugVelocityColor(vsg::vec2(10.f, 10.f), 40.f);
        EXPECT_FLOAT_EQ(high.x, 1.f);
        EXPECT_FLOAT_EQ(high.y, 1.f);
        EXPECT_FLOAT_EQ(high.z, 0.5f);
        const vsg::vec3 low = debugVelocityColor(vsg::vec2(-10.f, -10.f), 40.f);
        EXPECT_FLOAT_EQ(low.x, 0.f);
        EXPECT_FLOAT_EQ(low.y, 0.f);
        EXPECT_FLOAT_EQ(low.z, 0.5f);
    }

    // ---- reprojectVelocity
    //
    // The projection is written entry by entry (vsg::mat4 is column-major, p(column, row)): a plain symmetric
    // perspective with p(0,0) = 1.5 and p(1,1) = 2.0 (the values the spec's view-space reconstruction divides
    // by), p(2,3) = -1 so that clip.w = -viewZ, and arbitrary depth terms p(2,2), p(3,2) that do not influence
    // clip.xy / clip.w.
    vsg::mat4 makeProjection()
    {
        vsg::mat4 p;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                p(c, r) = 0.f;
        p(0, 0) = 1.5f;
        p(1, 1) = 2.0f;
        p(2, 2) = -1.002f;
        p(2, 3) = -1.f;
        p(3, 2) = -0.2002f;
        return p;
    }

    vsg::mat4 translation(float x, float y, float z)
    {
        return vsg::translate(x, y, z);
    }

    TEST(RenderGBufferTest, reprojectVelocity_identical_matrices_give_zero)
    {
        const vsg::mat4 proj = makeProjection();
        const vsg::mat4 view = translation(3.f, -2.f, 7.f);
        const vsg::mat4 invView = vsg::inverse(view);
        const vsg::vec2 v = reprojectVelocity(vsg::vec2(0.3f, 0.8f), 25.f, proj, invView, view, proj);
        EXPECT_NEAR(v.x, 0.f, 1e-5f);
        EXPECT_NEAR(v.y, 0.f, 1e-5f);
    }

    TEST(RenderGBufferTest, reprojectVelocity_no_surface_gives_zero)
    {
        const vsg::mat4 proj = makeProjection();
        const vsg::mat4 identity;
        // Even with a very different previous view.
        const vsg::mat4 prevView = translation(5.f, 0.f, 0.f);
        for (float depth : { 0.f, -1.f })
        {
            const vsg::vec2 v = reprojectVelocity(vsg::vec2(0.2f, 0.7f), depth, proj, identity, prevView, proj);
            EXPECT_FLOAT_EQ(v.x, 0.f);
            EXPECT_FLOAT_EQ(v.y, 0.f);
        }
    }

    // Derivation (by hand). Current frame: view = identity, so invView = identity. Pixel at the image centre,
    // uv = (0.5, 0.5), depth d = 10: viewPos = (0, 0, -10) = world position; current UV = (0.5, 0.5).
    // Previous frame: prevView = translate(-2, 0, 0) (the camera stood 2 units to +x of where it is now).
    //   prevView * world = (-2, 0, -10); prevClip.x = 1.5 * -2 = -3; prevClip.w = 10; ndc.x = -0.3;
    //   previous u = -0.3 * 0.5 + 0.5 = 0.35; prevClip.y = 0 so previous v = 0.5.
    // velocity = current - previous = (0.5 - 0.35, 0) = (0.15, 0).
    TEST(RenderGBufferTest, reprojectVelocity_sideways_camera_motion)
    {
        const vsg::mat4 proj = makeProjection();
        const vsg::mat4 identity;
        const vsg::mat4 prevView = translation(-2.f, 0.f, 0.f);
        const vsg::vec2 v = reprojectVelocity(vsg::vec2(0.5f, 0.5f), 10.f, proj, identity, prevView, proj);
        EXPECT_NEAR(v.x, 0.15f, 1e-5f);
        EXPECT_NEAR(v.y, 0.f, 1e-5f);
    }

    // Vertical, same pixel: prevView = translate(0, -1, 0): prevClip.y = 2.0 * -1 = -2; w = 10; ndc.y = -0.2;
    // previous v = -0.2 * 0.5 + 0.5 = 0.4; velocity.y = 0.5 - 0.4 = 0.1.
    TEST(RenderGBufferTest, reprojectVelocity_vertical_camera_motion)
    {
        const vsg::mat4 proj = makeProjection();
        const vsg::mat4 identity;
        const vsg::mat4 prevView = translation(0.f, -1.f, 0.f);
        const vsg::vec2 v = reprojectVelocity(vsg::vec2(0.5f, 0.5f), 10.f, proj, identity, prevView, proj);
        EXPECT_NEAR(v.x, 0.f, 1e-5f);
        EXPECT_NEAR(v.y, 0.1f, 1e-5f);
    }

    // Forward motion: the point was 4 units further away in the previous frame: prevView = translate(0, 0, -4)
    // gives prevViewPos.z = -10 - 4 = -14. Pixel uv = (0.75, 0.5), d = 10:
    //   viewPos.x = (0.75 * 2 - 1) * 10 / 1.5 = 3.3333333; prevClip.x = 1.5 * 3.3333333 = 5; prevClip.w = 14;
    //   ndc.x = 5 / 14 = 0.357142857; previous u = 0.357142857 * 0.5 + 0.5 = 0.678571429;
    //   velocity.x = 0.75 - 0.678571429 = 0.071428571. y is 0: the pixel lies on the horizontal centre line.
    // Points right of the centre move outward (positive), those on the left the opposite way, the centre stays.
    TEST(RenderGBufferTest, reprojectVelocity_forward_motion_is_radial)
    {
        const vsg::mat4 proj = makeProjection();
        const vsg::mat4 identity;
        const vsg::mat4 prevView = translation(0.f, 0.f, -4.f);
        const vsg::vec2 right = reprojectVelocity(vsg::vec2(0.75f, 0.5f), 10.f, proj, identity, prevView, proj);
        EXPECT_NEAR(right.x, 0.071428571f, 1e-5f);
        EXPECT_NEAR(right.y, 0.f, 1e-5f);

        const vsg::vec2 left = reprojectVelocity(vsg::vec2(0.25f, 0.5f), 10.f, proj, identity, prevView, proj);
        EXPECT_NEAR(left.x, -0.071428571f, 1e-5f);

        const vsg::vec2 centre = reprojectVelocity(vsg::vec2(0.5f, 0.5f), 10.f, proj, identity, prevView, proj);
        EXPECT_NEAR(centre.x, 0.f, 1e-5f);
        EXPECT_NEAR(centre.y, 0.f, 1e-5f);
    }

    // ---- Tonemap::debugPushConstants
    TEST(RenderGBufferTest, debugPushConstants_mode_follows_debug_view_order)
    {
        EXPECT_FLOAT_EQ(Tonemap::debugPushConstants(DebugView::None, 1.f, 2.f).x, 0.f);
        EXPECT_FLOAT_EQ(Tonemap::debugPushConstants(DebugView::Normals, 1.f, 2.f).x, 1.f);
        EXPECT_FLOAT_EQ(Tonemap::debugPushConstants(DebugView::Depth, 1.f, 2.f).x, 2.f);
        EXPECT_FLOAT_EQ(Tonemap::debugPushConstants(DebugView::Velocity, 1.f, 2.f).x, 3.f);
    }

    TEST(RenderGBufferTest, debugPushConstants_carries_range_and_scale)
    {
        const vsg::vec4 push = Tonemap::debugPushConstants(DebugView::Depth, 123.f, 40.f);
        EXPECT_FLOAT_EQ(push.y, 123.f);
        EXPECT_FLOAT_EQ(push.z, 40.f);
        EXPECT_FLOAT_EQ(push.w, 0.f);
    }
}
