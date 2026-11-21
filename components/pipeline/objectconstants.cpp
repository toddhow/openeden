#include "objectconstants.hpp"

#include <vsg/core/Value.h>

#include "terrain.hpp"

namespace Pipeline
{
    vsg::ShaderStage::SpecializationConstants objectFragmentConstants(const std::set<std::string>& defines)
    {
        const auto has = [&](const char* name) { return defines.count(name) != 0; };
        const auto flag = [](bool on) { return vsg::Value<VkBool32>::create(on ? VK_TRUE : VK_FALSE); };
        // default/default.frag.slang's derived defines.
        const bool normal = has("NORMAL");
        const bool perPixel
            = normal && (has("NORMAL_MAP") || has("SPECULAR_MAP") || has("PBR") || has("FORCE_PER_PIXEL_LIGHTING"));
        const bool pbrLit = normal && has("PBR");
        vsg::ShaderStage::SpecializationConstants constants;
        constants[TerrainConstantId::GiSky] = flag(has("GI_SKY"));
        constants[TerrainConstantId::Wetness] = flag(has("WETNESS"));
        constants[TerrainConstantId::WetnessPuddles] = flag(has("WETNESS_PUDDLES"));
        constants[TerrainConstantId::WetnessDebug] = flag(has("WETNESS_DEBUG"));
        constants[TerrainConstantId::Cover] = flag(has("COVER"));
        constants[TerrainConstantId::CoverDebug] = flag(has("COVER_DEBUG"));
        constants[TerrainConstantId::AmbientCube] = flag(has("AMBIENT_CUBE"));
        constants[TerrainConstantId::PointShadow] = flag(has("POINT_SHADOW"));
        constants[TerrainConstantId::AoTerrain] = flag(false);
        constants[ObjectConstantId::Gi] = flag(has("GI"));
        constants[ObjectConstantId::PerPixelLighting] = flag(perPixel);
        constants[ObjectConstantId::PbrLit] = flag(pbrLit);
        constants[ObjectConstantId::PbrPhysical] = flag(has("PBR_PHYSICAL"));
        constants[ObjectConstantId::WetLit] = flag(pbrLit && has("WETNESS"));
        constants[ObjectConstantId::CoverLit] = flag(pbrLit && has("COVER"));
        constants[ObjectConstantId::LodFade] = flag(has("LOD_FADE"));
        constants[ObjectConstantId::AdjustCoverage] = flag(has("ADJUST_COVERAGE"));
        // Plan B. The old single step parallax is the fallback only where parallax occlusion does not run (the shader's
        // `#elif`); the precipitation light is a constant only where the shader can draw it (a diffuse map and
        // coordinates), otherwise vsgUtil::constantDefinesFor keeps the define.
        const bool pom = has("PARALLAX_POM") && has("HEIGHT_MAP");
        constants[ObjectConstantId::Pom] = flag(pom);
        constants[ObjectConstantId::ParallaxDiscard] = flag(has("PARALLAX_DISCARD"));
        constants[ObjectConstantId::Parallax] = flag(has("PARALLAX") && !pom);
        constants[ObjectConstantId::ReconstructNormalZ] = flag(has("RECONSTRUCT_NORMAL_Z"));
        constants[ObjectConstantId::Rmaos] = flag(has("MATERIAL_MAP_RMAOS"));
        constants[ObjectConstantId::PrecipLit] = flag(has("PRECIPITATION_LIT"));
        // lib/material/parallax.slang: low, high, and medium (also the value when parallax is off).
        int minSteps = 10, maxSteps = 20, refineTaps = 4;
        if (has("PARALLAX_POM_LOW"))
        {
            minSteps = 8;
            maxSteps = 12;
            refineTaps = 0;
        }
        else if (has("PARALLAX_POM_HIGH"))
        {
            minSteps = 16;
            maxSteps = 48;
        }
        constants[TerrainConstantId::PomMinSteps] = vsg::intValue::create(minSteps);
        constants[TerrainConstantId::PomMaxSteps] = vsg::intValue::create(maxSteps);
        constants[TerrainConstantId::PomRefineTaps] = vsg::intValue::create(refineTaps);
        return constants;
    }

    vsg::ShaderStage::SpecializationConstants objectVertexConstants(const std::set<std::string>& defines)
    {
        vsg::ShaderStage::SpecializationConstants constants;
        constants[ObjectConstantId::LodSway]
            = vsg::Value<VkBool32>::create(defines.count("LOD_SWAY") ? VK_TRUE : VK_FALSE);
        return constants;
    }
}
