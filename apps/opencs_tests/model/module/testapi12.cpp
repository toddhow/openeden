// Task 0091, stage a: the shapes of Editor Module API 1.2 and what a host older than 1.2 does with them. Written from
// the spec alone, before the host changed.
//
// What this pins:
//  - kApiVersion is {1, 2}.
//  - A context or registrar written for 1.0 (implementing only the pure virtual functions) still compiles, and the new
//    functions have do-nothing defaults: no selection, no setting, no task (startTask returns 0), and a registrar that
//    refuses registerSetting and subscribe with a reason that names the API version.
//  - A module that declares 1.2 is refused by a host that provides 1.1 or 1.0, with a message naming both versions; a
//    module that declares 1.0 or 1.1 loads on a 1.2 host.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
    using namespace openeden::editor;

    /// A context as a 1.0 host wrote it: only the pure virtual functions.
    class Context10 final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
    };

    class Registrar10 final : public EditorRegistrar
    {
    public:
        Registration registerWorkspace(const WorkspaceDesc&) override { return {}; }
        Registration registerPanel(const PanelDesc&) override { return {}; }
        Registration registerCommand(const CommandDesc&) override { return {}; }
        Registration registerGizmoProvider(const GizmoProviderDesc&) override { return {}; }
        Registration registerAssetType(const AssetTypeDesc&) override { return {}; }
        Registration registerInspectorSection(const InspectorSectionDesc&) override { return {}; }
        Registration registerTimelineTrackProvider(const TimelineTrackProviderDesc&) override { return {}; }
        EditorContext& context() override { return mContext; }

    private:
        Context10 mContext;
    };

    class EmptyModule final : public EditorModule
    {
    public:
        void registerUi(EditorRegistrar&) override {}
        void shutdown() override {}
    };

    EditorModule* createEmpty()
    {
        return new EmptyModule();
    }

    void destroyEmpty(EditorModule* module)
    {
        delete module;
    }

    EditorModuleDescriptor descriptorFor(openeden::sdk::Version version)
    {
        return EditorModuleDescriptor{ version, "empty", &createEmpty, &destroyEmpty };
    }
}

TEST(EditorModuleApi12, theVersionIsOneDotTwo)
{
    EXPECT_EQ(kApiVersion, (openeden::sdk::Version{ 1, 2 }));
}

TEST(EditorModuleApi12, aSelectionIsAKindAndAnIdAndEmptyWithoutAKind)
{
    const Selection none;
    EXPECT_TRUE(none.empty());
    EXPECT_EQ(none, Selection());

    const Selection joint{ "joint", "spine_01" };
    EXPECT_FALSE(joint.empty());
    EXPECT_EQ(joint, (Selection{ "joint", "spine_01" }));
    EXPECT_NE(joint, (Selection{ "joint", "spine_02" }));
    EXPECT_NE(joint, (Selection{ "region", "spine_01" }));
    EXPECT_NE(joint, none);
}

TEST(EditorModuleApi12, anEventIsPlainDataWithNothingSetByDefault)
{
    const EditorEvent event;
    EXPECT_EQ(event.type, EditorEventType::SelectionChanged);
    EXPECT_TRUE(event.subject.empty());
    EXPECT_TRUE(event.detail.empty());
    EXPECT_EQ(event.step, 0u);
}

TEST(EditorModuleApi12, aContextWrittenFor10GivesNothingForTheNewCalls)
{
    Context10 context;
    EXPECT_TRUE(context.selection().empty());
    context.select({ "joint", "x" }); // does nothing, does not throw
    EXPECT_TRUE(context.selection().empty());
    context.publish(EditorEvent());
    EXPECT_FALSE(context.setting("any.setting").has_value());

    TaskDesc task;
    task.work = [](TaskControl&) {};
    EXPECT_EQ(context.startTask(std::move(task)), 0u);
    context.cancelTask(1); // safe for an unknown id
}

TEST(EditorModuleApi12, aRegistrarWrittenFor10RefusesSettingsAndSubscriptionsAndNamesTheVersion)
{
    Registrar10 registrar;

    SettingDesc setting;
    setting.property.id = "x.setting";
    setting.defaultValue = true;
    const Registration refusedSetting = registrar.registerSetting(setting);
    EXPECT_FALSE(refusedSetting.ok);
    EXPECT_NE(refusedSetting.error.find("1.2"), std::string::npos) << refusedSetting.error;

    const Registration refusedSubscription = registrar.subscribe([](const EditorEvent&) {});
    EXPECT_FALSE(refusedSubscription.ok);
    EXPECT_NE(refusedSubscription.error.find("1.2"), std::string::npos) << refusedSubscription.error;
}

TEST(EditorModuleApi12, aModuleThatDeclaresOneTwoIsRefusedByAHostThatProvidesOneOneAndNamesBothVersions)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.setProvidedApiVersion({ 1, 1 });
    const EditorModuleDescriptor descriptor = descriptorFor(kApiVersion);
    host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed);
    EXPECT_NE(modules[0].mMessage.find("1.2"), std::string::npos) << modules[0].mMessage;
    EXPECT_NE(modules[0].mMessage.find("1.1"), std::string::npos) << modules[0].mMessage;
}

TEST(EditorModuleApi12, aModuleThatDeclaresOneTwoIsRefusedByAHostThatProvidesOneZero)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.setProvidedApiVersion({ 1, 0 });
    const EditorModuleDescriptor descriptor = descriptorFor(kApiVersion);
    host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed);
    EXPECT_NE(modules[0].mMessage.find("1.2"), std::string::npos) << modules[0].mMessage;
    EXPECT_NE(modules[0].mMessage.find("1.0"), std::string::npos) << modules[0].mMessage;
}

TEST(EditorModuleApi12, modulesThatDeclareOneZeroOrOneOneLoadOnAnOneTwoHost)
{
    for (const std::uint16_t minor : { std::uint16_t(0), std::uint16_t(1), std::uint16_t(2) })
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        const EditorModuleDescriptor descriptor = descriptorFor({ 1, minor });
        host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

        const auto modules = host.modules();
        ASSERT_EQ(modules.size(), 1u) << "minor " << minor;
        EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Loaded) << modules[0].mMessage;
    }
}
