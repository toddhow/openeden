#include "velocity.hpp"

#include <vsg/commands/Commands.h>
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
#include <vsg/state/Sampler.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/vk/Context.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/image.hpp>
#include <components/vsgutil/recordwithviewid.hpp>
#include <components/vsgutil/setviewportstate.hpp>

namespace Render
{
    namespace
    {
        const char* const sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
)GLSL";

        // The CPU twin is Render::GBuffer::reprojectVelocity (gbuffermath.hpp): change both together. The linear depth
        // is the distance in front of the camera (view space looks down -Z); projScale holds the projection's x and y
        // scales, which carry the sign of the target's y axis. A pixel with no opaque surface (depth 0), or one that
        // was behind the previous camera, has no velocity.
        const char* const sFragmentShader = R"GLSL(#version 450
layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D objectMotion; // skinned actors' own motion, from the scene pass
layout(push_constant) uniform Push
{
    mat4 toPrevClip;
    vec4 projScale; // xy: the projection's [0][0] and [1][1]
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outVelocity;
void main()
{
    float d = texture(linearDepth, texCoord).r;
    outVelocity = vec4(0.0);
    if (!(d > 0.0))
        return;
    vec2 velocity = texture(objectMotion, texCoord).rg;
    vec3 viewPos = vec3((texCoord * 2.0 - 1.0) * d / push.projScale.xy, -d);
    vec4 prevClip = push.toPrevClip * vec4(viewPos, 1.0);
    if (prevClip.w > 0.0)
        velocity += texCoord - (prevClip.xy / prevClip.w * 0.5 + 0.5);
    outVelocity = vec4(velocity, 0.0, 0.0);
}
)GLSL";

        vsg::ref_ptr<vsg::ShaderStage> compile(VkShaderStageFlagBits stage, const char* source)
        {
            auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
            auto compiler = vsg::ShaderCompiler::create();
            if (!compiler->compile(shaderStage))
            {
                Log(Debug::Error) << "Failed to compile a velocity shader";
                return {};
            }
            return shaderStage;
        }

        vsg::ref_ptr<vsg::RenderPass> createPass(vsg::Device* device)
        {
            vsg::AttachmentDescription attachment = {};
            attachment.format = Velocity::format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vsg::SubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachments.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
            // The image is written every frame while the previous frame's tonemap may still be reading it, and the
            // pass reads the scene's linear depth, which the scene's pass wrote.
            vsg::SubpassDependency before = { VK_SUBPASS_EXTERNAL, 0,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 };
            vsg::SubpassDependency after = { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                0 };
            return vsg::RenderPass::create(device, vsg::RenderPass::Attachments{ attachment },
                vsg::RenderPass::Subpasses{ subpass }, vsg::RenderPass::Dependencies{ before, after });
        }
    }

    Velocity::Velocity(vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile)
        : mCompile(std::move(compile))
        , mView(vsg::View::create())
        , mPass(createPass(context.device))
        , mGraph(vsg::RenderGraph::create())
        , mMatrix(vsg::mat4Value::create(vsg::mat4()))
        , mScale(vsg::vec4Value::create(vsg::vec4(1.f, 1.f, 0.f, 0.f)))
    {
        mView->viewDependentState = nullptr;
    }

    Velocity::~Velocity() {}

    void Velocity::setMatrix(const vsg::mat4& toPreviousClip, float projectionX, float projectionY)
    {
        mMatrix->set(toPreviousClip);
        mScale->set(vsg::vec4(projectionX, projectionY, 0.f, 0.f));
    }

    vsg::ref_ptr<vsg::Framebuffer> Velocity::registrationFramebuffer(vsg::Context& context)
    {
        auto image = vsgUtil::createImage(
            format, VkExtent2D{ 1, 1 }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto view = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
        return vsg::Framebuffer::create(mPass, vsg::ImageViews{ view }, 1, 1, 1);
    }

    bool Velocity::build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth,
        vsg::ref_ptr<vsg::ImageView> objectMotion, const VkExtent2D& extent)
    {
        if (!mVertex)
            mVertex = compile(VK_SHADER_STAGE_VERTEX_BIT, sVertexShader);
        if (!mFragment)
            mFragment = compile(VK_SHADER_STAGE_FRAGMENT_BIT, sFragmentShader);
        if (!mVertex || !mFragment || !linearDepth || !objectMotion)
            return false;

        auto image
            = vsgUtil::createImage(format, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto resultView = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
        auto framebuffer
            = vsg::Framebuffer::create(mPass, vsg::ImageViews{ resultView }, extent.width, extent.height, 1);
        mLive.push_back(resultView);
        mLive.push_back(framebuffer);

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // Exact texels: the depth is the scene's size.
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto setLayout = vsg::DescriptorSetLayout::create(
            vsg::DescriptorSetLayoutBindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 1, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } });
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{
                { VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(vsg::mat4) + sizeof(vsg::vec4)) } });
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        auto blend
            = vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{ { VK_FALSE, VK_BLEND_FACTOR_ONE,
                VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                    | VK_COLOR_COMPONENT_A_BIT } });
        const vsg::GraphicsPipelineStates states{ vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            raster, vsg::MultisampleState::create(), blend, depthStencil,
            vsg::ViewportState::create(0, 0, extent.width, extent.height) };
        auto pipeline = vsg::BindGraphicsPipeline::create(
            vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{ mVertex, mFragment }, states));
        vsg::Descriptors descriptors{ vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, linearDepth,
                                                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
                                          0, 0, imageType),
            vsg::DescriptorImage::create(
                vsg::ImageInfo::create(sampler, objectMotion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 1, 0,
                imageType) };

        auto commands = vsg::Commands::create();
        commands->children
            = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, extent.width, extent.height)),
                  pipeline,
                  vsg::BindDescriptorSet::create(
                      VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
                  vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, mMatrix),
                  vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vsg::mat4), mScale),
                  vsg::Draw::create(3, 1, 0, 0) };

        // As the bloom: compiled for the view that the pass's render pass was registered with.
        auto compileForView = mCompile->clone(~vsg::Mask(0));
        const vsg::ref_ptr<vsg::View> view = mView;
        uint32_t viewId = 0;
        bool matched = false;
        compileForView->contextSelectionFunction = [view, &viewId, &matched](vsg::Context& selected) {
            if (selected.view.get() != view.get())
                return false;
            matched = true;
            viewId = selected.viewID;
            return true;
        };
        if (!compileForView->compile(commands))
            return false;
        if (!matched)
        {
            Log(Debug::Error) << "The velocity view is not registered with the compile manager";
            return false;
        }

        mGraph->framebuffer = framebuffer;
        mGraph->renderPass = mPass;
        mGraph->renderArea = { { 0, 0 }, extent };
        mGraph->windowResizeHandler = nullptr;
        auto draw = vsgUtil::RecordWithViewId::create(commands, viewId);
        mGraph->children = { draw };
        mLive.push_back(draw);
        mResult = resultView;
        return true;
    }
}
