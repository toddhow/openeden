#include "shadersettings.hpp"

#include <algorithm>
#include <sstream>

#include <vsg/state/ShaderModule.h>

#include <components/fallback/fallback.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/cloudvolumemath.hpp>
#include <components/settings/settings.hpp>

namespace
{
    void add(const std::string& identifier, bool value, std::stringstream& stream)
    {
        stream << "const bool " << identifier << " = " << (value ? "true" : "false") << ";\n";
    }
    void add(const std::string& identifier, int value, std::stringstream& stream)
    {
        stream << "const int " << identifier << " = " << value << ";\n";
    }
    void add(const std::string& identifier, float value, std::stringstream& stream)
    {
        stream << "const float " << identifier << " = " << value << ";\n";
    }

    void addFallbackBool(const std::string& setting, std::stringstream& stream)
    {
        add(setting, Fallback::Map::getBool(setting), stream);
    }
    void addFallbackInt(const std::string& setting, std::stringstream& stream)
    {
        add(setting, Fallback::Map::getInt(setting), stream);
    }
    void addFallbackFloat(const std::string& setting, std::stringstream& stream)
    {
        add(setting, Fallback::Map::getFloat(setting), stream);
    }
}

namespace Resource
{
    ShaderSettings::ShaderSettings()
    {
        std::stringstream source;
        addFallbackBool("LightAttenuation_UseConstant", source);
        addFallbackFloat("LightAttenuation_ConstantValue", source);
        addFallbackBool("LightAttenuation_UseLinear", source);
        addFallbackInt("LightAttenuation_LinearMethod", source);
        addFallbackFloat("LightAttenuation_LinearValue", source);
        addFallbackFloat("LightAttenuation_LinearRadiusMult", source);
        addFallbackBool("LightAttenuation_UseQuadratic", source);
        addFallbackInt("LightAttenuation_QuadraticMethod", source);
        addFallbackFloat("LightAttenuation_QuadraticValue", source);
        addFallbackFloat("LightAttenuation_QuadraticRadiusMult", source);
        //      fallback=LightAttenuation_OutQuadInLin,0
        add("cClassicFalloff", Settings::Manager::getBool("classic falloff", "Shaders"), source);

        objects["light.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        add("cEnableShadows", Settings::Manager::getBool("enable shadows", "Shadows"), source);
        add("cPCSS", Settings::Manager::getBool("percentage closer soft shadows", "Shadows"), source);
        add("cShadowMapCount", Settings::Manager::getInt("number of shadow maps", "Shadows"), source);
        add("cBlockerSamples", std::clamp(Settings::Manager::getInt("blocker samples", "Shadows"), 0, 16), source);
        add("cShadowSamples", std::clamp(Settings::Manager::getInt("pcf samples", "Shadows"), 0, 16), source);
        add("cShadowMaxDist", Settings::Manager::getFloat("maximum shadow map distance", "Shadows"), source);
        add("cShadowFadeStart", Settings::Manager::getFloat("shadow fade start", "Shadows"), source);
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
            source);
        // OpenEden (task 0042): the volumetric clouds' result is drawn by the cloud dome (sky/clouds/main.frag) where
        // the dome's material says so.
        add("cVolumetricClouds",
            Render::CloudVolume::useVolumetricClouds(Settings::Manager::getBool("volumetric clouds", "Rendering"), hdr,
                Settings::Manager::getBool("atmosphere", "Rendering")),
            source);
        objects["cloud.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        add("cClampLighting", Settings::Manager::getBool("clamp lighting", "Shaders"), source);
        objects["material.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        // openeden-vsg: OpenMW's fog modes (task 0012).
        add("cRadialFog", Settings::Manager::getBool("radial fog", "Fog"), source);
        add("cExponentialFog", Settings::Manager::getBool("exponential fog", "Fog"), source);
        // OpenEden (task 0040): the sun's glow in haze (lib/view/fog.glsl). The editor and other hosts that never load
        // the game's settings see the defaults, which are off.
        const bool sunGlow = Settings::Manager::getBool("sun glow", "Rendering") && hdr
            && Settings::Manager::getBool("atmosphere", "Rendering");
        add("cSunGlow", sunGlow, source);
        add("cSunGlowDebug", sunGlow && Settings::Manager::getBool("sun glow debug", "Rendering"), source);
        // Read only when it is on: a host without the game's settings file has no such key.
        add("cSunGlowStrength", sunGlow ? Settings::Manager::getFloat("sun glow strength", "Rendering") : 0.f, source);
        // OpenEden (task 0041): the volumetric fog (lib/view/volume.glsl); only the main view has a real volume.
        const bool volumetrics = hdr && Settings::Manager::getBool("volumetrics", "Rendering");
        add("cVolumetrics", volumetrics, source);
        add("cVolumetricsDebug", volumetrics && Settings::Manager::getBool("volumetrics debug", "Rendering"), source);
        objects["fog.settings"] = vsg::ShaderModule::create(source.str());

        source.str("");
        if (Settings::Manager::getBool("debug chunks", "Terrain"))
            source << "#define DEBUG_CHUNKS 1\n";
        objects["terrain.settings"] = vsg::ShaderModule::create(source.str());
    }
}
