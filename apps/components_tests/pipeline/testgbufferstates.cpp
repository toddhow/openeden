#include <components/pipeline/gbufferblend.hpp>

#include <gtest/gtest.h>

namespace
{
    VkPipelineColorBlendAttachmentState attachment(VkColorComponentFlags mask, VkBool32 blend)
    {
        VkPipelineColorBlendAttachmentState state{};
        state.colorWriteMask = mask;
        state.blendEnable = blend;
        return state;
    }

    constexpr VkColorComponentFlags rgba
        = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    TEST(PipelineGBufferStatesTest, blend_states_with_the_same_entries_compare_equal)
    {
        const vsg::ColorBlendState::ColorBlendAttachments first{ attachment(rgba, VK_FALSE) };
        auto a = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 3);
        auto b = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 3);
        EXPECT_EQ(a->compare(*b), 0);
    }

    TEST(PipelineGBufferStatesTest, blend_states_with_different_extra_entries_differ)
    {
        const vsg::ColorBlendState::ColorBlendAttachments first{ attachment(rgba, VK_FALSE) };
        auto writes = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 3);
        auto masked = Pipeline::GBufferBlendState::create(first, attachment(0, VK_FALSE), 3);
        auto blended = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_TRUE), 3);
        EXPECT_NE(writes->compare(*masked), 0);
        EXPECT_NE(writes->compare(*blended), 0);
    }

    TEST(PipelineGBufferStatesTest, blend_states_with_different_totals_differ)
    {
        const vsg::ColorBlendState::ColorBlendAttachments first{ attachment(rgba, VK_FALSE) };
        auto three = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 3);
        auto four = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 4);
        EXPECT_NE(three->compare(*four), 0);
    }

    TEST(PipelineGBufferStatesTest, blend_state_differs_from_a_plain_one)
    {
        const vsg::ColorBlendState::ColorBlendAttachments first{ attachment(rgba, VK_FALSE) };
        auto plain = vsg::ColorBlendState::create(first);
        auto gbuffer = Pipeline::GBufferBlendState::create(first, attachment(rgba, VK_FALSE), 3);
        EXPECT_NE(gbuffer->compare(*plain), 0);
        EXPECT_NE(plain->compare(*gbuffer), 0);
    }
}
