#include "tonemap.hpp"

#include <sstream>

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
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/vk/Context.h>

#include <components/debug/debuglog.hpp>
#include <components/vsgutil/recordwithviewid.hpp>
#include <components/vsgutil/setviewportstate.hpp>

namespace Render
{
    namespace
    {
        // A fullscreen triangle. The float target has Vulkan's top-left origin, as the swapchain does, so the texture
        // coordinates are not flipped (unlike PostProcess's images, which have OpenGL's).
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

        // The curves and constants repeat Render::Hdr (hdrmath.hpp); the constants are written in from there.
        const char* const sFragmentBody = R"GLSL(
layout(set=0, binding=0) uniform sampler2D sceneColor;
#ifdef BLOOM
layout(set=0, binding=1) uniform sampler2D bloomColor;
#endif
layout(push_constant) uniform Push
{
    vec4 params; // x exposure scale, y curve, z encode, w bloom strength
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;

vec3 reinhard(vec3 x)
{
    return x * (vec3(1.0) + x / (REINHARD_WHITE * REINHARD_WHITE)) / (vec3(1.0) + x);
}
vec3 aces(vec3 x)
{
    return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

void main()
{
    vec3 color = texture(sceneColor, texCoord).rgb;
#ifdef BLOOM
    color += texture(bloomColor, texCoord).rgb * push.params.w;
#endif
    // A NaN would spread through the 8-bit chain after this; draw black instead.
    if (any(isnan(color)))
        color = vec3(0.0);
    color = clamp(color * push.params.x, vec3(0.0), vec3(MAX_TARGET_VALUE));
    if (push.params.y > 1.5)
        color = aces(color);
    else if (push.params.y > 0.5)
        color = reinhard(color);
    color = clamp(color, 0.0, 1.0);
    if (push.params.z > 0.5)
        color = pow(color, vec3(1.0 / DISPLAY_GAMMA));
    outColor = vec4(color, 1.0);
}
)GLSL";

        std::string fragmentSource(bool bloom)
        {
            std::ostringstream out;
            out.precision(9);
            out << "#version 450\n";
            if (bloom)
                out << "#define BLOOM 1\n";
            out << "#define DISPLAY_GAMMA " << Hdr::displayGamma << "\n";
            out << "#define REINHARD_WHITE " << Hdr::reinhardWhitePoint << "\n";
            out << "#define MAX_TARGET_VALUE " << Hdr::maxTargetValue << "\n";
            out << sFragmentBody;
            return out.str();
        }

        vsg::ref_ptr<vsg::ShaderStage> compile(VkShaderStageFlagBits stage, const std::string& source)
        {
            auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
            auto compiler = vsg::ShaderCompiler::create();
            if (!compiler->compile(shaderStage))
            {
                Log(Debug::Error) << "Failed to compile the tonemap shader";
                return {};
            }
            return shaderStage;
        }

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

    Tonemap::Tonemap(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<vsg::RenderGraph> graph)
        : mCompile(std::move(compile))
        , mGraph(std::move(graph))
        , mView(vsg::View::create())
        , mPush(vsg::vec4Value::create(pushConstants(Parameters{})))
    {
        mView->viewDependentState = nullptr;
    }

    Tonemap::~Tonemap() {}

    void Tonemap::setParameters(const Parameters& parameters)
    {
        mParameters = parameters;
        mPush->set(pushConstants(parameters));
    }

    bool Tonemap::setSource(
        vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom, const VkExtent2D& extent)
    {
        // Compiled once: a resize only changes the image, the viewport and the pipeline built around them.
        auto& fragment = bloom ? mFragmentBloom : mFragment;
        if (!mVertex)
            mVertex = compile(VK_SHADER_STAGE_VERTEX_BIT, sVertexShader);
        if (!fragment)
            fragment = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSource(bloom != nullptr));
        if (!mVertex || !fragment)
            return false;
        const auto vertex = mVertex;

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vsg::DescriptorSetLayoutBindings bindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } };
        if (bloom)
            bindings.push_back({ 1, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vsg::vec4) } });

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
        auto pipeline = vsg::BindGraphicsPipeline::create(
            vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{ vertex, fragment }, states));

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

        auto commands = vsg::Commands::create();
        commands->children
            = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, extent.width, extent.height)),
                  pipeline,
                  vsg::BindDescriptorSet::create(
                      VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
                  vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, mPush), vsg::Draw::create(3, 1, 0, 0) };

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
