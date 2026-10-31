#include <components/plugin/manifest.hpp>
#include <components/plugin/settings_service.hpp>
#include <components/settings/settings.hpp>
#include <openeden/settings.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

    // ---- Settings kept in Settings::Manager, as the game's settings.cfg (task 0016) --------------------------------

    constexpr std::string_view kPluginId = "w2test";
    constexpr std::string_view kCategory = "plugin.w2test";

    // Settings::Manager's maps are global, so this removes only what it may have left, under "plugin.".
    class EngineSettingsTest : public ::testing::Test
    {
    protected:
        void SetUp() override { clearPluginCategories(); }
        void TearDown() override { clearPluginCategories(); }

        static void clearPluginCategories()
        {
            const auto isPlugin = [](const auto& entry) { return categoryOf(entry).starts_with("plugin."); };
            std::erase_if(Settings::Manager::mUserSettings, isPlugin);
            std::erase_if(Settings::Manager::mDefaultSettings, isPlugin);
            std::erase_if(Settings::Manager::mChangedSettings, isPlugin);
        }

        static std::string_view categoryOf(const Settings::CategorySetting& entry) { return entry.first; }

        static std::string_view categoryOf(const Settings::CategorySettingValueMap::value_type& entry)
        {
            return entry.first.first;
        }

        static void setUser(std::string_view category, std::string_view key, std::string_view value)
        {
            Settings::Manager::mUserSettings[Settings::CategorySetting{ category, key }] = value;
        }

        static void setUser(std::string_view key, std::string_view value) { setUser(kCategory, key, value); }

        static bool hasUser(std::string_view category, std::string_view key)
        {
            return Settings::Manager::mUserSettings.contains(std::make_pair(category, key));
        }

        static bool hasUser(std::string_view key) { return hasUser(kCategory, key); }

        static std::string getUser(std::string_view key)
        {
            const auto found = Settings::Manager::mUserSettings.find(std::make_pair(kCategory, key));
            return found == Settings::Manager::mUserSettings.end() ? std::string() : found->second;
        }

        static std::size_t countUserPluginEntries()
        {
            return static_cast<std::size_t>(std::count_if(Settings::Manager::mUserSettings.begin(),
                Settings::Manager::mUserSettings.end(),
                [](const auto& entry) { return categoryOf(entry).starts_with("plugin."); }));
        }

        // Defaults: flag = true, count = 5, scale = 1.5, name = "standard"
        static PluginManifest allTypesManifest(std::string id = std::string(kPluginId))
        {
            return createSampleManifest(std::move(id),
                { PluginSetting{ "flag", "bool", true, 0, 0.0, "" }, PluginSetting{ "count", "int", false, 5, 0.0, "" },
                    PluginSetting{ "scale", "float", false, 0, 1.5, "" },
                    PluginSetting{ "name", "string", false, 0, 0.0, "standard" } });
        }

        static void loadEngine(SettingsServiceImpl& service, const PluginManifest& manifest)
        {
            service.useEngineSettings(); // The header says: before loadManifestDefaults
            service.loadManifestDefaults(manifest);
        }

        static void expectDefaults(const SettingsServiceImpl& service)
        {
            EXPECT_TRUE(service.getBool(kPluginId, "flag"));
            EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
            EXPECT_FLOAT_EQ(service.getFloat(kPluginId, "scale"), 1.5f);
            EXPECT_EQ(service.getString(kPluginId, "name"), "standard");
        }
    };

    TEST_F(EngineSettingsTest, ManifestDefaultsApplyWhenTheUserHasNoValue)
    {
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        expectDefaults(service);
    }

    TEST_F(EngineSettingsTest, NothingIsWrittenWhenValuesAreNeverChanged)
    {
        // Acceptance: settings appear under [plugin.<id>] only when changed
        const Settings::CategorySettingValueMap before = Settings::Manager::mUserSettings;

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        expectDefaults(service);
        EXPECT_FALSE(service.getBool(kPluginId, "not_in_manifest"));

        EXPECT_EQ(Settings::Manager::mUserSettings, before);
    }

    TEST_F(EngineSettingsTest, UsersValueOfEachTypeIsApplied)
    {
        setUser("flag", "false");
        setUser("count", "-12");
        setUser("scale", "2.25");
        setUser("name", "custom name");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_FALSE(service.getBool(kPluginId, "flag"));
        EXPECT_EQ(service.getInt(kPluginId, "count"), -12);
        EXPECT_FLOAT_EQ(service.getFloat(kPluginId, "scale"), 2.25f);
        EXPECT_EQ(service.getString(kPluginId, "name"), "custom name");
    }

    TEST_F(EngineSettingsTest, UsersEmptyStringIsAValueForAStringSetting)
    {
        setUser("name", "");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getString(kPluginId, "name"), "");
    }

    TEST_F(EngineSettingsTest, WrongTypedBoolFallsBackToTheDefault)
    {
        setUser("flag", "maybe");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_TRUE(service.getBool(kPluginId, "flag"));
    }

    TEST_F(EngineSettingsTest, WrongTypedIntFallsBackToTheDefault)
    {
        // "12abc" and "1.5" start like numbers; the last does not fit an int
        for (const std::string_view value : { "abc", "", "12abc", "1.5", "99999999999999" })
        {
            setUser("count", value);

            SettingsServiceImpl service;
            loadEngine(service, allTypesManifest());

            EXPECT_EQ(service.getInt(kPluginId, "count"), 5) << "user's value: '" << value << "'";
        }
    }

    TEST_F(EngineSettingsTest, WrongTypedFloatFallsBackToTheDefault)
    {
        for (const std::string_view value : { "abc", "", "1.5x" })
        {
            setUser("scale", value);

            SettingsServiceImpl service;
            loadEngine(service, allTypesManifest());

            EXPECT_FLOAT_EQ(service.getFloat(kPluginId, "scale"), 1.5f) << "user's value: '" << value << "'";
        }
    }

    TEST_F(EngineSettingsTest, OneWrongValueDoesNotAffectTheOthers)
    {
        setUser("count", "abc");
        setUser("name", "kept");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
        EXPECT_EQ(service.getString(kPluginId, "name"), "kept");
    }

    TEST_F(EngineSettingsTest, SettingANonDefaultValueWritesItSoItWouldBeSaved)
    {
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        service.setBool(kPluginId, "flag", false);
        service.setInt(kPluginId, "count", 77);
        service.setFloat(kPluginId, "scale", 2.5f);
        service.setString(kPluginId, "name", "changed");

        // settings.cfg's own spelling of a bool (Settings::Manager::setBool)
        EXPECT_EQ(getUser("flag"), "false");
        EXPECT_EQ(getUser("count"), "77");
        ASSERT_TRUE(hasUser("scale"));
        EXPECT_FLOAT_EQ(std::stof(getUser("scale")), 2.5f);
        EXPECT_EQ(getUser("name"), "changed");

        EXPECT_FALSE(service.getBool(kPluginId, "flag"));
        EXPECT_EQ(service.getInt(kPluginId, "count"), 77);
        EXPECT_FLOAT_EQ(service.getFloat(kPluginId, "scale"), 2.5f);
        EXPECT_EQ(service.getString(kPluginId, "name"), "changed");
    }

    TEST_F(EngineSettingsTest, WrittenValuesAreReadBackByTheNextSession)
    {
        {
            SettingsServiceImpl first;
            loadEngine(first, allTypesManifest());
            first.setBool(kPluginId, "flag", false);
            first.setInt(kPluginId, "count", 77);
            first.setFloat(kPluginId, "scale", 0.1f);
            first.setString(kPluginId, "name", "changed");
        }

        SettingsServiceImpl second;
        loadEngine(second, allTypesManifest());

        EXPECT_FALSE(second.getBool(kPluginId, "flag"));
        EXPECT_EQ(second.getInt(kPluginId, "count"), 77);
        EXPECT_FLOAT_EQ(second.getFloat(kPluginId, "scale"), 0.1f);
        EXPECT_EQ(second.getString(kPluginId, "name"), "changed");
    }

    TEST_F(EngineSettingsTest, SettingBackToTheDefaultRemovesTheValue)
    {
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        service.setBool(kPluginId, "flag", false);
        service.setInt(kPluginId, "count", 77);
        service.setFloat(kPluginId, "scale", 2.5f);
        service.setString(kPluginId, "name", "changed");
        ASSERT_EQ(countUserPluginEntries(), 4u);

        service.setBool(kPluginId, "flag", true);
        service.setInt(kPluginId, "count", 5);
        service.setFloat(kPluginId, "scale", 1.5f);
        service.setString(kPluginId, "name", "standard");

        EXPECT_EQ(countUserPluginEntries(), 0u);
        expectDefaults(service);
    }

    TEST_F(EngineSettingsTest, SettingAValueTheUserHadBackToTheDefaultRemovesIt)
    {
        setUser("count", "9");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        ASSERT_EQ(service.getInt(kPluginId, "count"), 9);

        service.setInt(kPluginId, "count", 5);

        EXPECT_FALSE(hasUser("count"));
        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
    }

    TEST_F(EngineSettingsTest, SettingTheFloatNearestADoubleDefaultCountsAsTheDefault)
    {
        // The manifest holds a double and the setter takes a float, and 0.85 has no exact float: the plugin passing
        // 0.85f is still setting the value its manifest says.
        SettingsServiceImpl service;
        loadEngine(service,
            createSampleManifest(std::string(kPluginId), { PluginSetting{ "damping", "float", false, 0, 0.85, "" } }));

        service.setFloat(kPluginId, "damping", 0.5f);
        ASSERT_TRUE(hasUser("damping"));

        service.setFloat(kPluginId, "damping", 0.85f);

        EXPECT_FALSE(hasUser("damping"));
    }

    TEST_F(EngineSettingsTest, ResetToDefaultsRemovesTheValues)
    {
        setUser("name", "from_user");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        service.setInt(kPluginId, "count", 77);
        ASSERT_TRUE(hasUser("count"));
        ASSERT_TRUE(hasUser("name"));

        service.resetToDefaults(kPluginId);

        EXPECT_EQ(countUserPluginEntries(), 0u);
        expectDefaults(service);
    }

    TEST_F(EngineSettingsTest, ReadsComeFromTheCacheNotFromSettingsManager)
    {
        // Header: "Values are cached here, so reading one does not touch Settings::Manager"; the spec's per-frame rule
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        setUser("count", "1234");

        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
    }

    TEST_F(EngineSettingsTest, PluginsDoNotSeeEachOthersValues)
    {
        const std::string_view otherCategory = "plugin.w2other";
        setUser(otherCategory, "count", "100");

        SettingsServiceImpl service;
        service.useEngineSettings();
        service.loadManifestDefaults(allTypesManifest());
        service.loadManifestDefaults(allTypesManifest("w2other"));

        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
        EXPECT_EQ(service.getInt("w2other", "count"), 100);

        service.setInt(kPluginId, "count", 7);
        EXPECT_EQ(service.getInt("w2other", "count"), 100);
        EXPECT_TRUE(hasUser("count"));
        EXPECT_EQ(Settings::Manager::mUserSettings.at(Settings::CategorySetting{ otherCategory, "count" }), "100");

        service.resetToDefaults(kPluginId);
        EXPECT_FALSE(hasUser("count"));
        EXPECT_TRUE(hasUser(otherCategory, "count"));
        EXPECT_EQ(service.getInt("w2other", "count"), 100);
    }

    TEST_F(EngineSettingsTest, ValuesOutsideThePluginsCategoryAreNotRead)
    {
        // Only "plugin.<id>" is the plugin's, not the category of an ID that merely starts the same
        setUser("plugin.w2test2", "count", "100");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);
    }

    TEST_F(EngineSettingsTest, WithoutUseEngineSettingsNothingTouchesSettingsManager)
    {
        setUser("count", "100");
        const Settings::CategorySettingValueMap userBefore = Settings::Manager::mUserSettings;
        const Settings::CategorySettingValueMap defaultBefore = Settings::Manager::mDefaultSettings;
        const Settings::CategorySettingVector changedBefore = Settings::Manager::mChangedSettings;

        SettingsServiceImpl service;
        service.loadManifestDefaults(allTypesManifest());

        // The user's value is not read
        EXPECT_EQ(service.getInt(kPluginId, "count"), 5);

        service.setBool(kPluginId, "flag", false);
        service.setInt(kPluginId, "count", 77);
        service.setFloat(kPluginId, "scale", 2.5f);
        service.setString(kPluginId, "name", "changed");
        EXPECT_EQ(service.getInt(kPluginId, "count"), 77);

        service.resetToDefaults(kPluginId);

        EXPECT_EQ(Settings::Manager::mUserSettings, userBefore);
        EXPECT_EQ(Settings::Manager::mDefaultSettings, defaultBefore);
        EXPECT_EQ(Settings::Manager::mChangedSettings, changedBefore);
    }
} // namespace
