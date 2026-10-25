#include <components/plugin/manifest.hpp>
#include <components/plugin/settings_service.hpp>
#include <openeden/settings.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::PluginManifest;
    using openeden::plugin::PluginSetting;
    using openeden::plugin::SettingsServiceImpl;
    using openeden::sdk::SettingsService;

    PluginManifest createSampleManifest(std::string id, std::vector<PluginSetting> settings)
    {
        PluginManifest manifest;
        manifest.id = std::move(id);
        manifest.name = manifest.id;
        manifest.pluginVersion = "1.0.0";
        manifest.settings = std::move(settings);
        return manifest;
    }

    TEST(SettingsServiceTest, PerPluginNamespaceIsolation)
    {
        SettingsServiceImpl service;

        // Two plugins define the same setting key "enabled"
        service.setBool("plugin_motion", "enabled", true);
        service.setBool("plugin_graphics", "enabled", false);

        EXPECT_TRUE(service.getBool("plugin_motion", "enabled"));
        EXPECT_FALSE(service.getBool("plugin_graphics", "enabled"));

        // Setting a value in one namespace never affects another
        service.setInt("plugin_motion", "substeps", 4);
        service.setInt("plugin_graphics", "substeps", 1);

        EXPECT_EQ(service.getInt("plugin_motion", "substeps"), 4);
        EXPECT_EQ(service.getInt("plugin_graphics", "substeps"), 1);
    }

    TEST(SettingsServiceTest, ManifestSettingsDefaultsLoadedCorrectly)
    {
        SettingsServiceImpl service;

        PluginSetting sBool{ "enable_ragdoll", "bool", false, 0, 0.0, "" };
        PluginSetting sInt{ "max_bodies", "int", false, 64, 0.0, "" };
        PluginSetting sFloat{ "gravity_scale", "float", false, 0, 1.5, "" };
        PluginSetting sString{ "solver_type", "string", false, 0, 0.0, "iterative" };

        const PluginManifest manifest = createSampleManifest("plugin_physics", { sBool, sInt, sFloat, sString });

        service.loadManifestDefaults(manifest);

        EXPECT_FALSE(service.getBool("plugin_physics", "enable_ragdoll"));
        EXPECT_EQ(service.getInt("plugin_physics", "max_bodies"), 64);
        EXPECT_FLOAT_EQ(service.getFloat("plugin_physics", "gravity_scale"), 1.5f);
        EXPECT_EQ(service.getString("plugin_physics", "solver_type"), "iterative");
    }

    TEST(SettingsServiceTest, OffByDefaultVerificationForNewPlugins)
    {
        SettingsServiceImpl service;

        // Manifest where features are specified
        PluginSetting sFeature1{ "ragdoll_physics", "bool", false, 0, 0.0, "" };
        PluginSetting sFeature2{ "fluid_simulation", "bool", false, 0, 0.0, "" };

        const PluginManifest manifest = createSampleManifest("creativemotion", { sFeature1, sFeature2 });

        service.loadManifestDefaults(manifest);

        // Core rule: newly installed plugin settings must default to false/off
        EXPECT_FALSE(service.getBool("creativemotion", "ragdoll_physics"));
        EXPECT_FALSE(service.getBool("creativemotion", "fluid_simulation"));

        // Any unconfigured bool setting defaults to false
        EXPECT_FALSE(service.getBool("creativemotion", "unconfigured_flag", false));
        EXPECT_FALSE(service.getBool("unknown_plugin", "enabled", false));
    }

    TEST(SettingsServiceTest, FallbackReturnedWhenKeyNotFound)
    {
        SettingsServiceImpl service;

        EXPECT_TRUE(service.getBool("plugin_none", "missing_bool", true));
        EXPECT_FALSE(service.getBool("plugin_none", "missing_bool", false));
        EXPECT_EQ(service.getInt("plugin_none", "missing_int", 99), 99);
        EXPECT_FLOAT_EQ(service.getFloat("plugin_none", "missing_float", 3.14f), 3.14f);
        EXPECT_EQ(service.getString("plugin_none", "missing_str", "default_val"), "default_val");
    }

    TEST(SettingsServiceTest, SetOverridesManifestDefaults)
    {
        SettingsServiceImpl service;

        PluginSetting sInt{ "iterations", "int", false, 8, 0.0, "" };
        const PluginManifest manifest = createSampleManifest("solver_plugin", { sInt });

        service.loadManifestDefaults(manifest);
        EXPECT_EQ(service.getInt("solver_plugin", "iterations"), 8);

        // Explicit set overrides manifest default
        service.setInt("solver_plugin", "iterations", 16);
        EXPECT_EQ(service.getInt("solver_plugin", "iterations"), 16);
    }

    TEST(SettingsServiceTest, ResetToDefaultsRestoresManifestValues)
    {
        SettingsServiceImpl service;

        PluginSetting sBool{ "feature_x", "bool", false, 0, 0.0, "" };
        PluginSetting sFloat{ "damping", "float", false, 0, 0.85, "" };
        const PluginManifest manifest = createSampleManifest("my_plugin", { sBool, sFloat });

        service.loadManifestDefaults(manifest);

        // Mutate
        service.setBool("my_plugin", "feature_x", true);
        service.setFloat("my_plugin", "damping", 0.10f);

        EXPECT_TRUE(service.getBool("my_plugin", "feature_x"));
        EXPECT_FLOAT_EQ(service.getFloat("my_plugin", "damping"), 0.10f);

        // Reset
        service.resetToDefaults("my_plugin");

        EXPECT_FALSE(service.getBool("my_plugin", "feature_x"));
        EXPECT_FLOAT_EQ(service.getFloat("my_plugin", "damping"), 0.85f);
    }

    TEST(SettingsServiceTest, DistinctTypesPreserveValuesAccurately)
    {
        SettingsServiceImpl service;

        service.setString("plugin_data", "complex_path", "data/textures/diffuse_01.dds");
        service.setFloat("plugin_data", "scale", 2.71828f);

        EXPECT_EQ(service.getString("plugin_data", "complex_path"), "data/textures/diffuse_01.dds");
        EXPECT_FLOAT_EQ(service.getFloat("plugin_data", "scale"), 2.71828f);
    }
} // namespace
