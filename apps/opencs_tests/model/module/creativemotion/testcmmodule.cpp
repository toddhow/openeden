// Task 0032: the creativemotion_editor module as Eden Studio loads it, written from the spec alone.
//
// Assumptions (see the report):
//  - documents.hpp declares `const openeden::editor::EditorModuleDescriptor*
//  creativemotion_editor::moduleDescriptor()`,
//    the module's descriptor for ModuleHost::loadStatic (the exported C entry point would clash with the sample
//    module's in this executable). Its id is "creativemotion_editor".
//  - CREATIVEMOTION_EDITOR_PATH is a compile definition: a string literal naming the built shared library.
//  - The workspace id is "creativemotion" (title "CreativeMotion"); every other id starts with "creativemotion.".
//    The workspace id itself is accepted if it equals "creativemotion" or starts with "creativemotion.".
//  - The panel "creativemotion.panel" shows context().activeAsset()'s properties, and the gizmo provider
//    "creativemotion.gizmos" returns the active asset's gizmos.
//  - ModuleHost obtains the module's EditorContext from the context it is constructed with.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/creativemotion_editor/documents.hpp"

#include "cmtestsupport.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace CMTest
{
    namespace
    {
        using namespace openeden::editor;
        using CSMModule::ModuleHost;
        using CSMModule::ModuleInfo;
        using CSMModule::ModuleRegistry;
        using CSMModule::ModuleSource;

        bool hasModulePrefix(const std::string& id)
        {
            return id == "creativemotion" || id.rfind("creativemotion.", 0) == 0;
        }

        void expectEveryIdHasThePrefix(const ModuleRegistry& registry)
        {
            for (const auto& entry : registry.workspaces())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            for (const auto& entry : registry.panels())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            for (const auto& entry : registry.commands())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            for (const auto& entry : registry.gizmoProviders())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            for (const auto& entry : registry.sections())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
            for (const auto& entry : registry.trackProviders())
                EXPECT_TRUE(hasModulePrefix(entry.mDesc.id)) << entry.mDesc.id;
        }

        class ActiveAssetContext : public EditorContext
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
            AssetDocument* activeAsset() override { return mActive; }

            AssetDocument* mActive = nullptr;
            std::vector<std::unique_ptr<Command>> mPushed;
        };

        class CmModuleTest : public TempDirTest
        {
        protected:
            void SetUp() override
            {
                TempDirTest::SetUp();
                mHost = std::make_unique<ModuleHost>(mRegistry, mContext);
            }

            void TearDown() override
            {
                mContext.mActive = nullptr;
                mDocument.reset();
                mHost.reset();
                TempDirTest::TearDown();
            }

            void loadStatic()
            {
                const EditorModuleDescriptor* descriptor = creativemotion_editor::moduleDescriptor();
                ASSERT_NE(descriptor, nullptr);
                mHost->loadStatic("creativemotion_editor", *descriptor, ModuleSource::Origin::Core);
            }

            ModuleRegistry mRegistry;
            ActiveAssetContext mContext;
            std::unique_ptr<ModuleHost> mHost;
            std::unique_ptr<AssetDocument> mDocument;
        };

        TEST_F(CmModuleTest, theDescriptorHasTheModulesIdAndTheCurrentApiVersion)
        {
            const EditorModuleDescriptor* descriptor = creativemotion_editor::moduleDescriptor();
            ASSERT_NE(descriptor, nullptr);
            EXPECT_STREQ(descriptor->id, "creativemotion_editor");
            EXPECT_TRUE(openeden::sdk::isCompatible(descriptor->apiVersion, kApiVersion));
            EXPECT_NE(descriptor->create, nullptr);
            EXPECT_NE(descriptor->destroy, nullptr);
        }

        TEST_F(CmModuleTest, staticLoadingGoesLoadedAndRegistersTheDocumentedUi)
        {
            loadStatic();
            const auto modules = mHost->modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mId, "creativemotion_editor");
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

            // The workspace.
            ASSERT_EQ(mRegistry.workspaces().size(), 1u);
            const WorkspaceDesc& workspace = mRegistry.workspaces().front().mDesc;
            EXPECT_EQ(workspace.title, "CreativeMotion");
            EXPECT_TRUE(hasModulePrefix(workspace.id)) << workspace.id;

            // The panel.
            ASSERT_EQ(mRegistry.panels().size(), 2u);
            const PanelDesc& panel = mRegistry.panels().front().mDesc;
            EXPECT_EQ(panel.id, "creativemotion.panel");
            EXPECT_EQ(panel.workspaceId, workspace.id);
            EXPECT_EQ(panel.dock, DockArea::Right);

            // The gizmo provider.
            ASSERT_EQ(mRegistry.gizmoProviders().size(), 2u);
            EXPECT_EQ(mRegistry.gizmoProviders().front().mDesc.id, "creativemotion.gizmos");

            // The two asset types.
            ASSERT_EQ(mRegistry.assetTypes().size(), 2u);
            const auto findType = [&](const std::string& format) -> const AssetTypeDesc* {
                for (const auto& entry : mRegistry.assetTypes())
                    if (entry.mDesc.formatId == format)
                        return &entry.mDesc;
                return nullptr;
            };
            const AssetTypeDesc* rigType = findType("opas.rig");
            const AssetTypeDesc* profileType = findType("opas.profile");
            ASSERT_NE(rigType, nullptr);
            ASSERT_NE(profileType, nullptr);
            EXPECT_EQ(rigType->fileExtension, ".rig.json");
            EXPECT_EQ(profileType->fileExtension, ".profile.json");
            EXPECT_FALSE(rigType->label.empty());
            EXPECT_FALSE(profileType->label.empty());
            EXPECT_TRUE(static_cast<bool>(rigType->open));
            EXPECT_TRUE(static_cast<bool>(profileType->open));

            // Files are routed by extension.
            const auto* byRig = mRegistry.findAssetType("humanoid.rig.json");
            const auto* byProfile = mRegistry.findAssetType("humanoid-default.profile.json");
            ASSERT_NE(byRig, nullptr);
            ASSERT_NE(byProfile, nullptr);
            EXPECT_EQ(byRig->mDesc.formatId, "opas.rig");
            EXPECT_EQ(byProfile->mDesc.formatId, "opas.profile");

            // The two inspector sections, one for each format.
            ASSERT_EQ(mRegistry.sections().size(), 2u);
            std::vector<std::string> kinds;
            for (const auto& entry : mRegistry.sections())
            {
                kinds.push_back(entry.mDesc.selectionKind);
                EXPECT_FALSE(entry.mDesc.title.empty());
                EXPECT_TRUE(static_cast<bool>(entry.mDesc.create));
            }
            std::sort(kinds.begin(), kinds.end());
            EXPECT_EQ(kinds, (std::vector<std::string>{ "opas.profile", "opas.rig" }));

            // Nothing is registered under a name that does not start with the module's prefix.
            expectEveryIdHasThePrefix(mRegistry);
        }

        TEST_F(CmModuleTest, thePanelAndTheGizmoProviderFollowTheActiveAsset)
        {
            loadStatic();

            // Nothing active: no gizmos, and no failure.
            mContext.mActive = nullptr;
            EXPECT_NO_THROW(mRegistry.panelSource("creativemotion.panel"));
            EXPECT_TRUE(mRegistry.gizmosOf("creativemotion.gizmos").empty());
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);

            // A rig is active.
            std::string error;
            mDocument = creativemotion_editor::openRig(writeRigFile("active", makeLimitsRig()), error);
            ASSERT_NE(mDocument, nullptr) << error;
            mContext.mActive = mDocument.get();

            PropertySource* source = mRegistry.panelSource("creativemotion.panel");
            ASSERT_NE(source, nullptr);
            const std::vector<PropertyDesc> descs = source->describe();
            EXPECT_TRUE(std::any_of(
                descs.begin(), descs.end(), [](const PropertyDesc& d) { return d.id == "joint.Spine.twistMaxRad"; }));
            EXPECT_EQ(std::get<std::string>(source->get("rig.name")), "cm-test-rig");

            const std::vector<Gizmo*> gizmos = mRegistry.gizmosOf("creativemotion.gizmos");
            ASSERT_EQ(gizmos.size(), 2u);
            EXPECT_EQ(gizmos[0]->id(), "creativemotion.rig.shapes");
            EXPECT_EQ(gizmos[1]->id(), "creativemotion.rig.limits");

            // A profile is active: its motor properties, no gizmos.
            mContext.mActive = nullptr;
            mDocument = creativemotion_editor::openProfile(writeProfileFile("active", makeProfile()), error);
            ASSERT_NE(mDocument, nullptr) << error;
            mContext.mActive = mDocument.get();
            source = mRegistry.panelSource("creativemotion.panel");
            ASSERT_NE(source, nullptr);
            EXPECT_EQ(std::get<std::string>(source->get("profile.rig")), "cm-test-rig");
            EXPECT_TRUE(mRegistry.gizmosOf("creativemotion.gizmos").empty());
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);
        }

        TEST_F(CmModuleTest, theRegisteredAssetTypesOpenFilesAndTheSectionsShowTheDocument)
        {
            loadStatic();
            const std::filesystem::path rigPath = writeRigFile("sections", makeLimitsRig());
            const std::filesystem::path profilePath = writeProfileFile("sections", makeProfile());

            std::string error;
            mDocument = mRegistry.openAsset(rigPath, error);
            ASSERT_NE(mDocument, nullptr) << error;
            EXPECT_FALSE(mDocument->isDirty());

            // The section whose selection kind is opas.rig shows the rig's properties.
            const auto rigSection = std::find_if(mRegistry.sections().begin(), mRegistry.sections().end(),
                [](const auto& entry) { return entry.mDesc.selectionKind == "opas.rig"; });
            ASSERT_NE(rigSection, mRegistry.sections().end());
            PropertySource* rigSource = mRegistry.sectionSource(rigSection->mDesc.id, *mDocument);
            ASSERT_NE(rigSource, nullptr);
            EXPECT_EQ(std::get<std::string>(rigSource->get("rig.name")), "cm-test-rig");
            EXPECT_EQ(std::get<double>(rigSource->get("joint.LeftShin.twistMaxRad")), 2.0);

            mDocument = mRegistry.openAsset(profilePath, error);
            ASSERT_NE(mDocument, nullptr) << error;
            const auto profileSection = std::find_if(mRegistry.sections().begin(), mRegistry.sections().end(),
                [](const auto& entry) { return entry.mDesc.selectionKind == "opas.profile"; });
            ASSERT_NE(profileSection, mRegistry.sections().end());
            PropertySource* profileSource = mRegistry.sectionSource(profileSection->mDesc.id, *mDocument);
            ASSERT_NE(profileSource, nullptr);
            EXPECT_EQ(std::get<double>(profileSource->get("default.maxTorqueNm")), 500.0);

            // A broken file reports through the registry too.
            writeText(mDir / "broken.rig.json", "not json");
            error.clear();
            EXPECT_EQ(mRegistry.openAsset(mDir / "broken.rig.json", error), nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(mHost->modules()[0].mState, ModuleInfo::State::Loaded);
        }

        TEST_F(CmModuleTest, shutdownWithdrawsEverything)
        {
            loadStatic();
            ASSERT_FALSE(mRegistry.assetTypes().empty());
            mHost->shutdown();
            EXPECT_TRUE(mRegistry.workspaces().empty());
            EXPECT_TRUE(mRegistry.panels().empty());
            EXPECT_TRUE(mRegistry.commands().empty());
            EXPECT_TRUE(mRegistry.gizmoProviders().empty());
            EXPECT_TRUE(mRegistry.assetTypes().empty());
            EXPECT_TRUE(mRegistry.sections().empty());
            EXPECT_TRUE(mRegistry.trackProviders().empty());
        }

        TEST_F(CmModuleTest, theSharedLibraryLoadsAndAShutdownLeavesTheRegistryEmpty)
        {
            const std::filesystem::path library = CREATIVEMOTION_EDITOR_PATH;
            ASSERT_TRUE(std::filesystem::exists(library)) << library;

            mHost->load({ ModuleSource{ "creativemotion_editor", library, ModuleSource::Origin::Core } });
            const auto modules = mHost->modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mId, "creativemotion_editor");
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

            EXPECT_EQ(mRegistry.workspaces().size(), 1u);
            EXPECT_EQ(mRegistry.panels().size(), 2u);
            EXPECT_EQ(mRegistry.gizmoProviders().size(), 2u);
            EXPECT_EQ(mRegistry.assetTypes().size(), 2u);
            EXPECT_EQ(mRegistry.sections().size(), 2u);
            EXPECT_NE(mRegistry.findAssetType("a.rig.json"), nullptr);
            EXPECT_NE(mRegistry.findAssetType("a.profile.json"), nullptr);
            expectEveryIdHasThePrefix(mRegistry);

            // A document opened through the library works, and is released before the library goes.
            std::string error;
            mDocument = mRegistry.openAsset(writeRigFile("dynamic", makeLimitsRig()), error);
            ASSERT_NE(mDocument, nullptr) << error;
            EXPECT_EQ(mDocument->gizmos().size(), 2u);
            mDocument.reset();

            mHost->shutdown();
            EXPECT_TRUE(mRegistry.workspaces().empty());
            EXPECT_TRUE(mRegistry.panels().empty());
            EXPECT_TRUE(mRegistry.commands().empty());
            EXPECT_TRUE(mRegistry.gizmoProviders().empty());
            EXPECT_TRUE(mRegistry.assetTypes().empty());
            EXPECT_TRUE(mRegistry.sections().empty());
            EXPECT_TRUE(mRegistry.trackProviders().empty());
        }

        TEST_F(CmModuleTest, theSharedLibraryCanBeLoadedTwiceInOneProcess)
        {
            const std::filesystem::path library = CREATIVEMOTION_EDITOR_PATH;
            for (int round = 0; round < 2; ++round)
            {
                ModuleRegistry registry;
                ModuleHost host(registry, mContext);
                host.load({ ModuleSource{ "creativemotion_editor", library, ModuleSource::Origin::Core } });
                ASSERT_EQ(host.modules().size(), 1u);
                EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Loaded) << host.modules()[0].mMessage;
                host.shutdown();
                EXPECT_TRUE(registry.assetTypes().empty());
            }
        }
    }
}
