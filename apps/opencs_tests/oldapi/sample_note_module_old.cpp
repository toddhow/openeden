// FROZEN (task 0091 stage a): a copy of the sample editor module as it was at API 1.1, with the id "sample_old". It is
// compiled twice against the frozen headers in oldapi/api1_0 and oldapi/api1_1 (never against sdk/include's), to show
// that a module built for 1.0 or 1.1 still loads and runs on a newer Eden Studio. Do not edit it to use new API.

// The sample editor module (docs/tasks/0031): one of every part of the Editor Module API, as the reference for module
// authors and as a test. It edits "notes" (a title and a weight, stored in a .note.json file), draws an axis cross for
// a note, and lists two keys on a timeline track. It uses only the headers in openeden/editor_module and the standard
// library (plus nlohmann::json for the file, which a module may choose freely).

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <memory>
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

    /// The data of one note, plus the state the tools need.
    struct NoteData
    {
        std::string mPath;
        std::string mTitle;
        double mWeight = 0.0;
        Vec3 mPosition{ 0.0, 0.0, sHandleStartHeightM }; // Not saved: where the gizmo's handle is.
        bool mDirty = false;
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
            return { title, weight };
        }

        PropertyValue get(std::string_view id) const override
        {
            if (id == "title")
                return mNote.mTitle;
            if (id == "weight")
                return mNote.mWeight;
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
        explicit NoteGizmo(NoteData& note)
            : mNote(note)
        {
        }

        std::string_view id() const override { return "sample.gizmo"; }

        void draw(OverlaySink& sink) const override
        {
            const Vec3 c = mNote.mPosition;
            const double r = sAxisHalfLengthM;
            sink.line({ c.x - r, c.y, c.z }, { c.x + r, c.y, c.z }, { 1, 0, 0, 1 });
            sink.line({ c.x, c.y - r, c.z }, { c.x, c.y + r, c.z }, { 0, 1, 0, 1 });
            sink.line({ c.x, c.y, c.z - r }, { c.x, c.y, c.z + r }, { 0, 0, 1, 1 });
            sink.point(c, { 1, 1, 0, 1 }, 6.0f);
        }

        std::vector<GizmoHandle> handles() const override { return { { "sample.handle", mNote.mPosition } }; }

        std::unique_ptr<Command> drag(std::string_view handleId, const Vec3& newPosition) override
        {
            if (handleId != "sample.handle")
                return nullptr;
            return std::make_unique<MoveHandleCommand>(mNote, newPosition);
        }

    private:
        NoteData& mNote;
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
        explicit NoteDocument(NoteData data)
            : mData(std::move(data))
            , mProperties(mData)
            , mGizmo(mData)
        {
        }

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

    std::unique_ptr<AssetDocument> openNote(const std::filesystem::path& path, std::string& error)
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
        return std::make_unique<NoteDocument>(std::move(data));
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
            command.handler
                = [](EditorContext& context) { context.log(LogLevel::Info, "Hello from the sample module"); };
            registrar.registerCommand(command);

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
            asset.open = &openNote;
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
        EditorContext* mContext = nullptr;
    };
}

OPENEDEN_EDITOR_MODULE("sample_old", SampleModule)
