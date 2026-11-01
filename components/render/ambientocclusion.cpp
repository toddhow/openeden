#include "ambientocclusion.hpp"

#include <sstream>

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

#include "aomath.hpp"

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

        // The CPU twins are Render::Ao::gtaoSliceVisibility and horizonCosine (aomath.hpp): change both together. View
        // space looks down -Z and the linear depth is the distance in front of the camera, as in the velocity pass;
        // proj.xy are the projection's x and y scales (their signs carry the target's axis directions). A pixel with no
        // opaque surface is fully open.
        const char* const sFragmentBody = R"GLSL(
layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D viewNormal;
layout(push_constant) uniform Push
{
    vec4 proj;
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outAo;

const float PI = 3.14159265358979;

float gradientNoise(vec2 p)
{
    // Interleaved gradient noise (Jimenez 2014): a fixed pattern by pixel position, no time.
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 viewPosAt(vec2 uv, float d)
{
    return vec3((uv * 2.0 - 1.0) * d / push.proj.xy, -d);
}

float horizonCosine(vec3 delta, vec3 toCamera)
{
    float len = length(delta);
    if (!(len > 0.0) || len >= AO_RADIUS)
        return -1.0;
    float weight = clamp(1.0 - len / AO_RADIUS, 0.0, 1.0);
    float cosine = dot(delta, toCamera) / len;
    // The falloff of Render::Ao::horizonCosine at 1.
    return -1.0 + (cosine + 1.0) * weight;
}

float sliceVisibility(float h1, float h2, float n)
{
    h1 = n + max(h1 - n, -0.5 * PI);
    h2 = n + min(h2 - n, 0.5 * PI);
    float sinN = sin(n);
    float cosN = cos(n);
    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN) + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

void main()
{
    outAo = vec4(1.0);
    float d = texture(linearDepth, texCoord).r;
    if (!(d > 0.0))
        return;
    vec3 n = texture(viewNormal, texCoord).xyz;
    if (!(dot(n, n) > 0.0))
        return;
    n = normalize(n);
    vec3 position = viewPosAt(texCoord, d);
    vec3 toCamera = normalize(-position);
    vec2 noise = vec2(gradientNoise(gl_FragCoord.xy), gradientNoise(gl_FragCoord.xy + vec2(5.588238, 5.588238)));
    // The radius on the screen, per axis (a view space circle), kept to a quarter of the screen.
    vec2 uvRadius = min(AO_RADIUS * abs(push.proj.xy) * 0.5 / d, vec2(0.25));
    float visibility = 0.0;
    for (int s = 0; s < AO_SLICES; ++s)
    {
        float phi = (float(s) + noise.x) / float(AO_SLICES) * PI;
        vec2 uvDir = vec2(cos(phi), sin(phi));
        vec3 dirView = normalize(vec3(uvDir * sign(push.proj.xy), 0.0));
        vec3 orthoDir = dirView - dot(dirView, toCamera) * toCamera;
        vec3 axis = normalize(cross(dirView, toCamera));
        vec3 projected = n - axis * dot(n, axis);
        float projectedLength = length(projected);
        if (projectedLength < 1e-4)
        {
            visibility += 1.0;
            continue;
        }
        float cosN = clamp(dot(projected, toCamera) / projectedLength, 0.0, 1.0);
        float nAngle = (dot(orthoDir, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
        float horizon[2] = float[2](-1.0, -1.0);
        for (int side = 0; side < 2; ++side)
        {
            float direction = side == 0 ? 1.0 : -1.0;
            for (int k = 1; k <= AO_STEPS; ++k)
            {
                float t = (float(k) - noise.y) / float(AO_STEPS);
                vec2 sampleUv = texCoord + direction * uvDir * uvRadius * t;
                float sampleDepth = texture(linearDepth, sampleUv).r;
                if (!(sampleDepth > 0.0))
                    continue;
                horizon[side] = max(horizon[side], horizonCosine(viewPosAt(sampleUv, sampleDepth) - position, toCamera));
            }
        }
        float h1 = -acos(clamp(horizon[1], -1.0, 1.0));
        float h2 = acos(clamp(horizon[0], -1.0, 1.0));
        visibility += projectedLength * sliceVisibility(h1, h2, nAngle);
    }
    outAo = vec4(clamp(visibility / float(AO_SLICES), 0.0, 1.0));
}
)GLSL";

        std::string fragmentSource(const AmbientOcclusion::Settings& settings)
        {
            std::ostringstream out;
            out.precision(9);
            out << "#version 450\n";
            out << "#define AO_RADIUS " << settings.radius << "\n";
            out << "#define AO_SLICES " << settings.slices << "\n";
            out << "#define AO_STEPS " << settings.steps << "\n";
            out << sFragmentBody;
            return out.str();
        }

        vsg::ref_ptr<vsg::ShaderStage> compile(VkShaderStageFlagBits stage, const std::string& source)
        {
            auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
            auto compiler = vsg::ShaderCompiler::create();
            if (!compiler->compile(shaderStage))
            {
                Log(Debug::Error) << "Failed to compile an ambient occlusion shader";
                return {};
            }
            return shaderStage;
        }

        vsg::ref_ptr<vsg::RenderPass> createPass(vsg::Device* device)
        {
            vsg::AttachmentDescription attachment = {};
            attachment.format = AmbientOcclusion::format;
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
            // As the velocity pass: written every frame while the previous frame's tonemap may still read it, and it
            // reads what the scene's pass wrote.
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

    AmbientOcclusion::AmbientOcclusion(
        vsg::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile, const Settings& settings)
        : mSettings(settings)
        , mCompile(std::move(compile))
        , mView(vsg::View::create())
        , mPass(createPass(context.device))
        , mGraph(vsg::RenderGraph::create())
        , mProjection(vsg::vec4Value::create(vsg::vec4(1.f, 1.f, 0.f, 0.f)))
    {
        mView->viewDependentState = nullptr;
    }

    AmbientOcclusion::~AmbientOcclusion() {}

    void AmbientOcclusion::setProjection(float projectionX, float projectionY)
    {
        mProjection->set(vsg::vec4(projectionX, projectionY, 0.f, 0.f));
    }

    vsg::ref_ptr<vsg::Framebuffer> AmbientOcclusion::registrationFramebuffer(vsg::Context& context)
    {
        auto image = vsgUtil::createImage(
            format, VkExtent2D{ 1, 1 }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto view = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
        return vsg::Framebuffer::create(mPass, vsg::ImageViews{ view }, 1, 1, 1);
    }

    bool AmbientOcclusion::build(vsg::Context& context, vsg::ref_ptr<vsg::ImageView> linearDepth,
        vsg::ref_ptr<vsg::ImageView> normal, const VkExtent2D& sceneExtent)
    {
        if (!mVertex)
            mVertex = compile(VK_SHADER_STAGE_VERTEX_BIT, sVertexShader);
        if (!mFragment)
            mFragment = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSource(mSettings));
        if (!mVertex || !mFragment || !linearDepth || !normal)
            return false;

        const VkExtent2D extent = Ao::halfExtent(sceneExtent);
        auto image
            = vsgUtil::createImage(format, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        auto resultView = vsg::createImageView(context, image, VK_IMAGE_ASPECT_COLOR_BIT);
        auto framebuffer
            = vsg::Framebuffer::create(mPass, vsg::ImageViews{ resultView }, extent.width, extent.height, 1);
        mLive.push_back(resultView);
        mLive.push_back(framebuffer);

        const auto imageType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // Exact texels: the images are the scene's size.
        auto sampler = vsg::Sampler::create();
        sampler->minFilter = sampler->magFilter = VK_FILTER_NEAREST;
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto setLayout = vsg::DescriptorSetLayout::create(
            vsg::DescriptorSetLayoutBindings{ { 0, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 1, imageType, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr } });
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(vsg::vec4)) } });
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
                vsg::ImageInfo::create(sampler, normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 1, 0, imageType) };

        auto commands = vsg::Commands::create();
        commands->children = {
            vsgUtil::SetViewportState::create(vsg::ViewportState::create(0, 0, extent.width, extent.height)), pipeline,
            vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, vsg::DescriptorSet::create(setLayout, descriptors)),
            vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 0, mProjection), vsg::Draw::create(3, 1, 0, 0)
        };

        // As the velocity pass: compiled for the view that the pass's render pass was registered with.
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
            Log(Debug::Error) << "The ambient occlusion view is not registered with the compile manager";
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
        mResultExtent = extent;
        return true;
    }
}
