// Tests for the pure parts of the ambient occlusion pass, Render::Ao and the G-buffer's ambient output (task 0029,
// stage b). Written from the task spec before the implementation.

#include <components/render/aomath.hpp>
#include <components/render/gbuffermath.hpp>
#include <components/render/tonemap.hpp>

#include <gtest/gtest.h>

namespace Render
{
    TEST(RenderAoPassTest, the_ambient_output_comes_last_and_only_when_asked_for)
    {
        const auto without = GBuffer::extraColorFormats(true, true);
        ASSERT_EQ(without.size(), 4u);
        const auto with = GBuffer::extraColorFormats(true, true, true);
        ASSERT_EQ(with.size(), 5u);
        for (std::size_t i = 0; i < 4; ++i)
            EXPECT_EQ(with[i], without[i]);
        EXPECT_EQ(with[4], GBuffer::ambientFormat);
    }

    TEST(RenderAoPassTest, the_ambient_output_is_a_float_format_that_holds_light_above_one)
    {
        EXPECT_EQ(GBuffer::ambientFormat, VK_FORMAT_R16G16B16A16_SFLOAT);
    }

    TEST(RenderAoPassTest, the_debug_views_ao_and_ambient_are_named_and_numbered_after_the_older_ones)
    {
        EXPECT_EQ(GBuffer::parseDebugView("ao"), GBuffer::DebugView::Ao);
        EXPECT_EQ(GBuffer::parseDebugView("ambient"), GBuffer::DebugView::Ambient);
        EXPECT_EQ(static_cast<int>(GBuffer::DebugView::Subsurface), 6);
        EXPECT_EQ(Tonemap::debugPushConstants(GBuffer::DebugView::Ao, 1.f, 1.f).x, 7.f);
        EXPECT_EQ(Tonemap::debugPushConstants(GBuffer::DebugView::Ambient, 1.f, 1.f).x, 8.f);
    }

    TEST(RenderAoPassTest, the_older_debug_names_still_parse)
    {
        EXPECT_EQ(GBuffer::parseDebugView("normals"), GBuffer::DebugView::Normals);
        EXPECT_EQ(GBuffer::parseDebugView("subsurface"), GBuffer::DebugView::Subsurface);
        EXPECT_FALSE(GBuffer::parseDebugView("nonsense").has_value());
    }

    TEST(RenderAoPassTest, ao_needs_its_setting_the_g_buffer_and_the_material_output)
    {
        EXPECT_TRUE(Ao::useAo(true, true, true));
        EXPECT_FALSE(Ao::useAo(false, true, true));
        EXPECT_FALSE(Ao::useAo(true, false, true));
        EXPECT_FALSE(Ao::useAo(true, true, false));
    }

    TEST(RenderAoPassTest, the_pass_runs_at_half_resolution_rounded_up_and_never_empty)
    {
        const VkExtent2D even = Ao::halfExtent(VkExtent2D{ 1280, 720 });
        EXPECT_EQ(even.width, 640u);
        EXPECT_EQ(even.height, 360u);
        const VkExtent2D odd = Ao::halfExtent(VkExtent2D{ 1281, 721 });
        EXPECT_EQ(odd.width, 641u);
        EXPECT_EQ(odd.height, 361u);
        const VkExtent2D tiny = Ao::halfExtent(VkExtent2D{ 1, 1 });
        EXPECT_EQ(tiny.width, 1u);
        EXPECT_EQ(tiny.height, 1u);
        const VkExtent2D zero = Ao::halfExtent(VkExtent2D{ 0, 0 });
        EXPECT_EQ(zero.width, 1u);
    }

    TEST(RenderAoPassTest, the_tonemaps_ao_push_constants_hold_the_half_resolution_texel_and_the_strength)
    {
        const vsg::vec4 push = Tonemap::aoPushConstants(VkExtent2D{ 640, 360 }, 1.5f);
        EXPECT_FLOAT_EQ(push.x, 1.f / 640.f);
        EXPECT_FLOAT_EQ(push.y, 1.f / 360.f);
        EXPECT_FLOAT_EQ(push.z, 1.5f);
    }

    TEST(RenderAoPassTest, the_defaults_are_sane)
    {
        EXPECT_GT(Ao::defaultRadius, 0.f);
        EXPECT_GE(Ao::defaultSlices, 1);
        EXPECT_GE(Ao::defaultSteps, 1);
    }
}

namespace Render
{
    TEST(RenderAoPassTest, strength_zero_means_no_occlusion_even_for_a_fully_closed_pixel)
    {
        EXPECT_EQ(Ao::aoCurve(0.f, 0.f), 1.f);
        EXPECT_EQ(Ao::aoCurve(0.5f, 0.f), 1.f);
        EXPECT_EQ(Ao::aoCurve(0.f, 2.f), 0.f);
        EXPECT_TRUE(std::isfinite(Ao::aoCurve(0.f, -1.f)));
    }
}
