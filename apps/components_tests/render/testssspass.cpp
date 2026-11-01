// Tests for the pure parts of the subsurface scattering step in the tonemap (task 0033, stage b). Written from the task
// spec before the implementation.

#include <components/render/gbuffermath.hpp>
#include <components/render/tonemap.hpp>

#include <gtest/gtest.h>

namespace Render
{
    TEST(RenderSssPassTest, the_debug_view_has_a_name_and_a_number_after_the_older_ones)
    {
        EXPECT_EQ(GBuffer::parseDebugView("sss"), GBuffer::DebugView::Sss);
        EXPECT_EQ(static_cast<int>(GBuffer::DebugView::Sss), 9);
        EXPECT_EQ(Tonemap::debugPushConstants(GBuffer::DebugView::Sss, 1.f, 1.f).x, 9.f);
    }

    TEST(RenderSssPassTest, the_older_debug_numbers_did_not_move)
    {
        EXPECT_EQ(static_cast<int>(GBuffer::DebugView::Ao), 7);
        EXPECT_EQ(static_cast<int>(GBuffer::DebugView::Ambient), 8);
        EXPECT_EQ(GBuffer::parseDebugView("ambient"), GBuffer::DebugView::Ambient);
    }

    TEST(RenderSssPassTest, the_tonemaps_sss_push_constants_hold_strength_width_and_focal_length)
    {
        const vsg::vec4 push = Tonemap::sssPushConstants(0.75f, 1.5f, 623.5f);
        EXPECT_FLOAT_EQ(push.x, 0.75f);
        EXPECT_FLOAT_EQ(push.y, 1.5f);
        EXPECT_FLOAT_EQ(push.z, 623.5f);
        EXPECT_FLOAT_EQ(push.w, 0.f);
    }

    TEST(RenderSssPassTest, no_sources_means_no_sss_step)
    {
        Tonemap::SssSources sources;
        EXPECT_FALSE(sources.valid());
    }
}
