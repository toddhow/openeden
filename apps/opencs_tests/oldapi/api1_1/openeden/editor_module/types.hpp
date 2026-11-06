#pragma once

// The Editor Module API, part 1: plain data and small interfaces (docs/design/eden-studio-plan.md sections 3.3 to 3.9,
// docs/tasks/0031). Standard library types only: no Qt, vsg or Jolt type may appear here.

#include <openeden/version.hpp>

#include <cstdint>
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
    inline constexpr openeden::sdk::Version kApiVersion{ 1, 1 };

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

    /// Overlay data submitted each time it is drawn; Studio draws it (plan section 3.5).
    class OverlaySink
    {
    public:
        virtual ~OverlaySink() = default;
        virtual void line(const Vec3& a, const Vec3& b, const Colour& colour) = 0;
        virtual void point(const Vec3& p, const Colour& colour, float sizePixels) = 0;
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
