// Task 0091, stage a: modules BUILT against the frozen 1.0 and 1.1 headers (apps/opencs_tests/oldapi) still load and
// run on today's host. Each is a real shared library, loaded by path, so that its vtables and structs are the old
// ones; a change of a module-implemented interface or a struct that crosses the boundary would show here.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using namespace openeden::editor;

    class Context10 final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command> command) override
        {
            command->redo();
            mPushed.push_back(std::move(command));
        }
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }

        std::vector<std::unique_ptr<Command>> mPushed;
    };

    class CountingSink final : public OverlaySink
    {
    public:
        void line(const Vec3&, const Vec3&, const Colour&) override { ++mLines; }
        void point(const Vec3&, const Colour&, float) override { ++mPoints; }
        void label(const Vec3&, std::string_view, const Colour&) override { ++mLabels; }
        int mLines = 0;
        int mPoints = 0;
        int mLabels = 0;
    };

    struct OldModule
    {
        const char* mName;
        const char* mPath;
        std::uint16_t mMinor;
    };

    const OldModule sOldModules[] = {
        { "api1_0", OLD_API_1_0_MODULE_PATH, 0 },
        { "api1_1", OLD_API_1_1_MODULE_PATH, 1 },
    };

    class OldApiModuleTest : public ::testing::TestWithParam<OldModule>
    {
    protected:
        void SetUp() override
        {
            mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-oldapi-") + GetParam().mName);
            std::filesystem::remove_all(mDir);
            std::filesystem::create_directories(mDir);
        }
        void TearDown() override
        {
            mDocument.reset();
            std::filesystem::remove_all(mDir);
        }

        std::filesystem::path writeNote()
        {
            const std::filesystem::path path = mDir / "one.note.json";
            std::ofstream(path) << "{\"format\":\"sample.note\",\"title\":\"Old\",\"weight\":2.5}";
            return path;
        }

        std::filesystem::path mDir;
        std::unique_ptr<AssetDocument> mDocument;
    };

    TEST_P(OldApiModuleTest, loadsOnTheCurrentHostAndRegistersEverything)
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        {
            CSMModule::ModuleHost host(registry, context);
            host.load({ { "sample_old", GetParam().mPath, CSMModule::ModuleSource::Origin::Extension } });
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 1u);
            ASSERT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Loaded) << modules[0].mMessage;
            EXPECT_EQ(registry.panels().size(), 1u);
            EXPECT_EQ(registry.assetTypes().size(), 1u);
            EXPECT_EQ(registry.gizmoProviders().size(), 1u);
            EXPECT_EQ(registry.trackProviders().size(), 1u);
        }
        EXPECT_TRUE(registry.panels().empty());
    }

    TEST_P(OldApiModuleTest, itsDocumentGizmoPropertiesAndTracksWorkThroughTheOldVtables)
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        host.load({ { "sample_old", GetParam().mPath, CSMModule::ModuleSource::Origin::Extension } });

        const std::filesystem::path path = writeNote();
        std::string error;
        mDocument = registry.openAsset(path, error);
        ASSERT_NE(mDocument, nullptr) << error;

        EXPECT_EQ(std::get<std::string>(mDocument->properties().get("title")), "Old");
        auto command = mDocument->properties().makeSetCommand("weight", 4.0);
        ASSERT_NE(command, nullptr);
        command->redo();
        EXPECT_DOUBLE_EQ(std::get<double>(mDocument->properties().get("weight")), 4.0);
        EXPECT_TRUE(mDocument->isDirty());
        command->undo();
        EXPECT_DOUBLE_EQ(std::get<double>(mDocument->properties().get("weight")), 2.5);

        const std::vector<Gizmo*> gizmos = mDocument->gizmos();
        ASSERT_EQ(gizmos.size(), 1u);
        CountingSink sink;
        gizmos[0]->draw(sink);
        EXPECT_EQ(sink.mLines, 3);
        EXPECT_EQ(sink.mPoints, 1);
        const std::vector<GizmoHandle> handles = gizmos[0]->handles();
        ASSERT_EQ(handles.size(), 1u);
        EXPECT_EQ(handles[0].id, "sample.handle");
        auto move = gizmos[0]->drag("sample.handle", { 1, 2, 3 });
        ASSERT_NE(move, nullptr);
        move->redo();
        EXPECT_DOUBLE_EQ(gizmos[0]->handles()[0].position.z, 3.0);

        const std::vector<TimelineTrack*> tracks = mDocument->tracks();
        ASSERT_EQ(tracks.size(), 1u);
        EXPECT_EQ(tracks[0]->keys().size(), 2u);
        // The document has code in the module's library: it goes before the host unloads it.
        mDocument.reset();
    }

    TEST_P(OldApiModuleTest, aHostThatProvidesOnlyTheModulesOwnMinorStillLoadsIt)
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        host.setProvidedApiVersion({ 1, GetParam().mMinor });
        host.load({ { "sample_old", GetParam().mPath, CSMModule::ModuleSource::Origin::Extension } });
        ASSERT_EQ(host.modules().size(), 1u);
        EXPECT_EQ(host.modules()[0].mState, CSMModule::ModuleInfo::State::Loaded) << host.modules()[0].mMessage;
    }

    INSTANTIATE_TEST_SUITE_P(OldApis, OldApiModuleTest, ::testing::ValuesIn(sOldModules),
        [](const ::testing::TestParamInfo<OldModule>& entry) { return std::string(entry.param.mName); });

    TEST(OldApiModuleHostTest, aOneOneModuleIsRefusedByAOneZeroHostAndNamesBothVersions)
    {
        CSMModule::ModuleRegistry registry;
        Context10 context;
        CSMModule::ModuleHost host(registry, context);
        host.setProvidedApiVersion({ 1, 0 });
        host.load({ { "sample_old", OLD_API_1_1_MODULE_PATH, CSMModule::ModuleSource::Origin::Extension } });
        ASSERT_EQ(host.modules().size(), 1u);
        const CSMModule::ModuleInfo info = host.modules()[0];
        EXPECT_EQ(info.mState, CSMModule::ModuleInfo::State::Failed);
        EXPECT_NE(info.mMessage.find("1.1"), std::string::npos) << info.mMessage;
        EXPECT_NE(info.mMessage.find("1.0"), std::string::npos) << info.mMessage;
    }
}
