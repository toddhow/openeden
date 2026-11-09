// The sample editor module (docs/tasks/0031, 0091): one of every part of the Editor Module API, as the reference for
// module authors and as a test. It edits "notes" (a title and a weight, stored in a .note.json file), draws an axis
// cross and the title as a label for a note, and lists two keys on a timeline track. Since API 1.2 it also: registers
// two settings (shown in Studio's preferences window), subscribes to the editor event bus (and counts what it hears),
// reacts to a click on its handle (the host's selection), and runs a checksum as a background task. It uses only the
// headers in openeden/editor_module and the standard library (plus nlohmann::json for the file, which a module may
// choose freely).

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace
{
    using namespace openeden::editor;

    constexpr const char* sFormatId = "sample.note";
    constexpr double sWeightMin = 0.0;
    constexpr double sWeightMax = 10.0;
    constexpr double sAxisHalfLengthM = 3.0; // The axis cross reaches this far each way, in meters.
    constexpr double sHandleStartHeightM = 1.0; // Where a new note's handle starts.
    constexpr double sSecondKeySeconds = 1.5;
    constexpr const char* sGreetingSetting = "sample.greeting";
    constexpr const char* sAxisSetting = "sample.axisHalfLength";
    constexpr const char* sDefaultGreeting = "Hello from the sample module";
    constexpr double sAxisMinM = 0.5;
    constexpr double sAxisMaxM = 10.0;
    constexpr const char* sHandleId = "sample.handle";
    constexpr int sChecksumSteps = 4; // the task reports progress in this many steps
    // FNV-1a, 64 bit (the offset basis and the prime of the published algorithm).
    constexpr std::uint64_t sFnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t sFnvPrime = 1099511628211ull;

    /// The data of one note, plus the state the tools need.
    struct NoteData
    {
        std::string mPath;
        std::string mTitle;
        double mWeight = 0.0;
        Vec3 mPosition{ 0.0, 0.0, sHandleStartHeightM }; // Not saved: where the gizmo's handle is.
        bool mDirty = false;
        // Not saved, set by the module from what Studio tells it (API 1.2):
        std::int64_t mEventsSeen = 0; // every event of the bus
        bool mHandleSelected = false; // the host's selection is this note's handle
        std::string mChecksum; // the result of the background task, empty until it finished
    };

    class SetTitleCommand final : public Command
    {
    public:
        SetTitleCommand(NoteData& note, std::string value)
            : mNote(note)
            , mOld(note.mTitle)
            , mNew(std::move(value))
        {
        }
        std::string_view label() const override { return "Change note title"; }
        void redo() override
        {
            mNote.mTitle = mNew;
            mNote.mDirty = true;
        }
        void undo() override { mNote.mTitle = mOld; }

    private:
        NoteData& mNote;
        std::string mOld;
        std::string mNew;
    };

    /// Weight edits share a merge ID, so dragging a slider is one undo step.
    class SetWeightCommand final : public Command
    {
    public:
        static constexpr int sMergeId = 1;

        SetWeightCommand(NoteData& note, double value)
            : mNote(note)
            , mOld(note.mWeight)
            , mNew(value)
        {
        }
        std::string_view label() const override { return "Change note weight"; }
        void redo() override
        {
            mNote.mWeight = mNew;
            mNote.mDirty = true;
        }
        void undo() override { mNote.mWeight = mOld; }
        int mergeId() const override { return sMergeId; }
        bool mergeWith(const Command& next) override
        {
            const auto* other = dynamic_cast<const SetWeightCommand*>(&next);
            if (other == nullptr || &other->mNote != &mNote)
                return false;
            mNew = other->mNew;
            return true;
        }

    private:
        NoteData& mNote;
        double mOld;
        double mNew;
    };

    class MoveHandleCommand final : public Command
    {
    public:
        MoveHandleCommand(NoteData& note, const Vec3& to)
            : mNote(note)
            , mOld(note.mPosition)
            , mNew(to)
        {
        }
        std::string_view label() const override { return "Move note handle"; }
        void redo() override { mNote.mPosition = mNew; }
        void undo() override { mNote.mPosition = mOld; }

    private:
        NoteData& mNote;
        Vec3 mOld;
        Vec3 mNew;
    };

    class NoteProperties final : public PropertySource
    {
    public:
        explicit NoteProperties(NoteData& note)
            : mNote(note)
        {
        }

        std::vector<PropertyDesc> describe() const override
        {
            PropertyDesc title;
            title.id = "title";
            title.label = "Title";
            title.type = PropertyType::String;
            PropertyDesc weight;
            weight.id = "weight";
            weight.label = "Weight";
            weight.type = PropertyType::Float;
            weight.unit = "kg";
            weight.min = sWeightMin;
            weight.max = sWeightMax;
            weight.step = 0.25;
            // Read only: what the module heard on the bus and what its background task found (API 1.2).
            PropertyDesc events;
            events.id = "events";
            events.label = "Events seen";
            events.type = PropertyType::Int;
            events.readOnly = true;
            events.group = "Studio";
            PropertyDesc checksum;
            checksum.id = "checksum";
            checksum.label = "Title checksum";
            checksum.type = PropertyType::String;
            checksum.readOnly = true;
            checksum.group = "Studio";
            return { title, weight, events, checksum };
        }

        PropertyValue get(std::string_view id) const override
        {
            if (id == "title")
                return mNote.mTitle;
            if (id == "weight")
                return mNote.mWeight;
            if (id == "events")
                return mNote.mEventsSeen;
            if (id == "checksum")
                return mNote.mChecksum;
            return std::string();
        }

        std::unique_ptr<Command> makeSetCommand(std::string_view id, const PropertyValue& value) override
        {
            if (id == "title")
            {
                if (const auto* text = std::get_if<std::string>(&value))
                    return std::make_unique<SetTitleCommand>(mNote, *text);
                return nullptr;
            }
            if (id == "weight")
            {
                const auto* number = std::get_if<double>(&value);
                if (number == nullptr || !(*number >= sWeightMin) || !(*number <= sWeightMax))
                    return nullptr;
                return std::make_unique<SetWeightCommand>(mNote, *number);
            }
            return nullptr;
        }

    private:
        NoteData& mNote;
    };

    /// An axis cross at the note's position, one point on it, and a handle to move it.
    class NoteGizmo final : public Gizmo
    {
    public:
        NoteGizmo(NoteData& note, EditorContext* context)
            : mNote(note)
            , mContext(context)
        {
        }

        std::string_view id() const override { return "sample.gizmo"; }

        void draw(OverlaySink& sink) const override
        {
            const Vec3 c = mNote.mPosition;
            // The length is a setting, read at every drawing (API 1.2); a host without settings gives the default.
            double r = sAxisHalfLengthM;
            if (mContext != nullptr)
                if (const std::optional<PropertyValue> value = mContext->setting(sAxisSetting))
                    if (const double* number = std::get_if<double>(&*value))
                        r = *number;
            sink.line({ c.x - r, c.y, c.z }, { c.x + r, c.y, c.z }, { 1, 0, 0, 1 });
            sink.line({ c.x, c.y - r, c.z }, { c.x, c.y + r, c.z }, { 0, 1, 0, 1 });
            sink.line({ c.x, c.y, c.z - r }, { c.x, c.y, c.z + r }, { 0, 0, 1, 1 });
            sink.point(c, { 1, 1, 0, 1 }, 6.0f);
            // The title as a label at the handle (API 1.2); Studio draws it as text of a fixed pixel height. Yellow
            // while the handle is the host's selection (a click on it).
            const Colour labelColour = mNote.mHandleSelected ? Colour{ 1, 1, 0.2f, 1 } : Colour{ 1, 1, 1, 1 };
            sink.label(c, mNote.mTitle, labelColour);
        }

        std::vector<GizmoHandle> handles() const override { return { { sHandleId, mNote.mPosition } }; }

        // Studio picks the handle with the mouse (API 1.2) and calls this with the new position while it is dragged;
        // the command it gets from here is run as a preview, and the last one is the single undo step of the drag.
        std::unique_ptr<Command> drag(std::string_view handleId, const Vec3& newPosition) override
        {
            if (handleId != sHandleId)
                return nullptr;
            return std::make_unique<MoveHandleCommand>(mNote, newPosition);
        }

    private:
        NoteData& mNote;
        EditorContext* mContext; // not owned; null when a host gave none
    };

    class NoteTrack final : public TimelineTrack
    {
    public:
        std::string_view id() const override { return "sample.track"; }
        std::string_view title() const override { return "Sample keys"; }
        std::vector<TimelineKey> keys() const override { return { { 0.0, "start" }, { sSecondKeySeconds, "end" } }; }
    };

    class NoteDocument final : public AssetDocument
    {
    public:
        NoteDocument(NoteData data, EditorContext* context)
            : mData(std::move(data))
            , mProperties(mData)
            , mGizmo(mData, context)
        {
        }

        NoteData& data() { return mData; }

        std::string_view assetId() const override { return mData.mPath; }
        PropertySource& properties() override { return mProperties; }
        std::vector<Gizmo*> gizmos() override { return { &mGizmo }; }
        std::vector<TimelineTrack*> tracks() override { return { &mTrack }; }
        bool isDirty() const override { return mData.mDirty; }

        bool save(std::string& error) override
        {
            // Canonical text: fixed key order, two-space indent, so saving twice gives the same bytes.
            nlohmann::ordered_json json;
            json["format"] = sFormatId;
            json["title"] = mData.mTitle;
            json["weight"] = mData.mWeight;
            std::ofstream out(mData.mPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "cannot write '" + mData.mPath + "'";
                return false;
            }
            out << json.dump(2) << '\n';
            out.close();
            if (!out)
            {
                error = "writing '" + mData.mPath + "' failed";
                return false;
            }
            error.clear();
            mData.mDirty = false;
            return true;
        }

    private:
        NoteData mData;
        NoteProperties mProperties;
        NoteGizmo mGizmo;
        NoteTrack mTrack;
    };

    std::unique_ptr<AssetDocument> openNote(
        const std::filesystem::path& path, std::string& error, EditorContext* context)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "cannot open '" + path.string() + "'";
            return nullptr;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
        if (json.is_discarded() || !json.is_object())
        {
            error = path.string() + ": not valid JSON";
            return nullptr;
        }
        if (json.value("format", std::string()) != sFormatId)
        {
            error = path.string() + ": the format is not '" + sFormatId + "'";
            return nullptr;
        }
        NoteData data;
        data.mPath = path.string();
        data.mTitle = json.value("title", std::string());
        data.mWeight = json.value("weight", 0.0);
        return std::make_unique<NoteDocument>(std::move(data), context);
    }

    class SampleModule final : public EditorModule
    {
    public:
        void registerUi(EditorRegistrar& registrar) override
        {
            mContext = &registrar.context();

            registrar.registerWorkspace({ "sample.workspace", "Sample" });

            PanelDesc panel;
            panel.id = "sample.panel";
            panel.title = "Note";
            panel.workspaceId = "sample.workspace";
            panel.dock = DockArea::Right;
            // The panel shows the active note, if there is one.
            panel.source = [this]() -> PropertySource* {
                AssetDocument* active = mContext->activeAsset();
                return active != nullptr ? &active->properties() : nullptr;
            };
            registrar.registerPanel(panel);

            CommandDesc command;
            command.id = "sample.command";
            command.label = "Say Hello";
            command.menuPath = "Sample/Hello";
            command.handler = [this](EditorContext& context) { sayHello(context); };
            registrar.registerCommand(command);

            // The simulation sandbox (API 1.3): a command that looks at it and changes nothing.
            CommandDesc probe;
            probe.id = "sample.sandboxProbe";
            probe.label = "Probe Sandbox";
            probe.menuPath = "Sample/Probe Sandbox";
            probe.handler = [](EditorContext& context) { probeSandbox(context); };
            registrar.registerCommand(probe);

            // Settings (API 1.2): shown in the preferences window and kept in eden-studio.cfg under [module.sample].
            SettingDesc greeting;
            greeting.property.id = sGreetingSetting;
            greeting.property.label = "Greeting";
            greeting.property.type = PropertyType::String;
            greeting.property.tooltip = "What Say Hello writes to the log";
            greeting.defaultValue = std::string(sDefaultGreeting);
            registrar.registerSetting(greeting);

            SettingDesc axis;
            axis.property.id = sAxisSetting;
            axis.property.label = "Axis half length";
            axis.property.type = PropertyType::Float;
            axis.property.unit = "m";
            axis.property.min = sAxisMinM;
            axis.property.max = sAxisMaxM;
            axis.property.step = 0.5;
            axis.defaultValue = sAxisHalfLengthM;
            registrar.registerSetting(axis);

            // The editor event bus (API 1.2): handlers run on the UI thread, in registration order.
            registrar.subscribe([this](const EditorEvent& event) { onEvent(event); });

            GizmoProviderDesc gizmos;
            gizmos.id = "sample.gizmos";
            gizmos.gizmos = [this]() -> std::vector<Gizmo*> {
                AssetDocument* active = mContext->activeAsset();
                return active != nullptr ? active->gizmos() : std::vector<Gizmo*>();
            };
            registrar.registerGizmoProvider(gizmos);

            AssetTypeDesc asset;
            asset.formatId = sFormatId;
            asset.fileExtension = ".note.json";
            asset.label = "Sample note";
            asset.open = [this](const std::filesystem::path& path, std::string& error) {
                return openNote(path, error, mContext);
            };
            registrar.registerAssetType(asset);

            InspectorSectionDesc section;
            section.id = "sample.section";
            section.title = "Note";
            section.selectionKind = sFormatId;
            section.create = [](AssetDocument& document) -> PropertySource* { return &document.properties(); };
            registrar.registerInspectorSection(section);

            TimelineTrackProviderDesc tracks;
            tracks.id = "sample.tracks";
            tracks.tracks = [this]() -> std::vector<TimelineTrack*> {
                AssetDocument* active = mContext->activeAsset();
                return active != nullptr ? active->tracks() : std::vector<TimelineTrack*>();
            };
            registrar.registerTimelineTrackProvider(tracks);
        }

        void shutdown() override { mContext = nullptr; }

    private:
        NoteDocument* activeNote() const
        {
            return mContext != nullptr ? dynamic_cast<NoteDocument*>(mContext->activeAsset()) : nullptr;
        }

        void onEvent(const EditorEvent& event)
        {
            NoteDocument* note = activeNote();
            if (note == nullptr)
                return;
            ++note->data().mEventsSeen;
            if (event.type == EditorEventType::SelectionChanged)
            {
                // A click on a handle arrives as the selection {"handle", <handle id>}.
                note->data().mHandleSelected = event.subject == "handle" && event.detail == sHandleId;
                mContext->requestRedraw();
            }
        }

        /// API 1.3: Studio's simulation sandbox is null on a host that has none, so a module checks before it asks. A
        /// real module builds it from a rig and a profile file and pushes (see creativemotion_editor); this one only
        /// reports what is there.
        static void probeSandbox(EditorContext& context)
        {
            SimulationSandbox* sandbox = context.simulationSandbox();
            if (sandbox == nullptr)
                context.log(LogLevel::Info, "Sandbox: this host has none");
            else if (!sandbox->built())
                context.log(LogLevel::Info, "Sandbox: nothing built");
            else
                context.log(LogLevel::Info,
                    "Sandbox: " + std::to_string(sandbox->bodyNames().size()) + " bodies, "
                        + std::to_string(sandbox->pendingPushes()) + " pushes waiting");
        }

        void sayHello(EditorContext& context)
        {
            // The greeting is a setting; a host without settings gives none, so the default is used.
            std::string greeting = sDefaultGreeting;
            if (const std::optional<PropertyValue> value = context.setting(sGreetingSetting))
                if (const std::string* text = std::get_if<std::string>(&*value))
                    greeting = *text;

            // A background task (API 1.2): a checksum of the title, in steps, with progress and a check for cancel. The
            // work function runs on a Studio thread and touches only its own copy of the title; the result is applied
            // in `done`, on the UI thread, to the note that is open then.
            if (NoteDocument* note = activeNote())
            {
                auto text = std::make_shared<std::string>(note->data().mTitle);
                auto result = std::make_shared<std::uint64_t>(sFnvOffset);
                TaskDesc task;
                task.label = "Checksum of the title";
                task.assetId = std::string(note->assetId());
                task.work = [text, result](TaskControl& control) {
                    std::uint64_t hash = sFnvOffset;
                    const std::size_t step = (text->size() + sChecksumSteps - 1) / sChecksumSteps;
                    for (int part = 0; part < sChecksumSteps; ++part)
                    {
                        if (control.cancelled())
                            return;
                        for (std::size_t i = part * step; i < std::min(text->size(), (part + 1) * step); ++i)
                        {
                            hash ^= static_cast<unsigned char>((*text)[i]);
                            hash *= sFnvPrime;
                        }
                        control.reportProgress(static_cast<double>(part + 1) / sChecksumSteps, "hashing");
                    }
                    *result = hash;
                };
                task.done = [this, result](TaskOutcome outcome, const std::string&) {
                    NoteDocument* open = activeNote();
                    if (outcome != TaskOutcome::Finished || open == nullptr)
                        return;
                    char buffer[17];
                    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(*result));
                    open->data().mChecksum = buffer;
                    mContext->requestRedraw();
                };
                context.startTask(std::move(task)); // 0: this host runs no tasks, and nothing else happens
            }
            context.log(LogLevel::Info, greeting);
        }

        EditorContext* mContext = nullptr;
    };
}

OPENEDEN_EDITOR_MODULE("sample", SampleModule)
