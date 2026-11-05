// Per-module document filtering (task 0031 follow-up, toddhow 2026-11-04): a module's panel shows only that module's
// documents, and the Asset Browser can list one module's assets only. Written from the request alone, before the code.
//
// Assumptions where the request left room:
//  - The owner of a document is the module that registered the asset type that opened it; ModuleRegistry::assetOwner
//    (path) gives its id (empty when no type handles the file).
//  - ModuleRegistry::appliesTo(registeredBy, activeOwner): true when no document is open (activeOwner empty) or the
//    same module registered the thing; false for another module's.
//  - panelSource(panelId, activeOwner) is null while another module's document is the active one, and then does not
//    call the panel's callback; with the owner empty or equal it is panelSource(panelId).
//  - findAssets(roots, ownerFilter): the files a registered type handles, sorted, once each; with a filter only the
//    files the filter's module registered a type for. The filter is a module id; an unknown one lists nothing.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "advancedbodies/abtestsupport.hpp"
#include "editor_modules/advancedbodies_editor/documents.hpp"
#include "editor_modules/creativemotion_editor/documents.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        class NoteProperties final : public PropertySource
        {
        public:
            std::vector<PropertyDesc> describe() const override { return {}; }
            PropertyValue get(std::string_view) const override { return std::string(); }
            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }
        };

        class NullContext final : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view) override {}
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return mActive; }
            AssetDocument* mActive = nullptr;
        };

        // Two scripted modules "alpha" and "beta", each with one asset type, one panel and a count of panel calls.
        struct Calls
        {
            int mAlphaPanel = 0;
            int mBetaPanel = 0;
        };

        Calls sCalls;
        NoteProperties sNotes;

        class AlphaModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                registrar.registerWorkspace({ "alpha", "Alpha" });
                PanelDesc panel;
                panel.id = "alpha.panel";
                panel.title = "Alpha";
                panel.workspaceId = "alpha";
                panel.source = []() -> PropertySource* {
                    ++sCalls.mAlphaPanel;
                    return &sNotes;
                };
                registrar.registerPanel(panel);
                AssetTypeDesc type;
                type.formatId = "alpha.note";
                type.fileExtension = ".alpha.json";
                type.label = "Alpha note";
                type.open = [](const std::filesystem::path&, std::string& error) -> std::unique_ptr<AssetDocument> {
                    error = "not needed";
                    return nullptr;
                };
                registrar.registerAssetType(type);
            }
            void shutdown() override {}
        };

        class BetaModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                registrar.registerWorkspace({ "beta", "Beta" });
                PanelDesc panel;
                panel.id = "beta.panel";
                panel.title = "Beta";
                panel.workspaceId = "beta";
                panel.source = []() -> PropertySource* {
                    ++sCalls.mBetaPanel;
                    return &sNotes;
                };
                registrar.registerPanel(panel);
                AssetTypeDesc type;
                type.formatId = "beta.note";
                type.fileExtension = ".beta.json";
                type.label = "Beta note";
                type.open = [](const std::filesystem::path&, std::string& error) -> std::unique_ptr<AssetDocument> {
                    error = "not needed";
                    return nullptr;
                };
                registrar.registerAssetType(type);
            }
            void shutdown() override {}
        };

        EditorModule* createAlpha()
        {
            return new AlphaModule();
        }
        EditorModule* createBeta()
        {
            return new BetaModule();
        }
        void destroyModule(EditorModule* module)
        {
            delete module;
        }

        class ModuleOwnerTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                sCalls = Calls();
                mRoot = std::filesystem::temp_directory_path() / "csm-owner-test";
                std::error_code ignored;
                std::filesystem::remove_all(mRoot, ignored);
                std::filesystem::create_directories(mRoot / "sub");
                touch(mRoot / "b.alpha.json");
                touch(mRoot / "a.beta.json");
                touch(mRoot / "sub" / "c.alpha.json");
                touch(mRoot / "readme.txt");
                mHost = std::make_unique<ModuleHost>(mRegistry, mContext);
                mAlpha = { {}, "alpha", &createAlpha, &destroyModule };
                mBeta = { {}, "beta", &createBeta, &destroyModule };
                mAlpha.apiVersion = kApiVersion;
                mBeta.apiVersion = kApiVersion;
                mHost->loadStatic("alpha", mAlpha, ModuleSource::Origin::Core);
                mHost->loadStatic("beta", mBeta, ModuleSource::Origin::Core);
            }

            void TearDown() override
            {
                mHost.reset();
                std::error_code ignored;
                std::filesystem::remove_all(mRoot, ignored);
            }

            static void touch(const std::filesystem::path& path) { std::ofstream(path) << "{}"; }

            std::filesystem::path mRoot;
            ModuleRegistry mRegistry;
            NullContext mContext;
            EditorModuleDescriptor mAlpha{};
            EditorModuleDescriptor mBeta{};
            std::unique_ptr<ModuleHost> mHost;
        };

        TEST_F(ModuleOwnerTest, aFilesOwnerIsTheModuleOfItsAssetType)
        {
            EXPECT_EQ(mRegistry.assetOwner(mRoot / "b.alpha.json"), "alpha");
            EXPECT_EQ(mRegistry.assetOwner(mRoot / "a.beta.json"), "beta");
            EXPECT_EQ(mRegistry.assetOwner(mRoot / "readme.txt"), "");
        }

        TEST_F(ModuleOwnerTest, aPanelsOwnerIsItsModule)
        {
            EXPECT_EQ(mRegistry.panelOwner("alpha.panel"), "alpha");
            EXPECT_EQ(mRegistry.panelOwner("beta.panel"), "beta");
            EXPECT_EQ(mRegistry.panelOwner("nothing"), "");
        }

        TEST(ModuleOwnerRules, appliesToIsTrueWithNoDocumentOrTheSameModule)
        {
            EXPECT_TRUE(ModuleRegistry::appliesTo("alpha", ""));
            EXPECT_TRUE(ModuleRegistry::appliesTo("alpha", "alpha"));
            EXPECT_FALSE(ModuleRegistry::appliesTo("alpha", "beta"));
        }

        TEST_F(ModuleOwnerTest, aPanelIsEmptyWhileAnotherModulesDocumentIsActiveAndItsCallbackIsNotCalled)
        {
            EXPECT_NE(mRegistry.panelSource("alpha.panel", "alpha"), nullptr);
            EXPECT_EQ(sCalls.mAlphaPanel, 1);
            EXPECT_EQ(mRegistry.panelSource("alpha.panel", "beta"), nullptr);
            EXPECT_EQ(sCalls.mAlphaPanel, 1) << "no call for another module's document";
            EXPECT_EQ(mRegistry.panelSource("beta.panel", "beta") != nullptr, true);
            EXPECT_EQ(mRegistry.panelSource("beta.panel", "alpha"), nullptr);
            EXPECT_EQ(sCalls.mBetaPanel, 1);
            // No document open: the panel is asked, as before.
            EXPECT_NE(mRegistry.panelSource("alpha.panel", ""), nullptr);
            EXPECT_EQ(sCalls.mAlphaPanel, 2);
            // The old one-argument call is unchanged.
            EXPECT_NE(mRegistry.panelSource("alpha.panel"), nullptr);
            EXPECT_EQ(sCalls.mAlphaPanel, 3);
        }

        TEST_F(ModuleOwnerTest, findAssetsListsEverythingSortedOnceWithoutAFilter)
        {
            const std::vector<std::filesystem::path> found = mRegistry.findAssets({ mRoot, mRoot / "sub" });
            ASSERT_EQ(found.size(), 3u);
            EXPECT_EQ(found[0].filename(), "a.beta.json");
            EXPECT_EQ(found[1].filename(), "b.alpha.json");
            EXPECT_EQ(found[2].filename(), "c.alpha.json");
        }

        TEST_F(ModuleOwnerTest, findAssetsWithAnOwnerFilterListsOnlyThatModulesFiles)
        {
            const std::vector<std::filesystem::path> alpha = mRegistry.findAssets({ mRoot }, "alpha");
            ASSERT_EQ(alpha.size(), 2u);
            EXPECT_EQ(alpha[0].filename(), "b.alpha.json");
            EXPECT_EQ(alpha[1].filename(), "c.alpha.json");
            const std::vector<std::filesystem::path> beta = mRegistry.findAssets({ mRoot }, "beta");
            ASSERT_EQ(beta.size(), 1u);
            EXPECT_EQ(beta[0].filename(), "a.beta.json");
            EXPECT_TRUE(mRegistry.findAssets({ mRoot }, "gamma").empty());
            EXPECT_TRUE(mRegistry.findAssets({ mRoot / "missing" }).empty());
        }

        // ---- the two real modules ---------------------------------------------------------------------------------

        TEST(ModuleOwnerRealModules, creativeMotionsPanelShowsNothingForABodyDocumentAndTheBodyPanelOnlyItsOwn)
        {
            ModuleRegistry registry;
            NullContext context;
            registry.setContext(&context);
            ModuleHost host(registry, context);
            host.loadStatic(
                "creativemotion_editor", *creativemotion_editor::moduleDescriptor(), ModuleSource::Origin::Core);
            host.loadStatic(
                "advancedbodies_editor", *advancedbodies_editor::moduleDescriptor(), ModuleSource::Origin::Core);

            // A body package opened through the body module's own type: its owner is advancedbodies_editor.
            const std::filesystem::path dir = std::filesystem::temp_directory_path() / "csm-owner-real";
            std::error_code ignored;
            std::filesystem::remove_all(dir, ignored);
            const ABTest::PackageFixture fixture = ABTest::makePackageFixture();
            const std::filesystem::path bodyJson = ABTest::writeFolder(dir / "package", fixture.mFiles);
            std::string error;
            std::unique_ptr<AssetDocument> body = registry.openAsset(bodyJson, error);
            ASSERT_NE(body, nullptr) << error;
            EXPECT_EQ(registry.assetOwner(bodyJson), "advancedbodies_editor");
            EXPECT_EQ(registry.assetOwner("x.rig.json"), "creativemotion_editor");

            context.mActive = body.get();
            // Before: CreativeMotion's panel took any active asset's properties. Now it is empty for this owner.
            EXPECT_EQ(registry.panelSource("creativemotion.panel", "advancedbodies_editor"), nullptr);
            EXPECT_NE(registry.panelSource("advancedbodies.panel", "advancedbodies_editor"), nullptr);
            EXPECT_EQ(registry.panelSource("advancedbodies.panel", "creativemotion_editor"), nullptr);
            context.mActive = nullptr;
            body.reset();
            std::filesystem::remove_all(dir, ignored);
        }
    }
}
