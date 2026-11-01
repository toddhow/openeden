#include "vulkanpass.hpp"

#include <algorithm>
#include <format>
#include <string_view>

#include <components/misc/strings/algorithm.hpp>
#include <components/version/version.hpp>

namespace Fx
{
    namespace
    {
        static_assert(sizeof(OmwData) % 4 == 0);

        const char* const sHeader = R"GLSL(#version 450

#define OMW_API_VERSION @API@
#define OMW_REVERSE_Z 1
#define OMW_NORMALS 0
#define OMW_USE_BINDINGS 1
#define OMW_MULTIVIEW 0
#define OMW_HDR 0
#define omw_In layout(location=0) in
#define omw_Out out
#define omw_Position gl_Position
#define omw_Texture1D texture
#define omw_Texture2D texture
#define omw_Texture2DArray texture
#define omw_Texture3D texture
#define omw_Vertex vec2(0.0)
#define texture1D texture
#define texture2D texture
#define texture3D texture
#define gl_FragColor omw_FragColor
#define varying layout(location=0) in

// Words reserved since the older GLSL versions `.omwfx` code was written for, which it may use as names.
#define sample omw_reserved_sample
#define patch omw_reserved_patch
#define precise omw_reserved_precise
#define subroutine omw_reserved_subroutine
#define common omw_reserved_common
#define partition omw_reserved_partition
#define active omw_reserved_active
#define filter omw_reserved_filter
#define resource omw_reserved_resource
#define input omw_reserved_input
#define output omw_reserved_output

layout(location=0) out vec4 omw_FragColor;

struct _omw_data {
    mat4 projectionMatrix;
    mat4 invProjectionMatrix;
    mat4 viewMatrix;
    mat4 prevViewMatrix;
    mat4 invViewMatrix;
    vec4 eyePos;
    vec4 eyeVec;
    vec4 fogColor;
    vec4 ambientColor;
    vec4 skyColor;
    vec4 sunColor;
    vec4 sunPos;
    vec4 sunVec;
    vec2 resolution;
    vec2 rcpResolution;
    float fogNear;
    float fogFar;
    float near;
    float far;
    float fov;
    float gameHour;
    float sunVis;
    float waterHeight;
    bool isWaterEnabled;
    float simulationTime;
    float deltaSimulationTime;
    int frameNumber;
    float windSpeed;
    float weatherTransition;
    int weatherID;
    int nextWeatherID;
    bool isUnderwater;
    bool isInterior;
};

layout(set=0, binding=0) uniform sampler2D omw_SamplerLastShader;
layout(set=0, binding=1) uniform sampler2D omw_SamplerLastPass;
layout(set=0, binding=2) uniform sampler2D omw_SamplerDepth;
layout(set=0, binding=4) uniform sampler2D omw_SamplerNormals;
layout(set=0, binding=5) uniform sampler2D omw_SamplerDistortion;
)GLSL";

        const char* const sHelpers = R"GLSL(
const int omw_PointLightsCount = 0;
const vec4 omw_PointLights[1] = vec4[1](vec4(0.0));

int omw_GetPointLightCount()
{
    return omw_PointLightsCount;
}

vec3 omw_GetPointLightWorldPos(int index)
{
    return omw_PointLights[(index * 3)].xyz;
}

vec3 omw_GetPointLightDiffuse(int index)
{
    return omw_PointLights[(index * 3) + 1].xyz;
}

vec3 omw_GetPointLightAttenuation(int index)
{
    return omw_PointLights[(index * 3) + 2].xyz;
}

float omw_GetPointLightRadius(int index)
{
    return omw_PointLights[(index * 3) + 2].w;
}

mat4 omw_ProjectionMatrix()
{
#if OMW_MULTIVIEW
    return projectionMatrixMultiView[gl_ViewID_OVR];
#else
    return omw.projectionMatrix;
#endif
}

mat4 omw_InvProjectionMatrix()
{
#if OMW_MULTIVIEW
    return invProjectionMatrixMultiView[gl_ViewID_OVR];
#else
    return omw.invProjectionMatrix;
#endif
}

    float omw_GetDepth(vec2 uv)
    {
#if OMW_MULTIVIEW
        float depth = omw_Texture2DArray(omw_SamplerDepth, vec3(uv, gl_ViewID_OVR)).r;
#else
        float depth = omw_Texture2D(omw_SamplerDepth, uv).r;
#endif
#if OMW_REVERSE_Z
        return 1.0 - depth;
#else
        return depth;
#endif
    }

    vec4 omw_GetDistortion(vec2 uv)
    {
#if OMW_MULTIVIEW
        return omw_Texture2DArray(omw_SamplerDistortion, vec3(uv, gl_ViewID_OVR));
#else
        return omw_Texture2D(omw_SamplerDistortion, uv);
#endif
    }

    vec4 omw_GetLastShader(vec2 uv)
    {
#if OMW_MULTIVIEW
        return omw_Texture2DArray(omw_SamplerLastShader, vec3(uv, gl_ViewID_OVR));
#else
        return omw_Texture2D(omw_SamplerLastShader, uv);
#endif
    }

    vec4 omw_GetLastPass(vec2 uv)
    {
#if OMW_MULTIVIEW
        return omw_Texture2DArray(omw_SamplerLastPass, vec3(uv, gl_ViewID_OVR));
#else
        return omw_Texture2D(omw_SamplerLastPass, uv);
#endif
    }

    vec3 omw_GetNormals(vec2 uv)
    {
#if OMW_MULTIVIEW
        return omw_Texture2DArray(omw_SamplerNormals, vec3(uv, gl_ViewID_OVR)).rgb * 2.0 - 1.0;
#else
        return omw_Texture2D(omw_SamplerNormals, uv).rgb * 2.0 - 1.0;
#endif
    }

    vec3 omw_GetNormalsWorldSpace(vec2 uv)
    {
        return (vec4(omw_GetNormals(uv), 0.0) * omw.viewMatrix).rgb;
    }

    vec3 omw_GetWorldPosFromUV(vec2 uv)
    {
        float depth = omw_GetDepth(uv);
#if (OMW_REVERSE_Z == 1)
        float flippedDepth = 1.0 - depth;
#else
        float flippedDepth = depth * 2.0 - 1.0;
#endif
        vec4 clip_space = vec4(uv * 2.0 - 1.0, flippedDepth, 1.0);
        vec4 world_space = omw.invViewMatrix * (omw.invProjectionMatrix * clip_space);
        return world_space.xyz / world_space.w;
    }

    float omw_GetLinearDepth(vec2 uv)
    {
#if (OMW_REVERSE_Z == 1)
        float depth = omw_GetDepth(uv);
        float dist = omw.near * omw.far / (omw.far + depth * (omw.near - omw.far));
#else
        float depth = omw_GetDepth(uv) * 2.0 - 1.0;
        float dist = 2.0 * omw.near * omw.far / (omw.far + omw.near - depth * (omw.far - omw.near));
#endif

        return dist;
    }

float omw_EstimateFogCoverageFromUV(vec2 uv)
    {
#if OMW_RADIAL_FOG
        vec3 uvPos = omw_GetWorldPosFromUV(uv);
        float dist = length(uvPos - omw.eyePos.xyz);
#else
        float dist = omw_GetLinearDepth(uv);
#endif
#if OMW_EXPONENTIAL_FOG
        float fogValue = 1.0 - exp(-2.0 * max(0.0, dist - omw.fogNear/2.0) / (omw.fogFar - omw.fogNear/2.0));
#else
        float fogValue = clamp((dist - omw.fogNear) / (omw.fogFar - omw.fogNear), 0.0, 1.0);
#endif

        return fogValue;
    }

#if OMW_HDR
    uniform sampler2D omw_EyeAdaptation;
#endif

    float omw_GetEyeAdaptation()
    {
#if OMW_HDR
        return omw_Texture2D(omw_EyeAdaptation, vec2(0.5, 0.5)).r;
#else
        return 1.0;
#endif
    }
)GLSL";

        // std140 size and alignment of a uniform's GLSL type; arrays use a vec4 stride.
        struct Std140
        {
            std::size_t size;
            std::size_t alignment;
        };

        Std140 std140Of(std::string_view type, std::size_t arrayLength)
        {
            Std140 element{ 4, 4 };
            if (type == "vec2")
                element = { 8, 8 };
            else if (type == "vec3")
                element = { 12, 16 };
            else if (type == "vec4")
                element = { 16, 16 };
            if (arrayLength == 0)
                return element;
            return { 16 * arrayLength, 16 };
        }
    }

    bool translatePass(
        Technique& technique, const Types::PassDesc& pass, const VulkanPassOptions& options, VulkanPass& out)
    {
        if (!pass.mFragment || pass.mFragment->empty())
            return false;

        out = VulkanPass{};
        std::string samplers;
        std::string constants;
        std::string members;
        // std140 starts the member after a nested struct on a 16-byte boundary.
        std::size_t offset = (sizeof(OmwData) + 15) / 16 * 16;
        std::uint32_t binding = VulkanBinding_FirstSampler;

        for (const std::string& target : pass.mRenderTargets)
        {
            if (target.empty())
                continue;
            samplers += std::format("layout(set=0, binding={}) uniform sampler2D {};\n", binding++, target);
            out.samplers.push_back(target);
        }

        for (const auto& uniform : technique.getUniformMap())
        {
            const std::optional<std::string> glsl = uniform->getGLSL(false);
            if (!glsl)
                continue;
            constexpr std::string_view keyword = "uniform ";
            if (!std::string_view(*glsl).starts_with(keyword))
            {
                constants += *glsl + '\n';
                continue;
            }
            // "uniform <type> <name>[<length>];"
            const std::string_view declaration = std::string_view(*glsl).substr(keyword.size());
            const std::size_t space = declaration.find(' ');
            const std::string_view type = declaration.substr(0, space);
            if (type.starts_with("sampler"))
            {
                samplers += std::format("layout(set=0, binding={}) {}\n", binding++, *glsl);
                out.samplers.push_back(uniform->mName);
                continue;
            }
            std::size_t arrayLength = 0;
            if (const std::size_t bracket = declaration.find('['); bracket != std::string_view::npos)
                arrayLength = static_cast<std::size_t>(std::stoul(std::string(declaration.substr(bracket + 1))));
            const Std140 layout = std140Of(type, arrayLength);
            offset = (offset + layout.alignment - 1) / layout.alignment * layout.alignment;
            out.uniforms.push_back({ uniform->mName, offset });
            offset += layout.size;
            members += std::format("    {}\n", declaration);
        }
        out.uniformBytes = (offset + 15) / 16 * 16;

        out.fragment = sHeader;
        out.fragment.replace(out.fragment.find("@API@"), 5, std::to_string(Version::getPostprocessingApiRevision()));
        std::string extensions;
        for (const std::string& extension : technique.getGLSLExtensions())
            extensions += std::format("#ifdef {0}\n#extension {0}: enable\n#endif\n", extension);
        // Extension directives go right after #version.
        out.fragment.insert(out.fragment.find('\n') + 1, extensions);
        out.fragment += std::format("#define OMW_RADIAL_FOG {}\n#define OMW_EXPONENTIAL_FOG {}\n",
            options.radialFog ? 1 : 0, options.exponentialFog ? 1 : 0);
        out.fragment += samplers;
        out.fragment += constants;
        out.fragment += std::format("layout(std140, set=0, binding={}) uniform _data {{\n    _omw_data omw;\n{}}};\n",
            int(VulkanBinding_Uniforms), members);
        out.fragment += sHelpers;
        out.fragment += technique.getShared();
        out.fragment += '\n';
        out.fragment += *pass.mFragment;
        return true;
    }
}
