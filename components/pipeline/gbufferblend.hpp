#ifndef OPENEDEN_PIPELINE_GBUFFERBLEND_H
#define OPENEDEN_PIPELINE_GBUFFERBLEND_H

#include <cstddef>
#include <vector>

#include <vsg/state/ColorBlendState.h>

#include "options.hpp"

namespace Pipeline
{
    /// Whether a draw writes the G-buffer's outputs (task 0026): opaque, depth-writing draws of the default shader that
    /// are not particles or debug geometry (colour mode 3). The terrain always does and does not use this.
    bool writesGBuffer(const Options& key);

    /// Task 0075 (the lava flip): whether a pipeline gets the slope-scaled depth bias of the TAA overlay pass
    /// (`Render::Taa::overlayDepthBiasSlope`, set on `Graphics::overlayDepthBiasSlope`): the default shader's blended,
    /// depth-tested draws, with or without depth writes (the depth-sorted bin, drawn after the resolve with the
    /// unjittered camera against the jittered depth; the Red Mountain lava is blended with depth writes, found in
    /// the game), with a positive slope and no bias of their own already.
    bool usesOverlayDepthBias(const Options& key, float slope);

    /// Entry 0 is `first`; entries 1..count-1 are the G-buffer's: no blending, all channels when `key` writes the
    /// G-buffer, none otherwise (the pass's attachments are then left as the opaque scene made them).
    std::vector<VkPipelineColorBlendAttachmentState> gbufferBlendAttachments(
        const VkPipelineColorBlendAttachmentState& first, const Options& key, std::size_t colorAttachmentCount);

    /*
     * A colour blend state with one entry per colour output of the HDR target's pass (task 0026): those of
     * `attachments` first, then copies of `extra`, `total` entries in all. Owned by the pipeline's state list, like
     * vsg::ColorBlendState.
     *
     * Why a fixed count and not the count of the pass being compiled for: VSG keeps one compiled pipeline per view id
     * and reuses it for any view whose merged pipeline states compare equal, and the main view shares its id (and,
     * with equal states, its pipelines) with the preview and the local map, whose passes have one colour attachment.
     * A count that depended on the pass would give whichever view compiled first its way, and the main pass could
     * draw with a one-entry blend state. A fixed count is always right for the main pass; the one-attachment passes
     * get entries for attachments they do not have, which drivers ignore (Vulkan asks for an exact match, so the
     * validation layer would warn). Trying another view id for the main, or for those two views, hung the game at
     * start (task 0026, stage a log).
     */
    class GBufferBlendState : public vsg::Inherit<vsg::ColorBlendState, GBufferBlendState>
    {
    public:
        GBufferBlendState(const vsg::ColorBlendState::ColorBlendAttachments& first,
            const VkPipelineColorBlendAttachmentState& extra, std::size_t total);

        void apply(vsg::Context& context, VkGraphicsPipelineCreateInfo& pipelineInfo) const override;
        int compare(const vsg::Object& rhs) const override;

        VkPipelineColorBlendAttachmentState extra{};
        std::size_t total = 0;

    protected:
        ~GBufferBlendState() override;
    };
}

#endif
