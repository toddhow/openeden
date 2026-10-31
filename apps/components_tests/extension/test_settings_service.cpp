#include <components/extension/manifest.hpp>
#include <components/extension/settings_service.hpp>
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
    using openeden::extension::ExtensionManifest;
    using openeden::extension::ExtensionSetting;
    using openeden::extension::SettingsServiceImpl;
    using openeden::sdk::SettingsService;

    ExtensionManifest createSampleManifest(std::string id, std::vector<ExtensionSetting> settings)
    {
        ExtensionManifest manifest;
        manifest.id = std::move(id);
        manifest.name = manifest.id;
        manifest.extensionVersion = "1.0.0";
        manifest.settings = std::move(settings);
        return manifest;
    }

    TEST(SettingsServiceTest, PerExtensionNamespaceIsolation)
    {
        SettingsServiceImpl service;

        // Two extensions define the same setting key "enabled"
        service.setBool("extension_motion", "enabled", true);
        service.setBool("extension_graphics", "enabled", false);

        EXPECT_TRUE(service.getBool("extension_motion", "enabled"));
        EXPECT_FALSE(service.getBool("extension_graphics", "enabled"));

        // Setting a value in one namespace never affects another
        service.setInt("extension_motion", "substeps", 4);
        service.setInt("extension_graphics", "substeps", 1);

        EXPECT_EQ(service.getInt("extension_motion", "substeps"), 4);
        EXPECT_EQ(service.getInt("extension_graphics", "substeps"), 1);
    }

    TEST(SettingsServiceTest, ManifestSettingsDefaultsLoadedCorrectly)
    {
        SettingsServiceImpl service;

        ExtensionSetting sBool{ "enable_ragdoll", "bool", false, 0, 0.0, "" };
        ExtensionSetting sInt{ "max_bodies", "int", false, 64, 0.0, "" };
        ExtensionSetting sFloat{ "gravity_scale", "float", false, 0, 1.5, "" };
        ExtensionSetting sString{ "solver_type", "string", false, 0, 0.0, "iterative" };

        const ExtensionManifest manifest = createSampleManifest("extension_physics", { sBool, sInt, sFloat, sString });

        service.loadManifestDefaults(manifest);

        EXPECT_FALSE(service.getBool("extension_physics", "enable_ragdoll"));
        EXPECT_EQ(service.getInt("extension_physics", "max_bodies"), 64);
        EXPECT_FLOAT_EQ(service.getFloat("extension_physics", "gravity_scale"), 1.5f);
        EXPECT_EQ(service.getString("extension_physics", "solver_type"), "iterative");
    }

    TEST(SettingsServiceTest, OffByDefaultVerificationForNewExtensions)
    {
        SettingsServiceImpl service;

        // Manifest where features are specified
        ExtensionSetting sFeature1{ "ragdoll_physics", "bool", false, 0, 0.0, "" };
        ExtensionSetting sFeature2{ "fluid_simulation", "bool", false, 0, 0.0, "" };

        const ExtensionManifest manifest = createSampleManifest("creativemotion", { sFeature1, sFeature2 });

        service.loadManifestDefaults(manifest);

        // Core rule: newly installed extension settings must default to false/off
        EXPECT_FALSE(service.getBool("creativemotion", "ragdoll_physics"));
        EXPECT_FALSE(service.getBool("creativemotion", "fluid_simulation"));

        // Any unconfigured bool setting defaults to false
        EXPECT_FALSE(service.getBool("creativemotion", "unconfigured_flag", false));
        EXPECT_FALSE(service.getBool("unknown_extension", "enabled", false));
    }

    TEST(SettingsServiceTest, FallbackReturnedWhenKeyNotFound)
    {
        SettingsServiceImpl service;

        EXPECT_TRUE(service.getBool("extension_none", "missing_bool", true));
        EXPECT_FALSE(service.getBool("extension_none", "missing_bool", false));
        EXPECT_EQ(service.getInt("extension_none", "missing_int", 99), 99);
        EXPECT_FLOAT_EQ(service.getFloat("extension_none", "missing_float", 3.14f), 3.14f);
        EXPECT_EQ(service.getString("extension_none", "missing_str", "default_val"), "default_val");
    }

    TEST(SettingsServiceTest, SetOverridesManifestDefaults)
    {
        SettingsServiceImpl service;

        ExtensionSetting sInt{ "iterations", "int", false, 8, 0.0, "" };
        const ExtensionManifest manifest = createSampleManifest("solver_extension", { sInt });

        service.loadManifestDefaults(manifest);
        EXPECT_EQ(service.getInt("solver_extension", "iterations"), 8);

        // Explicit set overrides manifest default
        service.setInt("solver_extension", "iterations", 16);
        EXPECT_EQ(service.getInt("solver_extension", "iterations"), 16);
    }

    TEST(SettingsServiceTest, ResetToDefaultsRestoresManifestValues)
    {
        SettingsServiceImpl service;

        ExtensionSetting sBool{ "feature_x", "bool", false, 0, 0.0, "" };
        ExtensionSetting sFloat{ "damping", "float", false, 0, 0.85, "" };
        const ExtensionManifest manifest = createSampleManifest("my_extension", { sBool, sFloat });

        service.loadManifestDefaults(manifest);

        // Mutate
        service.setBool("my_extension", "feature_x", true);
        service.setFloat("my_extension", "damping", 0.10f);

        EXPECT_TRUE(service.getBool("my_extension", "feature_x"));
        EXPECT_FLOAT_EQ(service.getFloat("my_extension", "damping"), 0.10f);

        // Reset
        service.resetToDefaults("my_extension");

        EXPECT_FALSE(service.getBool("my_extension", "feature_x"));
        EXPECT_FLOAT_EQ(service.getFloat("my_extension", "damping"), 0.85f);
    }

    TEST(SettingsServiceTest, DistinctTypesPreserveValuesAccurately)
    {
        SettingsServiceImpl service;

        service.setString("extension_data", "complex_path", "data/textures/diffuse_01.dds");
        service.setFloat("extension_data", "scale", 2.71828f);

        EXPECT_EQ(service.getString("extension_data", "complex_path"), "data/textures/diffuse_01.dds");
        EXPECT_FLOAT_EQ(service.getFloat("extension_data", "scale"), 2.71828f);
    }

    // ---- Settings kept in Settings::Manager, as the game's settings.cfg (task 0016) --------------------------------

    constexpr std::string_view kExtensionId = "w2test";
    constexpr std::string_view kCategory = "extension.w2test";

    // Settings::Manager's maps are global, so this removes only what it may have left, under "extension.".
    class EngineSettingsTest : public ::testing::Test
    {
    protected:
        void SetUp() override { clearExtensionCategories(); }
        void TearDown() override { clearExtensionCategories(); }

        static void clearExtensionCategories()
        {
            const auto isExtension = [](const auto& entry) { return categoryOf(entry).starts_with("extension."); };
            std::erase_if(Settings::Manager::mUserSettings, isExtension);
            std::erase_if(Settings::Manager::mDefaultSettings, isExtension);
            std::erase_if(Settings::Manager::mChangedSettings, isExtension);
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

        static std::size_t countUserExtensionEntries()
        {
            return static_cast<std::size_t>(
                std::count_if(Settings::Manager::mUserSettings.begin(), Settings::Manager::mUserSettings.end(),
                    [](const auto& entry) { return categoryOf(entry).starts_with("extension."); }));
        }

        // Defaults: flag = true, count = 5, scale = 1.5, name = "standard"
        static ExtensionManifest allTypesManifest(std::string id = std::string(kExtensionId))
        {
            return createSampleManifest(std::move(id),
                { ExtensionSetting{ "flag", "bool", true, 0, 0.0, "" },
                    ExtensionSetting{ "count", "int", false, 5, 0.0, "" },
                    ExtensionSetting{ "scale", "float", false, 0, 1.5, "" },
                    ExtensionSetting{ "name", "string", false, 0, 0.0, "standard" } });
        }

        static void loadEngine(SettingsServiceImpl& service, const ExtensionManifest& manifest)
        {
            service.useEngineSettings(); // The header says: before loadManifestDefaults
            service.loadManifestDefaults(manifest);
        }

        static void expectDefaults(const SettingsServiceImpl& service)
        {
            EXPECT_TRUE(service.getBool(kExtensionId, "flag"));
            EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
            EXPECT_FLOAT_EQ(service.getFloat(kExtensionId, "scale"), 1.5f);
            EXPECT_EQ(service.getString(kExtensionId, "name"), "standard");
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
        // Acceptance: settings appear under [extension.<id>] only when changed
        const Settings::CategorySettingValueMap before = Settings::Manager::mUserSettings;

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        expectDefaults(service);
        EXPECT_FALSE(service.getBool(kExtensionId, "not_in_manifest"));

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

        EXPECT_FALSE(service.getBool(kExtensionId, "flag"));
        EXPECT_EQ(service.getInt(kExtensionId, "count"), -12);
        EXPECT_FLOAT_EQ(service.getFloat(kExtensionId, "scale"), 2.25f);
        EXPECT_EQ(service.getString(kExtensionId, "name"), "custom name");
    }

    TEST_F(EngineSettingsTest, UsersEmptyStringIsAValueForAStringSetting)
    {
        setUser("name", "");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getString(kExtensionId, "name"), "");
    }

    TEST_F(EngineSettingsTest, WrongTypedBoolFallsBackToTheDefault)
    {
        setUser("flag", "maybe");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_TRUE(service.getBool(kExtensionId, "flag"));
    }

    TEST_F(EngineSettingsTest, WrongTypedIntFallsBackToTheDefault)
    {
        // "12abc" and "1.5" start like numbers; the last does not fit an int
        for (const std::string_view value : { "abc", "", "12abc", "1.5", "99999999999999" })
        {
            setUser("count", value);

            SettingsServiceImpl service;
            loadEngine(service, allTypesManifest());

            EXPECT_EQ(service.getInt(kExtensionId, "count"), 5) << "user's value: '" << value << "'";
        }
    }

    TEST_F(EngineSettingsTest, WrongTypedFloatFallsBackToTheDefault)
    {
        for (const std::string_view value : { "abc", "", "1.5x" })
        {
            setUser("scale", value);

            SettingsServiceImpl service;
            loadEngine(service, allTypesManifest());

            EXPECT_FLOAT_EQ(service.getFloat(kExtensionId, "scale"), 1.5f) << "user's value: '" << value << "'";
        }
    }

    TEST_F(EngineSettingsTest, OneWrongValueDoesNotAffectTheOthers)
    {
        setUser("count", "abc");
        setUser("name", "kept");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
        EXPECT_EQ(service.getString(kExtensionId, "name"), "kept");
    }

    TEST_F(EngineSettingsTest, SettingANonDefaultValueWritesItSoItWouldBeSaved)
    {
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        service.setBool(kExtensionId, "flag", false);
        service.setInt(kExtensionId, "count", 77);
        service.setFloat(kExtensionId, "scale", 2.5f);
        service.setString(kExtensionId, "name", "changed");

        // settings.cfg's own spelling of a bool (Settings::Manager::setBool)
        EXPECT_EQ(getUser("flag"), "false");
        EXPECT_EQ(getUser("count"), "77");
        ASSERT_TRUE(hasUser("scale"));
        EXPECT_FLOAT_EQ(std::stof(getUser("scale")), 2.5f);
        EXPECT_EQ(getUser("name"), "changed");

        EXPECT_FALSE(service.getBool(kExtensionId, "flag"));
        EXPECT_EQ(service.getInt(kExtensionId, "count"), 77);
        EXPECT_FLOAT_EQ(service.getFloat(kExtensionId, "scale"), 2.5f);
        EXPECT_EQ(service.getString(kExtensionId, "name"), "changed");
    }

    TEST_F(EngineSettingsTest, WrittenValuesAreReadBackByTheNextSession)
    {
        {
            SettingsServiceImpl first;
            loadEngine(first, allTypesManifest());
            first.setBool(kExtensionId, "flag", false);
            first.setInt(kExtensionId, "count", 77);
            first.setFloat(kExtensionId, "scale", 0.1f);
            first.setString(kExtensionId, "name", "changed");
        }

        SettingsServiceImpl second;
        loadEngine(second, allTypesManifest());

        EXPECT_FALSE(second.getBool(kExtensionId, "flag"));
        EXPECT_EQ(second.getInt(kExtensionId, "count"), 77);
        EXPECT_FLOAT_EQ(second.getFloat(kExtensionId, "scale"), 0.1f);
        EXPECT_EQ(second.getString(kExtensionId, "name"), "changed");
    }

    TEST_F(EngineSettingsTest, SettingBackToTheDefaultRemovesTheValue)
    {
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        service.setBool(kExtensionId, "flag", false);
        service.setInt(kExtensionId, "count", 77);
        service.setFloat(kExtensionId, "scale", 2.5f);
        service.setString(kExtensionId, "name", "changed");
        ASSERT_EQ(countUserExtensionEntries(), 4u);

        service.setBool(kExtensionId, "flag", true);
        service.setInt(kExtensionId, "count", 5);
        service.setFloat(kExtensionId, "scale", 1.5f);
        service.setString(kExtensionId, "name", "standard");

        EXPECT_EQ(countUserExtensionEntries(), 0u);
        expectDefaults(service);
    }

    TEST_F(EngineSettingsTest, SettingAValueTheUserHadBackToTheDefaultRemovesIt)
    {
        setUser("count", "9");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        ASSERT_EQ(service.getInt(kExtensionId, "count"), 9);

        service.setInt(kExtensionId, "count", 5);

        EXPECT_FALSE(hasUser("count"));
        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
    }

    TEST_F(EngineSettingsTest, SettingTheFloatNearestADoubleDefaultCountsAsTheDefault)
    {
        // The manifest holds a double and the setter takes a float, and 0.85 has no exact float: the extension passing
        // 0.85f is still setting the value its manifest says.
        SettingsServiceImpl service;
        loadEngine(service,
            createSampleManifest(
                std::string(kExtensionId), { ExtensionSetting{ "damping", "float", false, 0, 0.85, "" } }));

        service.setFloat(kExtensionId, "damping", 0.5f);
        ASSERT_TRUE(hasUser("damping"));

        service.setFloat(kExtensionId, "damping", 0.85f);

        EXPECT_FALSE(hasUser("damping"));
    }

    TEST_F(EngineSettingsTest, ResetToDefaultsRemovesTheValues)
    {
        setUser("name", "from_user");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());
        service.setInt(kExtensionId, "count", 77);
        ASSERT_TRUE(hasUser("count"));
        ASSERT_TRUE(hasUser("name"));

        service.resetToDefaults(kExtensionId);

        EXPECT_EQ(countUserExtensionEntries(), 0u);
        expectDefaults(service);
    }

    TEST_F(EngineSettingsTest, ReadsComeFromTheCacheNotFromSettingsManager)
    {
        // Header: "Values are cached here, so reading one does not touch Settings::Manager"; the spec's per-frame rule
        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        setUser("count", "1234");

        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
    }

    TEST_F(EngineSettingsTest, ExtensionsDoNotSeeEachOthersValues)
    {
        const std::string_view otherCategory = "extension.w2other";
        setUser(otherCategory, "count", "100");

        SettingsServiceImpl service;
        service.useEngineSettings();
        service.loadManifestDefaults(allTypesManifest());
        service.loadManifestDefaults(allTypesManifest("w2other"));

        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
        EXPECT_EQ(service.getInt("w2other", "count"), 100);

        service.setInt(kExtensionId, "count", 7);
        EXPECT_EQ(service.getInt("w2other", "count"), 100);
        EXPECT_TRUE(hasUser("count"));
        EXPECT_EQ(Settings::Manager::mUserSettings.at(Settings::CategorySetting{ otherCategory, "count" }), "100");

        service.resetToDefaults(kExtensionId);
        EXPECT_FALSE(hasUser("count"));
        EXPECT_TRUE(hasUser(otherCategory, "count"));
        EXPECT_EQ(service.getInt("w2other", "count"), 100);
    }

    TEST_F(EngineSettingsTest, ValuesOutsideTheExtensionsCategoryAreNotRead)
    {
        // Only "extension.<id>" is the extension's, not the category of an ID that merely starts the same
        setUser("extension.w2test2", "count", "100");

        SettingsServiceImpl service;
        loadEngine(service, allTypesManifest());

        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);
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
        EXPECT_EQ(service.getInt(kExtensionId, "count"), 5);

        service.setBool(kExtensionId, "flag", false);
        service.setInt(kExtensionId, "count", 77);
        service.setFloat(kExtensionId, "scale", 2.5f);
        service.setString(kExtensionId, "name", "changed");
        EXPECT_EQ(service.getInt(kExtensionId, "count"), 77);

        service.resetToDefaults(kExtensionId);

        EXPECT_EQ(Settings::Manager::mUserSettings, userBefore);
        EXPECT_EQ(Settings::Manager::mDefaultSettings, defaultBefore);
        EXPECT_EQ(Settings::Manager::mChangedSettings, changedBefore);
    }
} // namespace
