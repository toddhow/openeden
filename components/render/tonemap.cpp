#include "tonemap.hpp"

#include <algorithm>
#include <set>
#include <string>

#include <vsg/commands/Draw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/PushConstants.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/Context.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/recordwithviewid.hpp>
#include <components/vsgutil/setviewportstate.hpp>

namespace Render
{
    namespace
    {
        float curveIndex(Hdr::Tonemap curve)
        {
            switch (curve)
            {
                case Hdr::Tonemap::Vanilla:
                    return 0.f;
                case Hdr::Tonemap::Reinhard:
                    return 1.f;
                case Hdr::Tonemap::Aces:
                    return 2.f;
            }
            return 0.f;
        }
    }

    vsg::vec4 Tonemap::pushConstants(const Parameters& parameters)
    {
        return vsg::vec4(Hdr::exposureScale(parameters.exposureStops), curveIndex(parameters.curve),
            parameters.encode ? 1.f : 0.f, parameters.bloomStrength);
    }

    Tonemap::Tonemap(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<vsg::RenderGraph> graph,
        vsg::ref_ptr<const vsg::Options> shaderOptions)
        : mCompile(std::move(compile))
        , mGraph(std::move(graph))
        , mShaderOptions(std::move(shaderOptions))
        , mView(vsg::View::create())
        , mPush(vsg::vec4Value::create(pushConstants(Parameters{})))
        , mPushDebug(vsg::vec4Value::create(debugPushConstants(GBuffer::DebugView::None, 0.f, 0.f)))
        , mPushAo(vsg::vec4Value::create(aoPushConstants(VkExtent2D{ 1, 1 }, 1.f)))
        , mPushSss(vsg::vec4Value::create(sssPushConstants(0.f, 1.f, 0.f)))
        , mPushGrade(vsg::vec4Value::create(gradePushConstants(Parameters{})))
        , mPushGradeTint(vsg::vec4Value::create(gradeTintPushConstants(Parameters{})))
    {
        mView->viewDependentState = nullptr;
    }

    vsg::vec4 Tonemap::debugPushConstants(GBuffer::DebugView view, float depthRange, float velocityScale)
    {
        return vsg::vec4(static_cast<float>(static_cast<int>(view)), depthRange, velocityScale, 0.f);
    }

    vsg::vec4 Tonemap::aoPushConstants(const VkExtent2D& aoExtent, float strength)
    {
        return vsg::vec4(1.f / static_cast<float>(std::max(1u, aoExtent.width)),
            1.f / static_cast<float>(std::max(1u, aoExtent.height)), strength, 0.f);
    }

    vsg::vec4 Tonemap::sssPushConstants(float strength, float width, float focalPixels)
    {
        return vsg::vec4(strength, width, focalPixels, 0.f);
    }

    vsg::vec4 Tonemap::gradePushConstants(const Parameters& parameters)
    {
        return vsg::vec4(parameters.gradeSaturation, parameters.gradeContrast, parameters.gradeTintAmount,
            parameters.gradeOn ? 1.f : 0.f);
    }

    vsg::vec4 Tonemap::gradeTintPushConstants(const Parameters& parameters)
    {
        return vsg::vec4(parameters.gradeTint[0], parameters.gradeTint[1], parameters.gradeTint[2], 0.f);
    }

    Tonemap::~Tonemap() {}

    void Tonemap::setParameters(const Parameters& parameters)
    {
        mParameters = parameters;
        mPush->set(pushConstants(parameters));
        mPushDebug->set(debugPushConstants(parameters.debugView, parameters.depthRange, parameters.velocityScale));
        mPushAo->set(aoPushConstants(mAoExtent, parameters.aoStrength));
        mPushSss->set(sssPushConstants(parameters.sssStrength, parameters.sssWidth, parameters.sssFocalPixels));
        mPushGrade->set(gradePushConstants(parameters));
        mPushGradeTint->set(gradeTintPushConstants(parameters));
    }

    bool Tonemap::setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom,
        const VkExtent2D& extent, const DebugSources& debugSources)
    {
        return setSource(std::move(color), std::move(bloom), extent, debugSources, AoSources(), SssSources());
    }

    bool Tonemap::setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom,
        const VkExtent2D& extent, const DebugSources& debugSources, const AoSources& aoSources)
    {
        return setSource(std::move(color), std::move(bloom), extent, debugSources, aoSources, SssSources());
    }

    bool Tonemap::setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom,
        const VkExtent2D& extent, const DebugSources& debugSources, const AoSources& aoSources,
        const SssSources& sssSources)
    {
        const bool debug = mParameters.debugView != GBuffer::DebugView::None;
        const bool ao = aoSources.valid();
        const bool sss = sssSources.valid();
        // Task 0127 stage W-S4: the grade is a define of its own, so a pipeline without it is the pipeline of before.
        const bool grade = mParameters.gradeShader;
        if (ao)
        {
            mAoExtent = aoSources.aoExtent;
            mPushAo->set(aoPushConstants(mAoExtent, mParameters.aoStrength));
        }
        // Compiled once: a resize only changes the image, the viewport and the pipeline built around them. The
        // stages are post/fullscreen.vert and post/tonemap.frag (task 0072, stage g: they were strings here), the
        // fragment's constants in post/tonemapconstants.glsl.
        auto& stages = mStages[(bloom ? 1 : 0) + (debug ? 2 : 0) + (ao ? 4 : 0) + (sss ? 8 : 0) + (grade ? 16 : 0)];
        if (stages.empty())
        {
            std::set<std::string> defines;
            if (ao)
                defines.insert("AO");
            if (sss)
                defines.insert("SSS");
            if (bloom)
                defines.insert("BLOOM");
            if (debug)
                defines.insert("DEBUG_VIEW");
            if (grade)
                defines.insert("GRADE");
            std::string error;
            stages = vsgUtil::readPassStages(
                { { "post/fullscreen.vert", {} }, { "post/tonemap.frag", defines } }, mShaderOptions, &error);
            if (stages.empty())
                Log(Debug::Error) << "Failed to compile the tonemap shader: " << error;
        }
        if (stages.empty())
            return false;

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vsg::DescriptorSetLayoutBindings bindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } };
        if (bloom)
            bindings.push_back({ 1, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        if (debug)
            for (uint32_t binding = 2; binding <= 5; ++binding)
                bindings.push_back({ binding, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        if (ao)
            for (uint32_t binding = 6; binding <= 9; ++binding)
                bindings.push_back({ binding, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        if (sss)
            for (uint32_t binding = 10; binding <= 12; ++binding)
                bindings.push_back({ binding, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        // The push constants in order: the parameters, the occlusion's (with it), the subsurface scattering's (with it)
        // and the debug view's (with it).
        const uint32_t aoOffset = sizeof(vsg::vec4);
        const uint32_t sssOffset = static_cast<uint32_t>(sizeof(vsg::vec4)) * (ao ? 2 : 1);
        const uint32_t debugOffset = static_cast<uint32_t>(sizeof(vsg::vec4)) * (1 + (ao ? 1 : 0) + (sss ? 1 : 0));
        // The grade's two come after the debug view's (the shader declares them last).
        const uint32_t gradeOffset = debugOffset + static_cast<uint32_t>(debug ? sizeof(vsg::vec4) : 0);
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                static_cast<uint32_t>(gradeOffset + (grade ? 2 * sizeof(vsg::vec4) : 0)) } });

        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        auto colorBlend
            = vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{ { VK_FALSE, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT } });
        const vsg::GraphicsPipelineStates states{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            raster, vsg::MultisampleState::create(), colorBlend, depthStencil,
            vsg::ViewportState::create(0, 0, extent.width, extent.height) };
        auto pipeline = vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(layout, stages, states));

        // Exact texels: the source has the swapchain's size.
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vsg::Descriptors descriptors{ vsg::DescriptorImage::create(
            vsg::ImageInfo::create(sampler, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 0, 0, imageType) };
        if (bloom)
        {
            auto linear = vsg::Sampler::create();
            linear->addressModeU = linear->addressModeV = linear->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            descriptors.push_back(vsg::DescriptorImage::create(
                vsg::ImageInfo::create(linear, bloom, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 1, 0, imageType));
        }

        if (debug)
        {
            // A missing image shows as black: the one image that exists stands in for it (never read for that mode).
            const auto fallback = debugSources.normal ? debugSources.normal : color;
            const auto image = [&](const vsg::ref_ptr<vsg::ImageView>& view, uint32_t binding) {
                return vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(sampler, view ? view : fallback, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                    binding, 0, imageType);
            };
            descriptors.push_back(image(debugSources.normal, 2));
            descriptors.push_back(image(debugSources.linearDepth, 3));
            descriptors.push_back(image(debugSources.velocity, 4));
            descriptors.push_back(image(debugSources.material, 5));
        }

        if (ao)
        {
            const auto aoImage = [&](const vsg::ref_ptr<vsg::ImageView>& view, uint32_t binding) {
                return vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), binding, 0,
                    imageType);
            };
            descriptors.push_back(aoImage(aoSources.ambient, 6));
            descriptors.push_back(aoImage(aoSources.ao, 7));
            descriptors.push_back(aoImage(aoSources.normal, 8));
            descriptors.push_back(aoImage(aoSources.linearDepth, 9));
        }

        if (sss)
        {
            const auto sssImage = [&](const vsg::ref_ptr<vsg::ImageView>& view, uint32_t binding) {
                return vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), binding, 0,
                    imageType);
            };
            descriptors.push_back(sssImage(sssSources.material, 10));
            descriptors.push_back(sssImage(sssSources.linearDepth, 11));
            descriptors.push_back(sssImage(sssSources.normal, 12));
        }

        auto commands = vsg::Commands::create();
        commands->children
            = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, extent.width, extent.height)),
                  pipeline,
                  vsg::BindDescriptorSet::create(
                      VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
                  vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, mPush) };
        if (ao)
            commands->children.push_back(vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, aoOffset, mPushAo));
        if (sss)
            commands->children.push_back(vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, sssOffset, mPushSss));
        if (debug)
            commands->children.push_back(
                vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, debugOffset, mPushDebug));
        if (grade)
        {
            commands->children.push_back(
                vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, gradeOffset, mPushGrade));
            commands->children.push_back(vsg::PushConstants::create(
                VK_SHADER_STAGE_FRAGMENT_BIT, gradeOffset + static_cast<uint32_t>(sizeof(vsg::vec4)), mPushGradeTint));
        }
        commands->children.push_back(vsg::Draw::create(3, 1, 0, 0));

        // The tonemap pass has no view in its graph: it records under the view it was compiled for.
        auto compileForView = mCompile->clone(~vsg::Mask(0));
        const vsg::ref_ptr<vsg::View> view = mView;
        uint32_t viewId = 0;
        bool matched = false;
        compileForView->contextSelectionFunction = [view, &viewId, &matched](vsg::Context& context) {
            if (context.view.get() != view.get())
                return false;
            matched = true;
            viewId = context.viewID;
            return true;
        };
        if (!compileForView->compile(commands))
            return false;
        if (!matched)
        {
            Log(Debug::Error) << "The tonemap view is not registered with the compile manager";
            return false;
        }

        if (mDraw)
            mCompile->detach(mDraw);
        mDraw = vsgUtil::RecordWithViewId::create(commands, viewId);
        mGraph->children = { mDraw };
        return true;
    }
}
