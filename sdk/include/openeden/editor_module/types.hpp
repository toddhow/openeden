#pragma once

// The Editor Module API, part 1: plain data and small interfaces (docs/design/eden-studio-plan.md sections 3.3 to 3.9,
// docs/tasks/0031). Standard library types only: no Qt, vsg or Jolt type may appear here.

#include <openeden/version.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openeden::editor
{
    /// The API's own version (plan section 4.4), separate from the engine SDK's. A module built for {M, m} loads when
    /// openeden::sdk::isCompatible({M, m}, kApiVersion). Minor versions only add.
    ///
    /// 1.1 (task 0031 follow-up, toddhow 2026-11-04) adds filled overlay triangles (OverlaySink::triangle) and the user
    /// data folder (EditorContext::userDataPath). Each is a new virtual function at the END of its interface with a
    /// do-nothing default, so a 1.0 module still loads on a 1.1 host. A module built with these headers declares 1.1,
    /// so a 1.0 host refuses it (its tables have no such functions).
    ///
    /// 1.2 (task 0091, toddhow 2026-11-06) adds a selection value the host owns, an editor event bus, picking and
    /// dragging of gizmo handles in the viewport, text labels that Studio draws, module settings and background tasks.
    /// Like 1.1 it only adds: new virtual functions at the END of the interfaces Studio implements (EditorContext,
    /// EditorRegistrar, OverlaySink) with do-nothing defaults, and new types. Nothing a module implements (Gizmo,
    /// AssetDocument, PropertySource, Command) and no struct that crosses the boundary has changed, so a 1.0 or 1.1
    /// module's vtables and layouts are read exactly as before. A module built with these headers declares 1.2, so a
    /// 1.0 or 1.1 host refuses it.
    ///
    /// 1.3 (task 0065, toddhow 2026-11-08) adds the simulation sandbox: SimulationSandbox and SandboxBodyPose (new
    /// types) and EditorContext::simulationSandbox() (a new virtual function at the END of EditorContext, default
    /// null). A module builds one ragdoll from a rig and a profile file, queues pushes on a named bone and reads the
    /// bodies' poses; Studio runs the simulation (Play, Pause, Step). Nothing a module implements and no struct that
    /// crossed the boundary before has changed, so 1.0 to 1.2 modules load as they did; a module built with these
    /// headers declares 1.3, so an older host refuses it.
    ///
    /// 1.4 (task 0147, toddhow 2026-11-12) adds what the face timeline needs: a curve track and a waveform track on
    /// Studio's timeline (CurveTrack and WaveformTrack, optional interfaces a module's TimelineTrack may ALSO
    /// implement; Studio finds them with dynamic_cast, so TimelineTrack itself is unchanged), a playhead Studio owns
    /// (EditorContext::playheadSeconds, setPlayhead, EditorEventType::PlayheadChanged, appended at the end of the
    /// enum) and a modal dialog (DialogDesc and EditorContext::showDialog). Only new types and new virtual functions at
    /// the END of EditorContext, default bodies; nothing a 1.0 to 1.3 module implements has changed, so those load as
    /// they did, and a module built with these headers declares 1.4, so an older host refuses it.
    inline constexpr openeden::sdk::Version kApiVersion{ 1, 4 };

    /// Meters, Z up (CreativeMotion's convention). Studio converts to the world's unit at its overlay boundary.
    struct Vec3
    {
        double x = 0;
        double y = 0;
        double z = 0;
    };

    struct Colour
    {
        float r = 1;
        float g = 1;
        float b = 1;
        float a = 1;
    };

    enum class PropertyType
    {
        Bool,
        Int,
        Float,
        String,
        Choice,
        Vec3
    };

    /// A Choice value is a std::string from PropertyDesc::choices.
    using PropertyValue = std::variant<bool, std::int64_t, double, std::string, Vec3>;

    struct PropertyDesc
    {
        std::string id;
        std::string label;
        PropertyType type = PropertyType::Float;
        std::string unit; ///< Text such as "N*m/rad".
        std::string tooltip;
        double min = std::numeric_limits<double>::lowest(); ///< Bounds Int and Float.
        double max = std::numeric_limits<double>::max();
        double step = 0;
        std::vector<std::string> choices;
        bool readOnly = false;
        std::string group;
    };

    /// One undoable change. Studio pushes it on its single undo stack, which calls redo() first.
    class Command
    {
    public:
        virtual ~Command() = default;
        virtual std::string_view label() const = 0;
        virtual void redo() = 0;
        virtual void undo() = 0;
        /// A value >= 0 lets Studio merge consecutive commands of the same id: it calls mergeWith on the earlier one
        /// with the later, and true means the later one is absorbed.
        virtual int mergeId() const { return -1; }
        virtual bool mergeWith(const Command& /*next*/) { return false; }
    };

    /// What a panel or inspector section shows: described fields, their values, and how to change one.
    class PropertySource
    {
    public:
        virtual ~PropertySource() = default;
        virtual std::vector<PropertyDesc> describe() const = 0;
        virtual PropertyValue get(std::string_view id) const = 0;
        /// Null refuses (unknown id, wrong type, out of range, read only). Changes nothing itself.
        virtual std::unique_ptr<Command> makeSetCommand(std::string_view id, const PropertyValue& value) = 0;
    };

    /// How a filled overlay shape meets the scene's depth (API 1.1).
    enum class OverlayDepth
    {
        Tested, ///< Hidden where the scene is in front of it.
        AlwaysOnTop ///< Drawn over everything.
    };

    /// API 1.2. What is selected in Studio: a kind chosen by the module that owns the asset ("joint", "region") and an
    /// id within the active asset ("spine_01"). An empty kind is "nothing selected". Studio owns the value; it is
    /// cleared when the active asset changes.
    struct Selection
    {
        std::string kind;
        std::string id;

        bool empty() const { return kind.empty(); }
        friend bool operator==(const Selection& a, const Selection& b) { return a.kind == b.kind && a.id == b.id; }
        friend bool operator!=(const Selection& a, const Selection& b) { return !(a == b); }
    };

    /// API 1.2. The events on Studio's bus (plan section 3.10).
    enum class EditorEventType
    {
        SelectionChanged, ///< subject: the selection's kind, detail: its id (both empty when it was cleared).
        AssetOpened, ///< subject: the asset's id.
        SimulationStepped, ///< step: the physics world's step index. Published once per fixed step, while Playing and
                           ///< for each Step press.
        ParameterChanged, ///< subject: the property's id, detail: the asset's id.
        PlayheadChanged ///< API 1.4. step: unused; subject: the playhead in seconds as text with 3 decimals.
    };

    /// API 1.2. Plain data: no pointers into Studio.
    struct EditorEvent
    {
        EditorEventType type = EditorEventType::SelectionChanged;
        std::string subject;
        std::string detail;
        std::uint64_t step = 0;
    };

    /// API 1.3. The pose of one sandbox body: centre of mass in meters (Z up, the sandbox's own frame, the ground is
    /// the plane z = 0) and its rotation as a unit quaternion.
    struct SandboxBodyPose
    {
        Vec3 positionM;
        double qx = 0;
        double qy = 0;
        double qz = 0;
        double qw = 1;
    };

    /// API 1.3 (task 0065). Studio's simulation sandbox: one ragdoll in the editor's one physics world, over a ground
    /// plane at z = 0, that a module builds from a rig and a profile, pushes and reads back. Studio implements it; a
    /// module only calls it (main thread only). It carries no CreativeMotion, Jolt or Qt type: a rig and a profile are
    /// handed over as file paths, bones are names, lengths are meters, impulses newton seconds.
    ///
    /// The simulation itself is Studio's: it runs with Play, Pause and Step. A push is queued and applied once, just
    /// before the next step, so while Paused it waits for the next Step press and while Playing for the next tick; it
    /// is never applied from inside a step. A push is not project data: it never goes on the undo stack, and reset() is
    /// its undo.
    class SimulationSandbox
    {
    public:
        virtual ~SimulationSandbox() = default;

        /// Replaces the current ragdoll with one built from these files. It stands with its rig's rest pose moved by
        ///  offsetM, turned by  yawRadians about Z, and limp (no motors). False with  error set when a file
        /// cannot be read or is invalid, or the world refuses the bodies; nothing is left in the world then.
        virtual bool build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
            const Vec3& offsetM, double yawRadians, std::string& error)
            = 0;
        virtual bool built() const = 0;
        /// The bones that carry a body, in the rig's body order (empty while nothing is built).
        virtual std::vector<std::string> bodyNames() const = 0;
        /// Queues an impulse (newton seconds, world space) at a world point (meters) of the body on  bone. False,
        /// and nothing queued, for an unknown bone, a non-finite value, a very large impulse, nothing built or a full
        /// queue.
        virtual bool push(std::string_view bone, const Vec3& impulseNs, const Vec3& worldPointM) = 0;
        /// Pushes waiting for the next step.
        virtual std::size_t pendingPushes() const = 0;
        /// The body's pose now. False for an unknown bone or nothing built.
        virtual bool bodyPose(std::string_view bone, SandboxBodyPose& out) const = 0;
        /// Builds the ragdoll again where it started; drops queued pushes. False with  error set if that fails.
        virtual bool reset(std::string& error) = 0;
        /// Removes the ragdoll and the ground. Safe when nothing is built.
        virtual void release() = 0;
    };

    /// Overlay data submitted each time it is drawn; Studio draws it (plan section 3.5).
    class OverlaySink
    {
    public:
        virtual ~OverlaySink() = default;
        virtual void line(const Vec3& a, const Vec3& b, const Colour& colour) = 0;
        virtual void point(const Vec3& p, const Colour& colour, float sizePixels) = 0;
        /// Text next to a point. Hosts older than 1.2 drop it; Studio 1.2 draws it in the viewport as screen-space text
        /// of a fixed pixel height, with the same colour rules as a line. ASCII only (lower case is drawn as upper
        /// case); any other byte, a text longer than CSMModule::kMaxLabelLength or a count past
        /// CSMModule::kMaxOverlayLabels is refused or dropped, and a label is part of the captured frame like any line.
        virtual void label(const Vec3& p, std::string_view text, const Colour& colour) = 0;
        /// API 1.1. One filled triangle of one colour; alpha below 1 is blended. Both sides are drawn. Studio refuses a
        /// triangle with a coordinate or colour that is not finite, and drops the triangles past a cap per drawing (see
        /// CSMModule::OverlayBuffer), so a module need not check its data twice. A host older than 1.1 never calls
        /// this and never loads a module that does. Added at the end of the interface; the default draws nothing.
        virtual void triangle(
            const Vec3& /*a*/, const Vec3& /*b*/, const Vec3& /*c*/, const Colour& /*colour*/, OverlayDepth /*depth*/)
        {
        }
    };

    struct GizmoHandle
    {
        std::string id;
        Vec3 position;
    };

    class Gizmo
    {
    public:
        virtual ~Gizmo() = default;
        virtual std::string_view id() const = 0;
        virtual void draw(OverlaySink& sink) const = 0;
        virtual std::vector<GizmoHandle> handles() const { return {}; }
        /// The command that moves a handle to a new position, or null.
        virtual std::unique_ptr<Command> drag(std::string_view /*handleId*/, const Vec3& /*newPosition*/)
        {
            return nullptr;
        }
    };

    struct TimelineKey
    {
        double timeSeconds = 0;
        std::string label;
    };

    class TimelineTrack
    {
    public:
        virtual ~TimelineTrack() = default;
        virtual std::string_view id() const = 0;
        virtual std::string_view title() const = 0;
        virtual std::vector<TimelineKey> keys() const = 0;
    };

    /// API 1.4. One key of a curve channel: a time and a value.
    struct CurveKey
    {
        double timeSeconds = 0;
        double value = 0;
    };

    /// API 1.4. What Studio needs to draw one channel of a CurveTrack.
    struct CurveChannelInfo
    {
        std::string id; ///< Unique within the track, e.g. "jawopen".
        std::string label;
        double minValue = 0; ///< The range the canvas shows (and the keys may take).
        double maxValue = 1;
        Colour colour;
        bool readOnly = false;
    };

    /// API 1.4 (task 0147). A timeline track of named channels of keys that the author edits on Studio's curve canvas.
    /// A module's TimelineTrack implements this ALSO (class MyTrack : public CurveTrack): TimelineTrack is unchanged,
    /// so older hosts see a plain track, and Studio finds this interface with dynamic_cast. Channel keys are sorted by
    /// time. The make*Command functions only BUILD a command (null refuses: unknown channel, key out of range, a
    /// time that is not finite); Studio pushes it, so every edit is one undo step. Studio checks the finite and range
    /// rules before calling, too.
    class CurveTrack : public TimelineTrack
    {
    public:
        virtual std::vector<CurveChannelInfo> channels() const = 0;
        virtual std::vector<CurveKey> channelKeys(std::string_view channelId) const = 0;
        /// The track's length in seconds (the right edge of the canvas).
        virtual double durationSeconds() const = 0;
        virtual std::unique_ptr<Command> makeAddKeyCommand(std::string_view channelId, const CurveKey& key) = 0;
        /// Moves key number  index (in channelKeys order) to a new time and value.
        virtual std::unique_ptr<Command> makeMoveKeyCommand(
            std::string_view channelId, std::size_t index, const CurveKey& newKey)
            = 0;
        virtual std::unique_ptr<Command> makeDeleteKeyCommand(std::string_view channelId, std::size_t index) = 0;
    };

    /// API 1.4. A mono PCM buffer. The pointer stays valid until the next call to the track that returned it or the
    /// track's destruction.
    struct WaveformData
    {
        const float* samples = nullptr; ///< mono, -1..1
        std::size_t sampleCount = 0;
        std::uint32_t sampleRate = 0;
    };

    /// API 1.4 (task 0147). A track that shows an audio waveform with Studio's playhead over it, and that Studio can
    /// play. Same convention as CurveTrack: implemented in addition to TimelineTrack.
    class WaveformTrack : public TimelineTrack
    {
    public:
        virtual WaveformData waveform() const = 0;
    };

    /// API 1.4. A modal dialog with a small form (the property types of PropertyDesc; Vec3 is refused). Values are
    /// edited in a copy and handed back only on OK.
    struct DialogDesc
    {
        std::string title;
        std::string message; ///< Optional text above the form.
        std::vector<PropertyDesc> fields;
        std::vector<PropertyValue>
            initial; ///< One per field, same order; a missing or mistyped one is the type's zero.
        std::string okLabel = "OK";
    };

    /// An opened asset. Owns its gizmos and tracks; the pointers live as long as the document.
    class AssetDocument
    {
    public:
        virtual ~AssetDocument() = default;
        virtual std::string_view assetId() const = 0;
        virtual PropertySource& properties() = 0;
        virtual std::vector<Gizmo*> gizmos() { return {}; }
        virtual std::vector<TimelineTrack*> tracks() { return {}; }
        virtual bool isDirty() const = 0;
        virtual bool save(std::string& error) = 0;
    };
}
