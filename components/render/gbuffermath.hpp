#ifndef OPENEDEN_RENDER_GBUFFERMATH_H
#define OPENEDEN_RENDER_GBUFFERMATH_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/vk/vulkan.h>

namespace Render::GBuffer
{
    /*
     * The pure parts of the stored depth, normals and velocity (task 0026, F3). The shaders repeat the debug colours
     * and the reprojection (Render::Tonemap's debug view, Render::Velocity): change both together.
     */

    enum class DebugView
    {
        None,
        Normals,
        Depth,
        Velocity,
        // Task 0027: the stored material (roughness in the normal image's w; metalness, reflectance and occlusion in
        // the material output; whether a pixel is marked as skin).
        Roughness,
        Material,
        Subsurface,
        // Task 0029: the filtered occlusion, and the ambient part of the colour that it darkens.
        Ao,
        Ambient,
        // Task 0033: what subsurface scattering changes (eight times the difference), white on skin.
        Sss
    };

    inline std::optional<DebugView> parseDebugView(std::string_view name)
    {
        if (name == "none")
            return DebugView::None;
        if (name == "normals")
            return DebugView::Normals;
        if (name == "depth")
            return DebugView::Depth;
        if (name == "velocity")
            return DebugView::Velocity;
        if (name == "roughness")
            return DebugView::Roughness;
        if (name == "material")
            return DebugView::Material;
        if (name == "subsurface")
            return DebugView::Subsurface;
        if (name == "ao")
            return DebugView::Ao;
        if (name == "ambient")
            return DebugView::Ambient;
        if (name == "sss")
            return DebugView::Sss;
        return std::nullopt;
    }

    // The G-buffer lives in the HDR path's own render pass, so it needs that path (see the task's Design).
    inline bool useGBuffer(bool settingOn, bool hdrPath)
    {
        return settingOn && hdrPath;
    }

    // View-space shading normal, and a marker in w; all zero where there is no opaque surface.
    constexpr VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    // Distance along the view axis in game units; 0 where there is no opaque surface.
    constexpr VkFormat linearDepthFormat = VK_FORMAT_R32_SFLOAT;
    // Screen motion of skinned actors (stage c).
    constexpr VkFormat objectMotionFormat = VK_FORMAT_R16G16_SFLOAT;
    // currentUV - previousUV (stage b).
    constexpr VkFormat velocityFormat = VK_FORMAT_R16G16_SFLOAT;

    // Metalness, reflectance, occlusion and the subsurface mark (task 0027); only with the material model on.
    constexpr VkFormat materialFormat = VK_FORMAT_R8G8B8A8_UNORM;

    // The ambient part of each opaque pixel's colour, in linear light (task 0029); only with ambient occlusion on.
    constexpr VkFormat ambientFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // The colour outputs after the scene's own, in output order (1, 2, 3, 4 with the material model, and 5 with the
    // ambient output, which needs the material model before it).
    inline std::vector<VkFormat> extraColorFormats(bool objectMotion, bool material = false, bool ambient = false)
    {
        std::vector<VkFormat> formats{ normalFormat, linearDepthFormat };
        if (objectMotion)
            formats.push_back(objectMotionFormat);
        if (material && objectMotion)
            formats.push_back(materialFormat);
        if (ambient && material && objectMotion)
            formats.push_back(ambientFormat);
        return formats;
    }

    inline vsg::vec3 debugNormalColor(const vsg::vec3& viewNormal)
    {
        if (viewNormal.x == 0.f && viewNormal.y == 0.f && viewNormal.z == 0.f)
            return vsg::vec3(0.f, 0.f, 0.f);
        return vsg::vec3(viewNormal.x * 0.5f + 0.5f, viewNormal.y * 0.5f + 0.5f, viewNormal.z * 0.5f + 0.5f);
    }

    inline float debugDepthGray(float linearDepth, float range)
    {
        if (linearDepth <= 0.f || range <= 0.f)
            return 0.f;
        return 1.f - std::exp(-linearDepth / range);
    }

    inline vsg::vec3 debugVelocityColor(const vsg::vec2& velocity, float scale)
    {
        const auto unit = [](float v) { return std::clamp(v, 0.f, 1.f); };
        return vsg::vec3(unit(0.5f + velocity.x * scale), unit(0.5f + velocity.y * scale), 0.5f);
    }

    // The CPU twin of the velocity shader: where a static point seen at `uv` with this linear depth was in the previous
    // frame. Matrices are column-major (vsg).
    inline vsg::vec2 reprojectVelocity(const vsg::vec2& uv, float linearDepth, const vsg::mat4& projection,
        const vsg::mat4& invView, const vsg::mat4& prevView, const vsg::mat4& prevProjection)
    {
        if (linearDepth <= 0.f)
            return vsg::vec2(0.f, 0.f);
        const vsg::vec4 viewPos((uv.x * 2.f - 1.f) * linearDepth / projection(0, 0),
            (uv.y * 2.f - 1.f) * linearDepth / projection(1, 1), -linearDepth, 1.f);
        const vsg::vec4 world = invView * viewPos;
        const vsg::vec4 prevClip = prevProjection * (prevView * world);
        if (prevClip.w <= 0.f) // behind the previous camera, as the shader
            return vsg::vec2(0.f, 0.f);
        const vsg::vec2 prevUv(prevClip.x / prevClip.w * 0.5f + 0.5f, prevClip.y / prevClip.w * 0.5f + 0.5f);
        return vsg::vec2(uv.x - prevUv.x, uv.y - prevUv.y);
    }
}

#endif
