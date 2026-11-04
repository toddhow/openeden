#include "shadersettings.hpp"

#include <algorithm>
#include <sstream>

#include <vsg/core/Value.h>
#include <vsg/state/ShaderModule.h>

#include <components/fallback/fallback.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/cloudvolumemath.hpp>
#include <components/settings/settings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

namespace
{
    // The entry of a constant the Slang twins declare (task 0072), or none.
    const Resource::SettingsConstantInfo* findConstant(const std::string& identifier)
    {
        for (const auto& entry : Resource::settingsConstantTable())
        {
            if (entry.mName == identifier)
                return &entry;
        }
        return nullptr;
    }

    void add(
        const std::string& identifier, bool value, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        stream << "const bool " << identifier << " = " << (value ? "true" : "false") << ";\n";
        if (const auto* entry = findConstant(identifier))
            constants.constants[entry->mId] = vsg::Value<VkBool32>::create(value ? VK_TRUE : VK_FALSE);
    }
    void add(const std::string& identifier, int value, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        stream << "const int " << identifier << " = " << value << ";\n";
        if (const auto* entry = findConstant(identifier))
            constants.constants[entry->mId] = vsg::intValue::create(value);
    }
    void add(
        const std::string& identifier, float value, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        stream << "const float " << identifier << " = " << value << ";\n";
        if (const auto* entry = findConstant(identifier))
            constants.constants[entry->mId] = vsg::floatValue::create(value);
    }

    void addFallbackBool(const std::string& setting, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        add(setting, Fallback::Map::getBool(setting), stream, constants);
    }
    void addFallbackInt(const std::string& setting, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        add(setting, Fallback::Map::getInt(setting), stream, constants);
    }
    void addFallbackFloat(const std::string& setting, std::stringstream& stream, vsgUtil::SettingsConstants& constants)
    {
        add(setting, Fallback::Map::getFloat(setting), stream, constants);
    }
}

namespace Resource
{
    std::span<const SettingsConstantInfo> settingsConstantTable()
    {
        // Grouped by settings file, with room to grow in each group. Ids never change once a twin uses them.
        static constexpr SettingsConstantInfo table[] = {
            // fog.settings (lib/view/fog.glsl)
            { "cRadialFog", settingsConstantBase + 0, 'b' },
            { "cExponentialFog", settingsConstantBase + 1, 'b' },
            { "cSunGlow", settingsConstantBase + 2, 'b' },
            { "cSunGlowDebug", settingsConstantBase + 3, 'b' },
            { "cSunGlowStrength", settingsConstantBase + 4, 'f' },
            { "cVolumetrics", settingsConstantBase + 5, 'b' },
            { "cVolumetricsDebug", settingsConstantBase + 6, 'b' },
            { "cAerialHaze", settingsConstantBase + 7, 'b' },
            { "cAerialVisibility", settingsConstantBase + 8, 'f' },
            { "cAerialScaleHeight", settingsConstantBase + 9, 'f' },
            { "cAerialWeatherGain", settingsConstantBase + 10, 'f' },
            // cloud.settings (sky/clouds/main.frag)
            { "cCloudsLit", settingsConstantBase + 20, 'b' },
            { "cVolumetricClouds", settingsConstantBase + 21, 'b' },
            // light.settings (lib/light/equations.glsl)
            { "LightAttenuation_UseConstant", settingsConstantBase + 40, 'b' },
            { "LightAttenuation_ConstantValue", settingsConstantBase + 41, 'f' },
            { "LightAttenuation_UseLinear", settingsConstantBase + 42, 'b' },
            { "LightAttenuation_LinearMethod", settingsConstantBase + 43, 'i' },
            { "LightAttenuation_LinearValue", settingsConstantBase + 44, 'f' },
            { "LightAttenuation_LinearRadiusMult", settingsConstantBase + 45, 'f' },
            { "LightAttenuation_UseQuadratic", settingsConstantBase + 46, 'b' },
            { "LightAttenuation_QuadraticMethod", settingsConstantBase + 47, 'i' },
            { "LightAttenuation_QuadraticValue", settingsConstantBase + 48, 'f' },
            { "LightAttenuation_QuadraticRadiusMult", settingsConstantBase + 49, 'f' },
            { "cClassicFalloff", settingsConstantBase + 50, 'b' },
            // shadow.settings (lib/view/shadow.glsl); its #define lines are not constants
            { "cEnableShadows", settingsConstantBase + 60, 'b' },
            { "cPCSS", settingsConstantBase + 61, 'b' },
            { "cShadowMapCount", settingsConstantBase + 62, 'i' },
            { "cBlockerSamples", settingsConstantBase + 63, 'i' },
            { "cShadowSamples", settingsConstantBase + 64, 'i' },
            { "cShadowMaxDist", settingsConstantBase + 65, 'f' },
            { "cShadowFadeStart", settingsConstantBase + 66, 'f' },
            // material.settings (lib/material/terms.glsl)
            { "cClampLighting", settingsConstantBase + 80, 'b' },
        };
        return table;
    }

    ShaderSettings::ShaderSettings()
    {
        constants = vsgUtil::SettingsConstants::create();
        std::stringstream source;
        addFallbackBool("LightAttenuation_UseConstant", source, *constants);
        addFallbackFloat("LightAttenuation_ConstantValue", source, *constants);
        addFallbackBool("LightAttenuation_UseLinear", source, *constants);
        addFallbackInt("LightAttenuation_LinearMethod", source, *constants);
        addFallbackFloat("LightAttenuation_LinearValue", source, *constants);
        addFallbackFloat("LightAttenuation_LinearRadiusMult", source, *constants);
        addFallbackBool("LightAttenuation_UseQuadratic", source, *constants);
        addFallbackInt("LightAttenuation_QuadraticMethod", source, *constants);
        addFallbackFloat("LightAttenuation_QuadraticValue", source, *constants);
        addFallbackFloat("LightAttenuation_QuadraticRadiusMult", source, *constants);
        //      fallback=LightAttenuation_OutQuadInLin,0
        add("cClassicFalloff", Settings::Manager::getBool("classic falloff", "Shaders"), source, *constants);

        objects["light.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        add("cEnableShadows", Settings::Manager::getBool("enable shadows", "Shadows"), source, *constants);
        add("cPCSS", Settings::Manager::getBool("percentage closer soft shadows", "Shadows"), source, *constants);
        add("cShadowMapCount", Settings::Manager::getInt("number of shadow maps", "Shadows"), source, *constants);
        add("cBlockerSamples", std::clamp(Settings::Manager::getInt("blocker samples", "Shadows"), 0, 16), source,
            *constants);
        add("cShadowSamples", std::clamp(Settings::Manager::getInt("pcf samples", "Shadows"), 0, 16), source,
            *constants);
        add("cShadowMaxDist", Settings::Manager::getFloat("maximum shadow map distance", "Shadows"), source,
            *constants);
        add("cShadowFadeStart", Settings::Manager::getFloat("shadow fade start", "Shadows"), source, *constants);
        // OpenEden (task 0038): cloud shadows multiply the sun's shadow (lib/view/shadow.glsl). The editor and other
        // hosts that never load the game's settings see the defaults, which are off.
        const bool hdr = Settings::Manager::getBool("hdr", "Rendering");
        const bool cloudShadows = Render::Cloud::useShadows(Settings::Manager::getBool("cloud shadows", "Rendering"),
            hdr, Settings::Manager::getBool("atmosphere", "Rendering"));
        if (cloudShadows)
        {
            source << "#define CLOUD_SHADOW 1\n";
            if (Settings::Manager::getBool("cloud shadow debug", "Rendering"))
                source << "#define CLOUD_SHADOW_DEBUG 1\n";
        }
        // OpenEden (task 0042): the ray marched clouds' shadow on the ground (lib/view/volumecloudshadow.glsl).
        if (Render::CloudVolume::useVolumetricClouds(Settings::Manager::getBool("volumetric clouds", "Rendering"), hdr,
                Settings::Manager::getBool("atmosphere", "Rendering"))
            && Settings::Manager::getBool("volumetric clouds shadows", "Rendering"))
            source << "#define VOLUME_CLOUD_SHADOW 1\n";
        if (Settings::Manager::getBool("enable debug hud", "Shadows"))
            source << "#define DEBUG_SHADOW 1\n";
        objects["shadow.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        // OpenEden (task 0038): the lit look of the sky's clouds (sky/clouds/main.frag).
        add("cCloudsLit",
            Render::Cloud::useClouds(Settings::Manager::getBool("clouds", "Rendering"), hdr)
                && Render::Cloud::parseLook(Settings::Manager::getString("clouds look", "Rendering"))
                    == Render::Cloud::Look::Lit,
            source, *constants);
        // OpenEden (task 0042): the volumetric clouds' result is drawn by the cloud dome (sky/clouds/main.frag) where
        // the dome's material says so.
        add("cVolumetricClouds",
            Render::CloudVolume::useVolumetricClouds(Settings::Manager::getBool("volumetric clouds", "Rendering"), hdr,
                Settings::Manager::getBool("atmosphere", "Rendering")),
            source, *constants);
        objects["cloud.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        add("cClampLighting", Settings::Manager::getBool("clamp lighting", "Shaders"), source, *constants);
        objects["material.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        // openeden-vsg: OpenMW's fog modes (task 0012).
        add("cRadialFog", Settings::Manager::getBool("radial fog", "Fog"), source, *constants);
        add("cExponentialFog", Settings::Manager::getBool("exponential fog", "Fog"), source, *constants);
        // OpenEden (task 0040): the sun's glow in haze (lib/view/fog.glsl). The editor and other hosts that never load
        // the game's settings see the defaults, which are off.
        const bool sunGlow = Settings::Manager::getBool("sun glow", "Rendering") && hdr
            && Settings::Manager::getBool("atmosphere", "Rendering");
        add("cSunGlow", sunGlow, source, *constants);
        add("cSunGlowDebug", sunGlow && Settings::Manager::getBool("sun glow debug", "Rendering"), source, *constants);
        // Read only when it is on: a host without the game's settings file has no such key.
        add("cSunGlowStrength", sunGlow ? Settings::Manager::getFloat("sun glow strength", "Rendering") : 0.f, source,
            *constants);
        // OpenEden (task 0041): the volumetric fog (lib/view/volume.glsl); only the main view has a real volume.
        const bool volumetrics = hdr && Settings::Manager::getBool("volumetrics", "Rendering");
        add("cVolumetrics", volumetrics, source, *constants);
        add("cVolumetricsDebug", volumetrics && Settings::Manager::getBool("volumetrics debug", "Rendering"), source,
            *constants);
        // OpenEden (task 0066, stage c): the aerial perspective of the whole-map view (lib/view/fog.glsl). It needs the
        // world LOD, distant terrain and `map view`; the values are only read then, since other hosts have no such
        // keys.
        const bool aerial = Settings::Manager::getBool("enabled", "WorldLod")
            && Settings::Manager::getBool("distant terrain", "Terrain")
            && Settings::Manager::getBool("map view", "WorldLod") && Settings::Manager::getBool("haze", "WorldLod");
        add("cAerialHaze", aerial, source, *constants);
        add("cAerialVisibility", aerial ? Settings::Manager::getFloat("haze visibility", "WorldLod") : 1.f, source,
            *constants);
        add("cAerialScaleHeight", aerial ? Settings::Manager::getFloat("haze height", "WorldLod") : 1.f, source,
            *constants);
        add("cAerialWeatherGain", aerial ? Settings::Manager::getFloat("haze weather gain", "WorldLod") : 0.f, source,
            *constants);
        objects["fog.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        if (Settings::Manager::getBool("debug chunks", "Terrain"))
            source << "#define DEBUG_CHUNKS 1\n";
        objects["terrain.settings"] = vsg::ShaderModule::create(source.str());
    }
}
