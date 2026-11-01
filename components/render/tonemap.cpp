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
#ifdef DEBUG_VIEW
layout(set=0, binding=2) uniform sampler2D gNormal;
layout(set=0, binding=3) uniform sampler2D gLinearDepth;
layout(set=0, binding=4) uniform sampler2D gVelocity;
layout(set=0, binding=5) uniform sampler2D gMaterial;
#endif
layout(push_constant) uniform Push
{
    vec4 params; // x exposure scale, y curve, z encode, w bloom strength
#ifdef DEBUG_VIEW
    vec4 debug;  // x mode (1 normals, 2 depth, 3 velocity, 4 roughness, 5 material, 6 subsurface), y depth range, z velocity scale
#endif
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
#ifdef DEBUG_VIEW
    // Task 0026: a stored image in place of the scene (the CPU twins are Render::GBuffer::debug*).
    {
        int mode = int(push.debug.x + 0.5);
        vec3 shown = vec3(0.0);
        if (mode == 1)
        {
            vec3 n = texture(gNormal, texCoord).xyz;
            shown = dot(n, n) > 0.0 ? normalize(n) * 0.5 + 0.5 : vec3(0.0);
        }
        else if (mode == 2)
        {
            float d = texture(gLinearDepth, texCoord).r;
            shown = vec3((d > 0.0 && push.debug.y > 0.0) ? 1.0 - exp(-d / push.debug.y) : 0.0);
        }
        else if (mode == 3)
        {
            vec2 v = texture(gVelocity, texCoord).rg;
            shown = vec3(clamp(0.5 + v * push.debug.z, 0.0, 1.0), 0.5);
        }
        else if (mode == 4)
        {
            // Task 0027: roughness is the normal image's w; black where there is no surface.
            vec4 n = texture(gNormal, texCoord);
            shown = vec3(dot(n.xyz, n.xyz) > 0.0 ? n.w : 0.0);
        }
        else if (mode == 5)
            shown = texture(gMaterial, texCoord).rgb; // metalness, reflectance, occlusion
        else if (mode == 6)
            shown = vec3(texture(gMaterial, texCoord).a);
        if (mode != 0)
        {
            outColor = vec4(shown, 1.0);
            return;
        }
    }
#endif
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

        std::string fragmentSource(bool bloom, bool debug)
        {
            std::ostringstream out;
            out.precision(9);
            out << "#version 450\n";
            if (bloom)
                out << "#define BLOOM 1\n";
            if (debug)
                out << "#define DEBUG_VIEW 1\n";
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
        , mPushDebug(vsg::vec4Value::create(debugPushConstants(GBuffer::DebugView::None, 0.f, 0.f)))
    {
        mView->viewDependentState = nullptr;
    }

    vsg::vec4 Tonemap::debugPushConstants(GBuffer::DebugView view, float depthRange, float velocityScale)
    {
        return vsg::vec4(static_cast<float>(static_cast<int>(view)), depthRange, velocityScale, 0.f);
    }

    Tonemap::~Tonemap() {}

    void Tonemap::setParameters(const Parameters& parameters)
    {
        mParameters = parameters;
        mPush->set(pushConstants(parameters));
        mPushDebug->set(debugPushConstants(parameters.debugView, parameters.depthRange, parameters.velocityScale));
    }

    bool Tonemap::setSource(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> bloom,
        const VkExtent2D& extent, const DebugSources& debugSources)
    {
        const bool debug = mParameters.debugView != GBuffer::DebugView::None;
        // Compiled once: a resize only changes the image, the viewport and the pipeline built around them.
        auto& fragment = mFragments[(bloom ? 1 : 0) + (debug ? 2 : 0)];
        if (!mVertex)
            mVertex = compile(VK_SHADER_STAGE_VERTEX_BIT, sVertexShader);
        if (!fragment)
            fragment = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSource(bloom != nullptr, debug));
        if (!mVertex || !fragment)
            return false;
        const auto vertex = mVertex;

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vsg::DescriptorSetLayoutBindings bindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } };
        if (bloom)
            bindings.push_back({ 1, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        if (debug)
            for (uint32_t binding = 2; binding <= 5; ++binding)
                bindings.push_back({ binding, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
        auto setLayout = vsg::DescriptorSetLayout::create(bindings);
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                static_cast<uint32_t>(debug ? 2 * sizeof(vsg::vec4) : sizeof(vsg::vec4)) } });

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

        auto commands = vsg::Commands::create();
        commands->children
            = { vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, extent.width, extent.height)),
                  pipeline,
                  vsg::BindDescriptorSet::create(
                      VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
                  vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, mPush) };
        if (debug)
            commands->children.push_back(
                vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vsg::vec4), mPushDebug));
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
