#ifndef OPENEDEN_RENDER_MATERIALMATH_H
#define OPENEDEN_RENDER_MATERIALMATH_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

namespace Render::Pbr
{
    /*
     * The pure parts of the material model (task 0027, F4 and R1). The shaders repeat the formulas
     * (files/shaders/lib/material/pbr.glsl): change both together.
     */

    // The lowest roughness: a smaller one makes the GGX peak so narrow that it flickers in 16-bit floats.
    constexpr float minRoughness = 0.045f;
    // Tier 1 values for a surface with no data (stock Morrowind assets): rough, non-metal, F0 = 0.04.
    constexpr float defaultRoughness = 0.8f;
    constexpr float defaultReflectance = 0.5f;
    // The terrain has no per-layer data (task 0027).
    constexpr float terrainRoughness = 0.95f;
    // Tier 3: the packed occlusion (R), roughness (G), metalness (B) file next to a diffuse texture. A proposal that
    // toddhow has to approve (task 0027); not a setting.
    constexpr std::string_view materialMapSuffix = "_orm";

    // Blinn-Phong exponent n to GGX: alpha = sqrt(2 / (n + 2)) (the usual relation), and alpha = roughness squared, so
    // roughness = (2 / (n + 2))^(1/4).
    inline float roughnessFromShininess(float shininess)
    {
        if (!(shininess > 0.f))
            return 1.f;
        return std::clamp(std::pow(2.f / (shininess + 2.f), 0.25f), minRoughness, 1.f);
    }

    // A specular colour of black is the vanilla "no specular" and keeps the dielectric default (0.5, F0 = 0.04); white
    // raises it to 1 (F0 = 0.16). Channels clamp to [0,1].
    inline float reflectanceFromSpecular(const vsg::vec3& colour)
    {
        const auto unit = [](float v) { return std::clamp(v, 0.f, 1.f); };
        const float luma = 0.2126f * unit(colour.x) + 0.7152f * unit(colour.y) + 0.0722f * unit(colour.z);
        return 0.5f + 0.5f * luma;
    }

    inline float f0FromReflectance(float reflectance)
    {
        return 0.16f * reflectance * reflectance;
    }

    inline vsg::vec3 specularF0(const vsg::vec3& base, float metalness, float reflectance)
    {
        const float dielectric = f0FromReflectance(reflectance);
        return vsg::vec3(dielectric + (base.x - dielectric) * metalness, dielectric + (base.y - dielectric) * metalness,
            dielectric + (base.z - dielectric) * metalness);
    }

    inline vsg::vec3 diffuseAlbedo(const vsg::vec3& base, float metalness)
    {
        return vsg::vec3(base.x * (1.f - metalness), base.y * (1.f - metalness), base.z * (1.f - metalness));
    }

    inline float distributionGGX(float NoH, float roughness)
    {
        const float a = roughness * roughness;
        const float a2 = a * a;
        const float d = NoH * NoH * (a2 - 1.f) + 1.f;
        return a2 / (3.14159265358979f * d * d);
    }

    // Height-correlated Smith visibility, V = G / (4 NoV NoL).
    inline float visibilitySmithGGX(float NoV, float NoL, float roughness)
    {
        const float a2 = roughness * roughness * roughness * roughness;
        const float v = NoL * std::sqrt(NoV * NoV * (1.f - a2) + a2);
        const float l = NoV * std::sqrt(NoL * NoL * (1.f - a2) + a2);
        return 0.5f / std::max(v + l, 1e-5f);
    }

    inline vsg::vec3 fresnelSchlick(float VoH, const vsg::vec3& f0)
    {
        const float t = std::pow(1.f - std::clamp(VoH, 0.f, 1.f), 5.f);
        return vsg::vec3(f0.x + (1.f - f0.x) * t, f0.y + (1.f - f0.y) * t, f0.z + (1.f - f0.z) * t);
    }

    struct Surface
    {
        float roughness = defaultRoughness;
        float metalness = 0.f;
        float reflectance = defaultReflectance;
        float occlusion = 1.f;
        bool subsurface = false;
    };

    inline Surface clampSurface(Surface s)
    {
        const auto fix
            = [](float v, float lo, float fallback) { return std::isnan(v) ? fallback : std::clamp(v, lo, 1.f); };
        s.roughness = fix(s.roughness, minRoughness, defaultRoughness);
        s.metalness = fix(s.metalness, 0.f, 0.f);
        s.reflectance = fix(s.reflectance, 0.f, defaultReflectance);
        s.occlusion = fix(s.occlusion, 0.f, 1.f);
        return s;
    }

    // What the G-buffer's `material` output holds; roughness goes in the normal image's w.
    inline vsg::vec4 packMaterial(const Surface& s)
    {
        const Surface c = clampSurface(s);
        return vsg::vec4(c.metalness, c.reflectance, c.occlusion, s.subsurface ? 1.f : 0.f);
    }

    inline Surface unpackMaterial(float roughness, const vsg::vec4& material)
    {
        Surface s;
        s.roughness = roughness;
        s.metalness = material.x;
        s.reflectance = material.y;
        s.occlusion = material.z;
        s.subsurface = material.w > 0.5f;
        return s;
    }

    enum class Look
    {
        Vanilla,
        Physical
    };

    inline std::optional<Look> parseLook(std::string_view name)
    {
        if (name == "vanilla")
            return Look::Vanilla;
        if (name == "physical")
            return Look::Physical;
        return std::nullopt;
    }

    // The physically based lighting lives in the HDR path's linear lighting (task 0027).
    inline bool usePbr(bool settingOn, bool hdrPath)
    {
        return settingOn && hdrPath;
    }

    // The material output is an extra colour output of the G-buffer.
    inline bool useMaterialOutput(bool pbr, bool gbuffer)
    {
        return pbr && gbuffer;
    }

    /*
     * The True PBR `_rmaos` layout of the tier 3 map (task 0044). A community convention (Community Shaders' "True
     * PBR"), not a standard: R roughness, G metalness, B ambient occlusion (0 = occluded), A specular. The shaders
     * repeat the decode (`pbrApplyMapRmaos` in files/shaders/lib/material/pbr.glsl): change both together.
     */
    constexpr std::string_view rmaosMapSuffix = "_rmaos";
    // The dielectric F0 that a specular level of 1 times a texture alpha of 1 gives; the layout's default.
    constexpr float defaultSpecularLevel = 0.04f;
    // The roughness scale of a set cannot be zero or negative (that means no scale).
    constexpr double maxRoughnessScale = 8.0;

    enum class MapLayout
    {
        Orm,
        Rmaos
    };

    struct MapTexel
    {
        float roughness = defaultRoughness;
        float metalness = 0.f;
        float occlusion = 1.f;
        // The texture's own specular multiplier; the `_orm` layout has none (1).
        float specular = 1.f;
    };

    // Reads one texel (channels 0 to 1). Out-of-range values clamp, NaN falls back to the tier 1 value.
    inline MapTexel decodeMapTexel(MapLayout layout, const vsg::vec4& rgba)
    {
        const auto unit = [](float v, float fallback) { return std::isnan(v) ? fallback : std::clamp(v, 0.f, 1.f); };
        MapTexel t;
        if (layout == MapLayout::Rmaos)
        {
            t.roughness = unit(rgba.x, defaultRoughness);
            t.metalness = unit(rgba.y, 0.f);
            t.occlusion = unit(rgba.z, 1.f);
            t.specular = unit(rgba.w, 1.f);
        }
        else
        {
            t.occlusion = unit(rgba.x, 1.f);
            t.roughness = unit(rgba.y, defaultRoughness);
            t.metalness = unit(rgba.z, 0.f);
        }
        t.roughness = std::max(t.roughness, minRoughness);
        return t;
    }

    // A set's `roughnessScale`: a scale of zero, below zero or NaN means "none".
    inline float scaledRoughness(float roughness, float scale)
    {
        if (std::isnan(roughness))
            return defaultRoughness;
        if (!(scale > 0.f))
            scale = 1.f;
        return std::clamp(roughness * scale, minRoughness, 1.f);
    }

    // F0 of a non-metal: the set's specular level times the texture's alpha (both 0 to 1; NaN gives the default).
    inline float dielectricF0(float specularLevel, float textureSpecular)
    {
        const auto unit = [](float v, float fallback) { return std::isnan(v) ? fallback : std::clamp(v, 0.f, 1.f); };
        return unit(specularLevel, defaultSpecularLevel) * unit(textureSpecular, 1.f);
    }

    // The reflectance (0 to 1) whose f0FromReflectance is f0.
    inline float reflectanceFromF0(float f0)
    {
        if (std::isnan(f0))
            return defaultReflectance;
        return std::clamp(std::sqrt(std::max(f0, 0.f) / 0.16f), 0.f, 1.f);
    }
}

#endif
