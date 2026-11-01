#include "tonemap.hpp"

#include <algorithm>
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
#include <components/render/sssmath.hpp>
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
#ifdef AO
layout(set=0, binding=6) uniform sampler2D gAmbient;
layout(set=0, binding=7) uniform sampler2D gAo;       // half resolution, nearest
layout(set=0, binding=8) uniform sampler2D gAoNormal;
layout(set=0, binding=9) uniform sampler2D gAoDepth;
#endif
#ifdef SSS
layout(set=0, binding=10) uniform sampler2D gSssMaterial; // alpha: 1 on skin
layout(set=0, binding=11) uniform sampler2D gSssDepth;
layout(set=0, binding=12) uniform sampler2D gSssNormal;
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
#ifdef AO
    vec4 ao;     // xy the size of one texel of the occlusion image, z strength
#endif
#ifdef SSS
    vec4 sss;    // x strength, y width in game units, z focal length in pixels
#endif
#ifdef DEBUG_VIEW
    vec4 debug;  // x mode (1 normals, 2 depth, 3 velocity, 4 roughness, 5 material, 6 subsurface, 7 ao, 8 ambient, 9 sss), y depth range, z velocity scale
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

#ifdef AO
// Task 0029: the CPU twins are Render::Ao::bilateralWeight and aoCurve (aomath.hpp). The half resolution occlusion is
// filtered over 5 by 5 of its texels, each weighted by whether its surface (the stored depth and normal at that
// texel centre) is this pixel's.
float aoSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float aoBilateral(float depthDifference, float depth, float normalDot)
{
    float relative = depth > 0.0 ? abs(depthDifference) / depth : 1.0;
    return (1.0 - aoSmooth(0.02, 0.05, relative)) * aoSmooth(0.707, 0.9, normalDot);
}
// pow(0, 0) is undefined in GLSL, so strength 0 (no occlusion) is handled first.
float aoApply(float ao, float strength)
{
    return strength > 0.0 ? pow(max(ao, 0.0), strength) : 1.0;
}
float aoFiltered(vec2 uv)
{
    float d0 = texture(gAoDepth, uv).r;
    if (!(d0 > 0.0))
        return 1.0;
    vec3 n0 = texture(gAoNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            vec2 tap = uv + vec2(float(x), float(y)) * push.ao.xy;
            vec2 centre = (floor(tap / push.ao.xy) + 0.5) * push.ao.xy;
            float d = texture(gAoDepth, centre).r;
            if (!(d > 0.0))
                continue;
            vec3 n = texture(gAoNormal, centre).xyz;
            n = dot(n, n) > 0.0 ? normalize(n) : n0;
            float w = aoBilateral(d - d0, d0, dot(n, n0)) * (1.0 - 0.1 * float(x * x + y * y));
            sum += texture(gAo, centre).r * w;
            total += w;
        }
    // No neighbour on this surface: no occlusion, rather than another surface's.
    return total > 1e-4 ? sum / total : 1.0;
}
#endif

#ifdef SSS
// Task 0033: the CPU twins are Render::Sss::gaussian, tapOffset, surfaceWeight and scatter (sssmath.hpp). For a pixel on
// skin, each colour channel becomes: itself times a gain, the gain being a wide Gaussian average of the neighbouring
// skin on the same surface over a very narrow one, blended in by the strength. The constants repeat sssmath.hpp (the
// numbers are written in).
float sssSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float sssGaussian(float distance, float sigma)
{
    if (!(sigma > 0.0))
        return distance == 0.0 ? 1.0 : 0.0;
    return exp(-distance * distance / (2.0 * sigma * sigma));
}
float sssSurface(float depthDelta, float width, float normalDot)
{
    float limit = max(width, 1e-4);
    return (1.0 - sssSmooth(limit, 2.0 * limit, abs(depthDelta))) * sssSmooth(0.2, 0.6, normalDot);
}
vec3 sssApply(vec2 uv, vec3 centre)
{
    // Skin is marked by the material output's alpha; multisampling averages it, so an edge pixel counts when half is skin.
    if (!(push.sss.x > 0.0) || texture(gSssMaterial, uv).a < 0.5)
        return centre;
    float depth = texture(gSssDepth, uv).r;
    if (!(depth > 0.0))
        return centre;
    float width = push.sss.y;
    float pixelsPerUnit = push.sss.z / depth;
    vec3 n0 = texture(gSssNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));
    // The spiral is not turned from pixel to pixel: with 16 taps a per-pixel turn made the wide and the narrow averages
    // differ by noise, and the face came out dotted (found by the frame check).
    float rotation = 0.0;
    vec3 sigma = SSS_CHANNEL_SIGMA * width;
    float narrow = SSS_NARROW_SIGMA * width;
    float reach = SSS_REACH * width;
    vec3 wideSum = centre;
    vec3 wideWeight = vec3(1.0);
    vec3 narrowSum = centre;
    float narrowWeight = 1.0;
    for (int i = 0; i < SSS_TAPS; ++i)
    {
        float r = reach * sqrt((float(i) + 0.5) / float(SSS_TAPS));
        float a = float(i) * 2.39996323 + rotation;
        vec2 tap = uv + vec2(cos(a), sin(a)) * (r * pixelsPerUnit) * texel;
        if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
            continue;
        if (texture(gSssMaterial, tap).a < 0.5)
            continue;
        float d = texture(gSssDepth, tap).r;
        if (!(d > 0.0))
            continue;
        vec3 n = texture(gSssNormal, tap).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n0;
        float v = sssSurface(d - depth, width, dot(n, n0));
        if (v <= 0.0)
            continue;
        vec3 c = texture(sceneColor, tap).rgb;
        vec3 w1 = vec3(sssGaussian(r, sigma.x), sssGaussian(r, sigma.y), sssGaussian(r, sigma.z)) * v;
        float w2 = sssGaussian(r, narrow) * v;
        wideSum += c * w1;
        wideWeight += w1;
        narrowSum += c * w2;
        narrowWeight += w2;
    }
    vec3 gain = clamp((wideSum / wideWeight + SSS_EPSILON) / (narrowSum / narrowWeight + SSS_EPSILON),
        vec3(SSS_GAIN_MIN), vec3(SSS_GAIN_MAX));
    return max(centre * (vec3(1.0) + push.sss.x * (gain - vec3(1.0))), vec3(0.0));
}
#endif

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
#ifdef AO
        else if (mode == 7)
            shown = vec3(aoApply(aoFiltered(texCoord), push.ao.z));
        else if (mode == 8)
            shown = texture(gAmbient, texCoord).rgb;
#endif
#ifdef SSS
        else if (mode == 9)
        {
            vec3 base = texture(sceneColor, texCoord).rgb;
            shown = clamp(abs(sssApply(texCoord, base) - base) * 8.0, vec3(0.0), vec3(1.0));
        }
#endif
        if (mode != 0)
        {
            outColor = vec4(shown, 1.0);
            return;
        }
    }
#endif
    vec3 color = texture(sceneColor, texCoord).rgb;
#ifdef SSS
    color = sssApply(texCoord, color);
#endif
#ifdef AO
    // Only the ambient part of the colour is darkened (Render::Ao::removeOcclusion).
    color = max(color - texture(gAmbient, texCoord).rgb * (1.0 - aoApply(aoFiltered(texCoord), push.ao.z)), vec3(0.0));
#endif
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

        std::string fragmentSource(bool bloom, bool debug, bool ao, bool sss)
        {
            std::ostringstream out;
            out.precision(9);
            out << "#version 450\n";
            if (ao)
                out << "#define AO 1\n";
            if (sss)
            {
                const auto& sigma = Sss::channelSigma;
                out << "#define SSS 1\n";
                out << "#define SSS_CHANNEL_SIGMA vec3(" << sigma.x << ", " << sigma.y << ", " << sigma.z << ")\n";
                out << "#define SSS_NARROW_SIGMA " << Sss::narrowSigma << "\n";
                out << "#define SSS_REACH " << Sss::reachInWidths << "\n";
                out << "#define SSS_TAPS " << Sss::tapCount << "\n";
                out << "#define SSS_EPSILON " << Sss::epsilon << "\n";
                out << "#define SSS_GAIN_MIN " << Sss::gainMin << "\n";
                out << "#define SSS_GAIN_MAX " << Sss::gainMax << "\n";
            }
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
        , mPushAo(vsg::vec4Value::create(aoPushConstants(VkExtent2D{ 1, 1 }, 1.f)))
        , mPushSss(vsg::vec4Value::create(sssPushConstants(0.f, 1.f, 0.f)))
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

    Tonemap::~Tonemap() {}

    void Tonemap::setParameters(const Parameters& parameters)
    {
        mParameters = parameters;
        mPush->set(pushConstants(parameters));
        mPushDebug->set(debugPushConstants(parameters.debugView, parameters.depthRange, parameters.velocityScale));
        mPushAo->set(aoPushConstants(mAoExtent, parameters.aoStrength));
        mPushSss->set(sssPushConstants(parameters.sssStrength, parameters.sssWidth, parameters.sssFocalPixels));
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
        if (ao)
        {
            mAoExtent = aoSources.aoExtent;
            mPushAo->set(aoPushConstants(mAoExtent, mParameters.aoStrength));
        }
        // Compiled once: a resize only changes the image, the viewport and the pipeline built around them.
        auto& fragment = mFragments[(bloom ? 1 : 0) + (debug ? 2 : 0) + (ao ? 4 : 0) + (sss ? 8 : 0)];
        if (!mVertex)
            mVertex = compile(VK_SHADER_STAGE_VERTEX_BIT, sVertexShader);
        if (!fragment)
            fragment = compile(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSource(bloom != nullptr, debug, ao, sss));
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
        auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ setLayout },
            vsg::PushConstantRanges{ { VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                static_cast<uint32_t>(debugOffset + (debug ? sizeof(vsg::vec4) : 0)) } });

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
