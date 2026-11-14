#pragma once

// The Editor Module API, part 2: what a module registers and what Studio gives it (docs/tasks/0031).

#include <openeden/editor_module/types.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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

    /// API 1.2. Calls with this signature run on Studio's main (UI) thread.
    using EventHandler = std::function<void(const EditorEvent&)>;

    /// API 1.2. One module setting: shown in the preferences window and stored in eden-studio.cfg under
    /// `[module.<module id>]`. the property id is the setting's full id ("sample.greeting"); it is unique across
    /// modules like every other id, and it is the key written. The type is Bool, Int, Float, String or Choice (a Vec3
    /// is refused); min and max bound an Int or a Float.
    struct SettingDesc
    {
        PropertyDesc property;
        PropertyValue defaultValue;
    };

    using TaskId = std::uint64_t;

    /// API 1.2. What the task's work function is given, on the pool thread. Both calls are thread safe.
    class TaskControl
    {
    public:
        virtual ~TaskControl() = default;
        /// True once the task was cancelled (cancelTask, or its asset was closed). The work function should return soon
        /// after; its result is then not delivered as Finished.
        virtual bool cancelled() const = 0;
        /// The fraction in [0, 1]; Studio clamps it. The progress callback is queued to the UI thread.
        virtual void reportProgress(double fraction, std::string_view message) = 0;
    };

    enum class TaskOutcome
    {
        Finished,
        Cancelled,
        Failed ///< The work function threw; the error text is passed to `done`.
    };

    /// API 1.2. A background task. `work` runs on Studio's thread pool and must not touch Studio, the active asset or
    /// the module's UI state; it keeps its result in something it captured (a shared_ptr). `progress` and `done` run
    /// on the UI thread, in order, `done` last and exactly once, and never after the task's asset was replaced or
    /// closed, or after the module was unloaded (such a task is cancelled and joined first).
    ///
    /// Cancelling by asset WAITS: replacing or closing the task's asset, and unloading the module, block the UI thread
    /// until `work` returns (cancelTask alone only sets the flag). `work` must therefore poll TaskControl::cancelled()
    /// often and return promptly when it is set; a work function that never checks it freezes the editor at those
    /// moments.
    struct TaskDesc
    {
        std::string label;
        std::string
            assetId; ///< The document the task belongs to; closing or replacing it cancels the task. May be empty.
        std::function<void(TaskControl&)> work;
        std::function<void(double fraction, const std::string& message)> progress; ///< May be empty.
        std::function<void(TaskOutcome, const std::string& error)> done; ///< May be empty.
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

        /// API 1.2. What is selected now (empty: nothing). Studio owns the value.
        virtual Selection selection() const { return {}; }
        /// API 1.2. Sets the selection. A change publishes EditorEventType::SelectionChanged; setting the same value
        /// again publishes nothing. The selection is cleared when the active asset is replaced or closed.
        virtual void select(const Selection& /*selection*/) {}
        /// API 1.2. Publishes an event to every subscriber (including this module's own), after the current dispatch if
        /// one is running. Studio itself publishes SelectionChanged, AssetOpened, SimulationStepped and (for edits made
        /// in a property form) ParameterChanged.
        virtual void publish(const EditorEvent& /*event*/) {}
        /// API 1.2. The current value of a setting registered with registerSetting (the default when the file holds
        /// none), or nothing if the id is unknown.
        virtual std::optional<PropertyValue> setting(std::string_view /*id*/) const { return std::nullopt; }
        /// API 1.2. Starts a background task. Returns its id, or 0 if it was refused (no work function, or the host
        /// runs no tasks). The id is only for cancelTask.
        virtual TaskId startTask(TaskDesc /*task*/) { return 0; }
        /// API 1.2. Asks a task to stop. Safe for an id that is done or unknown.
        virtual void cancelTask(TaskId /*id*/) {}

        /// API 1.3. Studio's simulation sandbox (see SimulationSandbox), or null when the host has none (a host older
        /// than 1.3 never has one: its tables do not contain this function, and it refuses a module built for 1.3).
        virtual SimulationSandbox* simulationSandbox() { return nullptr; }

        /// API 1.4. The timeline playhead in seconds (Studio owns it; clamped to the active tracks' length).
        virtual double playheadSeconds() const { return 0.0; }
        /// API 1.4. Moves the playhead; a change publishes EditorEventType::PlayheadChanged.
        virtual void setPlayhead(double /*seconds*/) {}
        /// API 1.4. Shows a modal dialog and returns the edited values (one per field) on OK, nothing on Cancel or when
        /// the dialog is refused (the description is invalid, or the host cannot show one).
        virtual std::optional<std::vector<PropertyValue>> showDialog(const DialogDesc& /*desc*/)
        {
            return std::nullopt;
        }
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

        /// API 1.2. Registers a setting (see SettingDesc). Refused with a reason for an empty or duplicate id, a type
        /// that is not allowed, or a default outside the declared range. When the file has no value for it the default
        /// is written; a stored value outside the range is clamped with one warning.
        virtual Registration registerSetting(const SettingDesc& /*desc*/)
        {
            return { false, "this Eden Studio does not provide settings (editor API 1.2)" };
        }
        /// API 1.2. Subscribes to Studio's event bus. Handlers run on the UI thread in the order of their registration
        /// (across modules, in load order). A handler that throws is withdrawn with its whole module, like any
        /// callback.
        virtual Registration subscribe(EventHandler /*handler*/)
        {
            return { false, "this Eden Studio does not provide an event bus (editor API 1.2)" };
        }
    };
}
