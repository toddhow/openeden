#ifndef OPENEDEN_COMPONENTS_FX_VULKANPASS_H
#define OPENEDEN_COMPONENTS_FX_VULKANPASS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec4.h>

#include "technique.hpp"

namespace Fx
{
    /// The built-in `omw` values of OpenMW's Fx::StateUpdater at 442eec6e2f, in its order and with std140 layout, so
    /// `.omwfx` code reads the same names. Booleans are 32-bit integers, as std140 lays them out.
    struct OmwData
    {
        vsg::mat4 projectionMatrix;
        vsg::mat4 invProjectionMatrix;
        vsg::mat4 viewMatrix;
        vsg::mat4 prevViewMatrix;
        vsg::mat4 invViewMatrix;
        vsg::vec4 eyePos;
        vsg::vec4 eyeVec;
        vsg::vec4 fogColor;
        vsg::vec4 ambientColor;
        vsg::vec4 skyColor;
        vsg::vec4 sunColor;
        vsg::vec4 sunPos;
        vsg::vec4 sunVec;
        vsg::vec2 resolution;
        vsg::vec2 rcpResolution;
        float fogNear = 0.f;
        float fogFar = 0.f;
        float near = 0.f;
        float far = 0.f;
        float fov = 0.f;
        float gameHour = 0.f;
        float sunVis = 0.f;
        float waterHeight = 0.f;
        std::int32_t isWaterEnabled = 0;
        float simulationTime = 0.f;
        float deltaSimulationTime = 0.f;
        std::int32_t frameNumber = 0;
        float windSpeed = 0.f;
        float weatherTransition = 0.f;
        std::int32_t weatherID = 0;
        std::int32_t nextWeatherID = 0;
        std::int32_t isUnderwater = 0;
        std::int32_t isInterior = 0;
    };

    /// The descriptor bindings of a translated pass. Binding_FirstSampler onward are VulkanPass::samplers, in order.
    enum VulkanBinding : std::uint32_t
    {
        VulkanBinding_LastShader = 0,
        VulkanBinding_LastPass = 1,
        VulkanBinding_Depth = 2,
        VulkanBinding_Uniforms = 3,
        VulkanBinding_Normals = 4,
        VulkanBinding_Distortion = 5,
        VulkanBinding_FirstSampler = 6,
    };

    /// Where one of a technique's non-constant uniforms lives in the pass's uniform block.
    struct VulkanUniformSlot
    {
        std::string name;
        std::size_t offset = 0;
    };

    struct VulkanPass
    {
        /// Vulkan GLSL of the fragment stage.
        std::string fragment;
        /// Names of the samplers at VulkanBinding_FirstSampler onward: the pass's render targets, then the technique's
        /// sampler uniforms.
        std::vector<std::string> samplers;
        /// The uniform block's slots after OmwData, and its whole size in bytes.
        std::vector<VulkanUniformSlot> uniforms;
        std::size_t uniformBytes = 0;
    };

    struct VulkanPassOptions
    {
        bool radialFog = false;
        bool exponentialFog = false;
    };

    /// Translates a parsed pass to Vulkan GLSL (task 0012, V4p), keeping OpenMW's `omw_` helpers of
    /// Fx::Pass::getPassHeader at 442eec6e2f, so `.omwfx` code is used unchanged:
    /// - Plain uniforms become members of one anonymous uniform block, after `omw`, so their bare names still resolve;
    ///   constant uniforms stay constants.
    /// - Samplers get explicit bindings.
    /// - `omw_In` is location 0, the fullscreen vertex stage's omw_TexCoord; `omw_FragColor` is output location 0.
    /// - The older names `.omwfx` code may use (texture2D, gl_FragColor, varying) are defined to the current ones.
    ///
    /// Reversed depth is on, as the renderer's. Point lights are declared with a count of zero, and normals, HDR and
    /// multiview are off. A pass with no fragment source returns false.
    bool translatePass(
        Technique& technique, const Types::PassDesc& pass, const VulkanPassOptions& options, VulkanPass& out);
}

#endif
