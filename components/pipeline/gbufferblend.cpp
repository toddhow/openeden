#include "gbufferblend.hpp"

#include <algorithm>

#include <vsg/vk/Context.h>

namespace Pipeline
{
    bool writesGBuffer(const Options& key)
    {
        return key.shader.path.empty() && !key.blend && key.colorWrite && key.depthWrite
            && !key.shader.hasMode(Mode::PARTICLE) && key.colorMode != 3;
    }

    bool usesOverlayDepthBias(const Options& key, float slope)
    {
        return slope > 0.f && key.blend && key.depthTest && key.shader.path.empty() && !key.depthBias;
    }

    std::vector<VkPipelineColorBlendAttachmentState> gbufferBlendAttachments(
        const VkPipelineColorBlendAttachmentState& first, const Options& key, std::size_t colorAttachmentCount)
    {
        std::vector<VkPipelineColorBlendAttachmentState> result;
        if (colorAttachmentCount == 0)
            return result;
        result.push_back(first);
        VkPipelineColorBlendAttachmentState extra{};
        extra.blendEnable = VK_FALSE;
        if (writesGBuffer(key))
            extra.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT;
        for (std::size_t i = 1; i < colorAttachmentCount; ++i)
            result.push_back(extra);
        return result;
    }

    GBufferBlendState::GBufferBlendState(const vsg::ColorBlendState::ColorBlendAttachments& first,
        const VkPipelineColorBlendAttachmentState& in_extra, std::size_t in_total)
        : Inherit(first)
        , extra(in_extra)
        , total(in_total)
    {
    }

    GBufferBlendState::~GBufferBlendState() {}

    int GBufferBlendState::compare(const vsg::Object& rhs_object) const
    {
        int result = ColorBlendState::compare(rhs_object);
        if (result != 0)
            return result;
        const auto& rhs = static_cast<const GBufferBlendState&>(rhs_object);
        if ((result = vsg::compare_value(extra.blendEnable, rhs.extra.blendEnable)))
            return result;
        if ((result = vsg::compare_value(extra.colorWriteMask, rhs.extra.colorWriteMask)))
            return result;
        return vsg::compare_value(total, rhs.total);
    }

    void GBufferBlendState::apply(vsg::Context& context, VkGraphicsPipelineCreateInfo& pipelineInfo) const
    {
        const std::size_t count = std::max(total, attachments.size());
        // Scratch memory lives until the pipeline has been created.
        auto entries = context.scratchMemory->allocate<VkPipelineColorBlendAttachmentState>(count);
        for (std::size_t i = 0; i < count; ++i)
            entries[i] = i < attachments.size() ? attachments[i] : extra;
        auto state = context.scratchMemory->allocate<VkPipelineColorBlendStateCreateInfo>();
        state->sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        state->pNext = nullptr;
        state->flags = 0;
        state->logicOpEnable = logicOpEnable;
        state->logicOp = logicOp;
        state->attachmentCount = static_cast<uint32_t>(count);
        state->pAttachments = entries;
        for (int i = 0; i < 4; ++i)
            state->blendConstants[i] = blendConstants[i];
        pipelineInfo.pColorBlendState = state;
    }
}
