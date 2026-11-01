// Task 0031, stage A: CSMModule::ModuleRegistry, written from the spec alone.
//
// Assumptions about names the spec leaves open (see the report):
//  - Header: apps/opencs/model/module/moduleregistry.hpp (ModuleRegistry), namespace CSMModule.
//  - ModuleRegistry is default constructible, derives from openeden::editor::EditorRegistrar, and its beginModule(id) /
//    endModule() bracket one module's registrations. The registrar methods take their description by const reference.
//  - workspaces(), panels(), commands(), gizmoProviders(), assetTypes(), sections(), trackProviders() each return a
//    container (size(), empty(), front(), iteration) of entries { std::string mModule; <Desc> mDesc; }.
//  - findAssetType(const std::filesystem::path&) returns a pointer to such an entry, or nullptr when none matches.
//  - Studio-side invocations that run module callbacks under try/catch:
//      bool runCommand(std::string_view id)                       (false when unknown, disabled or it threw)
//      PropertySource* panelSource(std::string_view panelId)      (nullptr when unknown or it threw)
//      std::vector<Gizmo*> gizmosOf(std::string_view providerId)  (empty when unknown or it threw)
//      std::unique_ptr<AssetDocument> openAsset(const std::filesystem::path&, std::string& error)
//    An exception withdraws that module's registrations (as removeModule does).
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;

        WorkspaceDesc workspace(const std::string& id)
        {
            WorkspaceDesc desc;
            desc.id = id;
            desc.title = "Title " + id;
            return desc;
        }

        PanelDesc panel(const std::string& id, const std::string& workspaceId)
        {
            PanelDesc desc;
            desc.id = id;
            desc.title = "Title " + id;
            desc.workspaceId = workspaceId;
            desc.dock = DockArea::Right;
            desc.source = []() -> PropertySource* { return nullptr; };
            return desc;
        }

        CommandDesc command(const std::string& id)
        {
            CommandDesc desc;
            desc.id = id;
            desc.label = "Label " + id;
            desc.menuPath = "Rig/Check";
            desc.defaultShortcut = "Ctrl+Shift+R";
            desc.handler = [](EditorContext&) {};
            return desc;
        }

        GizmoProviderDesc gizmoProvider(const std::string& id)
        {
            GizmoProviderDesc desc;
            desc.id = id;
            desc.gizmos = []() { return std::vector<Gizmo*>(); };
            return desc;
        }

        AssetTypeDesc assetType(const std::string& formatId, const std::string& extension)
        {
            AssetTypeDesc desc;
            desc.formatId = formatId;
            desc.fileExtension = extension;
            desc.label = "Label " + formatId;
            desc.open = [](const std::filesystem::path&, std::string& error) -> std::unique_ptr<AssetDocument> {
                error = "not implemented in this test";
                return nullptr;
            };
            return desc;
        }

        InspectorSectionDesc section(const std::string& id)
        {
            InspectorSectionDesc desc;
            desc.id = id;
            desc.title = "Title " + id;
            desc.selectionKind = "opas.rig";
            desc.create = [](AssetDocument&) -> PropertySource* { return nullptr; };
            return desc;
        }

        TimelineTrackProviderDesc trackProvider(const std::string& id)
        {
            TimelineTrackProviderDesc desc;
            desc.id = id;
            desc.tracks = []() { return std::vector<TimelineTrack*>(); };
            return desc;
        }

        /// Registers one of every kind under `prefix`, inside beginModule/endModule.
        void registerAll(ModuleRegistry& registry, const std::string& module, const std::string& prefix,
            const std::string& extension)
        {
            registry.beginModule(module);
            EXPECT_TRUE(registry.registerWorkspace(workspace(prefix + ".workspace")).ok);
            EXPECT_TRUE(registry.registerPanel(panel(prefix + ".panel", prefix + ".workspace")).ok);
            EXPECT_TRUE(registry.registerCommand(command(prefix + ".command")).ok);
            EXPECT_TRUE(registry.registerGizmoProvider(gizmoProvider(prefix + ".gizmos")).ok);
            EXPECT_TRUE(registry.registerAssetType(assetType(prefix + ".asset", extension)).ok);
            EXPECT_TRUE(registry.registerInspectorSection(section(prefix + ".section")).ok);
            EXPECT_TRUE(registry.registerTimelineTrackProvider(trackProvider(prefix + ".tracks")).ok);
            registry.endModule();
        }

        void expectCounts(const ModuleRegistry& registry, std::size_t expected)
        {
            EXPECT_EQ(registry.workspaces().size(), expected);
            EXPECT_EQ(registry.panels().size(), expected);
            EXPECT_EQ(registry.commands().size(), expected);
            EXPECT_EQ(registry.gizmoProviders().size(), expected);
            EXPECT_EQ(registry.assetTypes().size(), expected);
            EXPECT_EQ(registry.sections().size(), expected);
            EXPECT_EQ(registry.trackProviders().size(), expected);
        }

        void expectRefused(const Registration& registration)
        {
            EXPECT_FALSE(registration.ok);
            EXPECT_FALSE(registration.error.empty());
        }

        TEST(CSMModuleRegistryTest, startsEmpty)
        {
            ModuleRegistry registry;
            expectCounts(registry, 0);
            EXPECT_EQ(registry.findAssetType("anything.json"), nullptr);
        }

        TEST(CSMModuleRegistryTest, eachOfTheSevenRegistrationsIsStoredAndQueryable)
        {
            ModuleRegistry registry;
            registerAll(registry, "mod", "mod", ".mod.json");
            expectCounts(registry, 1);

            EXPECT_EQ(registry.workspaces().front().mDesc.id, "mod.workspace");
            EXPECT_EQ(registry.workspaces().front().mModule, "mod");
            EXPECT_EQ(registry.panels().front().mDesc.id, "mod.panel");
            EXPECT_EQ(registry.panels().front().mDesc.workspaceId, "mod.workspace");
            EXPECT_EQ(registry.commands().front().mDesc.id, "mod.command");
            EXPECT_EQ(registry.commands().front().mDesc.menuPath, "Rig/Check");
            EXPECT_EQ(registry.commands().front().mDesc.defaultShortcut, "Ctrl+Shift+R");
            EXPECT_EQ(registry.gizmoProviders().front().mDesc.id, "mod.gizmos");
            EXPECT_EQ(registry.assetTypes().front().mDesc.formatId, "mod.asset");
            EXPECT_EQ(registry.assetTypes().front().mDesc.fileExtension, ".mod.json");
            EXPECT_EQ(registry.sections().front().mDesc.id, "mod.section");
            EXPECT_EQ(registry.trackProviders().front().mDesc.id, "mod.tracks");
            EXPECT_EQ(registry.trackProviders().front().mModule, "mod");
        }

        TEST(CSMModuleRegistryTest, emptyIdsAreRefusedAndStoreNothing)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            expectRefused(registry.registerWorkspace(workspace("")));
            expectRefused(registry.registerPanel(panel("", "w")));
            expectRefused(registry.registerCommand(command("")));
            expectRefused(registry.registerGizmoProvider(gizmoProvider("")));
            expectRefused(registry.registerAssetType(assetType("", ".x.json")));
            expectRefused(registry.registerInspectorSection(section("")));
            expectRefused(registry.registerTimelineTrackProvider(trackProvider("")));
            registry.endModule();
            expectCounts(registry, 0);
        }

        TEST(CSMModuleRegistryTest, duplicateIdsAreRefusedAcrossModules)
        {
            ModuleRegistry registry;
            registerAll(registry, "first", "dup", ".dup.json");
            expectCounts(registry, 1);

            // A second module reusing every id of the first, with a fresh extension for the asset type.
            registry.beginModule("second");
            expectRefused(registry.registerWorkspace(workspace("dup.workspace")));
            expectRefused(registry.registerPanel(panel("dup.panel", "dup.workspace")));
            expectRefused(registry.registerCommand(command("dup.command")));
            expectRefused(registry.registerGizmoProvider(gizmoProvider("dup.gizmos")));
            expectRefused(registry.registerAssetType(assetType("dup.asset", ".other.json")));
            expectRefused(registry.registerInspectorSection(section("dup.section")));
            expectRefused(registry.registerTimelineTrackProvider(trackProvider("dup.tracks")));
            registry.endModule();
            expectCounts(registry, 1);
        }

        TEST(CSMModuleRegistryTest, duplicateIdInTheSameModuleIsRefused)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            EXPECT_TRUE(registry.registerCommand(command("mod.command")).ok);
            expectRefused(registry.registerCommand(command("mod.command")));
            registry.endModule();
            EXPECT_EQ(registry.commands().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, sameIdInDifferentKindsIsAllowed)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            EXPECT_TRUE(registry.registerWorkspace(workspace("mod.thing")).ok);
            EXPECT_TRUE(registry.registerCommand(command("mod.thing")).ok);
            EXPECT_TRUE(registry.registerGizmoProvider(gizmoProvider("mod.thing")).ok);
            registry.endModule();
            EXPECT_EQ(registry.workspaces().size(), 1u);
            EXPECT_EQ(registry.commands().size(), 1u);
            EXPECT_EQ(registry.gizmoProviders().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, panelNamingAnUnregisteredWorkspaceIsRefused)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            expectRefused(registry.registerPanel(panel("mod.panel", "nobody.registered.this")));
            registry.endModule();
            EXPECT_TRUE(registry.panels().empty());
        }

        TEST(CSMModuleRegistryTest, panelMayNameAWorkspaceOfAnotherModule)
        {
            ModuleRegistry registry;
            registry.beginModule("one");
            ASSERT_TRUE(registry.registerWorkspace(workspace("one.workspace")).ok);
            registry.endModule();

            registry.beginModule("two");
            EXPECT_TRUE(registry.registerPanel(panel("two.panel", "one.workspace")).ok);
            registry.endModule();
            EXPECT_EQ(registry.panels().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, assetTypeWithEmptyOrDuplicateExtensionIsRefused)
        {
            ModuleRegistry registry;
            registry.beginModule("one");
            expectRefused(registry.registerAssetType(assetType("one.asset", "")));
            EXPECT_TRUE(registry.registerAssetType(assetType("one.asset", ".one.json")).ok);
            registry.endModule();

            registry.beginModule("two");
            expectRefused(registry.registerAssetType(assetType("two.asset", ".one.json"))); // extension owned already
            registry.endModule();
            EXPECT_EQ(registry.assetTypes().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, emptyCallbacksAreRefusedAndStoreNothing)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            ASSERT_TRUE(registry.registerWorkspace(workspace("mod.workspace")).ok);

            PanelDesc noSource = panel("mod.panel", "mod.workspace");
            noSource.source = nullptr;
            expectRefused(registry.registerPanel(noSource));

            CommandDesc noHandler = command("mod.command");
            noHandler.handler = nullptr;
            expectRefused(registry.registerCommand(noHandler));

            GizmoProviderDesc noGizmos = gizmoProvider("mod.gizmos");
            noGizmos.gizmos = nullptr;
            expectRefused(registry.registerGizmoProvider(noGizmos));

            AssetTypeDesc noOpen = assetType("mod.asset", ".mod.json");
            noOpen.open = nullptr;
            expectRefused(registry.registerAssetType(noOpen));

            InspectorSectionDesc noCreate = section("mod.section");
            noCreate.create = nullptr;
            expectRefused(registry.registerInspectorSection(noCreate));

            TimelineTrackProviderDesc noTracks = trackProvider("mod.tracks");
            noTracks.tracks = nullptr;
            expectRefused(registry.registerTimelineTrackProvider(noTracks));
            registry.endModule();

            EXPECT_EQ(registry.workspaces().size(), 1u);
            EXPECT_TRUE(registry.panels().empty());
            EXPECT_TRUE(registry.commands().empty());
            EXPECT_TRUE(registry.gizmoProviders().empty());
            EXPECT_TRUE(registry.assetTypes().empty());
            EXPECT_TRUE(registry.sections().empty());
            EXPECT_TRUE(registry.trackProviders().empty());
        }

        TEST(CSMModuleRegistryTest, commandWithoutEnabledWhenIsAccepted)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            CommandDesc desc = command("mod.command");
            desc.enabledWhen = nullptr;
            EXPECT_TRUE(registry.registerCommand(desc).ok);
            registry.endModule();
            EXPECT_EQ(registry.commands().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, findAssetTypePicksTheLongestMatchingExtension)
        {
            ModuleRegistry registry;
            registry.beginModule("mod");
            ASSERT_TRUE(registry.registerAssetType(assetType("plain.json", ".json")).ok);
            ASSERT_TRUE(registry.registerAssetType(assetType("opas.rig", ".rig.json")).ok);
            registry.endModule();

            const auto* rig = registry.findAssetType(std::filesystem::path("project") / "a.rig.json");
            ASSERT_NE(rig, nullptr);
            EXPECT_EQ(rig->mDesc.formatId, "opas.rig");

            const auto* plain = registry.findAssetType("b.json");
            ASSERT_NE(plain, nullptr);
            EXPECT_EQ(plain->mDesc.formatId, "plain.json");

            EXPECT_EQ(registry.findAssetType("c.txt"), nullptr);
            EXPECT_EQ(registry.findAssetType("rig.json.txt"), nullptr);
        }

        TEST(CSMModuleRegistryTest, removeModuleWithdrawsExactlyThatModulesRegistrations)
        {
            ModuleRegistry registry;
            registerAll(registry, "one", "one", ".one.json");
            registerAll(registry, "two", "two", ".two.json");
            expectCounts(registry, 2);

            registry.removeModule("one");
            expectCounts(registry, 1);
            EXPECT_EQ(registry.workspaces().front().mDesc.id, "two.workspace");
            EXPECT_EQ(registry.panels().front().mDesc.id, "two.panel");
            EXPECT_EQ(registry.commands().front().mDesc.id, "two.command");
            EXPECT_EQ(registry.gizmoProviders().front().mDesc.id, "two.gizmos");
            EXPECT_EQ(registry.assetTypes().front().mDesc.formatId, "two.asset");
            EXPECT_EQ(registry.sections().front().mDesc.id, "two.section");
            EXPECT_EQ(registry.trackProviders().front().mDesc.id, "two.tracks");
            EXPECT_EQ(registry.findAssetType("x.one.json"), nullptr);
            EXPECT_NE(registry.findAssetType("x.two.json"), nullptr);

            // The withdrawn ids and extension can be registered again.
            registerAll(registry, "three", "one", ".one.json");
            expectCounts(registry, 2);

            registry.removeModule("no-such-module"); // harmless
            expectCounts(registry, 2);
        }

        TEST(CSMModuleRegistryTest, removeModuleTwiceIsHarmless)
        {
            ModuleRegistry registry;
            registerAll(registry, "one", "one", ".one.json");
            registry.removeModule("one");
            registry.removeModule("one");
            expectCounts(registry, 0);
        }

        TEST(CSMModuleRegistryTest, aThrowingCommandHandlerIsCaughtAndWithdrawsOnlyThatModule)
        {
            ModuleRegistry registry;
            registerAll(registry, "good", "good", ".good.json");

            registry.beginModule("bad");
            CommandDesc desc = command("bad.command");
            desc.handler = [](EditorContext&) { throw std::runtime_error("boom"); };
            ASSERT_TRUE(registry.registerCommand(desc).ok);
            registry.endModule();

            EXPECT_EQ(registry.commands().size(), 2u);
            EXPECT_NO_THROW(EXPECT_FALSE(registry.runCommand("bad.command")));
            ASSERT_EQ(registry.commands().size(), 1u);
            EXPECT_EQ(registry.commands().front().mDesc.id, "good.command");
            EXPECT_EQ(registry.workspaces().size(), 1u);
        }

        TEST(CSMModuleRegistryTest, aWorkingCommandHandlerIsRun)
        {
            ModuleRegistry registry;
            int calls = 0;
            registry.beginModule("mod");
            CommandDesc desc = command("mod.command");
            desc.handler = [&calls](EditorContext&) { ++calls; };
            ASSERT_TRUE(registry.registerCommand(desc).ok);
            registry.endModule();

            EXPECT_TRUE(registry.runCommand("mod.command"));
            EXPECT_EQ(calls, 1);
            EXPECT_FALSE(registry.runCommand("unknown.command"));
            EXPECT_EQ(calls, 1);
        }

        TEST(CSMModuleRegistryTest, aThrowingPanelSourceIsCaughtAndWithdrawsOnlyThatModule)
        {
            ModuleRegistry registry;
            registerAll(registry, "good", "good", ".good.json");

            registry.beginModule("bad");
            ASSERT_TRUE(registry.registerWorkspace(workspace("bad.workspace")).ok);
            PanelDesc desc = panel("bad.panel", "bad.workspace");
            desc.source = []() -> PropertySource* { throw std::runtime_error("boom"); };
            ASSERT_TRUE(registry.registerPanel(desc).ok);
            registry.endModule();

            EXPECT_EQ(registry.panels().size(), 2u);
            EXPECT_NO_THROW(EXPECT_EQ(registry.panelSource("bad.panel"), nullptr));
            EXPECT_EQ(registry.panels().size(), 1u);
            EXPECT_EQ(registry.workspaces().size(), 1u);
            EXPECT_EQ(registry.panels().front().mDesc.id, "good.panel");
        }

        TEST(CSMModuleRegistryTest, aThrowingGizmoProviderIsCaughtAndWithdrawsOnlyThatModule)
        {
            ModuleRegistry registry;
            registerAll(registry, "good", "good", ".good.json");

            registry.beginModule("bad");
            GizmoProviderDesc desc = gizmoProvider("bad.gizmos");
            desc.gizmos = []() -> std::vector<Gizmo*> { throw std::runtime_error("boom"); };
            ASSERT_TRUE(registry.registerGizmoProvider(desc).ok);
            registry.endModule();

            EXPECT_EQ(registry.gizmoProviders().size(), 2u);
            EXPECT_NO_THROW(EXPECT_TRUE(registry.gizmosOf("bad.gizmos").empty()));
            ASSERT_EQ(registry.gizmoProviders().size(), 1u);
            EXPECT_EQ(registry.gizmoProviders().front().mDesc.id, "good.gizmos");
        }

        TEST(CSMModuleRegistryTest, aThrowingAssetOpenIsCaughtAndWithdrawsOnlyThatModule)
        {
            ModuleRegistry registry;
            registerAll(registry, "good", "good", ".good.json");

            registry.beginModule("bad");
            AssetTypeDesc desc = assetType("bad.asset", ".bad.json");
            desc.open = [](const std::filesystem::path&, std::string&) -> std::unique_ptr<AssetDocument> {
                throw std::runtime_error("boom");
            };
            ASSERT_TRUE(registry.registerAssetType(desc).ok);
            registry.endModule();

            EXPECT_EQ(registry.assetTypes().size(), 2u);
            std::string error;
            EXPECT_NO_THROW(EXPECT_EQ(registry.openAsset("thing.bad.json", error), nullptr));
            EXPECT_FALSE(error.empty());
            ASSERT_EQ(registry.assetTypes().size(), 1u);
            EXPECT_EQ(registry.assetTypes().front().mDesc.formatId, "good.asset");
            EXPECT_EQ(registry.findAssetType("thing.bad.json"), nullptr);
        }

        TEST(CSMModuleRegistryTest, openAssetWithoutARegisteredTypeGivesAnError)
        {
            ModuleRegistry registry;
            std::string error;
            EXPECT_EQ(registry.openAsset("thing.unknown", error), nullptr);
            EXPECT_FALSE(error.empty());
        }
    }
}
