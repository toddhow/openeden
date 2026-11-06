// Task 0091, stage e: module settings. Written from the spec alone, against a stub.
//
// Pinned (see modulesettings.hpp): stored in the section "module.<module id>" under the setting's own id; defaults
// are written when absent; a stored value out of range is clamped, unreadable or unlisted falls back to the default,
// each with one warning for that setting and the repaired value written back; set() is strict about the type, clamps,
// refuses unlisted choices and writes at once; bad descriptions are refused with a reason.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"
#include "apps/opencs/model/module/modulesettings.hpp"

#include <openeden/editor_module/module.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

namespace
{
    using namespace CSMModule;
    using namespace openeden::editor;

    SettingDesc boolSetting(const std::string& id, bool value)
    {
        SettingDesc desc;
        desc.property.id = id;
        desc.property.label = id;
        desc.property.type = PropertyType::Bool;
        desc.defaultValue = value;
        return desc;
    }

    SettingDesc intSetting(const std::string& id, std::int64_t value, double min, double max)
    {
        SettingDesc desc;
        desc.property.id = id;
        desc.property.type = PropertyType::Int;
        desc.property.min = min;
        desc.property.max = max;
        desc.defaultValue = value;
        return desc;
    }

    SettingDesc floatSetting(const std::string& id, double value, double min, double max)
    {
        SettingDesc desc;
        desc.property.id = id;
        desc.property.type = PropertyType::Float;
        desc.property.min = min;
        desc.property.max = max;
        desc.defaultValue = value;
        return desc;
    }

    SettingDesc textSetting(const std::string& id, const std::string& value)
    {
        SettingDesc desc;
        desc.property.id = id;
        desc.property.type = PropertyType::String;
        desc.defaultValue = value;
        return desc;
    }

    SettingDesc choiceSetting(const std::string& id, const std::string& value, std::vector<std::string> choices)
    {
        SettingDesc desc;
        desc.property.id = id;
        desc.property.type = PropertyType::Choice;
        desc.property.choices = std::move(choices);
        desc.defaultValue = value;
        return desc;
    }

    class ModuleSettingsTest : public ::testing::Test
    {
    protected:
        ModuleSettingsTest()
            : mSettings([this](const std::string& text) { mWarnings.push_back(text); })
        {
            mSettings.setStore(&mStore);
        }

        MemorySettingsStore mStore;
        std::vector<std::string> mWarnings;
        ModuleSettings mSettings;
    };

    TEST_F(ModuleSettingsTest, theSectionIsModuleDotId)
    {
        EXPECT_EQ(ModuleSettings::sectionOf("sample"), "module.sample");
    }

    TEST_F(ModuleSettingsTest, aDefaultIsWrittenWhenAbsentAndReadBack)
    {
        ASSERT_TRUE(mSettings.add("sample", textSetting("sample.greeting", "hello")).ok);
        EXPECT_EQ(mStore.get("module.sample", "sample.greeting"), std::optional<std::string>("hello"));
        const auto value = mSettings.get("sample.greeting");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(std::get<std::string>(*value), "hello");
        EXPECT_TRUE(mSettings.takeWritten());
        EXPECT_FALSE(mSettings.takeWritten()); // taken
        EXPECT_TRUE(mWarnings.empty());
    }

    TEST_F(ModuleSettingsTest, aStoredValueIsUsedAndNotOverwritten)
    {
        mStore.set("module.sample", "sample.count", "7");
        ASSERT_TRUE(mSettings.add("sample", intSetting("sample.count", 3, 0, 10)).ok);
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.get("sample.count")), 7);
        EXPECT_EQ(mStore.get("module.sample", "sample.count"), std::optional<std::string>("7"));
        EXPECT_FALSE(mSettings.takeWritten());
    }

    TEST_F(ModuleSettingsTest, everyTypeIsWrittenAsTextThatReadsBackTheSame)
    {
        ASSERT_TRUE(mSettings.add("m", boolSetting("m.flag", true)).ok);
        ASSERT_TRUE(mSettings.add("m", intSetting("m.int", -42, -100, 100)).ok);
        ASSERT_TRUE(mSettings.add("m", floatSetting("m.float", 0.1, -1.0, 1.0)).ok);
        ASSERT_TRUE(mSettings.add("m", choiceSetting("m.choice", "b", { "a", "b", "c" })).ok);
        EXPECT_EQ(mStore.get("module.m", "m.flag"), std::optional<std::string>("true"));
        EXPECT_EQ(mStore.get("module.m", "m.int"), std::optional<std::string>("-42"));
        EXPECT_EQ(mStore.get("module.m", "m.float"), std::optional<std::string>("0.1"));
        EXPECT_EQ(mStore.get("module.m", "m.choice"), std::optional<std::string>("b"));

        // A second registry over the same store reads the same values (a new session).
        ModuleSettings again([](const std::string&) {});
        again.setStore(&mStore);
        ASSERT_TRUE(again.add("m", boolSetting("m.flag", false)).ok);
        ASSERT_TRUE(again.add("m", floatSetting("m.float", 0.5, -1.0, 1.0)).ok);
        EXPECT_EQ(std::get<bool>(*again.get("m.flag")), true);
        EXPECT_EQ(std::get<double>(*again.get("m.float")), 0.1);
    }

    TEST_F(ModuleSettingsTest, anOutOfRangeStoredNumberIsClampedWithOneWarningAndWrittenBack)
    {
        mStore.set("module.sample", "sample.count", "99");
        ASSERT_TRUE(mSettings.add("sample", intSetting("sample.count", 3, 0, 10)).ok);
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.get("sample.count")), 10);
        EXPECT_EQ(mStore.get("module.sample", "sample.count"), std::optional<std::string>("10"));
        ASSERT_EQ(mWarnings.size(), 1u);
        EXPECT_NE(mWarnings[0].find("sample.count"), std::string::npos) << mWarnings[0];

        mStore.set("module.sample", "sample.ratio", "-5.5");
        ASSERT_TRUE(mSettings.add("sample", floatSetting("sample.ratio", 0.5, 0.0, 1.0)).ok);
        EXPECT_EQ(std::get<double>(*mSettings.get("sample.ratio")), 0.0);
        EXPECT_EQ(mWarnings.size(), 2u);
    }

    TEST_F(ModuleSettingsTest, anUnreadableOrUnlistedStoredValueFallsBackToTheDefaultWithOneWarning)
    {
        mStore.set("module.m", "m.int", "abc");
        mStore.set("module.m", "m.float", "nan");
        mStore.set("module.m", "m.flag", "maybe");
        mStore.set("module.m", "m.choice", "z");
        ASSERT_TRUE(mSettings.add("m", intSetting("m.int", 3, 0, 10)).ok);
        ASSERT_TRUE(mSettings.add("m", floatSetting("m.float", 0.25, 0.0, 1.0)).ok);
        ASSERT_TRUE(mSettings.add("m", boolSetting("m.flag", true)).ok);
        ASSERT_TRUE(mSettings.add("m", choiceSetting("m.choice", "a", { "a", "b" })).ok);
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.get("m.int")), 3);
        EXPECT_EQ(std::get<double>(*mSettings.get("m.float")), 0.25);
        EXPECT_EQ(std::get<bool>(*mSettings.get("m.flag")), true);
        EXPECT_EQ(std::get<std::string>(*mSettings.get("m.choice")), "a");
        EXPECT_EQ(mWarnings.size(), 4u);
        EXPECT_EQ(mStore.get("module.m", "m.int"), std::optional<std::string>("3"));
        EXPECT_EQ(mStore.get("module.m", "m.choice"), std::optional<std::string>("a"));
    }

    TEST_F(ModuleSettingsTest, setIsStrictAboutTheTypeClampsAndWritesAtOnce)
    {
        ASSERT_TRUE(mSettings.add("m", intSetting("m.int", 3, 0, 10)).ok);
        ASSERT_TRUE(mSettings.add("m", floatSetting("m.float", 0.5, 0.0, 1.0)).ok);
        ASSERT_TRUE(mSettings.add("m", choiceSetting("m.choice", "a", { "a", "b" })).ok);
        ASSERT_TRUE(mSettings.add("m", boolSetting("m.flag", false)).ok);
        mSettings.takeWritten();

        EXPECT_EQ(std::get<std::int64_t>(*mSettings.set("m.int", PropertyValue(std::int64_t(8)))), 8);
        EXPECT_EQ(mStore.get("module.m", "m.int"), std::optional<std::string>("8"));
        EXPECT_TRUE(mSettings.takeWritten());

        // Clamped, with a warning (once for this setting).
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.set("m.int", PropertyValue(std::int64_t(50)))), 10);
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.set("m.int", PropertyValue(std::int64_t(-50)))), 0);
        EXPECT_EQ(mWarnings.size(), 1u);
        EXPECT_EQ(std::get<double>(*mSettings.set("m.float", PropertyValue(2.0))), 1.0);

        // Wrong type or unlisted choice: nothing changes.
        EXPECT_FALSE(mSettings.set("m.int", PropertyValue(3.0)).has_value());
        EXPECT_FALSE(mSettings.set("m.float", PropertyValue(std::int64_t(1))).has_value());
        EXPECT_FALSE(mSettings.set("m.choice", PropertyValue(std::string("z"))).has_value());
        EXPECT_FALSE(mSettings.set("m.flag", PropertyValue(std::string("true"))).has_value());
        EXPECT_FALSE(mSettings.set("m.unknown", PropertyValue(true)).has_value());
        EXPECT_FALSE(mSettings.set("m.float", PropertyValue(std::numeric_limits<double>::quiet_NaN())).has_value());
        EXPECT_EQ(std::get<std::int64_t>(*mSettings.get("m.int")), 0);
        EXPECT_EQ(std::get<std::string>(*mSettings.set("m.choice", PropertyValue(std::string("b")))), "b");
        EXPECT_EQ(mStore.get("module.m", "m.choice"), std::optional<std::string>("b"));
    }

    TEST_F(ModuleSettingsTest, badDescriptionsAreRefusedWithAReason)
    {
        const auto refused = [&](const SettingDesc& desc) {
            const Registration result = mSettings.add("m", desc);
            EXPECT_FALSE(result.ok);
            EXPECT_FALSE(result.error.empty());
            return !result.ok;
        };
        EXPECT_TRUE(refused(boolSetting("", true)));
        SettingDesc vector;
        vector.property.id = "m.vec";
        vector.property.type = PropertyType::Vec3;
        vector.defaultValue = Vec3{};
        EXPECT_TRUE(refused(vector));
        EXPECT_TRUE(refused(intSetting("m.a", 99, 0, 10))); // the default is out of range
        EXPECT_TRUE(refused(floatSetting("m.b", 2.0, 0.0, 1.0)));
        EXPECT_TRUE(refused(floatSetting("m.c", std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0)));
        EXPECT_TRUE(refused(intSetting("m.d", 5, 10, 0))); // min above max
        EXPECT_TRUE(refused(choiceSetting("m.e", "z", { "a", "b" }))); // the default is not listed
        EXPECT_TRUE(refused(choiceSetting("m.f", "a", {}))); // no choices
        SettingDesc wrongType = intSetting("m.g", 1, 0, 10);
        wrongType.defaultValue = 1.5; // a double default for an Int setting
        EXPECT_TRUE(refused(wrongType));
        EXPECT_TRUE(mSettings.entries().empty());
        EXPECT_FALSE(mStore.get("module.m", "m.a").has_value()); // nothing written for a refused one
    }

    TEST_F(ModuleSettingsTest, aDuplicateIdIsRefusedEvenFromAnotherModule)
    {
        ASSERT_TRUE(mSettings.add("one", boolSetting("shared.id", true)).ok);
        const Registration second = mSettings.add("two", boolSetting("shared.id", false));
        EXPECT_FALSE(second.ok);
        EXPECT_NE(second.error.find("shared.id"), std::string::npos) << second.error;
        EXPECT_EQ(std::get<bool>(*mSettings.get("shared.id")), true);
    }

    TEST_F(ModuleSettingsTest, entriesKeepRegistrationOrderAndRemoveModuleKeepsTheStoredValues)
    {
        mSettings.add("one", boolSetting("one.a", false));
        mSettings.add("two", boolSetting("two.a", false));
        mSettings.add("one", boolSetting("one.b", false));
        ASSERT_EQ(mSettings.entries().size(), 3u);
        EXPECT_EQ(mSettings.entries()[0].mDesc.property.id, "one.a");
        EXPECT_EQ(mSettings.entries()[0].mModule, "one");
        EXPECT_EQ(mSettings.entries()[1].mDesc.property.id, "two.a");
        EXPECT_EQ(mSettings.entries()[2].mDesc.property.id, "one.b");

        mSettings.set("one.a", PropertyValue(true));
        mSettings.removeModule("one");
        ASSERT_EQ(mSettings.entries().size(), 1u);
        EXPECT_EQ(mSettings.entries()[0].mModule, "two");
        EXPECT_FALSE(mSettings.get("one.a").has_value());
        // The value stays in the store and is read when the module comes back.
        EXPECT_EQ(mStore.get("module.one", "one.a"), std::optional<std::string>("true"));
        mSettings.add("one", boolSetting("one.a", false));
        EXPECT_EQ(std::get<bool>(*mSettings.get("one.a")), true);
    }

    TEST_F(ModuleSettingsTest, theDefaultStoreIsInMemoryWithoutSetStore)
    {
        ModuleSettings alone;
        ASSERT_TRUE(alone.add("m", boolSetting("m.flag", true)).ok);
        EXPECT_EQ(std::get<bool>(*alone.get("m.flag")), true);
        alone.set("m.flag", PropertyValue(false));
        EXPECT_EQ(std::get<bool>(*alone.get("m.flag")), false);
    }

    // ---- through the registrar -----------------------------------------------------------------------

    class NullContext final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
    };

    std::vector<Registration> sResults;

    class SettingsModule final : public EditorModule
    {
    public:
        void registerUi(EditorRegistrar& registrar) override
        {
            sResults.clear();
            sResults.push_back(registrar.registerSetting(boolSetting("reg.flag", true)));
            sResults.push_back(registrar.registerSetting(intSetting("reg.count", 3, 0, 10)));
            sResults.push_back(registrar.registerSetting(boolSetting("reg.flag", false))); // duplicate
            CommandDesc command;
            command.id = "reg.command";
            command.label = "c";
            command.handler = [](EditorContext&) {};
            registrar.registerCommand(command);
        }
        void shutdown() override {}
    };

    EditorModule* createSettingsModule()
    {
        return new SettingsModule();
    }

    void destroySettingsModule(EditorModule* module)
    {
        delete module;
    }

    TEST(CSMModuleSettingsRegistry, aModuleRegistersSettingsThroughTheRegistrarAndTheyGoWithTheModule)
    {
        ModuleRegistry registry;
        MemorySettingsStore store;
        registry.settings().setStore(&store);
        NullContext context;
        {
            ModuleHost host(registry, context);
            const EditorModuleDescriptor descriptor{ kApiVersion, "reg", &createSettingsModule,
                &destroySettingsModule };
            host.loadStatic("reg", descriptor, ModuleSource::Origin::Extension);
            ASSERT_EQ(host.modules().at(0).mState, ModuleInfo::State::Loaded);
            ASSERT_EQ(sResults.size(), 3u);
            EXPECT_TRUE(sResults[0].ok);
            EXPECT_TRUE(sResults[1].ok);
            EXPECT_FALSE(sResults[2].ok);
            EXPECT_FALSE(sResults[2].error.empty());

            ASSERT_EQ(registry.settings().entries().size(), 2u);
            EXPECT_EQ(registry.settings().entries()[0].mModule, "reg");
            EXPECT_EQ(store.get("module.reg", "reg.count"), std::optional<std::string>("3"));
        }
        // Shut down: the entries are gone, the file keeps the values.
        EXPECT_TRUE(registry.settings().entries().empty());
        EXPECT_EQ(store.get("module.reg", "reg.flag"), std::optional<std::string>("true"));
    }

    TEST(CSMModuleSettingsRegistry, aSettingOutsideRegisterUiIsRefused)
    {
        ModuleRegistry registry;
        const Registration result = registry.registerSetting(boolSetting("x.flag", true));
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.error.empty());
    }
}
