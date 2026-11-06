#ifndef CSM_MODULE_MODULEREGISTRY_H
#define CSM_MODULE_MODULEREGISTRY_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "eventbus.hpp"
#include "modulesettings.hpp"

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    template <class Desc>
    struct Entry
    {
        std::string mModule; ///< The module that registered it.
        Desc mDesc;
    };

    /// Everything the loaded editor modules registered (task 0031; plan sections 3.1 to 3.9). It implements the
    /// registrar the modules see, for one module at a time (beginModule / endModule), keeps each registration with
    /// its owner, and withdraws a module's registrations as a whole.
    ///
    /// Studio does not call a module's callbacks directly: it calls runCommand, panelSource, gizmosOf, openAsset and
    /// the like, which catch what a callback throws, withdraw that module and tell whoever listens (ModuleHost, which
    /// then marks the module Failed). No exception leaves them. Main thread only.
    ///
    /// Ownership: it owns the descriptions (copies); the objects the callbacks return belong to the module.
    class ModuleRegistry final : public openeden::editor::EditorRegistrar
    {
    public:
        using WorkspaceEntry = Entry<openeden::editor::WorkspaceDesc>;
        using PanelEntry = Entry<openeden::editor::PanelDesc>;
        using CommandEntry = Entry<openeden::editor::CommandDesc>;
        using GizmoProviderEntry = Entry<openeden::editor::GizmoProviderDesc>;
        using AssetTypeEntry = Entry<openeden::editor::AssetTypeDesc>;
        using SectionEntry = Entry<openeden::editor::InspectorSectionDesc>;
        using TrackProviderEntry = Entry<openeden::editor::TimelineTrackProviderDesc>;

        ModuleRegistry();
        ~ModuleRegistry() override;

        ModuleRegistry(const ModuleRegistry&) = delete;
        ModuleRegistry& operator=(const ModuleRegistry&) = delete;

        /// What registrations made until endModule() belong to. Not nestable.
        void beginModule(const std::string& moduleId);
        void endModule();
        /// Withdraws every registration of the module. Does nothing for an unknown one.
        void removeModule(const std::string& moduleId);

        /// What modules get from context(). Null (the default) gives them a context that does nothing.
        void setContext(openeden::editor::EditorContext* context) { mContext = context; }
        /// Called after a module's registrations were withdrawn because one of its callbacks threw.
        void setFailureHandler(std::function<void(const std::string& moduleId, const std::string& message)> handler)
        {
            mOnFailure = std::move(handler);
        }

        // EditorRegistrar
        openeden::editor::Registration registerWorkspace(const openeden::editor::WorkspaceDesc& desc) override;
        openeden::editor::Registration registerPanel(const openeden::editor::PanelDesc& desc) override;
        openeden::editor::Registration registerCommand(const openeden::editor::CommandDesc& desc) override;
        openeden::editor::Registration registerGizmoProvider(const openeden::editor::GizmoProviderDesc& desc) override;
        openeden::editor::Registration registerAssetType(const openeden::editor::AssetTypeDesc& desc) override;
        openeden::editor::Registration registerInspectorSection(
            const openeden::editor::InspectorSectionDesc& desc) override;
        openeden::editor::Registration registerTimelineTrackProvider(
            const openeden::editor::TimelineTrackProviderDesc& desc) override;
        openeden::editor::EditorContext& context() override;
        openeden::editor::Registration subscribe(openeden::editor::EventHandler handler) override;
        openeden::editor::Registration registerSetting(const openeden::editor::SettingDesc& desc) override;

        /// The editor event bus (task 0091 stage b). A handler that throws withdraws its module like any callback.
        EventBus& bus() { return mBus; }
        /// The modules' settings (API 1.2, task 0091 stage e).
        ModuleSettings& settings() { return mSettings; }

        const std::vector<WorkspaceEntry>& workspaces() const { return mWorkspaces; }
        const std::vector<PanelEntry>& panels() const { return mPanels; }
        const std::vector<CommandEntry>& commands() const { return mCommands; }
        const std::vector<GizmoProviderEntry>& gizmoProviders() const { return mGizmoProviders; }
        const std::vector<AssetTypeEntry>& assetTypes() const { return mAssetTypes; }
        const std::vector<SectionEntry>& sections() const { return mSections; }
        const std::vector<TrackProviderEntry>& trackProviders() const { return mTrackProviders; }

        /// The asset type with the longest file extension the path's file name ends with, or null.
        const AssetTypeEntry* findAssetType(const std::filesystem::path& path) const;

        /// The module that registered the asset type that handles  path (see findAssetType), or empty if none does.
        /// This is the owner of every document opened from that path.
        std::string assetOwner(const std::filesystem::path& path) const;

        /// The module that registered the panel, or empty.
        std::string panelOwner(std::string_view panelId) const;

        /// Whether something a module registered (a panel, a gizmo provider, a track provider) applies while a
        /// document of  activeOwner is the active one: always when no document is open ( activeOwner empty), else
        /// only when the same module registered it. A module's panel shows only that module's documents.
        static bool appliesTo(const std::string& registeredBy, const std::string& activeOwner);

        /// The files under  roots that a registered asset type handles, sorted by path, once each. With a non-empty
        ///  ownerFilter only those whose type the module of that id registered.
        std::vector<std::filesystem::path> findAssets(
            const std::vector<std::filesystem::path>& roots, const std::string& ownerFilter = std::string()) const;

        /// Runs a command's handler if it exists and is enabled. \return false if unknown, disabled or it threw.
        bool runCommand(std::string_view commandId);
        /// Whether a command is enabled now (false for unknown, or if enabledWhen threw).
        bool isCommandEnabled(std::string_view commandId);
        /// The panel's current source, or null.
        openeden::editor::PropertySource* panelSource(std::string_view panelId);
        /// The same, but null (and the panel's callback not called) while a document of another module than the
        /// panel's is the active one ( activeOwner, see appliesTo).
        openeden::editor::PropertySource* panelSource(std::string_view panelId, const std::string& activeOwner);
        std::vector<openeden::editor::Gizmo*> gizmosOf(std::string_view providerId);
        std::vector<openeden::editor::TimelineTrack*> tracksOf(std::string_view providerId);
        /// The section's source for an asset, or null.
        openeden::editor::PropertySource* sectionSource(std::string_view sectionId, openeden::editor::AssetDocument&);
        /// Opens through the registered type for the path. Null with \a error set if there is none or it failed.
        std::unique_ptr<openeden::editor::AssetDocument> openAsset(
            const std::filesystem::path& path, std::string& error);

    private:
        class NullContext;

        openeden::editor::Registration refuse(const std::string& why) const;
        bool inModule(openeden::editor::Registration& result) const;
        void fail(const std::string& moduleId, const std::string& message);

        std::vector<WorkspaceEntry> mWorkspaces;
        std::vector<PanelEntry> mPanels;
        std::vector<CommandEntry> mCommands;
        std::vector<GizmoProviderEntry> mGizmoProviders;
        std::vector<AssetTypeEntry> mAssetTypes;
        std::vector<SectionEntry> mSections;
        std::vector<TrackProviderEntry> mTrackProviders;
        EventBus mBus;
        ModuleSettings mSettings;
        std::string mCurrent;
        bool mHasCurrent = false;
        openeden::editor::EditorContext* mContext = nullptr;
        std::unique_ptr<NullContext> mNullContext;
        std::function<void(const std::string&, const std::string&)> mOnFailure;
    };
}

#endif
