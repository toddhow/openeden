// Task 0147: the shapes of Editor Module API 1.4 (curve and waveform tracks, the playhead, dialogs) and what hosts and
// modules of other versions do with them.
//
// What this pins:
//  - kApiVersion is {1, 4}.
//  - A context written for 1.0 (only the pure virtual functions) still compiles and answers "nothing" to the 1.4 calls:
//    playhead 0, setPlayhead ignored, showDialog gives nothing.
//  - A module that declares 1.4 is refused by a host that provides 1.3 or older, with both versions named; modules that
//    declare 1.0 to 1.4 load on a 1.4 host; a module BUILT against the frozen 1.3 headers (a separate shared library)
//    loads and registers on today's host.
//  - CurveTrack and WaveformTrack ARE TimelineTracks (a host or module that only knows TimelineTrack sees a plain
//    track), and the new enum value PlayheadChanged was appended after the old ones.
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

    class OneTrack final : public CurveTrack
    {
    public:
        std::string_view id() const override { return "t"; }
        std::string_view title() const override { return "T"; }
        std::vector<TimelineKey> keys() const override { return { { 0.5, "k" } }; }
        std::vector<CurveChannelInfo> channels() const override { return {}; }
        std::vector<CurveKey> channelKeys(std::string_view) const override { return {}; }
        double durationSeconds() const override { return 1.0; }
        std::unique_ptr<Command> makeAddKeyCommand(std::string_view, const CurveKey&) override { return nullptr; }
        std::unique_ptr<Command> makeMoveKeyCommand(std::string_view, std::size_t, const CurveKey&) override
        {
            return nullptr;
        }
        std::unique_ptr<Command> makeDeleteKeyCommand(std::string_view, std::size_t) override { return nullptr; }
    };
}

TEST(EditorModuleApi14, theVersionIsOneDotFour)
{
    EXPECT_EQ(kApiVersion, (openeden::sdk::Version{ 1, 4 }));
}

TEST(EditorModuleApi14, aContextWrittenFor10AnswersNothingToTheNewCalls)
{
    Context10 context;
    EXPECT_DOUBLE_EQ(context.playheadSeconds(), 0.0);
    context.setPlayhead(3.0);
    EXPECT_DOUBLE_EQ(context.playheadSeconds(), 0.0);
    DialogDesc dialog;
    dialog.title = "t";
    EXPECT_FALSE(context.showDialog(dialog).has_value());
}

TEST(EditorModuleApi14, trackInterfacesAreTimelineTracksAndPlayheadEventIsAppended)
{
    OneTrack track;
    TimelineTrack& plain = track;
    EXPECT_EQ(plain.keys().size(), 1u);
    EXPECT_NE(dynamic_cast<CurveTrack*>(&plain), nullptr);
    EXPECT_EQ(dynamic_cast<WaveformTrack*>(&plain), nullptr);
    // Values of the existing events are unchanged; the new one follows them.
    EXPECT_EQ(static_cast<int>(EditorEventType::SelectionChanged), 0);
    EXPECT_EQ(static_cast<int>(EditorEventType::AssetOpened), 1);
    EXPECT_EQ(static_cast<int>(EditorEventType::SimulationStepped), 2);
    EXPECT_EQ(static_cast<int>(EditorEventType::ParameterChanged), 3);
    EXPECT_EQ(static_cast<int>(EditorEventType::PlayheadChanged), 4);
}

TEST(EditorModuleApi14, aModuleThatDeclaresOneFourIsRefusedByOlderHostsAndNamesBothVersions)
{
    for (const std::uint16_t hostMinor : { std::uint16_t(0), std::uint16_t(1), std::uint16_t(2), std::uint16_t(3) })
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        host.setProvidedApiVersion({ 1, hostMinor });
        const EditorModuleDescriptor descriptor = descriptorFor(kApiVersion);
        host.loadStatic("empty", descriptor, CSMModule::ModuleSource::Origin::Extension);

        const auto modules = host.modules();
        ASSERT_EQ(modules.size(), 1u) << hostMinor;
        EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed) << hostMinor;
        EXPECT_NE(modules[0].mMessage.find("1.4"), std::string::npos) << modules[0].mMessage;
        EXPECT_NE(modules[0].mMessage.find(std::string("1.") + std::to_string(hostMinor)), std::string::npos)
            << modules[0].mMessage;
    }
}

TEST(EditorModuleApi14, modulesThatDeclareAnyEarlierMinorLoadOnAnOneFourHost)
{
    for (const std::uint16_t minor :
        { std::uint16_t(0), std::uint16_t(1), std::uint16_t(2), std::uint16_t(3), std::uint16_t(4) })
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

TEST(EditorModuleApi14, aModuleBuiltAgainstTheFrozenOneThreeHeadersStillLoadsAndRegisters)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.load({ { "sample_old", OLD_API_1_3_MODULE_PATH, CSMModule::ModuleSource::Origin::Extension } });
    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Loaded) << modules[0].mMessage;
    EXPECT_EQ(registry.panels().size(), 1u);
    EXPECT_EQ(registry.gizmoProviders().size(), 1u);
}

TEST(EditorModuleApi14, aOneThreeModuleIsRefusedByAOneTwoHostAndNamesBothVersions)
{
    CSMModule::ModuleRegistry registry;
    Context10 context;
    CSMModule::ModuleHost host(registry, context);
    host.setProvidedApiVersion({ 1, 2 });
    host.load({ { "sample_old", OLD_API_1_3_MODULE_PATH, CSMModule::ModuleSource::Origin::Extension } });
    const auto modules = host.modules();
    ASSERT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed);
    EXPECT_NE(modules[0].mMessage.find("1.3"), std::string::npos) << modules[0].mMessage;
    EXPECT_NE(modules[0].mMessage.find("1.2"), std::string::npos) << modules[0].mMessage;
}
