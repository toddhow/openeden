#pragma once

// The Editor Module API, part 2: what a module registers and what Studio gives it (docs/tasks/0031).

#include <openeden/editor_module/types.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace openeden::editor
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error
    };

    enum class DockArea
    {
        Left,
        Right,
        Bottom
    };

    /// What Studio gives a module. Main thread only.
    class EditorContext
    {
    public:
        virtual ~EditorContext() = default;
        /// Executes redo() and records one undo entry (merging by Command::mergeId).
        virtual void pushCommand(std::unique_ptr<Command> command) = 0;
        virtual void markDirty(std::string_view assetId) = 0;
        /// The module's own log channel, prefixed with its id.
        virtual void log(LogLevel level, std::string_view message) = 0;
        virtual void requestRedraw() = 0;
        /// Opens through the registered asset types. Null if there is none or it failed.
        virtual AssetDocument* openAsset(const std::filesystem::path& path) = 0;
        virtual AssetDocument* activeAsset() = 0;
        /// API 1.1. Studio's user data folder, where the game keeps what it derives from the player's data (for example
        /// <user data>/openeden/bodies/), or an empty path if the host has none. Read only: a module may look there
        /// and read; it never learns where anything else is. Added at the end of the interface; the default is an
        /// empty path, so a host written for 1.0 still compiles.
        virtual std::filesystem::path userDataPath() const { return {}; }
    };

    struct WorkspaceDesc
    {
        std::string id;
        std::string title;
    };

    struct PanelDesc
    {
        std::string id;
        std::string title;
        std::string workspaceId;
        DockArea dock = DockArea::Right;
        /// The panel shows source() if non-null, now and after every change of the undo stack.
        std::function<PropertySource*()> source;
    };

    struct CommandDesc
    {
        std::string id;
        std::string label;
        std::string menuPath; ///< "Rig/Check": submenu Rig, item Check. Empty: the module's own menu.
        std::string defaultShortcut; ///< Text such as "Ctrl+Shift+R"; may be empty.
        std::function<void(EditorContext&)> handler;
        std::function<bool()> enabledWhen; ///< Empty means always.
    };

    struct GizmoProviderDesc
    {
        std::string id;
        std::function<std::vector<Gizmo*>()> gizmos;
    };

    struct AssetTypeDesc
    {
        std::string formatId; ///< "opas.rig"
        std::string fileExtension; ///< ".rig.json"
        std::string label;
        std::function<std::unique_ptr<AssetDocument>(const std::filesystem::path&, std::string& error)> open;
    };

    struct InspectorSectionDesc
    {
        std::string id;
        std::string title;
        std::string selectionKind; ///< A format id such as "opas.rig": shown for an open asset of that format.
        std::function<PropertySource*(AssetDocument&)> create;
    };

    struct TimelineTrackProviderDesc
    {
        std::string id;
        std::function<std::vector<TimelineTrack*>()> tracks;
    };

    struct Registration
    {
        bool ok = true;
        std::string error;
    };

    /// What registerUi receives. Ids are unique per kind across all modules; prefix them with the module id. A refused
    /// registration (empty or duplicate id, unknown workspace for a panel, empty extension or a duplicate one, empty
    /// callback) stores nothing and says why.
    class EditorRegistrar
    {
    public:
        virtual ~EditorRegistrar() = default;
        virtual Registration registerWorkspace(const WorkspaceDesc& desc) = 0;
        virtual Registration registerPanel(const PanelDesc& desc) = 0;
        virtual Registration registerCommand(const CommandDesc& desc) = 0;
        virtual Registration registerGizmoProvider(const GizmoProviderDesc& desc) = 0;
        virtual Registration registerAssetType(const AssetTypeDesc& desc) = 0;
        virtual Registration registerInspectorSection(const InspectorSectionDesc& desc) = 0;
        virtual Registration registerTimelineTrackProvider(const TimelineTrackProviderDesc& desc) = 0;
        virtual EditorContext& context() = 0;
    };
}
