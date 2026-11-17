#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_source.hpp>
#include <openeden/version.hpp>

// The slot through which a game pack hands its WorldCellSource to the host (decision 0016, task 0103 stage N3a-1;
// docs/design/neutral-world-model.md 10.5). Reached by Host::getWorldSourceService(), which only a pack of kind "game"
// gets (every other kind gets nullptr and a recorded denial, like the bindings service). The host, if it consumes
// sources at all, keeps them in the core's neutral world store (components/worldmodel); a host that does not (Eden
// Studio, a test host) says so through consumerEnabled(), and a pack then does not register.
//
// This header is separate from world_source.hpp on purpose: that header and world_model.hpp are frozen at 1.0, and a
// new type there would raise their minor number. Carries no implementation and no engine, renderer, Jolt, Qt or game
// type. Main thread only, like the source interface.

namespace openeden::sdk::game
{
    /// Version policy: a new virtual function goes at the END of WorldSourceService with a default that means "not
    /// supported" and raises the minor number; anything else is a major change. Host::getWorldSourceService() is the
    /// last virtual function of Host, so adding it left every earlier slot where it was (kSdkVersion stays 4.0).
    inline constexpr Version kWorldSourceServiceVersion{ 1, 3 };

    /// Version 1.3 (task 0202): the placement ids a host mints for objects a script creates (PlaceAtMe) are
    /// kCreatedObjectBase + n, n from 1. No load order form id has this high byte (0xFF), so they never collide with a
    /// pack's own placements, and they sort after them.
    inline constexpr WorldId kCreatedObjectBase = 0xFF800000u;
    inline constexpr std::uint32_t kMaxCreatedObjects = 0x007FFFFFu;

    /// What moveObject changes of an object: the position and/or the rotation (a unit quaternion in the neutral
    /// convention; the pack converts its own angles). A part that is not set stays as it is.
    struct ObjectMove
    {
        bool hasPosition = false;
        Vec3d position;
        bool hasRotation = false;
        Quatd rotation;
        /// Put the object back where the pack's record (or its creation) placed it; position and rotation are ignored.
        bool toOriginal = false;
    };

    /// One content file of the session (a `content=` line of the engine's configuration): the plugin a game pack
    /// builds its world from. Plain strings, so that nothing of the host crosses the boundary.
    struct SessionContentFile
    {
        /// The name as the session's content list gives it ("Skyrim.esm").
        std::string name;
        /// The absolute path of the file, UTF-8.
        std::string path;
    };

    class WorldSourceService
    {
    public:
        virtual ~WorldSourceService() = default;

        /// Hands the host a source. The pack keeps ownership and must withdraw the source before it is destroyed (in
        /// stop() and in its destructor; withdrawing twice is harmless). False with a located `error` when the host
        /// refuses it: another source has the same game id, the same object is already registered, or the host's
        /// store is full. A refused source is not registered.
        [[nodiscard]] virtual bool registerSource(WorldCellSource& source, std::string& error) = 0;

        /// Takes the source back. Does nothing for a source that is not registered. After it returns the host holds no
        /// pointer to the source and none to anything the source built.
        virtual void withdrawSource(WorldCellSource& source) = 0;

        /// True when the host will read the sources it is given (the engine with `[Game] neutral world` on). A pack
        /// registers only when it is true; when it is false the pack behaves as it did before the service existed.
        [[nodiscard]] virtual bool consumerEnabled() const = 0;

        /// Version 1.1 (task 0107, stage N3a-2b). Appends to `out` the session's content files that one of the
        /// content loaders the pack registered with ContentService accepts (a Morrowind plugin is not handed to the
        /// Skyrim SE pack), in the session's load order, and returns how many it appended. 0 means "none, or this
        /// host does not hand out content" and is the default, so a host built before 1.1 answers it. The files
        /// outlive nothing: the pack reads them during start() and keeps only what it built. Main thread only.
        [[nodiscard]] virtual std::size_t sessionContent(std::vector<SessionContentFile>& out) const
        {
            (void)out;
            return 0;
        }

        /// Version 1.2 (task 0181, Papyrus slice 2). Asks the host to enable or disable one placement of the neutral
        /// world this `source` fed (a script's Enable and Disable). The host queues the change (bounded; the last
        /// change of a placement wins) and the engine applies it on the main thread, to the placement if its cell is
        /// loaded; a cell loaded later is built by the pack, which keeps its own override (this call only reaches cells
        /// that exist). `cellHint` is the pack's cell id when the pack knows it; a host looks the placement up in the
        /// loaded cells of the game and may ignore the hint (a persistent reference is held by a different cell than
        /// the one it stands in). True when the change was queued; false for a source that is not registered, a full
        /// queue, or a host built before 1.2 (the default). Main thread only.
        [[nodiscard]] virtual bool setObjectEnabled(
            WorldCellSource& source, WorldId cellHint, WorldId placement, bool enabled)
        {
            (void)source;
            (void)cellHint;
            (void)placement;
            (void)enabled;
            return false;
        }

        /// Version 1.3 (task 0202). Moves and/or turns one placement of the neutral world this `source` fed (a script's
        /// MoveTo, SetPosition, SetAngle). Queued in one ordered queue with setObjectEnabledFaded and placeObject and
        /// applied by the engine on the main thread, to the placement if its cell is loaded (a cell loaded later is
        /// built
        /// by the pack, which applies its own override). The object stays in the cell it is in: a move to another cell
        /// or world is refused by the engine (counted in the log) and stays only the pack's record. True when queued;
        /// false for an unknown source, a non-finite value, a full queue, or a host built before 1.3 (the default).
        [[nodiscard]] virtual bool moveObject(
            WorldCellSource& source, WorldId cellHint, WorldId placement, const ObjectMove& move)
        {
            (void)source;
            (void)cellHint;
            (void)placement;
            (void)move;
            return false;
        }

        /// Version 1.3. As setObjectEnabled, with the seconds the change takes: the object fades in or out over
        /// `fadeSeconds` (0: at once). The default, for a host built before 1.3, forwards to setObjectEnabled and
        /// ignores the time. Same queue as moveObject, so the order of a script's calls is kept.
        [[nodiscard]] virtual bool setObjectEnabledFaded(
            WorldCellSource& source, WorldId cellHint, WorldId placement, bool enabled, float fadeSeconds)
        {
            (void)fadeSeconds;
            return setObjectEnabled(source, cellHint, placement, enabled);
        }

        /// Version 1.3. Creates a new object (a script's PlaceAtMe) in the loaded cell with the pack's id `cellHint`
        /// from `prototype` (the base's description and the transform; the id field is ignored). Returns the id the new
        /// placement has at once (kCreatedObjectBase + n); the engine builds the object on its next update, and a later
        /// moveObject or setObjectEnabledFaded of that id waits behind it in the queue. kNoWorldId when the host cannot
        /// (a host built before 1.3, an unknown source, a full queue or id range, a prototype the host refuses: no
        /// actor, no leveled list, no base): no id is minted then.
        [[nodiscard]] virtual WorldId placeObject(
            WorldCellSource& source, WorldId cellHint, const WorldObject& prototype)
        {
            (void)source;
            (void)cellHint;
            (void)prototype;
            return kNoWorldId;
        }

        /// Task 0250 (additive: a new virtual at the END; the service's version number is NOT changed, the pinned
        /// 1.3 stays). Creates a new ACTOR (a conjured creature, a raised corpse) in the loaded cell that holds
        /// `prototype.transform.position`; `cellHint` is the pack's id of an interior cell and may be kNoWorldId for an
        /// exterior (the host finds the grid square by position). `prototype.kind` must be ObjectKind::Actor. The
        /// engine builds the actor as it builds a placed one and does NOT save it or rebuild it when the cell is loaded
        /// again; take it out again with setObjectEnabled(false). Same queue and id range as placeObject; kNoWorldId
        /// when the host cannot (a host built before this call: the default).
        [[nodiscard]] virtual WorldId placeActor(
            WorldCellSource& source, WorldId cellHint, const WorldObject& prototype)
        {
            (void)source;
            (void)cellHint;
            (void)prototype;
            return kNoWorldId;
        }
    };
}
