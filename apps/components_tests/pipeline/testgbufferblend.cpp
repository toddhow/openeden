// Tests for Pipeline::writesGBuffer and Pipeline::gbufferBlendAttachments (task 0026, "Interfaces").
//
// Written from the spec alone, without reading the implementation.

#include <components/pipeline/gbufferblend.hpp>
#include <components/pipeline/mode.hpp>
#include <components/pipeline/options.hpp>

#include <gtest/gtest.h>

namespace
{
    constexpr VkColorComponentFlags rgba
        = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // The plain default-shader opaque draw: the case that writes the G-buffer.
    Pipeline::Options qualifyingKey()
    {
        Pipeline::Options key;
        key.shader.path.clear();
        key.blend = false;
        key.colorWrite = true;
        key.depthWrite = true;
        key.colorMode = 0;
        return key;
    }

    VkPipelineColorBlendAttachmentState makeFirst()
    {
        VkPipelineColorBlendAttachmentState first{};
        first.blendEnable = VK_TRUE;
        first.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        first.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        first.colorBlendOp = VK_BLEND_OP_ADD;
        first.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT;
        return first;
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_plain_opaque_default_shader_qualifies)
    {
        EXPECT_TRUE(Pipeline::writesGBuffer(qualifyingKey()));
        EXPECT_TRUE(Pipeline::writesGBuffer(Pipeline::Options{}));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_custom_shader_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.shader.path = "water/main";
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_blend_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.blend = true;
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_no_color_write_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.colorWrite = false;
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_no_depth_write_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.depthWrite = false;
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_particle_mode_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.shader.addMode(Pipeline::Mode::PARTICLE);
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, writesGBuffer_color_mode_3_disqualifies)
    {
        Pipeline::Options key = qualifyingKey();
        key.colorMode = 3;
        EXPECT_FALSE(Pipeline::writesGBuffer(key));
        // Other colour modes do not.
        key.colorMode = 1;
        EXPECT_TRUE(Pipeline::writesGBuffer(key));
        key.colorMode = 2;
        EXPECT_TRUE(Pipeline::writesGBuffer(key));
    }

    TEST(PipelineGBufferBlendTest, gbufferBlendAttachments_count_zero_is_empty)
    {
        EXPECT_TRUE(Pipeline::gbufferBlendAttachments(makeFirst(), qualifyingKey(), 0).empty());
    }

    TEST(PipelineGBufferBlendTest, gbufferBlendAttachments_count_one_is_just_first)
    {
        const VkPipelineColorBlendAttachmentState first = makeFirst();
        const auto result = Pipeline::gbufferBlendAttachments(first, qualifyingKey(), 1);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0].blendEnable, first.blendEnable);
        EXPECT_EQ(result[0].srcColorBlendFactor, first.srcColorBlendFactor);
        EXPECT_EQ(result[0].dstColorBlendFactor, first.dstColorBlendFactor);
        EXPECT_EQ(result[0].colorBlendOp, first.colorBlendOp);
        EXPECT_EQ(result[0].colorWriteMask, first.colorWriteMask);
    }

    TEST(PipelineGBufferBlendTest, gbufferBlendAttachments_qualifying_key_writes_extra_attachments)
    {
        const VkPipelineColorBlendAttachmentState first = makeFirst();
        const auto result = Pipeline::gbufferBlendAttachments(first, qualifyingKey(), 3);
        ASSERT_EQ(result.size(), 3u);
        EXPECT_EQ(result[0].blendEnable, VK_TRUE);
        EXPECT_EQ(result[0].colorWriteMask, first.colorWriteMask);
        EXPECT_EQ(result[0].srcColorBlendFactor, first.srcColorBlendFactor);
        for (std::size_t i = 1; i < 3; ++i)
        {
            EXPECT_EQ(result[i].blendEnable, VK_FALSE) << i;
            EXPECT_EQ(result[i].colorWriteMask, rgba) << i;
        }
    }

    TEST(PipelineGBufferBlendTest, gbufferBlendAttachments_non_qualifying_key_masks_extra_attachments)
    {
        Pipeline::Options key = qualifyingKey();
        key.blend = true;
        const VkPipelineColorBlendAttachmentState first = makeFirst();
        const auto result = Pipeline::gbufferBlendAttachments(first, key, 3);
        ASSERT_EQ(result.size(), 3u);
        EXPECT_EQ(result[0].colorWriteMask, first.colorWriteMask);
        EXPECT_EQ(result[0].blendEnable, VK_TRUE);
        for (std::size_t i = 1; i < 3; ++i)
        {
            EXPECT_EQ(result[i].blendEnable, VK_FALSE) << i;
            EXPECT_EQ(result[i].colorWriteMask, 0u) << i;
        }
    }

    TEST(PipelineGBufferBlendTest, gbufferBlendAttachments_extra_blend_is_off_even_if_first_blends)
    {
        const VkPipelineColorBlendAttachmentState first = makeFirst();
        const auto result = Pipeline::gbufferBlendAttachments(first, qualifyingKey(), 2);
        ASSERT_EQ(result.size(), 2u);
        EXPECT_EQ(result[1].blendEnable, VK_FALSE);
    }
}
